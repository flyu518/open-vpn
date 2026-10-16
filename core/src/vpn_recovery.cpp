#include "vpn_recovery.h"

#include <algorithm>
#include <utility>

namespace vpn {

RecoveryLedger::RecoveryLedger(std::vector<std::string> persisted_sessions)
    : persisted_sessions_(std::move(persisted_sessions)) {}

std::vector<std::string> RecoveryLedger::orphanedSessions(
    const std::vector<std::string>& active_sessions) const {
    std::vector<std::string> result;
    for (const std::string& session_id : persisted_sessions_) {
        const bool active = std::find(active_sessions.begin(), active_sessions.end(),
                                      session_id) != active_sessions.end();
        const bool cleaned = std::find(cleaned_sessions_.begin(), cleaned_sessions_.end(),
                                       session_id) != cleaned_sessions_.end();
        if (!active && !cleaned) {
            result.push_back(session_id);
        }
    }
    return result;
}

bool RecoveryLedger::cleanup(std::string_view session_id) {
    const std::string id(session_id);
    if (std::find(persisted_sessions_.begin(), persisted_sessions_.end(), id) ==
        persisted_sessions_.end()) {
        return true;
    }
    if (std::find(cleaned_sessions_.begin(), cleaned_sessions_.end(), id) ==
        cleaned_sessions_.end()) {
        cleaned_sessions_.push_back(id);
    }
    return true;
}

}  // namespace vpn
