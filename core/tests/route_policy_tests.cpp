#include <cassert>
#include <string>
#include <vector>

#include "vpn_route_policy.h"

namespace {

vpn::RoutePolicy split_policy(const char* session_id,
                              std::initializer_list<const char*> routes) {
    vpn::RoutePolicy policy;
    policy.session_id = session_id;
    policy.mode = vpn::RouteMode::Split;
    for (const char* route : routes) {
        policy.routes.emplace_back(route);
    }
    return policy;
}

vpn::RoutePolicy full_policy(const char* session_id) {
    vpn::RoutePolicy policy;
    policy.session_id = session_id;
    policy.mode = vpn::RouteMode::Full;
    return policy;
}

void rejects_ipv4_containment_overlap() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"10.20.0.0/16"})};
    const vpn::RoutePolicy candidate =
        split_policy("office-b", {"10.20.10.0/24"});

    const vpn::RouteDecision decision =
        vpn::vpn_route_policy_validate(active, candidate);
    assert(decision.code == vpn::RouteDecisionCode::RouteConflict);
    assert(decision.diagnostic.find("10.20.10.0/24") != std::string::npos);
    assert(decision.diagnostic.find("10.20.0.0/16") != std::string::npos);
}

void rejects_ipv6_overlap() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"2001:db8:100::/48"})};
    const vpn::RoutePolicy candidate =
        split_policy("office-b", {"2001:db8:100:1::/64"});

    const vpn::RouteDecision decision =
        vpn::vpn_route_policy_validate(active, candidate);
    assert(decision.code == vpn::RouteDecisionCode::RouteConflict);
}

void allows_disjoint_split_routes() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"10.20.0.0/16"})};
    const vpn::RoutePolicy candidate =
        split_policy("office-b", {"10.30.0.0/16"});

    const vpn::RouteDecision decision =
        vpn::vpn_route_policy_validate(active, candidate);
    assert(decision.code == vpn::RouteDecisionCode::Allowed);
}

void rejects_second_full_tunnel() {
    const std::vector<vpn::RoutePolicy> active = {full_policy("office-a")};
    const vpn::RoutePolicy candidate = full_policy("office-b");

    const vpn::RouteDecision decision =
        vpn::vpn_route_policy_validate(active, candidate);
    assert(decision.code == vpn::RouteDecisionCode::FullTunnelExclusive);
}

void compatible_mode_reports_but_allows_overlap() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"10.20.0.0/16"})};
    const vpn::RoutePolicy candidate =
        split_policy("office-b", {"10.20.10.0/24", "10.30.0.0/16"});

    const vpn::RouteDecision decision = vpn::vpn_route_policy_validate(
        active, candidate, vpn::RouteConflictMode::Compatible);
    assert(decision.code == vpn::RouteDecisionCode::Allowed);
    assert(decision.conflicts.size() == 1);
    assert(decision.conflicts.front().candidate_route == "10.20.10.0/24");
    assert(decision.conflicts.front().active_session_id == "office-a");
}

void preferred_route_requires_manual_reconnect() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"10.20.0.0/16"})};
    vpn::RoutePolicy candidate = split_policy("office-b", {"10.20.10.0/24"});
    candidate.rules.push_back({vpn::RouteRuleAction::Prefer, "10.20.0.0/16"});

    const vpn::RouteDecision decision = vpn::vpn_route_policy_validate(
        active, candidate, vpn::RouteConflictMode::Compatible);
    assert(decision.code == vpn::RouteDecisionCode::RouteConflict);
    assert(decision.diagnostic.find("优先") != std::string::npos);
}

void route_rule_matches_ip_and_cidr() {
    assert(vpn::route_pattern_matches("10.20.10.0/24", "10.20.0.0/16"));
    assert(vpn::route_pattern_matches("2001:db8:1::/64", "2001:db8::/32"));
    assert(!vpn::route_pattern_matches("10.30.0.0/16", "10.20.0.0/16"));
    std::string normalized;
    assert(vpn::normalize_route_pattern("10.20.10.4", true, &normalized));
    assert(normalized == "10.20.10.4/32");
    assert(!vpn::normalize_route_pattern("example.com", false, &normalized));
}

