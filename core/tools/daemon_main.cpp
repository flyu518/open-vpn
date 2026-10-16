#include "openvpn3_engine.h"
#include "vpn_security.h"
#include "vpn_session_manager.h"

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::atomic<bool> stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true);
}

std::string write_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value) + "\n";
}

Json::Value response(bool ok, std::string message = {}) {
    Json::Value value(Json::objectValue);
    value["ok"] = ok;
    if (!message.empty()) {
        value["message"] = std::move(message);
    }
    return value;
}

std::string state_name(vpn::SessionState state) {
    switch (state) {
    case vpn::SessionState::Idle:
        return "idle";
    case vpn::SessionState::Connecting:
        return "connecting";
    case vpn::SessionState::Connected:
        return "connected";
    case vpn::SessionState::Disconnecting:
        return "disconnecting";
    case vpn::SessionState::Disconnected:
        return "disconnected";
    case vpn::SessionState::Failed:
        return "failed";
    }
    return "failed";
}

std::string state_label(vpn::SessionState state) {
    switch (state) {
    case vpn::SessionState::Idle:
        return "未连接";
    case vpn::SessionState::Connecting:
        return "正在连接";
    case vpn::SessionState::Connected:
        return "已连接";
    case vpn::SessionState::Disconnecting:
        return "正在断开";
    case vpn::SessionState::Disconnected:
        return "已断开";
    case vpn::SessionState::Failed:
        return "连接失败";
    }
    return "未知状态";
}

class Daemon;

class SessionController {
public:
    explicit SessionController(Daemon* daemon) : daemon_(daemon) {}

    Json::Value connect(const Json::Value& request);
    Json::Value disconnect(const std::string& session_id);
    Json::Value snapshot() const;
    void stopAll();

    void onEngineEvent(const std::string& session_id,
                       const vpn::EngineEvent& event);

private:
    struct Record {
        std::unique_ptr<vpn::OpenVPN3Session> session;
        std::string display_name;
        std::string remote_host;
        vpn::SessionState state = vpn::SessionState::Idle;
        std::string error;
    };

    static vpn::SessionState state_from_event(const vpn::EngineEvent& event,
                                              vpn::SessionState fallback);
    static Json::Value record_json(const std::string& session_id,
                                   const Record& record);

    Daemon* daemon_;
    mutable std::mutex mutex_;
    vpn::OpenVPN3Engine engine_;
    std::map<std::string, Record> records_;
};

class Daemon {
public:
    explicit Daemon(std::string socket_path)
        : socket_path_(std::move(socket_path)), controller_(this) {}

    ~Daemon() {
        stop();
    }

    int run();
    void stop();
    void broadcast(const Json::Value& value);

    SessionController& controller() {
        return controller_;
    }

private:
    void clientLoop(int client_fd);
    Json::Value handleRequest(const Json::Value& request);
    bool sendToClient(int client_fd, const Json::Value& value);

    std::string socket_path_;
    std::atomic<bool> running_{true};
    int server_fd_ = -1;
    SessionController controller_;
    std::mutex clients_mutex_;
    std::vector<int> clients_;
};

vpn::SessionState SessionController::state_from_event(
    const vpn::EngineEvent& event,
    vpn::SessionState fallback) {
    if (event.name == "CONNECTED") {
        return vpn::SessionState::Connected;
    }
    if (event.name == "DISCONNECTED") {
        return vpn::SessionState::Disconnected;
    }
    if (event.name == "RECONNECTING" || event.name == "RESOLVE") {
        return vpn::SessionState::Connecting;
    }
    if (event.error || event.fatal || event.name == "FATAL_ERROR") {
        return vpn::SessionState::Failed;
    }
    return fallback;
}

Json::Value SessionController::record_json(const std::string& session_id,
                                            const Record& record) {
    Json::Value value(Json::objectValue);
    value["sessionId"] = session_id;
    value["name"] = record.display_name;
    value["remoteHost"] = record.remote_host;
    value["state"] = state_name(record.state);
    value["stateLabel"] = state_label(record.state);
    if (!record.error.empty()) {
        value["error"] = vpn::redactSecrets(record.error);
    }
    return value;
}

Json::Value SessionController::connect(const Json::Value& request) {
    const std::string session_id = request.get("sessionId", "").asString();
    const std::string profile = request.get("profile", "").asString();
    if (session_id.empty() || profile.empty()) {
        return response(false, "会话标识和 profile 不能为空");
    }

    vpn::EngineCredentials credentials;
    const Json::Value& credential_value = request["credentials"];
    credentials.username = credential_value.get("username", "").asString();
    credentials.password = credential_value.get("password", "").asString();
    credentials.response = credential_value.get("response", "").asString();
    credentials.dynamic_challenge_cookie =
        credential_value.get("dynamicChallengeCookie", "").asString();

    auto record = std::make_unique<Record>();
    record->display_name = request.get("name", session_id).asString();
    record->state = vpn::SessionState::Connecting;
    record->session = engine_.createSession(
        session_id,
        [this, session_id](const vpn::EngineEvent& event) {
            onEngineEvent(session_id, event);
        });

    const vpn::ProfileEvaluation evaluation = record->session->setProfile(profile);
    if (!evaluation.ok) {
        return response(false, vpn::redactSecrets(evaluation.message));
    }
    record->remote_host = evaluation.remote_host;
    if (!record->session->setCredentials(credentials)) {
        return response(false, "VPN 凭据设置失败");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.find(session_id) != records_.end()) {
            return response(false, "会话标识已经存在");
        }
        records_.emplace(session_id, std::move(*record));
    }

    Record* stored = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stored = &records_.at(session_id);
    }
    if (!stored->session->connect()) {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.erase(session_id);
        return response(false, "VPN 引擎无法启动连接");
    }

    Json::Value result = response(true, "连接请求已提交");
    result["session"] = record_json(session_id, *stored);
    return result;
}

