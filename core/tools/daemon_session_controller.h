#pragma once

#include "daemon_protocol.h"
#include "openvpn3_engine.h"
#include "vpn_credential_store.h"
#include "vpn_session_manager.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace vpn::daemon {

class SessionController {
public:
    using NotificationSink = std::function<void(const Json::Value&)>;
    // 0 表示不限；Windows 官方 agent 为单隧道，调用方应传入 1。
    SessionController(NotificationSink notify, CredentialStore* credential_store,
                      std::size_t max_active_sessions = 0)
        : notify_(std::move(notify)), credential_store_(credential_store),
          max_active_sessions_(max_active_sessions) {}
    ~SessionController() { stopAll(); }

    Json::Value handleRequest(const Json::Value& request);
    void stopAll() noexcept;

private:
    struct Record {
        std::unique_ptr<OpenVPN3Session> session;
        std::string display_name;
        std::string remote_host;
        std::string profile_id;
        Credentials credentials;
        bool remember_credentials = false;
        SessionState state = SessionState::Connecting;
        std::chrono::steady_clock::time_point connected_at{};
        std::int64_t duration_seconds = 0;
        std::string error;
        std::vector<std::string> warnings;
        Json::Value route_conflicts{Json::arrayValue};
    };

    Json::Value connect(const Json::Value& request);
    Json::Value disconnect(const std::string& session_id);
    Json::Value snapshot() const;
    void onEngineEvent(const std::string& session_id, const EngineEvent& event);
    static Json::Value record_json(const std::string& session_id, const Record& record);

    NotificationSink notify_;
    CredentialStore* credential_store_;
    const std::size_t max_active_sessions_;
    // Mutations serialize engine start/stop, including their joins. Callbacks
    // acquire only mutex_; never hold mutex_ while invoking the engine.
    std::mutex operations_mutex_;
    mutable std::mutex mutex_;
    OpenVPN3Engine engine_;
    std::map<std::string, Record> records_;
};

}  // namespace vpn::daemon
