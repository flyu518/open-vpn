#include "vpn_session_manager.h"

#include <utility>

namespace vpn {
namespace {

ConnectResult failure(vpn_error_code_t code,
                      std::string diagnostic,
                      const SessionProfile& profile) {
    ConnectResult result;
    result.ok = false;
    result.error_code = code;
    result.diagnostic = std::move(diagnostic);
    result.snapshot.session_id = profile.session_id;
    result.snapshot.display_name = profile.display_name;
    result.snapshot.state = SessionState::Failed;
    result.snapshot.route_policy = profile.route_policy;
    return result;
}

}  // namespace

SessionManager::SessionManager(EventSink event_sink)
    : event_sink_(std::move(event_sink)) {}

bool SessionManager::is_active(SessionState state) {
    return state == SessionState::Connecting || state == SessionState::Connected ||
           state == SessionState::Disconnecting;
}

void SessionManager::emit(const SessionSnapshot& snapshot,
                          SessionEventType type,
                          std::string_view message) const {
    if (event_sink_) {
        event_sink_(snapshot, type, message);
    }
}

ConnectResult SessionManager::connect(const SessionProfile& profile,
                                      vpn_capability_t capability) {
    if (profile.session_id.empty() || sessions_.find(profile.session_id) != sessions_.end()) {
        return failure(VPN_ERR_DUPLICATE_SESSION, "会话标识已经存在", profile);
    }

    if (capability == VPN_CAPABILITY_MOBILE_SINGLE && !sessions_.empty()) {
        return failure(VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN,
                       "当前平台只能同时连接一个 VPN",
                       profile);
    }

    std::vector<RoutePolicy> active_policies;
    active_policies.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
        if (is_active(entry.second.snapshot.state)) {
            active_policies.push_back(entry.second.snapshot.route_policy);
        }
    }

    const RouteDecision route_decision =
        vpn_route_policy_validate(active_policies, profile.route_policy);
    if (route_decision.code != RouteDecisionCode::Allowed) {
        vpn_error_code_t error_code = VPN_ERR_INVALID_ROUTE;
        if (route_decision.code == RouteDecisionCode::RouteConflict) {
            error_code = VPN_ERR_ROUTE_CONFLICT;
        } else if (route_decision.code == RouteDecisionCode::FullTunnelExclusive) {
            error_code = VPN_ERR_FULL_TUNNEL_EXCLUSIVE;
        }
        return failure(error_code, route_decision.diagnostic, profile);
    }

    SessionSnapshot snapshot;
    snapshot.session_id = profile.session_id;
    snapshot.display_name = profile.display_name;
    snapshot.state = SessionState::Connecting;
    snapshot.route_policy = profile.route_policy;

    auto inserted = sessions_.emplace(
        profile.session_id,
        Record{snapshot, FakeEngine(profile.session_id)});
    emit(inserted.first->second.snapshot, SessionEventType::StateChanged, "正在连接");

    if (!inserted.first->second.engine.start()) {
        inserted.first->second.snapshot.state = SessionState::Failed;
        emit(inserted.first->second.snapshot, SessionEventType::Error, "VPN 引擎启动失败");
        ConnectResult result = failure(VPN_ERR_INVALID_ARGUMENT, "VPN 引擎启动失败", profile);
        sessions_.erase(inserted.first);
        return result;
    }

    inserted.first->second.snapshot.state = SessionState::Connected;
    emit(inserted.first->second.snapshot, SessionEventType::StateChanged, "已连接");

    ConnectResult result;
    result.ok = true;
    result.error_code = VPN_OK;
    result.diagnostic = "连接成功";
    result.snapshot = inserted.first->second.snapshot;
    return result;
}

void SessionManager::disconnect(std::string_view session_id) {
    const auto it = sessions_.find(std::string(session_id));
    if (it == sessions_.end()) {
        return;
    }

    it->second.snapshot.state = SessionState::Disconnecting;
    emit(it->second.snapshot, SessionEventType::StateChanged, "正在断开");
    it->second.engine.stop();
    it->second.snapshot.state = SessionState::Disconnected;
    emit(it->second.snapshot, SessionEventType::StateChanged, "已断开");
    sessions_.erase(it);
}

std::vector<SessionSnapshot> SessionManager::snapshot() const {
    std::vector<SessionSnapshot> result;
    result.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
        result.push_back(entry.second.snapshot);
    }
    return result;
}

}  // namespace vpn
