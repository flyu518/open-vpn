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
    allows_first_full_tunnel_when_no_session_is_active();
    return 0;
}
