#ifndef VPN_OPENVPN3_ENGINE_H
#define VPN_OPENVPN3_ENGINE_H

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "vpn_route_policy.h"

namespace vpn {

struct ProfileEvaluation {
    bool ok = false;
    std::string message;
    std::string remote_host;
    std::string remote_port;
    std::string remote_protocol;
};

struct EngineCredentials {
    std::string username;
    std::string password;
    std::string response;
    std::string dynamic_challenge_cookie;
};

struct EngineEvent {
    std::string name;
    std::string info;
    bool error = false;
    bool fatal = false;
};

struct SessionTrafficStats {
    std::int64_t bytes_in = 0;
    std::int64_t bytes_out = 0;
};

using EngineEventSink = std::function<void(const EngineEvent&)>;

class OpenVPN3Session {
public:
    ~OpenVPN3Session();

    OpenVPN3Session(const OpenVPN3Session&) = delete;
    OpenVPN3Session& operator=(const OpenVPN3Session&) = delete;

    const std::string& sessionId() const;
    bool isActive() const;
    SessionTrafficStats trafficStats() const;
    ProfileEvaluation setProfile(std::string_view profile_content);
    void setRouteConflictMode(RouteConflictMode mode);
    void setRouteRules(const std::vector<RouteRule>& rules);
    bool setCredentials(const EngineCredentials& credentials);
    bool connect();
    void stop();
    void pause();
    void resume();

private:
    friend class OpenVPN3Engine;

    class Impl;
    explicit OpenVPN3Session(std::string session_id, EngineEventSink event_sink);

    std::unique_ptr<Impl> impl_;
};

class OpenVPN3Engine {
public:
    OpenVPN3Engine();
    ~OpenVPN3Engine();

    OpenVPN3Engine(const OpenVPN3Engine&) = delete;
    OpenVPN3Engine& operator=(const OpenVPN3Engine&) = delete;

    ProfileEvaluation evaluateProfile(std::string_view profile_content);
    std::unique_ptr<OpenVPN3Session> createSession(std::string session_id,
                                                   EngineEventSink event_sink = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vpn

#endif
