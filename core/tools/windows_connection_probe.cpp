// Exercises the production controller/core without Qt or an installer.
// Passwords are consumed by WindowsCredentialStore and never written to output.
#include "daemon_session_controller.h"
#include "platform/windows/secure_store/credential_store.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <wincred.h>
#include <process.h>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>
#include <condition_variable>
#include "official_auth_reference.h"

namespace {
using namespace std::chrono_literals;
using Route = std::tuple<ULONG, UINT8, ULONG, ULONG>;
std::mutex output_mutex;

void report(const std::string &value) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << value << std::endl;
}

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::wstring wide(const std::string &text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), static_cast<int>(text.size()), nullptr, 0);
    require(count > 0, "invalid UTF-8 profile name");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                       static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string utf8(const wchar_t *text) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    require(count > 0, "invalid credential text");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    return result;
}

vpn::Credentials prompt_credentials(const std::string &name) {
    const std::wstring message = L"请输入 " + wide(name) +
        L" 的 VPN 凭据，仅用于本次核心连接验证。";
    CREDUI_INFOW info{};
    info.cbSize = sizeof(info);
    info.pszCaptionText = L"OpenVPN 核心连接验证";
    info.pszMessageText = message.c_str();
    wchar_t user[CREDUI_MAX_USERNAME_LENGTH + 1]{};
    wchar_t password[CREDUI_MAX_PASSWORD_LENGTH + 1]{};
    const auto suggested = wide(name.substr(0, name.find('@')));
    wcsncpy_s(user, suggested.c_str(), _TRUNCATE);
    BOOL save = FALSE;
    report("WAITING_FOR_USER_CREDENTIAL_INPUT=1");
    const auto status = CredUIPromptForCredentialsW(&info, L"OpenVPN Core Probe", nullptr,
        0, user, ARRAYSIZE(user), password, ARRAYSIZE(password), &save,
        CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI |
        CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_EXCLUDE_CERTIFICATES);
    if (status != NO_ERROR) {
        SecureZeroMemory(password, sizeof(password));
        throw std::runtime_error("user cancelled credential input");
    }
    vpn::Credentials result;
    result.username = utf8(user);
    result.password = utf8(password);
    SecureZeroMemory(password, sizeof(password));
    return result;
}

std::string read_file(const std::filesystem::path &path) {
    require(std::filesystem::file_size(path) <= 16 * 1024 * 1024,
            "profile exceeds size limit");
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "unable to read profile");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::set<NET_IFINDEX> interfaces() {
    ULONG size = 16384;
    std::vector<unsigned char> buffer(size);
    ULONG status;
    do {
        buffer.resize(size);
        status = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
    } while (status == ERROR_BUFFER_OVERFLOW);
    require(status == NO_ERROR, "unable to read adapters");
    std::set<NET_IFINDEX> result;
    for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
         adapter; adapter = adapter->Next)
        result.insert(adapter->IfIndex);
    return result;
}

struct NetworkState {
    std::set<std::pair<ULONG, UINT8>> addresses;
    std::set<Route> routes;
    bool addresses_ready = true;
    bool operator==(const NetworkState &) const = default;
};

NetworkState network_state(NET_IFINDEX index) {
    NetworkState state;
    MIB_UNICASTIPADDRESS_TABLE *addresses = nullptr;
    require(GetUnicastIpAddressTable(AF_INET, &addresses) == NO_ERROR,
            "unable to read addresses");
    for (ULONG i = 0; i < addresses->NumEntries; ++i) {
        const auto &row = addresses->Table[i];
        if (row.InterfaceIndex == index) {
            state.addresses.emplace(row.Address.Ipv4.sin_addr.S_un.S_addr,
                                     row.OnLinkPrefixLength);
            state.addresses_ready &= row.DadState == IpDadStatePreferred;
        }
    }
    FreeMibTable(addresses);
    MIB_IPFORWARD_TABLE2 *routes = nullptr;
    require(GetIpForwardTable2(AF_INET, &routes) == NO_ERROR,
            "unable to read routes");
    for (ULONG i = 0; i < routes->NumEntries; ++i) {
        const auto &row = routes->Table[i];
        if (row.InterfaceIndex == index)
            state.routes.emplace(row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr,
                                  row.DestinationPrefix.PrefixLength,
                                  row.NextHop.Ipv4.sin_addr.S_un.S_addr, row.Metric);
    }
    FreeMibTable(routes);
    return state;
}

