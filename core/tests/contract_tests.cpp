#include <cassert>
#include <cstdint>

#include "vpn_engine.h"

int main() {
    vpn_engine_t* desktop = nullptr;
    assert(vpn_engine_create(&desktop, VPN_CAPABILITY_DESKTOP_MULTI) == VPN_OK);
    assert(vpn_engine_capability(desktop) == VPN_CAPABILITY_DESKTOP_MULTI);
    assert(vpn_engine_max_active_sessions(desktop) > 1);
    vpn_engine_destroy(desktop);

    vpn_engine_t* mobile = nullptr;
    assert(vpn_engine_create(&mobile, VPN_CAPABILITY_MOBILE_SINGLE) == VPN_OK);
    assert(vpn_engine_capability(mobile) == VPN_CAPABILITY_MOBILE_SINGLE);
    assert(vpn_engine_max_active_sessions(mobile) == 1);
    vpn_engine_destroy(mobile);

    assert(VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN != VPN_ERR_ROUTE_CONFLICT);
    assert(VPN_ERR_ROUTE_CONFLICT != VPN_ERR_FULL_TUNNEL_EXCLUSIVE);
    assert(VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN != VPN_ERR_FULL_TUNNEL_EXCLUSIVE);
    return 0;
}
