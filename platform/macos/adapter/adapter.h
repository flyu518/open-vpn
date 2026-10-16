#ifndef VPN_MACOS_ADAPTER_H
#define VPN_MACOS_ADAPTER_H

#include <string>
#include <string_view>
#include <vector>

#include "adapter_contract.h"
#include "vpn_route_policy.h"

namespace vpn::platform::macos {

class MacOSAdapter {
public:
    struct Session {
        std::string id;
        std::string endpoint;
        RoutePolicy route_policy;
        bool established = false;
    };

    PlatformResult prepare(std::string_view session_id,
                           std::string_view remote_endpoint);
    PlatformResult establish(std::string_view session_id);
    PlatformResult applyRoutes(std::string_view session_id,
                               const std::vector<std::string>& routes);
    PlatformResult applyDns(std::string_view session_id,
                            const std::vector<std::string>& servers);
    PlatformResult destroy(std::string_view session_id);

    std::vector<std::string> activeSessions() const;
    std::vector<std::string> bypassEndpoints() const;
    std::string dnsOwner() const;

private:
    std::vector<Session> sessions_;
    std::string dns_owner_;
};

}  // namespace vpn::platform::macos

#endif
