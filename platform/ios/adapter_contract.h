#ifndef VPN_PLATFORM_IOS_ADAPTER_CONTRACT_H
#define VPN_PLATFORM_IOS_ADAPTER_CONTRACT_H

#include <string_view>

#include "adapter_contract.h"

namespace vpn::platform::ios {

inline Capability capability() {
    return Capability::MobileSingle;
}

inline PlatformResult prepare(std::string_view) {
    return not_implemented(capability(), "iOS 适配器预留中");
}

inline PlatformResult establish(std::string_view) {
    return not_implemented(capability(), "iOS Packet Tunnel 预留中");
}

inline PlatformResult apply_routes(std::string_view) {
    return not_implemented(capability(), "iOS 路由适配器预留中");
}

inline PlatformResult apply_dns(std::string_view) {
    return not_implemented(capability(), "iOS DNS 适配器预留中");
}

inline PlatformResult destroy(std::string_view) {
    return not_implemented(capability(), "iOS 适配器预留中");
}

}  // namespace vpn::platform::ios

#endif
