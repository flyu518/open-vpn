#include "wintun_adapter_lease.h"

#include <atomic>
#include <stdexcept>
#include <string>

namespace vpn::windows {
namespace {

std::runtime_error win32_error(const char *operation, DWORD code) {
    return std::runtime_error(std::string(operation) + "（Win32 错误 " +
                              std::to_string(code) + "）");
}

} // namespace

WintunAdapterLease::WintunAdapterLease()
    : WintunAdapterLease(load_api(), unique_name()) {}

WintunAdapterLease::WintunAdapterLease(Api api, const std::wstring &name)
    : api_(api), name_(name) {
    if (api_.library == nullptr || api_.create_adapter == nullptr ||
        api_.close_adapter == nullptr || api_.release_library == nullptr) {
        if (api_.library != nullptr && api_.release_library != nullptr)
            api_.release_library(api_.library);
        api_.library = nullptr;
        throw std::runtime_error("Wintun API 不完整，无法创建独立虚拟网卡");
    }

    adapter_ = api_.create_adapter(name.c_str(), L"OpenVPN", nullptr);
    if (adapter_ == nullptr) {
        const DWORD error = GetLastError();
        api_.release_library(api_.library);
        api_.library = nullptr;
        throw win32_error("创建 Wintun 虚拟网卡失败", error);
    }
}

WintunAdapterLease::~WintunAdapterLease() {
    if (adapter_ != nullptr)
        api_.close_adapter(adapter_);
    if (api_.library != nullptr)
        api_.release_library(api_.library);
}

WintunAdapterLease::Api WintunAdapterLease::load_api() {
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(),
                                             static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size())
        throw win32_error("读取 Windows 服务程序路径失败", GetLastError());
    executable_path.resize(length);

    const std::size_t separator = executable_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        throw std::runtime_error("无法定位 Windows 服务程序目录以加载 wintun.dll");
    const std::wstring dll_path = executable_path.substr(0, separator + 1) + L"wintun.dll";
    HMODULE library = LoadLibraryW(dll_path.c_str());
    if (library == nullptr)
        throw win32_error("加载同目录下的 wintun.dll 失败", GetLastError());

    Api api;
    api.library = library;
    api.create_adapter = reinterpret_cast<CreateAdapter>(
        GetProcAddress(library, "WintunCreateAdapter"));
    api.close_adapter = reinterpret_cast<CloseAdapter>(
        GetProcAddress(library, "WintunCloseAdapter"));
    api.release_library = &FreeLibrary;
    if (api.create_adapter == nullptr || api.close_adapter == nullptr) {
        const DWORD error = GetLastError();
        FreeLibrary(library);
        throw win32_error("wintun.dll 缺少创建/释放适配器接口", error);
    }
    return api;
}

std::wstring WintunAdapterLease::unique_name() {
    static std::atomic<unsigned long> next_id{0};
    return L"OpenVPN-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(next_id.fetch_add(1, std::memory_order_relaxed));
}

} // namespace vpn::windows
