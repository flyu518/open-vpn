// Registry ownership regression tests, without changing the machine registry.
#include <openvpn/io/io.hpp>
#include <openvpn/log/logbase.hpp>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <openvpn/tun/win/session_nrpt.hpp>
#include <cassert>
#include <map>
#include <set>

namespace openvpn::Win {
struct Registry {
    static constexpr PCWSTR gpol_nrpt_subkey = L"group";
    static constexpr PCWSTR local_nrpt_subkey = L"local";
    inline static std::set<std::wstring> keys{L"group", L"local"};
    inline static std::map<std::wstring, std::wstring> names;
    struct Key {
        std::wstring path;
        explicit Key(const std::wstring &value) : path(value) {}
        Key(const std::wstring &base, const std::wstring &value, bool create)
            : path(base + L"\\" + value) { if (create) keys.insert(path); }
        bool defined() const { return keys.contains(path); }
        const std::wstring &operator()() const { return path; }
    };
    struct KeyEnumerator : std::vector<std::wstring> {
        explicit KeyEnumerator(const Key &key) {
            for (const auto &path : keys)
                if (path.starts_with(key.path + L"\\"))
                    push_back(path.substr(key.path.size() + 1));
        }
    };
    static LSTATUS set_multi_string(const Key &key, PCWSTR, const std::wstring &value) {
        names[key.path] = value;
        return ERROR_SUCCESS;
    }
    static LSTATUS set_string(const Key &, PCWSTR, const std::wstring &) { return ERROR_SUCCESS; }
    static LSTATUS set_dword(const Key &, PCWSTR, DWORD) { return ERROR_SUCCESS; }
    static void delete_subkey(const std::wstring &path) { keys.erase(path); names.erase(path); }
};
struct MockNetApi {};
} // namespace openvpn::Win

int main() {
    using Registry = openvpn::Win::Registry;
    using DNS = openvpn::TunWin::SessionNrpt<Registry, openvpn::Win::MockNetApi>;
    const auto first = DNS::rule_id("100-61");
    const auto second = DNS::rule_id("100-63");
    assert(first != second);
    assert(first.starts_with("OpenVPNMultiSessionDNSRouting-"));
    const auto path = [](const std::string &id) {
        return L"group\\" + openvpn::wstring::from_utf8(id);
    };
    const auto official = L"group\\OpenVPNDNSRouting-100";
    Registry::keys.insert(official);
    DNS::create_rule(first, L".fixture-0.invalid", L"192.0.2.53", false);
    DNS::create_rule(second, L".fixture-1.invalid", L"192.0.2.54", false);
    DNS::delete_rules("100-63");
    assert(Registry::keys.contains(path(first)));
    assert(!Registry::keys.contains(path(second)));
    assert(Registry::keys.contains(official));
    DNS::create_rule(second, L".fixture-1.invalid", L"192.0.2.54", false);
    DNS::delete_rules("100-61");
    assert(!Registry::keys.contains(path(first)));
    assert(Registry::keys.contains(path(second)));
    DNS::delete_rules("");
    assert(!Registry::keys.contains(path(second)));
    assert(Registry::keys.contains(official));
}
