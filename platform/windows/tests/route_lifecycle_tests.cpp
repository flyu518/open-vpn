#include <cassert>

#include "platform/windows/adapter/adapter.h"

int main() {
    vpn::platform::windows::WindowsAdapter adapter;

    assert(adapter.prepare("office-a", "198.51.100.20").ok);
    assert(adapter.establish("office-a").ok);
    assert(adapter.applyRoutes("office-a", {"10.60.0.0/16"}).ok);
    assert(adapter.bypassEndpoints().size() == 1);

    assert(adapter.prepare("office-b", "198.51.100.21").ok);
    assert(adapter.establish("office-b").ok);
    assert(!adapter.applyRoutes("office-b", {"10.60.1.0/24"}).ok);
    assert(adapter.applyRoutes("office-b", {"10.70.0.0/16"}).ok);

    assert(adapter.applyDns("office-a", {"10.60.0.53"}).ok);
    assert(!adapter.applyDns("office-b", {"10.70.0.53"}).ok);
    assert(adapter.destroy("office-a").ok);
    assert(adapter.destroy("office-a").ok);
    assert(adapter.dnsOwner().empty());
    return 0;
}
