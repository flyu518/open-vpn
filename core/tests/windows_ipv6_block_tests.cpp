#include "shared_ipv6_block.h"
#include <cassert>
#include <set>
#include <stdexcept>
#include <cstdlib>
#include <new>

bool reject_allocations = false;
void *operator new(std::size_t size) {
    if (reject_allocations) throw std::bad_alloc();
    if (auto *value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }

int main() {
    std::set<unsigned int> routes;
    unsigned int additions = 0, removals = 0;
    vpn::windows::SharedIpv6Block manager({
        [&](unsigned int i) { ++additions; return routes.insert(i).second; },
        [&](unsigned int i) { ++removals; routes.erase(i); }});
    auto first = manager.acquire();
    auto second = manager.acquire();
    assert(routes.size() == 3 && additions == 3);
    second.reset();
    assert(routes.size() == 3 && removals == 0);
    second = manager.acquire();
    first.reset();
    assert(routes.size() == 3 && removals == 0);
    second.reset();
    assert(routes.empty() && removals == 3);

    routes = {0}; // An existing rule owned by another application.
    first = manager.acquire();
    first.reset();
    assert(routes == std::set<unsigned int>{0});

    routes.clear();
    vpn::windows::SharedIpv6Block failing({
        [&](unsigned int i) { if (i == 1) throw std::runtime_error("denied"); return routes.insert(i).second; },
        [&](unsigned int i) { routes.erase(i); }});
    bool threw = false;
    try { auto lease = failing.acquire(); } catch (const std::runtime_error &) { threw = true; }
    assert(threw && routes.empty());

    // Once an OS resource exists, recording its ownership must not allocate.
    bool acquired[3]{};
    vpn::windows::SharedIpv6Block no_alloc({
        [&](unsigned int i) { acquired[i] = true; reject_allocations = true; return true; },
        [&](unsigned int i) { acquired[i] = false; }});
    bool allocation_failed = false;
    try {
        auto lease = no_alloc.acquire();
        reject_allocations = false;
        lease.reset();
    } catch (const std::bad_alloc &) { reject_allocations = false; allocation_failed = true; }
    assert(!allocation_failed);
    assert(!acquired[0] && !acquired[1] && !acquired[2]);
}