Json::Value SessionController::disconnect(const std::string& session_id) {
    std::unique_ptr<vpn::OpenVPN3Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(session_id);
        if (it == records_.end()) {
            return response(true, "会话已经不存在");
        }
        session = std::move(it->second.session);
        records_.erase(it);
    }
    session->stop();
    return response(true, "断开请求已提交");
}

Json::Value SessionController::snapshot() const {
    Json::Value result(Json::arrayValue);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : records_) {
        result.append(record_json(item.first, item.second));
    }
    return result;
}

void SessionController::stopAll() {
    std::vector<std::unique_ptr<vpn::OpenVPN3Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : records_) {
            sessions.push_back(std::move(item.second.session));
        }
        records_.clear();
    }
    for (auto& session : sessions) {
        if (session) {
            session->stop();
        }
    }
}

void SessionController::onEngineEvent(const std::string& session_id,
                                      const vpn::EngineEvent& event) {
    Json::Value notification(Json::objectValue);
    notification["type"] = "sessionEvent";
    notification["sessionId"] = session_id;
    notification["event"] = event.name;
    notification["message"] = vpn::redactSecrets(event.info);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(session_id);
        if (it == records_.end()) {
            return;
        }
        it->second.state = state_from_event(event, it->second.state);
        if (event.error || event.fatal) {
            it->second.error = event.info;
        }
        notification["session"] = record_json(session_id, it->second);
    }
    daemon_->broadcast(notification);
}

bool Daemon::sendToClient(int client_fd, const Json::Value& value) {
    const std::string payload = write_json(value);
    const char* cursor = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t written = send(client_fd, cursor, remaining, 0);
        if (written <= 0) {
            return false;
        }
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

void Daemon::broadcast(const Json::Value& value) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(), [&](int client_fd) {
            return !sendToClient(client_fd, value);
        }),
        clients_.end());
}

Json::Value Daemon::handleRequest(const Json::Value& request) {
    const std::string command = request.get("command", "").asString();
    if (command == "ping") {
        return response(true, "VPN daemon 正常运行");
    }
    if (command == "snapshot") {
        Json::Value result = response(true);
        result["sessions"] = controller_.snapshot();
        return result;
    }
    if (command == "connect") {
        return controller_.connect(request);
    }
    if (command == "disconnect") {
        return controller_.disconnect(request.get("sessionId", "").asString());
    }
    return response(false, "未知命令：" + command);
}

void Daemon::clientLoop(int client_fd) {
    int no_sigpipe = 1;
    setsockopt(client_fd,
               SOL_SOCKET,
               SO_NOSIGPIPE,
               &no_sigpipe,
               sizeof(no_sigpipe));
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.push_back(client_fd);
    }

    std::string input;
    char buffer[4096];
    while (running_.load()) {
        const ssize_t count = recv(client_fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }
        input.append(buffer, static_cast<std::size_t>(count));
        for (;;) {
            const std::size_t newline = input.find('\n');
            if (newline == std::string::npos) {
                break;
            }
            const std::string line = input.substr(0, newline);
            input.erase(0, newline + 1);
            if (line.empty()) {
                continue;
            }
            Json::CharReaderBuilder builder;
            Json::Value request;
            std::string errors;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            const bool parsed = reader->parse(line.data(),
                                              line.data() + line.size(),
                                              &request,
                                              &errors);
            if (!parsed) {
                if (!sendToClient(client_fd, response(false, "JSON 请求无效"))) {
                    running_.store(false);
                }
                continue;
            }
            if (!sendToClient(client_fd, handleRequest(request))) {
                running_.store(false);
                break;
            }
        }
    }

    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), client_fd), clients_.end());
}

int Daemon::run() {
    ::unlink(socket_path_.c_str());
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "无法创建 VPN daemon socket：" << std::strerror(errno) << '\n';
        return 2;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(address.sun_path)) {
        std::cerr << "VPN daemon socket 路径过长\n";
        close(server_fd_);
        server_fd_ = -1;
        return 3;
    }
    std::strncpy(address.sun_path, socket_path_.c_str(), sizeof(address.sun_path) - 1);
    if (bind(server_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        chmod(socket_path_.c_str(), 0600) != 0 || listen(server_fd_, 8) != 0) {
        std::cerr << "无法启动 VPN daemon socket：" << std::strerror(errno) << '\n';
        close(server_fd_);
        server_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return 4;
    }

    std::cout << "VPN daemon 已启动：" << socket_path_ << '\n';
    while (running_.load() && !stop_requested.load()) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(server_fd_, &read_set);
        timeval timeout{1, 0};
        const int ready = select(server_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }
        const int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        std::thread(&Daemon::clientLoop, this, client_fd).detach();
    }
    stop();
    return 0;
}

void Daemon::stop() {
    running_.store(false);
    controller_.stopAll();
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    ::unlink(socket_path_.c_str());
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = "/tmp/vpn-ui.sock";
    if (argc == 3 && std::string(argv[1]) == "--socket") {
        socket_path = argv[2];
    } else if (argc != 1) {
        std::cout << "用法：" << argv[0] << " [--socket /tmp/vpn-ui.sock]\n";
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    Daemon daemon(std::move(socket_path));
    return daemon.run();
}
