#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "openvpn3_engine.h"
#include "vpn_security.h"
#include "vpn_session_manager.h"
#include "platform/windows/secure_store/credential_store.h"

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> stop_requested{false};

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT ||
        signal == CTRL_BREAK_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        stop_requested.store(true);
        return TRUE;
    }
    return FALSE;
}

std::string write_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value) + "\n";
}

Json::Value response(bool ok, std::string message = {}) {
    Json::Value value(Json::objectValue);
    value["ok"] = ok;
    if (!message.empty()) value["message"] = std::move(message);
    return value;
}

std::string state_name(vpn::SessionState state) {
    switch (state) {
    case vpn::SessionState::Idle:
        return "idle";
    case vpn::SessionState::Connecting:
        return "connecting";
    case vpn::SessionState::Connected:
        return "connected";
    case vpn::SessionState::Disconnecting:
        return "disconnecting";
    case vpn::SessionState::Disconnected:
        return "disconnected";
    case vpn::SessionState::Failed:
        return "failed";
    }
    return "failed";
}

vpn::RouteConflictMode route_mode_from_request(const Json::Value& request) {
    const std::string mode =
        request["routeStrategy"].get("conflictMode", "compatible").asString();
    return mode == "strict" || mode == "ask"
               ? vpn::RouteConflictMode::Strict
               : vpn::RouteConflictMode::Compatible;
}

class WindowsDaemon;

class SessionController {
public:
    SessionController(WindowsDaemon* daemon,
                      vpn::CredentialStore* credential_store)
        : daemon_(daemon), credential_store_(credential_store) {}

    Json::Value connect(const Json::Value& request);
    Json::Value disconnect(const std::string& session_id);
    Json::Value forgetCredentials(const std::string& profile_id);
    Json::Value snapshot() const;
    void stopAll();
    void onEngineEvent(const std::string& session_id,
                       const vpn::EngineEvent& event);

private:
    struct Record {
        std::unique_ptr<vpn::OpenVPN3Session> session;
        std::string display_name;
        std::string remote_host;
        vpn::SessionState state = vpn::SessionState::Idle;
        std::string error;
        std::vector<std::string> warnings;
    };

    static vpn::SessionState state_from_event(
        const vpn::EngineEvent& event,
        vpn::SessionState fallback);
    static Json::Value record_json(const std::string& session_id,
                                   const Record& record);

    WindowsDaemon* daemon_;
    vpn::CredentialStore* credential_store_;
    mutable std::mutex mutex_;
    vpn::OpenVPN3Engine engine_;
    std::map<std::string, Record> records_;
};

class WindowsDaemon {
public:
    explicit WindowsDaemon(std::wstring pipe_name)
        : pipe_name_(std::move(pipe_name)),
          credential_store_(std::make_unique<
                            vpn::platform::windows::WindowsCredentialStore>()),
          controller_(this, credential_store_.get()) {}

    ~WindowsDaemon() { stop(); }

    int run();
    void stop();
    void broadcast(const Json::Value& value);
    SessionController& controller() { return controller_; }

private:
    void client_loop(HANDLE pipe);
    Json::Value handle_request(const Json::Value& request);
    bool send_to_client(HANDLE pipe, const Json::Value& value);
    void remove_client(HANDLE pipe);

    std::wstring pipe_name_;
    std::atomic<bool> running_{true};
    HANDLE server_pipe_ = INVALID_HANDLE_VALUE;
    std::unique_ptr<vpn::CredentialStore> credential_store_;
    SessionController controller_;
    std::mutex clients_mutex_;
    std::vector<HANDLE> clients_;
};

vpn::SessionState SessionController::state_from_event(
    const vpn::EngineEvent& event,
    vpn::SessionState fallback) {
    if (event.name == "CONNECTED") return vpn::SessionState::Connected;
    if (event.name == "DISCONNECTED") return vpn::SessionState::Disconnected;
    if (event.name == "RECONNECTING" || event.name == "RESOLVE") {
        return vpn::SessionState::Connecting;
    }
    if (event.error || event.fatal || event.name == "FATAL_ERROR") {
        return vpn::SessionState::Failed;
    }
    return fallback;
}

