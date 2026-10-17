#pragma once

#include <cstddef>

namespace vpn::daemon {

// Zero is the SessionController convention for an unlimited desktop session set.
inline constexpr std::size_t windows_max_active_sessions() {
    return 0;
}

}  // namespace vpn::daemon
