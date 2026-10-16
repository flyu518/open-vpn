#ifndef VPN_PLATFORM_ANDROID_ADAPTER_CONTRACT_H
#define VPN_PLATFORM_ANDROID_ADAPTER_CONTRACT_H

#include <string_view>

#include "adapter_contract.h"

namespace vpn::platform::android {

inline Capability capability() {
    return Capability::MobileSingle;
}

inline PlatformResult prepare(std::string_view) {
    return not_implemented(capability(), "Android 适配器预留中");
}

inline PlatformResult establish(std::string_view) {
    return not_implemented(capability(), "Android VpnService 预留中");
}

inline PlatformResult apply_routes(std::string_view) {
    return not_implemented(capability(), "Android 路由适配器预留中");
}

inline PlatformResult apply_dns(std::string_view) {
    return not_implemented(capability(), "Android DNS 适配器预留中");
}

inline PlatformResult destroy(std::string_view) {
    return not_implemented(capability(), "Android 适配器预留中");
}

}  // namespace vpn::platform::android

#endif