Json::Value SessionController::record_json(const std::string& session_id,
                                            const Record& record) {
    Json::Value value(Json::objectValue);
    value["sessionId"] = session_id;
    value["name"] = record.display_name;
    value["remoteHost"] = record.remote_host;
    value["state"] = state_name(record.state);
    if (!record.error.empty()) value["error"] = vpn::redactSecrets(record.error);
    Json::Value warnings(Json::arrayValue);
    for (const std::string& warning : record.warnings) {
        warnings.append(vpn::redactSecrets(warning));
    }
    value["warnings"] = warnings;
    return value;
}

Json::Value SessionController::connect(const Json::Value& request) {
    const std::string session_id = request.get("sessionId", "").asString();
    const std::string profile = request.get("profile", "").asString();
    if (session_id.empty() || profile.empty()) {
        return response(false, "会话标识和 profile 不能为空");
    }

    vpn::EngineCredentials credentials;
    const Json::Value& credential_value = request["credentials"];
    credentials.username = credential_value.get("username", "").asString();
    credentials.password = credential_value.get("password", "").asString();
    credentials.response = credential_value.get("response", "").asString();
    credentials.dynamic_challenge_cookie =
        credential_value.get("dynamicChallengeCookie", "").asString();
    const std::string profile_id =
        request.get("profileId", session_id).asString();
    const bool has_credentials = !credentials.username.empty() ||
                                 !credentials.password.empty() ||
                                 !credentials.response.empty() ||
                                 !credentials.dynamic_challenge_cookie.empty();
    if (!has_credentials) {
        vpn::Credentials stored_credentials;
        const auto loaded = credential_store_->load(profile_id, &stored_credentials);
        if (!loaded.ok()) return response(false, loaded.diagnostic);
        credentials.username = stored_credentials.username;
        credentials.password = stored_credentials.password;
        credentials.response = stored_credentials.response;
    }

    auto record = std::make_unique<Record>();
    record->display_name = request.get("name", session_id).asString();
    record->state = vpn::SessionState::Connecting;
    record->session = engine_.createSession(
        session_id,
        [this, session_id](const vpn::EngineEvent& event) {
            onEngineEvent(session_id, event);
        });
    record->session->setRouteConflictMode(route_mode_from_request(request));

    const vpn::ProfileEvaluation evaluation = record->session->setProfile(profile);
    if (!evaluation.ok) return response(false, evaluation.message);
    record->remote_host = evaluation.remote_host;
    if (!record->session->setCredentials(credentials)) {
        return response(false, "VPN 凭据设置失败");
    }
    if (request.get("rememberCredentials", false).asBool()) {
        const vpn::Credentials saved{credentials.username, credentials.password,
                                     credentials.response, ""};
        const auto result = credential_store_->save(profile_id, saved);
        if (!result.ok()) return response(false, result.diagnostic);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.find(session_id) != records_.end()) {
            return response(false, "会话标识已经存在");
        }
        records_.emplace(session_id, std::move(*record));
    }

    Record* stored = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stored = &records_.at(session_id);
    }
    if (!stored->session->connect()) {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.erase(session_id);
        return response(false, "VPN 引擎无法启动连接");
    }

    Json::Value result = response(true, "连接请求已提交");
    result["session"] = record_json(session_id, *stored);
    return result;
}

Json::Value SessionController::disconnect(const std::string& session_id) {
    std::unique_ptr<vpn::OpenVPN3Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(session_id);
        if (it == records_.end()) return response(true, "会话已经不存在");
        session = std::move(it->second.session);
        records_.erase(it);
    }
    session->stop();
    return response(true, "断开请求已提交");
}

Json::Value SessionController::forgetCredentials(const std::string& profile_id) {
    const auto removed = credential_store_->remove(profile_id);
    return removed.ok() ? response(true, "凭据已删除")
                        : response(false, removed.diagnostic);
}

Json::Value SessionController::snapshot() const {
    Json::Value result(Json::arrayValue);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : records_) {
        result.append(record_json(item.first, item.second));
    }
    return result;
}

void SessionController::stopAll() {
    std::vector<std::unique_ptr<vpn::OpenVPN3Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : records_) sessions.push_back(std::move(item.second.session));
        records_.clear();
    }
    for (auto& session : sessions) {
        if (session) session->stop();
    }
}

