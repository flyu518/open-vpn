#ifndef VPN_OPENVPN3_ENGINE_CONFIG_H
#define VPN_OPENVPN3_ENGINE_CONFIG_H

#include "openvpn_project_config.h"

namespace vpn::openvpn3_config {

// Keep OpenVPN3 session behavior identical on macOS and Windows.  In
// particular, a missing agent/TUN device must eventually become a terminal
// failure instead of leaving the desktop UI in "connecting" forever.
inline constexpr int connection_timeout_seconds = project_config::connection_timeout_seconds;
inline constexpr bool retry_on_auth_failed = project_config::retry_on_auth_failed;

}  // namespace vpn::openvpn3_config

#endif
