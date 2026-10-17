#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "daemon_session_controller.h"
#include "platform/windows/secure_store/credential_store.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> stop_requested{false};

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT ||
        signal == CTRL_BREAK_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        stop_requested.store(true);
        return TRUE;
    }
    return FALSE;
}

class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { const auto value = value_; value_ = INVALID_HANDLE_VALUE; return value; }
private:
    HANDLE value_;
};

class WindowsDaemon {
    struct Client {
        explicit Client(HANDLE handle)
            : pipe(handle), cancelled(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
        ~Client() { DisconnectNamedPipe(pipe.get()); }
        void cancel() {
            if (!stopped.exchange(true)) {
                SetEvent(cancelled.get());
                CancelIoEx(pipe.get(), nullptr);
            }
        }
        Handle pipe;
        Handle cancelled;
        std::mutex write_mutex;
        std::atomic<bool> stopped{false};
        std::atomic<bool> finished{false};
        std::thread worker;
    };

public:
    explicit WindowsDaemon(std::wstring pipe_name)
        : pipe_name_(std::move(pipe_name)),
          stopped_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          // 官方 agent 只有一个 tun，必须在启动第二个引擎前拒绝请求。
          controller_([this](const Json::Value& value) { broadcast(value); }, &credential_store_, 1) {}
    ~WindowsDaemon() { stop(); }
    int run();
    void stop();

private:
    bool transfer(const std::shared_ptr<Client>& client, void* data, DWORD size,
                  DWORD& count, bool reading);
    bool send_to_client(const std::shared_ptr<Client>& client, const Json::Value& value);
    void client_loop(const std::shared_ptr<Client>& client);
    void broadcast(const Json::Value& value);
    void reap_clients();

    std::wstring pipe_name_;
    std::atomic<bool> running_{true};
    Handle stopped_;
    vpn::platform::windows::WindowsCredentialStore credential_store_;
    vpn::daemon::SessionController controller_;
    std::mutex clients_mutex_;
    std::vector<std::shared_ptr<Client>> clients_;
};

bool WindowsDaemon::transfer(const std::shared_ptr<Client>& client, void* data,
                             DWORD size, DWORD& count, bool reading) {
    if (!running_.load() || client->stopped.load()) return false;
    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completed.valid()) return false;
    OVERLAPPED operation{};
    operation.hEvent = completed.get();
    count = 0;
    const BOOL done = reading
        ? ReadFile(client->pipe.get(), data, size, &count, &operation)
        : WriteFile(client->pipe.get(), data, size, &count, &operation);
    if (done) return count > 0;
    const DWORD error = GetLastError();
    if (reading && error == ERROR_MORE_DATA) return count > 0;
    if (error != ERROR_IO_PENDING) return false;

    HANDLE events[] = {stopped_.get(), client->cancelled.get(), completed.get()};
    const DWORD waited = WaitForMultipleObjects(3, events, FALSE, reading ? INFINITE : 2000);
    if (waited != WAIT_OBJECT_0 + 2) {
        // Cancellation is asynchronous: retain OVERLAPPED and its buffer until
        // completion, then let their owners release them.
        CancelIoEx(client->pipe.get(), &operation);
        GetOverlappedResult(client->pipe.get(), &operation, &count, TRUE);
        return false;
    }
    const BOOL ok = GetOverlappedResult(client->pipe.get(), &operation, &count, FALSE);
    return (ok || (reading && GetLastError() == ERROR_MORE_DATA)) && count > 0;
}

bool WindowsDaemon::send_to_client(const std::shared_ptr<Client>& client,
                                   const Json::Value& value) {
    std::string payload = vpn::daemon::write_json(value);
    std::lock_guard<std::mutex> lock(client->write_mutex);
    std::size_t offset = 0;
    while (offset < payload.size()) {
        DWORD written = 0;
        const auto size = static_cast<DWORD>(std::min<std::size_t>(
            payload.size() - offset, std::numeric_limits<DWORD>::max()));
        if (!transfer(client, payload.data() + offset, size, written, false)) {
            client->cancel();
            return false;
        }
        offset += written;
    }
    return true;
}

