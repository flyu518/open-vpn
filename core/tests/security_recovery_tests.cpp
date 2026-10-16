#include <cassert>
#include <string>
#include <vector>

#include "vpn_recovery.h"
#include "vpn_security.h"

int main() {
    const std::string input =
        "用户=alice password=secret token=token-value privateKey=key-value";
    const std::string redacted = vpn::redactSecrets(input);
    assert(redacted.find("secret") == std::string::npos);
    assert(redacted.find("token-value") == std::string::npos);
    assert(redacted.find("key-value") == std::string::npos);
    assert(redacted.find("[已隐藏]") != std::string::npos);

    vpn::RecoveryLedger ledger({"office-a", "office-b"});
    const std::vector<std::string> orphaned = ledger.orphanedSessions({"office-b"});
    assert(orphaned.size() == 1);
    assert(orphaned.front() == "office-a");
    assert(ledger.cleanup("office-a"));
    assert(ledger.cleanup("office-a"));
    assert(ledger.orphanedSessions({"office-b"}).empty());
    return 0;
}