void route_rule_matches_domain_suffix() {
    std::string normalized;
    assert(vpn::normalize_domain_pattern(
        "*.Corp.Example.com", vpn::RouteRuleType::DomainSuffix, &normalized));
    assert(normalized == "corp.example.com");
    assert(vpn::domain_pattern_matches(
        "vpn.corp.example.com", normalized, vpn::RouteRuleType::DomainSuffix));
    assert(vpn::domain_pattern_matches(
        "corp.example.com", normalized, vpn::RouteRuleType::DomainSuffix));
    assert(!vpn::domain_pattern_matches(
        "notcorp.example.com", normalized, vpn::RouteRuleType::DomainSuffix));
    assert(!vpn::domain_pattern_matches(
        "corp.example.net", normalized, vpn::RouteRuleType::DomainSuffix));
    assert(vpn::normalize_domain_pattern(
        "api.example.com.", vpn::RouteRuleType::Domain, &normalized));
    assert(normalized == "api.example.com");
    assert(vpn::domain_pattern_matches(
        "api.example.com", normalized, vpn::RouteRuleType::Domain));
    assert(!vpn::domain_pattern_matches(
        "v2.api.example.com", normalized, vpn::RouteRuleType::Domain));
    assert(!vpn::normalize_domain_pattern(
        "bad domain.example.com", vpn::RouteRuleType::DomainSuffix, &normalized));
}

void excluded_route_is_removed_before_conflict_check() {
    const std::vector<vpn::RoutePolicy> active = {
        split_policy("office-a", {"10.20.0.0/16"})};
    vpn::RoutePolicy candidate = split_policy("office-b", {"10.20.10.0/24", "10.30.0.0/16"});
    candidate.rules.push_back({vpn::RouteRuleAction::Exclude, "10.20.0.0/16"});

    const vpn::RouteDecision decision = vpn::vpn_route_policy_validate(
        active, candidate, vpn::RouteConflictMode::Compatible);
    assert(decision.code == vpn::RouteDecisionCode::Allowed);
    assert(decision.conflicts.empty());
}

void preferred_domain_suffix_requires_manual_reconnect() {
    vpn::RoutePolicy active = split_policy("office-a", {});
    active.rules.push_back({vpn::RouteRuleAction::Prefer, "corp.example.com",
                            vpn::RouteRuleType::DomainSuffix});
    vpn::RoutePolicy candidate = split_policy("office-b", {});
    candidate.rules.push_back({vpn::RouteRuleAction::Prefer, "db.corp.example.com",
                               vpn::RouteRuleType::Domain});

    const vpn::RouteDecision decision = vpn::vpn_route_policy_validate(
        {active}, candidate, vpn::RouteConflictMode::Compatible);
    assert(decision.code == vpn::RouteDecisionCode::RouteConflict);
    assert(decision.diagnostic.find("域名规则") != std::string::npos);
}

void allows_first_full_tunnel_when_no_session_is_active() {
    const vpn::RoutePolicy candidate = full_policy("office-a");

    const vpn::RouteDecision decision =
        vpn::vpn_route_policy_validate({}, candidate);
    assert(decision.code == vpn::RouteDecisionCode::Allowed);
}

}  // namespace

int main() {
    rejects_ipv4_containment_overlap();
    rejects_ipv6_overlap();
    allows_disjoint_split_routes();
    rejects_second_full_tunnel();
    compatible_mode_reports_but_allows_overlap();
    preferred_route_requires_manual_reconnect();
    route_rule_matches_ip_and_cidr();
    route_rule_matches_domain_suffix();
    excluded_route_is_removed_before_conflict_check();
    preferred_domain_suffix_requires_manual_reconnect();
    allows_first_full_tunnel_when_no_session_is_active();
    return 0;
}
