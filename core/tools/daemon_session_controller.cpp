#include "daemon_session_controller.h"
#include "vpn_security.h"

#include <algorithm>
#include <regex>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace vpn::daemon {
namespace {

const char* state_name(SessionState state) {
    switch (state) {
    case SessionState::Idle: return "idle";
    case SessionState::Connecting: return "connecting";
    case SessionState::Connected: return "connected";
    case SessionState::Disconnecting: return "disconnecting";
    case SessionState::Disconnected: return "disconnected";
    case SessionState::Failed: return "failed";
    }
    return "failed";
}

const char* state_label(SessionState state) {
    switch (state) {
    case SessionState::Idle: return "未连接";
    case SessionState::Connecting: return "正在连接";
    case SessionState::Connected: return "已连接";
    case SessionState::Disconnecting: return "正在断开";
    case SessionState::Disconnected: return "已断开";
    case SessionState::Failed: return "连接失败";
    }
    return "未知状态";
}

SessionState state_from_event(const EngineEvent& event, SessionState fallback) {
    if (event.error || event.fatal || event.name == "FATAL_ERROR") return SessionState::Failed;
    if (event.name == "CONNECTED") return SessionState::Connected;
    if (event.name == "DISCONNECTED")
        return fallback == SessionState::Failed ? fallback : SessionState::Disconnected;
    if (event.name == "RECONNECTING" || event.name == "RESOLVE") return SessionState::Connecting;
    return fallback;
}

std::string user_warning_for_log(const std::string& info) {
    // A SetupClient log can contain the helper's entire response, including the
    // conflict diagnostic. Do not discard it just because it has a noisy prefix.
    if (info.find("兼容模式") != std::string::npos)
        return "兼容模式：与活动 VPN 重叠的路由已跳过";
    if (info.find("冲突") != std::string::npos || info.find("重叠") != std::string::npos)
        return "检测到 VPN 路由重叠，请查看路由策略";
    return {};
}

// helper 已输出路由裁决；只提取这两种固定格式，不把完整调试日志交给界面。
Json::Value route_conflicts_from_log(const std::string& info) {
    Json::Value result(Json::arrayValue);
    if (info.size() > 1024 * 1024) return result;
    if (info.find("冲突：当前 ") == std::string::npos && info.find("候选会话 ") == std::string::npos) return result;
    static const std::regex compatible(
        "冲突：当前 ([0-9A-Fa-f:.]+/[0-9]{1,3})；前一个会话 ([^\\r\\n ]{1,256}) 使用 ([0-9A-Fa-f:.]+/[0-9]{1,3})");
    static const std::regex strict(
        "候选会话 [^ ]{1,256} 的 ([^\\r\\n ]{1,256}) 与活动会话 ([^ ]{1,256}) 的 ([^\\r\\n ]{1,256}) 重叠");
#ifdef _WIN32
    const auto prefix = std::to_string(_getpid()) + ":";
#else
    const auto prefix = std::to_string(getpid()) + ":";
#endif
    for (const auto& pattern : {std::make_pair(&compatible, "skipped"), std::make_pair(&strict, "blocked")}) {
        for (auto it = std::sregex_iterator(info.begin(), info.end(), *pattern.first);
             it != std::sregex_iterator() && result.size() < 32; ++it) {
            const auto key = (*it)[2].str();
            const bool local = key.rfind(prefix, 0) == 0;
            Json::Value conflict(Json::objectValue);
            conflict["candidateRoute"] = (*it)[1].str();
            conflict["activeRoute"] = (*it)[3].str();
            conflict["peerSessionId"] = local ? key.substr(prefix.size()) : "";
            conflict["peerKey"] = key;
            conflict["peerName"] = local ? "其他 VPN 连接" : "其他 VPN 客户端";
            conflict["external"] = !local;
            conflict["action"] = pattern.second;
            if (std::find(result.begin(), result.end(), conflict) == result.end()) result.append(conflict);
        }
    }
    return result;
}

}  // namespace

Json::Value SessionController::record_json(const std::string& id, const Record& record) {
    Json::Value value(Json::objectValue);
    value["sessionId"] = id;
    value["name"] = record.display_name;
    value["remoteHost"] = record.remote_host;
    value["state"] = state_name(record.state);
    value["stateLabel"] = state_label(record.state);
    const auto traffic = record.session->trafficStats();
    value["downloadBytes"] = static_cast<Json::Int64>(traffic.bytes_in);
    value["uploadBytes"] = static_cast<Json::Int64>(traffic.bytes_out);
    auto seconds = record.duration_seconds;
    if (record.state == SessionState::Connected) {
        seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - record.connected_at).count();
    }
    value["durationSeconds"] = static_cast<Json::Int64>(std::max<std::int64_t>(0, seconds));
    if (!record.error.empty()) value["error"] = redactSecrets(record.error);
    value["warnings"] = Json::Value(Json::arrayValue);
    for (const auto& warning : record.warnings) value["warnings"].append(redactSecrets(warning));
    value["routeConflicts"] = record.route_conflicts;
    return value;
}

