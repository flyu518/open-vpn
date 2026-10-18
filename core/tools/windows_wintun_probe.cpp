// Standalone driver validation. No server credentials, routes or DNS changes.
#include <openvpn/io/io.hpp>
#include <openvpn/log/logbase.hpp>
#include "session_wintun_setup.h"
#include "wintun_adapter_lease.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace openvpn;

DWORD register_rings(HANDLE handle, const TunWin::RingBuffer::Ptr &buffer) {
    TunWin::TUN_REGISTER_RINGS rings{};
    rings.receive.ring = buffer->receive_ring();
    rings.receive.tail_moved = buffer->receive_ring_tail_moved();
    rings.receive.ring_size = sizeof(rings.receive.ring->data);
    rings.send.ring = buffer->send_ring();
    rings.send.tail_moved = buffer->send_ring_tail_moved();
    rings.send.ring_size = sizeof(rings.send.ring->data);
    DWORD returned = 0;
    return DeviceIoControl(handle, TUN_IOCTL_REGISTER_RINGS, &rings, sizeof(rings),
                           nullptr, 0, &returned, nullptr)
               ? ERROR_SUCCESS : GetLastError();
}

struct Session {
    std::unique_ptr<vpn::windows::WintunAdapterLease> lease;
    TunWin::Setup::Ptr setup;
    TunWin::RingBuffer::Ptr buffer;
    Win::ScopedHANDLE handle;
};

std::unique_ptr<Session> start_session(openvpn_io::io_context &io,
                                     std::ostream &log) {
    auto session = std::make_unique<Session>();
    session->lease = std::make_unique<vpn::windows::WintunAdapterLease>();
    session->setup.reset(new vpn::windows::SessionWintunSetup(
        io, false, session->lease->name()));
    // Exercise virtual dispatch through the same base type used by the agent.
    session->handle.reset(session->setup->get_handle(log));
    session->buffer.reset(new TunWin::RingBuffer(io));
    const auto error = register_rings(session->handle(), session->buffer);
    log << "REGISTER_RINGS=" << error << '\n';
    if (error != ERROR_SUCCESS)
        throw std::runtime_error("ring registration failed: " + std::to_string(error));
    return session;
}

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

int run(bool baseline, std::ostream &log) {
    openvpn_io::io_context io;
    auto first = start_session(io, log);
    const auto first_tap = first->setup->get_adapter_state();

    if (baseline) {
        // Reproduce the old unscoped enumeration with ONLY probe-owned devices.
        // Never open any adapter belonging to the installed app or another VPN.
        vpn::windows::WintunAdapterLease second_lease;
        TunWin::Util::TapNameGuidPairList adapters(TunWin::Wintun);
        const auto first_name = wstring::to_utf8(first->lease->name());
        const auto second_name = wstring::to_utf8(second_lease.name());
        adapters.erase(std::remove_if(adapters.begin(), adapters.end(),
            [&](const auto &adapter) {
                return adapter.name != first_name && adapter.name != second_name;
            }), adapters.end());
        std::stable_sort(adapters.begin(), adapters.end(),
            [&](const auto &a, const auto &b) {
                return a.name == first_name && b.name != first_name;
            });
        TunWin::Util::TapNameGuidPair selected;
        std::string path;
        Win::ScopedHANDLE handle(TunWin::Util::tap_open(
            TunWin::Wintun, adapters, path, selected));
        require(handle.defined(), "baseline open failed");
        TunWin::RingBuffer::Ptr buffer(new TunWin::RingBuffer(io));
        const auto error = register_rings(handle(), buffer);
        log << "BASELINE_SELECTED=" << selected.name << '\n'
            << "BASELINE_REGISTER_RINGS=" << error << '\n';
        // Close the handle before freeing memory mapped into the driver.
        handle.reset();
        require(selected.index == first_tap.index && error == ERROR_ALREADY_INITIALIZED,
                "baseline did not reproduce adapter reuse/ring registration failure");
        log << "BASELINE_REPRODUCED=1\n";
        return 0;
    }

    auto second = start_session(io, log);
    const auto second_tap = second->setup->get_adapter_state();
    require(first_tap.index != second_tap.index && first_tap.guid != second_tap.guid,
            "two sessions selected the same adapter");
    log << "DISTINCT_INTERFACES=" << first_tap.index << ',' << second_tap.index << '\n';

    first.reset();
    MIB_IF_ROW2 survivor{};
    survivor.InterfaceIndex = second_tap.index;
    require(GetIfEntry2(&survivor) == NO_ERROR,
            "closing first session removed the second adapter");

    vpn::windows::SessionWintunSetup duplicate(io, false, second->lease->name());
    Win::ScopedHANDLE duplicate_handle(duplicate.get_handle(log));
    TunWin::RingBuffer::Ptr duplicate_buffer(new TunWin::RingBuffer(io));
    const auto duplicate_error = register_rings(duplicate_handle(), duplicate_buffer);
    duplicate_handle.reset();
    require(duplicate_error == ERROR_ALREADY_INITIALIZED,
            "second session's ring registration did not survive closing the first");
    log << "SURVIVOR_RING_STILL_REGISTERED=1\n";

    second->handle.reset();
    vpn::windows::SessionWintunSetup reopened(io, false, second->lease->name());
    Win::ScopedHANDLE reopened_handle(reopened.get_handle(log));
    const auto reopen_error = register_rings(reopened_handle(), duplicate_buffer);
    reopened_handle.reset();
    require(reopen_error == ERROR_SUCCESS, "closed session did not release ring ownership");
    log << "CLOSE_AND_REOPEN_REGISTER_RINGS=0\n";
    second.reset();
    log << "DRIVER_ISOLATION_PASS=1\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::cerr << "usage: openvpn_windows_wintun_probe --fixed|--baseline output.log\n";
        return 2;
    }
    std::ofstream log{std::filesystem::path(argv[2])};
    if (!log)
        return 3;
    try {
        return run(std::wstring(argv[1]) == L"--baseline", log);
    } catch (const std::exception &error) {
        log << "PROBE_FAILED=" << error.what() << '\n';
        return 1;
    }
}
