#ifndef VPN_ROUTE_POLICY_H
#define VPN_ROUTE_POLICY_H

#include <string>
#include <vector>

namespace vpn {

enum class RouteMode {
    Split,
    Full
};

enum class RouteDecisionCode {
    Allowed,
    RouteConflict,
    FullTunnelExclusive,
    InvalidRoute
};

enum class RouteConflictMode {
    Strict,
    Compatible
};

struct RoutePolicy {
    std::string session_id;
    RouteMode mode = RouteMode::Split;
    std::vector<std::string> routes;
};

struct RouteConflict {
    std::string candidate_route;
    std::string active_session_id;
    std::string active_route;
};

struct RouteDecision {
    RouteDecisionCode code = RouteDecisionCode::Allowed;
    std::string diagnostic;
    std::vector<RouteConflict> conflicts;
};

RouteDecision vpn_route_policy_validate(
    const std::vector<RoutePolicy>& active_policies,
    const RoutePolicy& candidate,
    RouteConflictMode mode = RouteConflictMode::Strict);

}  // namespace vpn

#endif