Json::Value SessionController::connect(const Json::Value& request) {
    const auto id = request.get("sessionId", "").asString();
    const auto profile = request.get("profile", "").asString();
    if (id.empty() || profile.empty()) return response(false, "会话标识和 profile 不能为空");
    std::lock_guard<std::mutex> operation(operations_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.count(id)) return response(false, "会话标识已经存在");
        if (max_active_sessions_ != 0) {
            const auto active = static_cast<std::size_t>(std::count_if(
                records_.begin(), records_.end(), [](const auto& entry) {
                    const auto& record = entry.second;
                    // 失败/断开事件可能先于原生引擎的清理完成，仍须保留名额。
                    return record.state == SessionState::Connecting ||
                           record.state == SessionState::Connected ||
                           record.state == SessionState::Disconnecting ||
                           record.session->isActive();
                }));
            if (active >= max_active_sessions_) {
                return response(false,
                    "活动 VPN 会话数已达到上限，请先断开已有连接");
            }
        }
    }

    EngineCredentials credentials;
    const auto& supplied = request["credentials"];
    credentials.username = supplied.get("username", "").asString();
    credentials.password = supplied.get("password", "").asString();
    credentials.response = supplied.get("response", "").asString();
    credentials.dynamic_challenge_cookie = supplied.get("dynamicChallengeCookie", "").asString();
    const auto profile_id = request.get("profileId", id).asString();
    if (credentials.username.empty() && credentials.password.empty() &&
        credentials.response.empty() && credentials.dynamic_challenge_cookie.empty()) {
        if (!credential_store_) return response(false, "当前 daemon 没有可用的安全凭据存储");
        Credentials stored;
        const auto loaded = credential_store_->load(profile_id, &stored);
        if (!loaded.ok()) return response(false, redactSecrets(loaded.diagnostic));
        credentials.username = stored.username;
        credentials.password = stored.password;
        credentials.response = stored.response;
    }

    Record record;
    record.display_name = request.get("name", id).asString();
    record.profile_id = profile_id;
    record.remember_credentials = request.get("rememberCredentials", false).asBool();
    record.credentials = {credentials.username, credentials.password, credentials.response, ""};
    record.session = engine_.createSession(id, [this, id](const EngineEvent& event) {
        onEngineEvent(id, event);
    });
    const auto mode = request["routeStrategy"].get("conflictMode", "compatible").asString();
    record.session->setRouteConflictMode(mode == "strict" || mode == "ask"
        ? RouteConflictMode::Strict : RouteConflictMode::Compatible);
    std::vector<RouteRule> route_rules;
    for (const auto& value : request["routeStrategy"].get("rules", Json::Value(Json::arrayValue))) {
        if (!value.isObject() || !value.get("enabled", true).asBool()) continue;
        const auto type = value.get("type", "").asString();
        std::string normalized;
        RouteRuleType rule_type = RouteRuleType::Ip;
        if (type == "ip" || type == "cidr") {
            if (!normalize_route_pattern(value.get("pattern", "").asString(), type == "ip", &normalized))
                return response(false, "IP/CIDR 路由规则格式无效");
        } else if (type == "domain" || type == "domainSuffix") {
            rule_type = type == "domain" ? RouteRuleType::Domain : RouteRuleType::DomainSuffix;
            if (!normalize_domain_pattern(value.get("pattern", "").asString(), rule_type, &normalized))
                return response(false, "域名规则格式无效");
        } else {
            continue;
        }
        const auto action = value.get("action", "prefer").asString();
        if (action != "prefer" && action != "exclude")
            return response(false, "路由规则动作无效");
        route_rules.push_back({action == "exclude" ? RouteRuleAction::Exclude : RouteRuleAction::Prefer,
                               normalized, rule_type});
    }
    record.session->setRouteRules(route_rules);
    const auto evaluation = record.session->setProfile(profile);
    if (!evaluation.ok) return response(false, redactSecrets(evaluation.message));
    record.remote_host = evaluation.remote_host;
    if (!record.session->setCredentials(credentials)) return response(false, "VPN 凭据设置失败");

    OpenVPN3Session* session = record.session.get();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.emplace(id, std::move(record));
    }
    try {
        if (session->connect()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto result = response(true, "连接请求已提交");
            result["session"] = record_json(id, records_.at(id));
            return result;
        }
    } catch (...) {
        // Remove the record below, then stop/join without the callback lock.
    }
    std::unique_ptr<OpenVPN3Session> failed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed = std::move(records_.at(id).session);
        records_.erase(id);
    }
    failed->stop();
    return response(false, "VPN 引擎无法启动连接");
}