NetworkState wait_network_ready(NET_IFINDEX index) {
    auto previous = network_state(index);
    unsigned int stable = 0;
    for (unsigned int attempt = 0; attempt < 120; ++attempt) {
        std::this_thread::sleep_for(100ms);
        auto current = network_state(index);
        // The official Setup intentionally removes Windows' auto-created default
        // route after its five-second network-profile initialization timer.
        const bool rogue_default = std::any_of(current.routes.begin(), current.routes.end(),
            [](const Route &route) { return std::get<0>(route) == 0 && std::get<1>(route) == 0; });
        if (!rogue_default && !current.addresses.empty() && current.addresses_ready && current == previous) {
            if (++stable == 3) return current;
        } else stable = 0;
        previous = std::move(current);
    }
    throw std::runtime_error("Windows IP/route configuration did not become ready");
}

void require_unchanged(NET_IFINDEX index, const NetworkState &before, const char *message) {
    const auto after = wait_network_ready(index);
    if (before == after) return;
    report("NETWORK_DIFF interface=" + std::to_string(index) +
           " addresses=" + std::to_string(before.addresses.size()) + "->" +
           std::to_string(after.addresses.size()) + " routes=" +
           std::to_string(before.routes.size()) + "->" + std::to_string(after.routes.size()));
    for (const auto &[address, prefix, gateway, metric] : before.routes)
        if (!after.routes.contains({address, prefix, gateway, metric}))
            report("REMOVED_ROUTE=" + std::to_string(address) + "/" + std::to_string(prefix));
    for (const auto &[address, prefix, gateway, metric] : after.routes)
        if (!before.routes.contains({address, prefix, gateway, metric}))
            report("ADDED_ROUTE=" + std::to_string(address) + "/" + std::to_string(prefix));
    throw std::runtime_error(message);
}

Json::Value command(const char *name, const std::string &id = {}) {
    Json::Value request(Json::objectValue);
    request["command"] = name;
    if (!id.empty()) request["sessionId"] = id;
    return request;
}

void wait_connected(vpn::daemon::SessionController &controller, const std::string &id) {
    const auto deadline = std::chrono::steady_clock::now() + 40s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = controller.handleRequest(command("snapshot"));
        for (const auto &session : snapshot["sessions"]) {
            if (session["sessionId"].asString() != id) continue;
            const auto state = session["state"].asString();
            if (state == "connected") {
                report("CONNECTED=" + id);
                return;
            }
            require(state != "failed" && state != "disconnected",
                    "core reported connection failure; inspect event names and agent log");
        }
        std::this_thread::sleep_for(100ms);
    }
    throw std::runtime_error("probe connection deadline exceeded");
}

void require_interface_removed(NET_IFINDEX index) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!interfaces().contains(index)) return;
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("disconnected session's adapter was not removed");
}

