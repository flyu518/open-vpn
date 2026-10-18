#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "shared_ipv6_block.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <rpc.h>
#include <sddl.h>
#include <stdexcept>
#include <string>

namespace vpn::windows {
namespace {
MIB_IPFORWARD_ROW2 block_route(unsigned int index, DWORD metric) {
    MIB_IPFORWARD_ROW2 row;
    InitializeIpForwardEntry(&row);
    row.InterfaceIndex = 1;
    row.DestinationPrefix.Prefix.si_family = AF_INET6;
    row.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte[0] =
        index == 0 ? 0x20 : index == 1 ? 0x30 : 0xfc;
    row.DestinationPrefix.PrefixLength = index == 2 ? 7 : 4;
    row.NextHop.si_family = AF_INET6;
    row.Protocol = MIB_IPPROTO_NETMGMT;
    row.Metric = metric;
    return row;
}

void checked(DWORD status, const char *operation) {
    if (status != NO_ERROR)
        throw std::runtime_error(std::string(operation) + " (Win32 " + std::to_string(status) + ")");
}

// A protected write-ahead ownership record survives a service crash. The
// random metric tags only routes created by this product; existing official
// client routes never receive this tag and are never reclaimed by us.
class OwnerGate {
  public:
    OwnerGate() {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr))
            checked(GetLastError(), "create IPv6 owner gate security descriptor");
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        handle_ = CreateMutexW(&security, FALSE, L"Global\\OpenVPNMultiSessionIpv6RouteOwner");
        const auto error = GetLastError();
        LocalFree(descriptor);
        if (!handle_) checked(error, "create IPv6 route owner gate");
        // Object lifetime is the gate: reject an existing object even on the
        // same thread, rather than allowing recursive Windows mutex ownership.
        if (error == ERROR_ALREADY_EXISTS) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("IPv6 route owner is already active");
        }
    }
    ~OwnerGate() { if (handle_) CloseHandle(handle_); }
    OwnerGate(const OwnerGate &) = delete;
    OwnerGate &operator=(const OwnerGate &) = delete;
  private:
    HANDLE handle_ = nullptr;
};

class PersistentRoutes {
  public:
    PersistentRoutes() {
        checked(RegCreateKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\OpenVPNMultiSession\\NetworkState", 0, nullptr, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key_, nullptr),
            "open IPv6 route ownership record");
        try {
            DWORD previous = 0, size = sizeof(previous), type = 0;
            const auto result = RegQueryValueExW(key_, L"Ipv6SinkMetric", nullptr, &type,
                reinterpret_cast<BYTE *>(&previous), &size);
            if (result == ERROR_SUCCESS) {
                if (type != REG_DWORD || size != sizeof(previous) ||
                    previous < 0x40000000 || previous > 0x7fffffff)
                    throw std::runtime_error("invalid IPv6 route ownership record");
                remove_owned(previous);
                checked(RegDeleteValueW(key_, L"Ipv6SinkMetric"), "clear stale IPv6 route ownership");
                checked(RegFlushKey(key_), "flush recovered IPv6 route ownership");
            } else if (result != ERROR_FILE_NOT_FOUND) checked(result, "read IPv6 route ownership");
            UUID id{};
            const auto uuid_status = UuidCreate(&id);
            if (uuid_status != RPC_S_OK && uuid_status != RPC_S_UUID_LOCAL_ONLY)
                checked(uuid_status, "create IPv6 route ownership tag");
            metric_ = 0x40000000 | (id.Data1 & 0x3fffffff);
        } catch (...) {
            RegCloseKey(key_);
            throw;
        }
    }
    ~PersistentRoutes() {
        try {
            remove_owned(metric_);
            if (journaled_) {
                // Do not clear a record replaced by a newer service instance.
                DWORD value = 0, size = sizeof(value);
                if (RegQueryValueExW(key_, L"Ipv6SinkMetric", nullptr, nullptr,
                        reinterpret_cast<BYTE *>(&value), &size) == ERROR_SUCCESS && value == metric_) {
                    checked(RegDeleteValueW(key_, L"Ipv6SinkMetric"), "clear IPv6 route ownership");
                    RegFlushKey(key_);
                }
            }
        } catch (...) {
            // Keep the ownership record so the next service start can retry.
        }
        RegCloseKey(key_);
    }
    bool create(unsigned int index) {
        if (!journaled_) {
            checked(RegSetValueExW(key_, L"Ipv6SinkMetric", 0, REG_DWORD,
                reinterpret_cast<const BYTE *>(&metric_), sizeof(metric_)), "record IPv6 route ownership");
            checked(RegFlushKey(key_), "flush IPv6 route ownership");
            journaled_ = true;
        }
        auto row = block_route(index, metric_);
        const auto result = CreateIpForwardEntry2(&row);
        if (result == ERROR_OBJECT_ALREADY_EXISTS || result == ERROR_ALREADY_EXISTS)
            return false;
        checked(result, "create IPv6 leak-protection route");
        return true;
    }
    void remove(unsigned int index) { remove_one(index, metric_); }
  private:
    static void remove_one(unsigned int index, DWORD metric) {
        auto row = block_route(index, metric);
        const auto result = GetIpForwardEntry2(&row);
        if (result == ERROR_NOT_FOUND) return;
        checked(result, "read owned IPv6 route");
        if (row.Metric != metric || row.Protocol != MIB_IPPROTO_NETMGMT) return;
        checked(DeleteIpForwardEntry2(&row), "remove owned IPv6 route");
    }
    static void remove_owned(DWORD metric) {
        for (unsigned int i = 0; i < 3; ++i) remove_one(i, metric);
    }
    OwnerGate gate_; // Must precede opening/recovering any ownership journal.
    HKEY key_ = nullptr;
    DWORD metric_ = 0;
    bool journaled_ = false;
};
}

SharedIpv6Block::SharedIpv6Block() {
    auto routes = std::make_shared<PersistentRoutes>();
    api_ = {[routes](unsigned int i) { return routes->create(i); },
            [routes](unsigned int i) { routes->remove(i); }};
}
} // namespace vpn::windows
