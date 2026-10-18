#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t module_name[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_name, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return 2;

    std::filesystem::path daemon_path(module_name);
    daemon_path.replace_filename(L"openvpn_windows_daemon.exe");
    if (!std::filesystem::exists(daemon_path)) return 3;

    const std::wstring command = L"\"" + daemon_path.wstring() + L"\"";
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, daemon_path.parent_path().c_str(),
        &startup, &process);
    if (!started) return 4;

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

#else
int main() { return 0; }
#endif
