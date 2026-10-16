#include <cassert>
#include <string>

#include "platform/macos/adapter/adapter.h"

int main() {
    vpn::platform::macos::MacOSAdapter adapter;

    assert(adapter.prepare("office-a", "198.51.100.10").ok);
    assert(adapter.establish("office-a").ok);
    assert(adapter.applyRoutes("office-a", {"10.20.0.0/16"}).ok);
    assert(adapter.bypassEndpoints().size() == 1);

    assert(adapter.prepare("office-b", "198.51.100.11").ok);
    assert(adapter.establish("office-b").ok);
    const auto conflict = adapter.applyRoutes("office-b", {"10.20.1.0/24"});
    assert(!conflict.ok);

    assert(adapter.applyDns("office-a", {"10.20.0.53"}).ok);
    const auto dns_conflict = adapter.applyDns("office-b", {"10.30.0.53"});
    assert(!dns_conflict.ok);
    assert(adapter.dnsOwner() == "office-a");

    assert(adapter.destroy("office-a").ok);
    assert(adapter.destroy("office-a").ok);
    assert(adapter.applyRoutes("office-b", {"10.20.1.0/24"}).ok);
    assert(adapter.dnsOwner().empty());
    return 0;
}
