#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>

namespace vpn::windows {

template <typename AdapterList>
void retain_session_wintun_adapter(AdapterList &adapters,
                                   const std::string &adapter_name) {
    adapters.erase(std::remove_if(adapters.begin(), adapters.end(),
                                 [&](const auto &adapter) {
                                     return adapter.name != adapter_name;
                                 }),
                   adapters.end());
    if (adapter_name.empty() || adapters.size() != 1)
        throw std::runtime_error("无法唯一定位当前会话的 Wintun 网卡：" + adapter_name);
}

} // namespace vpn::windows
