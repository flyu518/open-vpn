// Explicit elevated OS-resource test. No VPN credentials or external servers.
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include "shared_ipv6_block.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

void require(bool ok, const char *error) { if (!ok) throw std::runtime_error(error); }
MIB_IPFORWARD_ROW2 route(unsigned int i) {
    MIB_IPFORWARD_ROW2 row;
    InitializeIpForwardEntry(&row);
    row.InterfaceIndex = 1;
    row.DestinationPrefix.Prefix.si_family = AF_INET6;
    row.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte[0] = i == 0 ? 0x20 : i == 1 ? 0x30 : 0xfc;
    row.DestinationPrefix.PrefixLength = i == 2 ? 7 : 4;
    row.NextHop.si_family = AF_INET6;
    row.Protocol = MIB_IPPROTO_NETMGMT;
    row.Metric = 256;
    return row;
}
unsigned int count() {
    unsigned int found = 0;
    for (unsigned int i = 0; i < 3; ++i) {
        auto row = route(i);
        const auto result = GetIpForwardEntry2(&row);
        require(result == NO_ERROR || result == ERROR_NOT_FOUND, "cannot inspect IPv6 routes");
        found += result == NO_ERROR;
    }
    return found;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) return 2;
    std::ofstream log{std::filesystem::path(argv[2])};
    try {
        const std::wstring mode(argv[1]);
        if (mode == L"--assert-busy") {
            bool rejected = false;
            try { vpn::windows::SharedIpv6Block contender; }
            catch (const std::runtime_error &error) {
                rejected = std::string(error.what()) == "IPv6 route owner is already active";
            }
            require(rejected, "a second live owner was allowed to recover active routes");
            log << "LIVE_OWNER_REJECTION_PASS=1\n";
            return 0;
        }
        if (mode == L"--live-owner") {
            require(count() == 0, "pre-existing IPv6 routes: refusing live-owner test");
            vpn::windows::SharedIpv6Block manager;
            auto lease = manager.acquire();
            wchar_t executable[32768]{};
            require(GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable)) != 0, "cannot find probe executable");
            auto child_log = std::filesystem::path(argv[2]).parent_path() / "ipv6-owner-contender.log";
            auto command = L"\"" + std::wstring(executable) + L"\" --assert-busy \"" + child_log.wstring() + L"\"";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "cannot create contender probe");
            CloseHandle(process.hThread);
            const auto waited = WaitForSingleObject(process.hProcess, 5000);
            if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 3);
            DWORD result = 3; GetExitCodeProcess(process.hProcess, &result); CloseHandle(process.hProcess);
            require(waited == WAIT_OBJECT_0 && result == 0, "cross-process owner gate failed");
            require(count() == 3, "contender removed active owner's routes");
            lease.reset();
            require(count() == 0, "live-owner scenario leaked routes");
            log << "LIVE_OWNER_ISOLATION_PASS=1\n";
            return 0;
        }
        if (mode == L"--crash") {
            require(count() == 0, "pre-existing IPv6 sink routes: refusing crash test");
            vpn::windows::SharedIpv6Block manager;
            auto lease = manager.acquire();
            require(count() == 3, "IPv6 sink route setup failed");
            log << "OWNED_ROUTES_BEFORE_CRASH=3\n" << std::flush;
            TerminateProcess(GetCurrentProcess(), 97);
            return 97;
        }
        if (mode == L"--recover") {
            require(count() == 3, "crash routes missing: recovery test would be vacuous");
            vpn::windows::SharedIpv6Block manager;
            require(count() == 0, "service restart did not recover its crash-owned routes");
            auto first = manager.acquire();
            auto second = manager.acquire();
            first.reset();
            require(count() == 3, "recovered manager lost surviving lease");
            second.reset();
            require(count() == 0, "recovered manager leaked routes");
            log << "SERVICE_CRASH_ROUTE_RECOVERY_PASS=1\n";
            return 0;
        }
        require(mode == L"--preserve-existing", "unknown mode");
        require(count() == 0, "pre-existing IPv6 sink routes: refusing fixture setup");
        struct Fixture {
            bool owned[3]{};
            ~Fixture() { for (unsigned int i = 0; i < 3; ++i) if (owned[i]) { auto row = route(i); DeleteIpForwardEntry2(&row); } }
        } fixture;
        for (unsigned int i = 0; i < 3; ++i) {
            auto row = route(i);
            require(CreateIpForwardEntry2(&row) == NO_ERROR, "cannot create fixture-owned IPv6 route");
            fixture.owned[i] = true;
        }
        {
            vpn::windows::SharedIpv6Block manager;
            auto lease = manager.acquire();
            lease.reset();
        }
        require(count() == 3, "manager removed another owner's existing IPv6 route");
        log << "EXISTING_ROUTE_PRESERVATION_PASS=1\n";
        return 0;
    } catch (const std::exception &error) {
        log << "PROBE_FAILED=" << error.what() << '\n';
        return 1;
    }
}