void WindowsDaemon::broadcast(const Json::Value& value) {
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    for (const auto& client : clients) send_to_client(client, value);
}

void WindowsDaemon::client_loop(const std::shared_ptr<Client>& client) {
    try {
        vpn::daemon::RequestBuffer input;
        char buffer[8192];
        DWORD count = 0;
        while (transfer(client, buffer, sizeof(buffer), count, true)) {
            if (!input.append(buffer, count, [&](const std::string& line) {
                    if (!running_.load() || client->stopped.load()) return false;
                    return send_to_client(client, vpn::daemon::process_request(line,
                        [this](const Json::Value& request) { return controller_.handleRequest(request); }));
                })) break;
        }
    } catch (...) {
        // Keep transport failures local to this pipe.
    }
    client->cancel();
    client->finished.store(true);
}

void WindowsDaemon::reap_clients() {
    std::vector<std::shared_ptr<Client>> finished;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            if ((*it)->finished.load()) {
                finished.push_back(*it);
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& client : finished) client->worker.join();
}

int WindowsDaemon::run() {
    if (!stopped_.valid()) return 2;
    while (running_.load() && !stop_requested.load()) {
        reap_clients();
        Handle pipe(CreateNamedPipeW(pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 1 << 20, 1 << 20, 0, nullptr));
        Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!pipe.valid() || !completed.valid()) {
            std::cerr << "无法创建 OpenVPN Windows named pipe，错误码：" << GetLastError() << '\n';
            return 2;
        }
        OVERLAPPED operation{};
        operation.hEvent = completed.get();
        bool connected = ConnectNamedPipe(pipe.get(), &operation) != FALSE;
        const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_PIPE_CONNECTED) connected = true;
        if (error == ERROR_IO_PENDING) {
            HANDLE events[] = {stopped_.get(), completed.get()};
            while (running_.load() && !stop_requested.load()) {
                const DWORD waited = WaitForMultipleObjects(2, events, FALSE, 100);
                if (waited == WAIT_TIMEOUT) {
                    reap_clients();
                    continue;
                }
                if (waited == WAIT_OBJECT_0 + 1) {
                    DWORD count = 0;
                    connected = GetOverlappedResult(pipe.get(), &operation, &count, FALSE) != FALSE;
                }
                break;
            }
            if (!connected) {
                CancelIoEx(pipe.get(), &operation);
                DWORD count = 0;
                GetOverlappedResult(pipe.get(), &operation, &count, TRUE);
            }
        }
        if (!connected) continue;
        auto client = std::make_shared<Client>(pipe.release());
        if (!client->cancelled.valid()) return 2;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(client);
        }
        try {
            client->worker = std::thread([this, client] { client_loop(client); });
        } catch (...) {
            client->cancel();
            stop();
            return 3;
        }
    }
    stop();
    return 0;
}

void WindowsDaemon::stop() {
    running_.store(false);
    if (stopped_.valid()) SetEvent(stopped_.get());
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    for (const auto& client : clients) client->cancel();
    for (const auto& client : clients) {
        if (client->worker.joinable()) client->worker.join();
    }
    controller_.stopAll();
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
    }
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::wstring pipe_name = LR"(\\.\pipe\openvpn-ui)";
    if (argc == 3 && std::wstring(argv[1]) == L"--pipe") {
        pipe_name = argv[2];
    } else if (argc != 1) {
        std::wcerr << L"用法：OpenVPN Windows daemon [--pipe \\\\.\\pipe\\openvpn-ui]\n";
        return 2;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
    WindowsDaemon daemon(std::move(pipe_name));
    return daemon.run();
}

#else
int main() { return 0; }
#endif
