#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <string>

namespace vpn::windows {

class WintunAdapterLease {
  public:
    using CreateAdapter = void *(WINAPI *)(LPCWSTR, LPCWSTR, const GUID *);
    using CloseAdapter = void(WINAPI *)(void *);
    using ReleaseLibrary = BOOL(WINAPI *)(HMODULE);

    struct Api {
        HMODULE library = nullptr;
        CreateAdapter create_adapter = nullptr;
        CloseAdapter close_adapter = nullptr;
        ReleaseLibrary release_library = nullptr;
    };

    WintunAdapterLease();
    WintunAdapterLease(Api api, const std::wstring &name);
    ~WintunAdapterLease();

    WintunAdapterLease(const WintunAdapterLease &) = delete;
    WintunAdapterLease &operator=(const WintunAdapterLease &) = delete;

    const std::wstring &name() const noexcept { return name_; }

  private:
    static Api load_api();
    static std::wstring unique_name();

    Api api_;
    std::wstring name_;
    void *adapter_ = nullptr;
};

} // namespace vpn::windows
