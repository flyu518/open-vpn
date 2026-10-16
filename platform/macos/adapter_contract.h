#ifndef VPN_PLATFORM_MACOS_ADAPTER_CONTRACT_H
#define VPN_PLATFORM_MACOS_ADAPTER_CONTRACT_H

#include <string_view>

#include "adapter_contract.h"

namespace vpn::platform::macos {

inline Capability capability() {
    return Capability::DesktopMulti;
}

inline PlatformResult prepare(std::string_view) {
    return not_implemented(capability(), "macOS 适配器尚未接入");
}

inline PlatformResult establish(std::string_view) {
    return not_implemented(capability(), "macOS utun 尚未建立");
}

inline PlatformResult apply_routes(std::string_view) {
    return not_implemented(capability(), "macOS 路由适配器尚未接入");
}

inline PlatformResult apply_dns(std::string_view) {
    return not_implemented(capability(), "macOS DNS 适配器尚未接入");
}

inline PlatformResult destroy(std::string_view) {
    return not_implemented(capability(), "macOS 适配器尚未接入");
}

}  // namespace vpn::platform::macos

#endif
