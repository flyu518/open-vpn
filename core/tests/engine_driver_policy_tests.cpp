#include <cassert>
#include <string>

#include "engine_fixture.h"
#include "openvpn3_engine.h"

namespace {

const std::string minimal_profile =
    "client\n"
    "remote 127.0.0.1 1194 udp\n"
    "nobind\n"
    "dev tun\n"
    "route-nopull\n";

bool configured_wintun(const std::string& profile) {
    vpn::OpenVPN3Engine engine;
    const auto session = engine.createSession("driver-policy");
    assert(session->setProfile(profile).ok);
    return engine_fixture::last_wintun_config();
}

void defaults_standard_tun_profiles_to_wintun_for_isolated_sessions() {
    assert(configured_wintun(minimal_profile));
    assert(configured_wintun("client\nremote 127.0.0.1 1194 udp\n"));
}

void honors_explicit_windows_driver_selection() {
    assert(configured_wintun(minimal_profile + "windows-driver wintun\n"));
    assert(!configured_wintun(minimal_profile + "windows-driver tap-windows6\n"));
}

void preserves_tap_profiles_and_tap_driver_override() {
    assert(!configured_wintun("client\nremote 127.0.0.1 1194 udp\ndev tap\n"));
    assert(!configured_wintun("client\nremote 127.0.0.1 1194 udp\ndev tap0\n"));
    assert(!configured_wintun("client\nremote 127.0.0.1 1194 udp\ndev tun\ndev-type tap\n"));
    assert(configured_wintun("client\nremote 127.0.0.1 1194 udp\n# dev tap\ndev tun\n"));
}

}  // namespace

int main() {
    defaults_standard_tun_profiles_to_wintun_for_isolated_sessions();
    honors_explicit_windows_driver_selection();
    preserves_tap_profiles_and_tap_driver_override();
    return 0;
}