void require_fixture_dns(bool first, bool second) {
    std::set<std::wstring> domains;
    for (const auto *base : {
        L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient\\DnsPolicyConfig",
        L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters\\DnsPolicyConfig"}) {
        HKEY root = nullptr;
        const auto status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, base, 0, KEY_READ, &root);
        if (status == ERROR_FILE_NOT_FOUND) continue;
        require(status == ERROR_SUCCESS, "unable to inspect fixture DNS rules");
        for (DWORD i = 0;; ++i) {
            wchar_t name[512];
            DWORD length = ARRAYSIZE(name);
            const auto enumerated = RegEnumKeyExW(root, i, name, &length, nullptr,
                                                   nullptr, nullptr, nullptr);
            if (enumerated == ERROR_NO_MORE_ITEMS) break;
            require(enumerated == ERROR_SUCCESS, "unable to enumerate fixture DNS rules");
            HKEY rule = nullptr;
            if (RegOpenKeyExW(root, name, 0, KEY_READ, &rule) != ERROR_SUCCESS) continue;
            wchar_t values[4096]{};
            DWORD size = sizeof(values), type = 0;
            if (RegQueryValueExW(rule, L"Name", nullptr, &type,
                    reinterpret_cast<BYTE *>(values), &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
                for (const wchar_t *domain = values; *domain && domain < values + ARRAYSIZE(values);
                     domain += wcslen(domain) + 1)
                    domains.insert(domain);
            }
            RegCloseKey(rule);
        }
        RegCloseKey(root);
    }
    require(domains.contains(L".fixture-0.invalid") == first,
            "first session DNS rule was overwritten, removed, or leaked");
    require(domains.contains(L".fixture-1.invalid") == second,
            "second session DNS rule was overwritten, removed, or leaked");
    report("FIXTURE_DNS_ISOLATED=" + std::to_string(first) + "," + std::to_string(second));
}

void require_fixture_ipv6_block(bool enabled) {
    MIB_IPFORWARD_TABLE2 *routes = nullptr;
    require(GetIpForwardTable2(AF_INET6, &routes) == NO_ERROR,
            "unable to inspect IPv6 leak protection");
    unsigned int count = 0;
    for (ULONG i = 0; i < routes->NumEntries; ++i) {
        const auto &row = routes->Table[i];
        if (row.InterfaceIndex != 1) continue;
        const auto prefix = row.DestinationPrefix.PrefixLength;
        const auto first = row.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte[0];
        if ((prefix == 4 && (first == 0x20 || first == 0x30)) ||
            (prefix == 7 && first == 0xfc)) ++count;
    }
    FreeMibTable(routes);
    report("FIXTURE_IPV6_BLOCK_ROUTES=" + std::to_string(count));
    require(count == (enabled ? 3u : 0u), "IPv6 leak protection was removed or leaked");
}

// A loopback peer that accepts TCP but never answers the TLS handshake.
// This exercises the production deadline without external network conditions.
class BlackholePeer {
  public:
    BlackholePeer() {
        WSADATA data{};
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(listener_ != INVALID_SOCKET, "unable to create loopback peer");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
                "unable to bind loopback peer");
        require(listen(listener_, 1) == 0, "unable to listen on loopback peer");
        int size = sizeof(address);
        require(getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0,
                "unable to read loopback port");
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] {
            const auto peer = accept(listener_, nullptr, nullptr);
            if (peer == INVALID_SOCKET) return;
            std::unique_lock<std::mutex> lock(mutex_);
            accepted_ = true;
            stopped_.notify_all();
            stopped_.wait(lock, [this] { return stop_; });
            closesocket(peer);
        });
    }
    ~BlackholePeer() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        stopped_.notify_all();
        closesocket(listener_);
        worker_.join();
        WSACleanup();
    }
    unsigned short port() const { return port_; }
    bool wait_for_connection() {
        std::unique_lock<std::mutex> lock(mutex_);
        return stopped_.wait_for(lock, 5s, [this] { return accepted_; });
    }
  private:
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable stopped_;
    bool stop_ = false;
    bool accepted_ = false;
};

