#include "platform/windows/adapter/adapter.h"

#include <algorithm>

namespace vpn::platform::windows {
namespace {

PlatformResult success() {
    return {true, Capability::DesktopMulti, "操作成功"};
}

PlatformResult failure(const char* message) {
    return {false, Capability::DesktopMulti, message};
}

WindowsAdapter::Session* find_session(std::vector<WindowsAdapter::Session>& sessions,
                                       std::string_view id) {
    const auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [id](const WindowsAdapter::Session& session) {
                                     return session.id == id;
                                 });
    return it == sessions.end() ? nullptr : &*it;
}

}  // namespace

PlatformResult WindowsAdapter::prepare(std::string_view session_id,
                                       std::string_view remote_endpoint) {
    if (session_id.empty() || remote_endpoint.empty() ||
        find_session(sessions_, session_id) != nullptr) {
        return failure("Windows VPN 会话参数无效或已存在");
    }

    Session session;
    session.id = session_id;
    session.endpoint = remote_endpoint;
    session.route_policy.session_id = session.id;
    session.route_policy.mode = RouteMode::Split;
    sessions_.push_back(std::move(session));
    return success();
}

PlatformResult WindowsAdapter::establish(std::string_view session_id) {
    Session* session = find_session(sessions_, session_id);
    if (session == nullptr) {
        return failure("Windows VPN 会话尚未准备");
    }
    session->established = true;
    return success();
}

PlatformResult WindowsAdapter::applyRoutes(
    std::string_view session_id,
    const std::vector<std::string>& routes) {
    Session* session = find_session(sessions_, session_id);
    if (session == nullptr || !session->established) {
        return failure("Windows VPN 虚拟网卡尚未建立");
    }

    std::vector<RoutePolicy> active;
    for (const Session& other : sessions_) {
        if (other.id != session->id && other.established) {
            active.push_back(other.route_policy);
        }
    }

    RoutePolicy candidate = session->route_policy;
    candidate.routes = routes;
    const RouteDecision decision = vpn_route_policy_validate(active, candidate);
    if (decision.code != RouteDecisionCode::Allowed) {
        return failure(decision.diagnostic.c_str());
    }

    session->route_policy = std::move(candidate);
    return success();
}

PlatformResult WindowsAdapter::applyDns(
    std::string_view session_id,
    const std::vector<std::string>& servers) {
    Session* session = find_session(sessions_, session_id);
    if (session == nullptr || !session->established) {
        return failure("Windows VPN 虚拟网卡尚未建立");
    }
    dns_servers_[session->id] = servers;
    return success();
}

PlatformResult WindowsAdapter::destroy(std::string_view session_id) {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(),
                                 [session_id](const Session& session) {
                                     return session.id == session_id;
                                 });
    if (it == sessions_.end()) {
        return success();
    }
    dns_servers_.erase(it->id);
    sessions_.erase(it);
    return success();
}

std::vector<std::string> WindowsAdapter::activeSessions() const {
    std::vector<std::string> result;
    for (const Session& session : sessions_) {
        if (session.established) {
            result.push_back(session.id);
        }
    }
    return result;
}

std::vector<std::string> WindowsAdapter::bypassEndpoints() const {
    std::vector<std::string> result;
    for (const Session& session : sessions_) {
        if (session.established) {
            result.push_back(session.endpoint);
        }
    }
    return result;
}

std::string WindowsAdapter::dnsOwner() const {
    return dns_servers_.size() == 1 ? dns_servers_.begin()->first : std::string();
}

}  // namespace vpn::platform::windows
