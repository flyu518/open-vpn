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

enum class RouteRuleAction {
    Prefer,
    Exclude
};

enum class RouteRuleType {
    Ip,
    Domain,
    DomainSuffix
};

struct RouteRule {
    RouteRuleAction action = RouteRuleAction::Prefer;
    std::string pattern;
    RouteRuleType type = RouteRuleType::Ip;
};

struct RoutePolicy {
    std::string session_id;
    RouteMode mode = RouteMode::Split;
    std::vector<std::string> routes;
    std::vector<RouteRule> rules;
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

// Normalize an IPv4/IPv6 address or CIDR pattern. A host-only pattern is
// accepted only when host_pattern is true and becomes /32 or /128.
bool normalize_route_pattern(const std::string& pattern,
                            bool host_pattern,
                            std::string* normalized);
bool route_pattern_matches(const std::string& route,
                           const std::string& pattern);
bool normalize_domain_pattern(const std::string& pattern,
                              RouteRuleType type,
                              std::string* normalized);
bool domain_pattern_matches(const std::string& host,
                            const std::string& pattern,
                            RouteRuleType type);

}  // namespace vpn

#endif