int run(const std::filesystem::path &manifest, bool preflight, bool fixture, bool crash, bool failures, bool strict,
        const std::filesystem::path &reference, bool reference_fixture) {
    Json::Value root;
    std::istringstream source(read_file(manifest));
    source >> root;
    const auto &profiles = root["profiles"];
    require(profiles.isArray() && profiles.size() == 2,
            "probe expects the two imported profiles");
    vpn::platform::windows::WindowsCredentialStore credentials;
    struct EphemeralCredentials {
        std::map<std::string, vpn::Credentials> values;
        ~EphemeralCredentials() {
            for (auto &[id, value] : values)
                if (!value.password.empty()) SecureZeroMemory(value.password.data(), value.password.size());
        }
    } ephemeral;
    for (const auto &profile : profiles) {
        if (fixture) {
            ephemeral.values[profile["id"].asString()] =
                {"fixture-user", "fixture-only", "", ""};
            report("CERTIFICATE_FIXTURE=" + profile["id"].asString());
            continue;
        }
        vpn::Credentials stored;
        const auto result = credentials.load(profile["id"].asString(), &stored);
        report("CREDENTIAL_STATUS=" + profile["id"].asString() + ":" +
               (result.ok() ? "available" : "unavailable"));
        if (!stored.password.empty()) SecureZeroMemory(stored.password.data(), stored.password.size());
        if (!result.ok()) {
            require(!preflight, "saved VPN credential is unavailable; user input is needed");
            ephemeral.values[profile["id"].asString()] =
                prompt_credentials(profile["name"].asString());
        }
    }
    if (preflight) return 0;
    if (reference_fixture) {
        const auto result = official_auth_reference(reference,
            manifest.parent_path() / "profiles" / profiles[0]["managedFile"].asString(),
            ephemeral.values.at(profiles[0]["id"].asString()));
        report("OFFICIAL_AUTH_REFERENCE=" + result);
        require(result == "AUTH_ACCEPTED", "official authentication reference fixture failed");
        return 0;
    }
    if (fixture) require_fixture_ipv6_block(false);

    std::mutex selected_mutex;
    std::map<std::string, NET_IFINDEX> selected_interfaces;
    std::set<std::string> authentication_rejected;
    vpn::daemon::SessionController controller([&](const Json::Value &event) {
        if (event.get("type", "").asString() == "sessionEvent") {
            const auto name = event.get("event", "").asString();
            const auto id = event["sessionId"].asString();
            if (name == "AUTH_FAILED") {
                std::lock_guard<std::mutex> lock(selected_mutex);
                authentication_rejected.insert(id);
            }
            if (name != "LOG") report("EVENT=" + id + ":" + name);
            else {
                // Read only our adapter marker, never print the engine log payload.
                const auto message = event.get("message", "").asString();
                const auto marker = message.find("Session Wintun adapter: ");
                const auto index = marker == std::string::npos ? marker :
                    message.find(" interface=", marker);
                if (index != std::string::npos) {
                    std::lock_guard<std::mutex> lock(selected_mutex);
                    selected_interfaces[id] = static_cast<NET_IFINDEX>(
                        std::stoul(message.substr(index + 11)));
                }
            }
        }
    }, &credentials);
    const auto prefix = "core-probe-" + std::to_string(_getpid()) + "-";
    if (failures) {
        const auto content = read_file(manifest.parent_path() / "profiles" /
                                       profiles[0]["managedFile"].asString());
        for (const bool cancel : {true, false}) {
            BlackholePeer peer;
            auto request = command("connect", prefix + (cancel ? "cancel" : "timeout"));
            request["profileId"] = profiles[0]["id"];
            request["name"] = "Loopback TLS blackhole";
            auto profile = content;
            const auto remote = profile.find("remote 127.0.0.1 21194");
            require(remote != std::string::npos, "fixture remote was not found");
            profile.replace(remote, std::string("remote 127.0.0.1 21194").size(),
                            "remote 127.0.0.1 " + std::to_string(peer.port()));
            request["profile"] = profile;
            request["credentials"]["username"] = "fixture-user";
            request["credentials"]["password"] = "fixture-only";
            const auto started = std::chrono::steady_clock::now();
            require(controller.handleRequest(request).get("ok", false).asBool(),
                    "core rejected failure-scenario request");
            const auto id = request["sessionId"].asString();
            if (cancel) {
                require(peer.wait_for_connection(), "core never reached the TLS peer");
                std::this_thread::sleep_for(100ms);
                const auto cancelling = std::chrono::steady_clock::now();
                controller.handleRequest(command("disconnect", id));
                require(std::chrono::steady_clock::now() - cancelling < 3s,
                        "cancelling TLS handshake did not finish promptly");
                report("HANDSHAKE_CANCEL_PASS=1");
            } else {
                bool failed = false;
                while (std::chrono::steady_clock::now() - started < 35s) {
                    const auto snapshot = controller.handleRequest(command("snapshot"));
                    for (const auto &session : snapshot["sessions"])
                        if (session["sessionId"].asString() == id && session["state"] == "failed")
                            failed = true;
                    if (failed) break;
                    std::this_thread::sleep_for(100ms);
                }
                const auto elapsed = std::chrono::steady_clock::now() - started;
                require(failed && elapsed >= 25s, "shared connection deadline did not terminate TLS handshake");
                report("CONNECTION_TIMEOUT_PASS=1 elapsed_ms=" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
                controller.handleRequest(command("disconnect", id));
            }
        }
        controller.stopAll();
        require_fixture_dns(false, false);
        require_fixture_ipv6_block(false);
        report("CORE_FAILURE_SCENARIOS_PASS=1");
        return 0;
    }
    auto connect = [&](unsigned int i) {
        const auto &profile = profiles[i];
        auto request = command("connect", prefix + std::to_string(i));
        request["profileId"] = profile["id"];
        request["name"] = profile["name"];
        request["routeStrategy"] = profile["routeStrategy"];
        if (strict && i == 1) request["routeStrategy"]["conflictMode"] = "strict";
        const auto provided = ephemeral.values.find(profile["id"].asString());
        if (provided != ephemeral.values.end()) {
            request["credentials"]["username"] = provided->second.username;
            request["credentials"]["password"] = provided->second.password;
        }
        const auto managed = profile.get("managedFile", "").asString();
        if (!managed.empty()) {
            require(std::filesystem::path(managed).filename() == managed,
                    "invalid managed profile name");
            request["profile"] = read_file(manifest.parent_path() / "profiles" / managed);
        } else {
            const auto path = profile["path"].asString();
            request["profile"] = read_file(std::filesystem::path(
                std::u8string(path.begin(), path.end())));
        }
        const auto result = controller.handleRequest(request);
        require(result.get("ok", false).asBool(), "core rejected connection request");
        const auto id = request["sessionId"].asString();
        try { wait_connected(controller, id); }
        catch (...) {
            bool rejected = false;
            { std::lock_guard<std::mutex> lock(selected_mutex); rejected = authentication_rejected.contains(id); }
            if (rejected && !reference.empty() && provided != ephemeral.values.end() && !managed.empty()) {
                report("OFFICIAL_AUTH_REFERENCE_STARTED=1");
                report("OFFICIAL_AUTH_REFERENCE=" + official_auth_reference(reference,
                    manifest.parent_path() / "profiles" / managed, provided->second));
            }
            throw;
        }
        std::lock_guard<std::mutex> lock(selected_mutex);
        require(selected_interfaces.contains(id), "service did not report its selected interface");
        report("SELECTED_INTERFACE=" + id + ":" + std::to_string(selected_interfaces.at(id)));
        return selected_interfaces.at(id);
    };

    const auto first = connect(0);
    const auto first_state = wait_network_ready(first);
    if (fixture) require_fixture_dns(true, false);
    if (fixture) require_fixture_ipv6_block(true);
    report("FIRST_NETWORK_STATE interface=" + std::to_string(first) +
           " addresses=" + std::to_string(first_state.addresses.size()) +
           " routes=" + std::to_string(first_state.routes.size()));
    require(!first_state.addresses.empty() && !first_state.routes.empty(),
            "first connection has no configured addresses/routes");
    if (strict) {
        bool rejected = false;
        try { connect(1); } catch (const std::runtime_error &) {
            const auto snapshot = controller.handleRequest(command("snapshot"));
            for (const auto &session : snapshot["sessions"])
                if (session["sessionId"] == prefix + "1" && session["state"] == "failed") {
                    const auto error = session.get("error", "").asString();
                    report("STRICT_REJECTION_ERROR=" + error);
                    rejected = error.find("重叠") != std::string::npos || error.find("冲突") != std::string::npos;
                    for (const auto &conflict : session["routeConflicts"])
                        rejected |= conflict.get("action", "").asString() == "blocked";
                    report("STRICT_CONFLICT_RECORDS=" + std::to_string(session["routeConflicts"].size()));
                }
        }
        require(rejected, "strict route overlap was not reported as a failure");
        require_unchanged(first, first_state, "strict rejection modified the first connection");
        require_fixture_dns(true, false);
        require_fixture_ipv6_block(true);
        controller.stopAll();
        require_interface_removed(first);
        require_fixture_dns(false, false);
        require_fixture_ipv6_block(false);
        report("STRICT_ROUTE_CONFLICT_PASS=1");
        return 0;
    }
    const auto second = connect(1);
    if (fixture) require_fixture_dns(true, true);
    if (fixture) require_fixture_ipv6_block(true);
    require(first != second, "both connections selected the same interface");
    require_unchanged(first, first_state,
            "starting second connection modified first connection's IP/routes");
    report("BOTH_CONNECTED=1 interfaces=" + std::to_string(first) + "," + std::to_string(second));
    if (crash) {
        report("SIMULATING_CLIENT_CRASH=1");
        TerminateProcess(GetCurrentProcess(), 97); // Deliberately skip all C++ destructors.
        return 97;
    }
    std::this_thread::sleep_for(5s);
    require_unchanged(first, first_state, "first connection lost its IP/routes while both online");

    controller.handleRequest(command("disconnect", prefix + "1"));
    require_interface_removed(second);
    if (fixture) require_fixture_dns(true, false);
    if (fixture) require_fixture_ipv6_block(true);
    require_unchanged(first, first_state,
            "disconnecting second connection modified first connection's IP/routes");
    report("FIRST_SURVIVED_SECOND_DISCONNECT=1\nSECOND_ADAPTER_CLEANED=1");

    const auto second_again = connect(1);
    const auto second_state = wait_network_ready(second_again);
    if (fixture) require_fixture_dns(true, true);
    require_unchanged(first, first_state, "reconnecting second modified first connection");
    report("SECOND_RECONNECTED=1");
    controller.handleRequest(command("disconnect", prefix + "0"));
    require_interface_removed(first);
    if (fixture) require_fixture_dns(false, true);
    if (fixture) require_fixture_ipv6_block(true);
    require_unchanged(second_again, second_state,
            "disconnecting first connection modified second connection's IP/routes");
    report("SECOND_SURVIVED_FIRST_DISCONNECT=1");
    controller.stopAll();
    require_interface_removed(second_again);
    if (fixture) require_fixture_dns(false, false);
    if (fixture) require_fixture_ipv6_block(false);
    report("CORE_CONNECTION_PROBE_PASS=1");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 3 && argc != 4) return 2;
    try {
        const std::wstring mode(argv[1]);
        if (mode == L"--fixture-check-cleanup") {
            require_fixture_dns(false, false);
            require_fixture_ipv6_block(false);
            report("CRASH_CLEANUP_PASS=1");
            return 0;
        }
        require(mode == L"--fixture" || mode == L"--fixture-crash" || mode == L"--fixture-failures" || mode == L"--fixture-strict" ||
                mode == L"--fixture-reference" || mode == L"--run-compare" ||
                mode == L"--run" || mode == L"--preflight", "unknown probe mode");
        require((mode == L"--run-compare" || mode == L"--fixture-reference") == (argc == 4), "reference mode requires official engine path");
        return run(std::filesystem::path(argv[2]), mode == L"--preflight",
                   mode.starts_with(L"--fixture"), mode == L"--fixture-crash", mode == L"--fixture-failures", mode == L"--fixture-strict",
                   argc == 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{}, mode == L"--fixture-reference");
    } catch (const std::exception &error) {
        report(std::string("PROBE_FAILED=") + error.what());
        return 1;
    }
}
