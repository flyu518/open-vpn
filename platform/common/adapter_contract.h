#ifndef VPN_PLATFORM_ADAPTER_CONTRACT_H
#define VPN_PLATFORM_ADAPTER_CONTRACT_H

#include <string_view>

namespace vpn::platform {

enum class Capability {
    DesktopMulti,
    MobileSingle
};

struct PlatformResult {
    bool ok = false;
    Capability capability = Capability::DesktopMulti;
    const char* message = "";
};

inline PlatformResult not_implemented(Capability capability,
                                      const char* message) {
    return PlatformResult{false, capability, message};
}

}  // namespace vpn::platform

#endif