Json::Value SessionController::disconnect(const std::string& id) {
    if (id.empty()) return response(false, "会话标识不能为空");
    std::lock_guard<std::mutex> operation(operations_mutex_);
    std::unique_ptr<OpenVPN3Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(id);
        if (it == records_.end()) return response(true, "会话已经不存在");
        session = std::move(it->second.session);
        records_.erase(it);
    }
    session->stop();
    return response(true, "断开请求已提交");
}

Json::Value SessionController::snapshot() const {
    Json::Value result(Json::arrayValue);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, record] : records_) result.append(record_json(id, record));
    return result;
}

void SessionController::stopAll() noexcept {
    std::lock_guard<std::mutex> operation(operations_mutex_);
    std::map<std::string, Record> records;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records.swap(records_);
    }
    for (auto& [id, record] : records) {
        try { record.session->stop(); } catch (...) {}
    }
}

void SessionController::onEngineEvent(const std::string& id, const EngineEvent& event) {
    Json::Value notification(Json::objectValue);
    notification["type"] = "sessionEvent";
    notification["sessionId"] = id;
    notification["event"] = event.name;
    notification["message"] = redactSecrets(event.info);
    bool save = false;
    Credentials credentials;
    std::string profile_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(id);
        if (it == records_.end()) return;
        auto& record = it->second;
        const auto next = state_from_event(event, record.state);
        const auto now = std::chrono::steady_clock::now();
        if (record.state == SessionState::Connected && next != SessionState::Connected) {
            record.duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                now - record.connected_at).count();
        }
        if (next == SessionState::Connected && record.state != SessionState::Connected) {
            record.connected_at = now;
            record.duration_seconds = 0;
            record.error.clear();
            save = record.remember_credentials;
            credentials = record.credentials;
            profile_id = record.profile_id;
        }
        if (next == SessionState::Connecting && record.state != SessionState::Connecting) {
            record.warnings.clear();
            record.route_conflicts = Json::Value(Json::arrayValue);
        }
        record.state = next;
        if (event.error || event.fatal || event.name == "FATAL_ERROR") record.error = event.info;
        const auto warning = event.name == "LOG" ? user_warning_for_log(event.info) : "";
        if (!warning.empty() && std::find(record.warnings.begin(), record.warnings.end(), warning) == record.warnings.end())
            record.warnings.push_back(warning);
        if (event.name == "LOG" || event.error || event.fatal) {
            for (auto conflict : route_conflicts_from_log(event.info)) {
                const auto peer = records_.find(conflict["peerSessionId"].asString());
                if (peer != records_.end()) conflict["peerName"] = redactSecrets(peer->second.display_name);
                if (record.route_conflicts.size() < 32 &&
                    std::find(record.route_conflicts.begin(), record.route_conflicts.end(), conflict) == record.route_conflicts.end())
                    record.route_conflicts.append(conflict);
            }
        }
        notification["state"] = state_name(record.state);
        notification["session"] = record_json(id, record);
    }
    if (save && credential_store_) {
        const auto saved = credential_store_->save(profile_id, credentials);
        if (!saved.ok()) notification["message"] = "连接已建立，但保存凭据失败：" + redactSecrets(saved.diagnostic);
    }
    notify_(notification);
}

Json::Value SessionController::handleRequest(const Json::Value& request) {
    const auto command = request["command"].asString();
    if (command == "ping") return response(true, "OpenVPN 服务正常运行");
    if (command == "snapshot") {
        auto result = response(true);
        result["sessions"] = snapshot();
        return result;
    }
    if (command == "connect") return connect(request);
    if (command == "disconnect") return disconnect(request.get("sessionId", "").asString());
    if (command == "forgetCredentials") {
        const auto id = request.get("profileId", "").asString();
        if (id.empty()) return response(false, "profileId 不能为空");
        if (!credential_store_) return response(false, "当前 daemon 没有可用的安全凭据存储");
        const auto removed = credential_store_->remove(id);
        return removed.ok() ? response(true, "凭据已删除") : response(false, redactSecrets(removed.diagnostic));
    }
    return response(false, "未知命令：" + command);
}

}  // namespace vpn::daemon
