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

struct RoutePolicy {
    std::string session_id;
    RouteMode mode = RouteMode::Split;
    std::vector<std::string> routes;
};

struct RouteDecision {
    RouteDecisionCode code = RouteDecisionCode::Allowed;
    std::string diagnostic;
};

RouteDecision vpn_route_policy_validate(
    const std::vector<RoutePolicy>& active_policies,
    const RoutePolicy& candidate);

}  // namespace vpn

#endif
