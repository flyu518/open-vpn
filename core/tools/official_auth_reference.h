#pragma once
#include "vpn_credential_store.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

// Diagnostic only: run the signed official native engine without TUN/IP/route
// setup. Credentials travel through inherited anonymous stdin, never argv,
// temporary files, management TCP sockets, or logs returned to the caller.
inline std::string official_auth_reference(const std::filesystem::path &executable,
                                           const std::filesystem::path &profile,
                                           const vpn::Credentials &credentials) {
    struct Handle {
        HANDLE value = nullptr;
        ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } input_read, input_write, output_read, output_write, process, thread;
    struct Buffer {
        std::string value;
        ~Buffer() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
    } output;
    if (credentials.username.find_first_of("\r\n") != std::string::npos ||
        credentials.password.find_first_of("\r\n") != std::string::npos)
        return "UNSUPPORTED_CREDENTIAL_FORMAT";
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&input_read.value, &input_write.value, &security, 0) ||
        !CreatePipe(&output_read.value, &output_write.value, &security, 0))
        return "PIPE_ERROR";
    if (!SetHandleInformation(input_write.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(output_read.value, HANDLE_FLAG_INHERIT, 0))
        return "PIPE_ERROR";
    auto command = L"\"" + executable.wstring() + L"\" --config \"" + profile.wstring() +
        L"\" --dev null --disable-dco --ifconfig-noexec --route-noexec --script-security 0"
        L" --auth-user-pass --auth-retry none --connect-retry-max 1 --connect-timeout 10 --verb 3";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read.value;
    startup.hStdOutput = output_write.value;
    startup.hStdError = output_write.value;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, profile.parent_path().c_str(), &startup, &child))
        return "PROCESS_START_ERROR";
    process.value = child.hProcess;
    thread.value = child.hThread;
    struct StopChild {
        HANDLE value;
        ~StopChild() {
            if (WaitForSingleObject(value, 0) == WAIT_TIMEOUT) {
                TerminateProcess(value, 0);
                WaitForSingleObject(value, 3000);
            }
        }
    } stop{process.value};
    CloseHandle(input_read.value); input_read.value = nullptr;
    CloseHandle(output_write.value); output_write.value = nullptr;
    auto send = [&](const std::string &value) {
        std::string line = value + "\r\n";
        DWORD written = 0;
        const auto ok = WriteFile(input_write.value, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        const bool complete = ok && written == line.size();
        SecureZeroMemory(line.data(), line.size());
        return complete;
    };
    bool user_sent = false, password_sent = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD available = 0;
        if (PeekNamedPipe(output_read.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            char bytes[4096];
            DWORD size = 0;
            if (!ReadFile(output_read.value, bytes, (std::min)(available, DWORD(sizeof(bytes))), &size, nullptr))
                return "PIPE_READ_ERROR";
            output.value.append(bytes, size);
            SecureZeroMemory(bytes, sizeof(bytes));
            if (!user_sent && output.value.find("Enter Auth Username:") != std::string::npos) {
                if (!send(credentials.username)) return "USERNAME_INPUT_ERROR";
                user_sent = true;
            }
            if (!password_sent && output.value.find("Enter Auth Password:") != std::string::npos) {
                if (!send(credentials.password)) return "PASSWORD_INPUT_ERROR";
                password_sent = true;
            }
            if (output.value.find("AUTH_FAILED") != std::string::npos) return "AUTH_REJECTED";
            if (output.value.find("Initialization Sequence Completed") != std::string::npos)
                return user_sent && password_sent ? "AUTH_ACCEPTED" : "AUTH_ACCEPTED_WITHOUT_CREDENTIAL_PROMPT";
            if (output.value.size() > 65536) output.value.erase(0, output.value.size() - 4096);
        }
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) {
            if (output.value.find("Options error") != std::string::npos) return "OPTIONS_ERROR";
            return "PROCESS_EXITED_BEFORE_AUTH_RESULT";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return "AUTH_TIMEOUT";
}
