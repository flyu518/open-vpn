#include <cassert>
#include <string>
#include <vector>

#include "vpn_session_manager.h"

namespace {

vpn::SessionProfile split_profile(const char* id, const char* route) {
    vpn::SessionProfile profile;
    profile.session_id = id;
    profile.display_name = id;
    profile.route_policy.session_id = id;
    profile.route_policy.mode = vpn::RouteMode::Split;
    profile.route_policy.routes.emplace_back(route);
    return profile;
}

vpn::SessionProfile full_profile(const char* id) {
    vpn::SessionProfile profile;
    profile.session_id = id;
    profile.display_name = id;
    profile.route_policy.session_id = id;
    profile.route_policy.mode = vpn::RouteMode::Full;
    return profile;
}

void desktop_accepts_two_disjoint_split_sessions() {
    vpn::SessionManager manager;
    const vpn::ConnectResult first = manager.connect(
        split_profile("office-a", "10.20.0.0/16"),
        VPN_CAPABILITY_DESKTOP_MULTI);
    const vpn::ConnectResult second = manager.connect(
        split_profile("office-b", "10.30.0.0/16"),
        VPN_CAPABILITY_DESKTOP_MULTI);

    assert(first.ok);
    assert(second.ok);
    assert(manager.snapshot().size() == 2);
}

void desktop_rejects_overlapping_second_session() {
    vpn::SessionManager manager;
    assert(manager.connect(split_profile("office-a", "10.20.0.0/16"),
                           VPN_CAPABILITY_DESKTOP_MULTI)
               .ok);

    const vpn::ConnectResult second = manager.connect(
        split_profile("office-b", "10.20.10.0/24"),
        VPN_CAPABILITY_DESKTOP_MULTI);
    assert(!second.ok);
    assert(second.error_code == VPN_ERR_ROUTE_CONFLICT);
    assert(manager.snapshot().size() == 1);
}

void desktop_rejects_second_full_tunnel() {
    vpn::SessionManager manager;
    assert(manager.connect(full_profile("office-a"), VPN_CAPABILITY_DESKTOP_MULTI)
               .ok);

    const vpn::ConnectResult second = manager.connect(
        full_profile("office-b"), VPN_CAPABILITY_DESKTOP_MULTI);
    assert(!second.ok);
    assert(second.error_code == VPN_ERR_FULL_TUNNEL_EXCLUSIVE);
    assert(manager.snapshot().size() == 1);
}

void mobile_rejects_second_active_session() {
    vpn::SessionManager manager;
    assert(manager.connect(split_profile("mobile-a", "10.40.0.0/16"),
                           VPN_CAPABILITY_MOBILE_SINGLE)
               .ok);

    const vpn::ConnectResult second = manager.connect(
        split_profile("mobile-b", "10.50.0.0/16"), VPN_CAPABILITY_MOBILE_SINGLE);
    assert(!second.ok);
    assert(second.error_code == VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN);
    assert(manager.snapshot().size() == 1);
    assert(manager.snapshot().front().session_id == "mobile-a");
}

void disconnect_removes_only_requested_session() {
    vpn::SessionManager manager;
    assert(manager.connect(split_profile("office-a", "10.20.0.0/16"),
                           VPN_CAPABILITY_DESKTOP_MULTI)
               .ok);
    assert(manager.connect(split_profile("office-b", "10.30.0.0/16"),
                           VPN_CAPABILITY_DESKTOP_MULTI)
               .ok);

    manager.disconnect("office-a");
    const std::vector<vpn::SessionSnapshot> remaining = manager.snapshot();
    assert(remaining.size() == 1);
    assert(remaining.front().session_id == "office-b");
}

void duplicate_session_id_is_rejected() {
    vpn::SessionManager manager;
    assert(manager.connect(split_profile("office-a", "10.20.0.0/16"),
                           VPN_CAPABILITY_DESKTOP_MULTI)
               .ok);

    const vpn::ConnectResult duplicate = manager.connect(
        split_profile("office-a", "10.30.0.0/16"),
        VPN_CAPABILITY_DESKTOP_MULTI);
    assert(!duplicate.ok);
    assert(duplicate.error_code == VPN_ERR_DUPLICATE_SESSION);
    assert(manager.snapshot().size() == 1);
}

}  // namespace

int main() {
    desktop_accepts_two_disjoint_split_sessions();
    desktop_rejects_overlapping_second_session();
    desktop_rejects_second_full_tunnel();
    mobile_rejects_second_active_session();
    disconnect_removes_only_requested_session();
    duplicate_session_id_is_rejected();
    return 0;
}
