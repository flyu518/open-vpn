#ifndef VPN_RECOVERY_H
#define VPN_RECOVERY_H

#include <string>
#include <string_view>
#include <vector>

namespace vpn {

class RecoveryLedger {
public:
    explicit RecoveryLedger(std::vector<std::string> persisted_sessions);

    std::vector<std::string> orphanedSessions(
        const std::vector<std::string>& active_sessions) const;
    bool cleanup(std::string_view session_id);

private:
    std::vector<std::string> persisted_sessions_;
    std::vector<std::string> cleaned_sessions_;
};

}  // namespace vpn

#endif