void SessionController::onEngineEvent(const std::string& session_id,
                                      const vpn::EngineEvent& event) {
    Json::Value notification(Json::objectValue);
    notification["type"] = "sessionEvent";
    notification["sessionId"] = session_id;
    notification["event"] = event.name;
    notification["message"] = vpn::redactSecrets(event.info);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(session_id);
        if (it == records_.end()) return;
        it->second.state = state_from_event(event, it->second.state);
        if (event.error || event.fatal) it->second.error = event.info;
        notification["state"] = state_name(it->second.state);
        notification["session"] = record_json(session_id, it->second);
    }
    daemon_->broadcast(notification);
}

bool WindowsDaemon::send_to_client(HANDLE pipe, const Json::Value& value) {
    const std::string payload = write_json(value);
    DWORD written = 0;
    return WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()),
                     &written, nullptr) && written == payload.size();
}

void WindowsDaemon::remove_client(HANDLE pipe) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), pipe),
                   clients_.end());
}

void WindowsDaemon::broadcast(const Json::Value& value) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (send_to_client(*it, value)) {
            ++it;
        } else {
            DisconnectNamedPipe(*it);
            CloseHandle(*it);
            it = clients_.erase(it);
        }
    }
}

Json::Value WindowsDaemon::handle_request(const Json::Value& request) {
    const std::string command = request.get("command", "").asString();
    if (command == "ping") return response(true, "OpenVPN 服务正常运行");
    if (command == "snapshot") {
        Json::Value result = response(true);
        result["sessions"] = controller_.snapshot();
        return result;
    }
    if (command == "connect") return controller_.connect(request);
    if (command == "disconnect") {
        return controller_.disconnect(request.get("sessionId", "").asString());
    }
    if (command == "forgetCredentials") {
        return controller_.forgetCredentials(request.get("profileId", "").asString());
    }
    return response(false, "未知命令：" + command);
}

void WindowsDaemon::client_loop(HANDLE pipe) {
    std::string input;
    char buffer[8192];
    DWORD read = 0;
    while (running_.load() && ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) &&
           read > 0) {
        input.append(buffer, buffer + read);
        for (;;) {
            const std::size_t newline = input.find('\n');
            if (newline == std::string::npos) break;
            const std::string line = input.substr(0, newline);
            input.erase(0, newline + 1);
            if (line.empty()) continue;
            Json::CharReaderBuilder builder;
            Json::Value request;
            std::string errors;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            if (!reader->parse(line.data(), line.data() + line.size(), &request,
                               &errors)) {
                if (!send_to_client(pipe, response(false, "JSON 请求无效"))) {
                    running_.store(false);
                    break;
                }
                continue;
            }
            if (!send_to_client(pipe, handle_request(request))) {
                running_.store(false);
                break;
            }
        }
    }
    remove_client(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

int WindowsDaemon::run() {
    while (running_.load() && !stop_requested.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 1 << 20, 1 << 20, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::cerr << "无法创建 OpenVPN Windows named pipe，错误码："
                      << GetLastError() << '\n';
            return 2;
        }
        server_pipe_ = pipe;
        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                                   ? TRUE
                                   : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            server_pipe_ = INVALID_HANDLE_VALUE;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(pipe);
        }
        std::thread(&WindowsDaemon::client_loop, this, pipe).detach();
        server_pipe_ = INVALID_HANDLE_VALUE;
    }
    stop();
    return 0;
}

void WindowsDaemon::stop() {
    if (!running_.exchange(false)) return;
    controller_.stopAll();
    if (server_pipe_ != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(server_pipe_);
        CloseHandle(server_pipe_);
        server_pipe_ = INVALID_HANDLE_VALUE;
    }
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (HANDLE pipe : clients_) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    clients_.clear();
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::wstring pipe_name = LR"(\\.\pipe\openvpn-ui)";
    if (argc == 3 && std::wstring(argv[1]) == L"--pipe") {
        pipe_name = argv[2];
    } else if (argc != 1) {
        std::wcerr << L"用法：OpenVPN Windows daemon [--pipe \\\\.\\pipe\\openvpn-ui]\n";
        return 2;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
    WindowsDaemon daemon(std::move(pipe_name));
    return daemon.run();
}

#else

int main() { return 0; }

#endif
