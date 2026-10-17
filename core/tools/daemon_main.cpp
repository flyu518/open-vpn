#include "daemon_session_controller.h"
#include "platform/macos/secure_store/keychain_store.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t stop_requested = 0;
void handle_signal(int) { stop_requested = 1; }

class Daemon {
    struct Client {
        explicit Client(int socket) : fd(socket) {
            const int no_sigpipe = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
            const timeval timeout{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        }
        ~Client() { close(fd); }
        void cancel() {
            if (!stopped.exchange(true)) shutdown(fd, SHUT_RDWR);
        }
        const int fd;
        std::mutex write_mutex;
        std::atomic<bool> stopped{false};
        std::atomic<bool> finished{false};
        std::thread worker;
    };

public:
    explicit Daemon(std::string socket_path)
        : socket_path_(std::move(socket_path)),
          controller_([this](const Json::Value& value) { broadcast(value); }, &credential_store_) {}
    ~Daemon() { stop(); }

    int run();
    void stop();

private:
    void clientLoop(const std::shared_ptr<Client>& client);
    bool sendToClient(const std::shared_ptr<Client>& client, const Json::Value& value);
    void broadcast(const Json::Value& value);
    void reapClients();

    std::string socket_path_;
    std::atomic<bool> running_{true};
    int server_fd_ = -1;
    bool socket_bound_ = false;
    vpn::platform::macos::KeychainCredentialStore credential_store_;
    vpn::daemon::SessionController controller_;
    std::mutex clients_mutex_;
    std::vector<std::shared_ptr<Client>> clients_;
};

bool Daemon::sendToClient(const std::shared_ptr<Client>& client, const Json::Value& value) {
    const std::string payload = vpn::daemon::write_json(value);
    // Both replies and engine notifications hold this lock for the WHOLE frame,
    // including every short write. The shared client keeps its fd alive.
    std::lock_guard<std::mutex> lock(client->write_mutex);
    if (client->stopped.load()) return false;
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const ssize_t written = send(client->fd, payload.data() + offset, payload.size() - offset, 0);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            client->cancel();
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

void Daemon::broadcast(const Json::Value& value) {
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    for (const auto& client : clients) sendToClient(client, value);
}

void Daemon::clientLoop(const std::shared_ptr<Client>& client) {
    try {
        vpn::daemon::RequestBuffer input;
        char buffer[8192];
        while (running_.load() && !client->stopped.load()) {
            const ssize_t count = recv(client->fd, buffer, sizeof(buffer), 0);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            if (!input.append(buffer, static_cast<std::size_t>(count), [&](const std::string& line) {
                    if (!running_.load() || client->stopped.load()) return false;
                    return sendToClient(client, vpn::daemon::process_request(line,
                        [this](const Json::Value& request) { return controller_.handleRequest(request); }));
                })) break;
        }
    } catch (...) {
        // A transport/allocation failure is local to this client, too.
    }
    client->cancel();
    client->finished.store(true);
}

void Daemon::reapClients() {
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

int Daemon::run() {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(address.sun_path)) {
        std::cerr << "OpenVPN 服务 socket 路径过长\n";
        return 3;
    }
    ::unlink(socket_path_.c_str());
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) return 2;
    std::strncpy(address.sun_path, socket_path_.c_str(), sizeof(address.sun_path) - 1);
    if (bind(server_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "无法绑定 OpenVPN 服务 socket：" << std::strerror(errno) << '\n';
        return 4;
    }
    socket_bound_ = true;
    if (chmod(socket_path_.c_str(), 0600) != 0 || listen(server_fd_, 8) != 0) return 4;
    std::cout << "OpenVPN 服务已启动：" << socket_path_ << '\n';

    while (running_.load() && !stop_requested) {
        reapClients();
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(server_fd_, &read_set);
        timeval timeout{0, 100000};
        const int ready = select(server_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        const int fd = accept(server_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        auto client = std::make_shared<Client>(fd);
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(client);
        }
        try {
            client->worker = std::thread([this, client] { clientLoop(client); });
        } catch (...) {
            client->cancel();
            stop();
            return 5;
        }
    }
    stop();
    return 0;
}

void Daemon::stop() {
    running_.store(false);
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    // Wake blocked recv/send before joining request or engine threads.
    for (const auto& client : clients) client->cancel();
    for (const auto& client : clients) {
        if (client->worker.joinable()) client->worker.join();
    }
    controller_.stopAll();
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
    }
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (socket_bound_) {
        ::unlink(socket_path_.c_str());
        socket_bound_ = false;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = "/tmp/openvpn-ui.sock";
    if (argc == 3 && std::string(argv[1]) == "--socket") {
        socket_path = argv[2];
    } else if (argc != 1) {
        std::cout << "用法：" << argv[0] << " [--socket /tmp/openvpn-ui.sock]\n";
        return 2;
    }
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    Daemon daemon(std::move(socket_path));
    return daemon.run();
}
