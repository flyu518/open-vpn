#ifndef VPN_SESSION_MANAGER_H
#define VPN_SESSION_MANAGER_H

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "vpn_engine.h"
#include "vpn_fake_engine.h"
#include "vpn_route_policy.h"

namespace vpn {

enum class SessionState {
    Idle,
    Connecting,
    Connected,
    Disconnecting,
    Disconnected,
    Failed
};

enum class SessionEventType {
    StateChanged,
    Error
};

struct SessionProfile {
    std::string session_id;
    std::string display_name;
    RoutePolicy route_policy;
};

struct SessionSnapshot {
    std::string session_id;
    std::string display_name;
    SessionState state = SessionState::Idle;
    RoutePolicy route_policy;
};

struct ConnectResult {
    bool ok = false;
    vpn_error_code_t error_code = VPN_OK;
    std::string diagnostic;
    SessionSnapshot snapshot;
};

using EventSink = std::function<void(const SessionSnapshot&,
                                     SessionEventType,
                                     std::string_view)>;

class SessionManager {
public:
    explicit SessionManager(EventSink event_sink = {});

    ConnectResult connect(const SessionProfile& profile,
                          vpn_capability_t capability);
    void disconnect(std::string_view session_id);
    std::vector<SessionSnapshot> snapshot() const;

private:
    struct Record {
        SessionSnapshot snapshot;
        FakeEngine engine;
    };

    static bool is_active(SessionState state);
    void emit(const SessionSnapshot& snapshot,
              SessionEventType type,
              std::string_view message) const;

    std::map<std::string, Record> sessions_;
    EventSink event_sink_;
};

}  // namespace vpn

#endif
