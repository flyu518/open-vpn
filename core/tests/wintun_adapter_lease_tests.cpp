#include "wintun_adapter_lease.h"
#include "wintun_adapter_selection.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int create_calls = 0;
int close_calls = 0;
int release_calls = 0;
int adapter_marker = 0;
HMODULE fake_library = reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1));

void *WINAPI fake_create_adapter(LPCWSTR name, LPCWSTR tunnel_type,
                                 const GUID *requested_guid) {
    ++create_calls;
    assert(std::wstring(name) == L"OpenVPN-test");
    assert(std::wstring(tunnel_type) == L"OpenVPN");
    assert(requested_guid == nullptr);
    return &adapter_marker;
}

void *WINAPI failing_create_adapter(LPCWSTR, LPCWSTR, const GUID *) {
    SetLastError(ERROR_ACCESS_DENIED);
    return nullptr;
}

void WINAPI fake_close_adapter(void *adapter) {
    assert(adapter == &adapter_marker);
    ++close_calls;
}

BOOL WINAPI fake_release_library(HMODULE library) {
    assert(library == fake_library);
    ++release_calls;
    return TRUE;
}

vpn::windows::WintunAdapterLease::Api fake_api(
    vpn::windows::WintunAdapterLease::CreateAdapter create_adapter) {
    return {fake_library, create_adapter, &fake_close_adapter,
            &fake_release_library};
}

void closes_adapter_before_releasing_the_library() {
    create_calls = close_calls = release_calls = 0;
    {
        vpn::windows::WintunAdapterLease lease(fake_api(&fake_create_adapter),
                                                L"OpenVPN-test");
        assert(create_calls == 1);
        assert(lease.name() == L"OpenVPN-test");
        assert(close_calls == 0);
        assert(release_calls == 0);
    }
    assert(close_calls == 1);
    assert(release_calls == 1);
}

void selects_only_the_adapter_owned_by_the_requested_session() {
    struct Adapter { std::string name; unsigned int index; };
    std::vector<Adapter> adapters{{"session-one", 61}, {"session-two", 63}};
    vpn::windows::retain_session_wintun_adapter(adapters, "session-two");
    assert(adapters.size() == 1);
    assert(adapters.front().index == 63);
}

void never_falls_back_to_another_sessions_adapter() {
    struct Adapter { std::string name; };
    std::vector<Adapter> adapters{{"session-one"}};
    bool threw = false;
    try {
        vpn::windows::retain_session_wintun_adapter(adapters, "session-two");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    assert(threw);
    assert(adapters.empty());
}

void rejects_ambiguous_adapter_ownership() {
    struct Adapter { std::string name; };
    std::vector<Adapter> adapters{{"session-two"}, {"session-two"}};
    bool threw = false;
    try {
        vpn::windows::retain_session_wintun_adapter(adapters, "session-two");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    assert(threw);
}

void releases_the_library_when_adapter_creation_fails() {
    close_calls = release_calls = 0;
    bool threw = false;
    try {
        vpn::windows::WintunAdapterLease lease(
            fake_api(&failing_create_adapter), L"OpenVPN-test");
    } catch (const std::runtime_error &error) {
        threw = std::string(error.what()).find("5") != std::string::npos;
    }
    assert(threw);
    assert(close_calls == 0);
    assert(release_calls == 1);
}

} // namespace

int main() {
    closes_adapter_before_releasing_the_library();
    releases_the_library_when_adapter_creation_fails();
    selects_only_the_adapter_owned_by_the_requested_session();
    never_falls_back_to_another_sessions_adapter();
    rejects_ambiguous_adapter_ownership();
    return 0;
}
