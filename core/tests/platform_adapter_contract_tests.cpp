#include <cassert>
#include <string_view>

#include "adapter_contract.h"
#include "platform/windows/adapter_contract.h"
#include "platform/macos/adapter_contract.h"
#include "platform/android/adapter_contract.h"
#include "platform/ios/adapter_contract.h"

int main() {
    assert(vpn::platform::windows::capability() ==
           vpn::platform::Capability::DesktopMulti);
    assert(vpn::platform::macos::capability() ==
           vpn::platform::Capability::DesktopMulti);
    assert(vpn::platform::android::capability() ==
           vpn::platform::Capability::MobileSingle);
    assert(vpn::platform::ios::capability() ==
           vpn::platform::Capability::MobileSingle);

    const std::string_view session_id = "contract-test";
    assert(!vpn::platform::windows::prepare(session_id).ok);
    assert(!vpn::platform::windows::establish(session_id).ok);
    assert(!vpn::platform::windows::apply_routes(session_id).ok);
    assert(!vpn::platform::windows::apply_dns(session_id).ok);
    assert(!vpn::platform::windows::destroy(session_id).ok);
    assert(!vpn::platform::macos::prepare(session_id).ok);
    assert(!vpn::platform::android::prepare(session_id).ok);
    assert(!vpn::platform::ios::prepare(session_id).ok);
    return 0;
}
