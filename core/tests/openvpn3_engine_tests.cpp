#include <cassert>
#include <string>

#include "openvpn3_engine.h"

namespace {

const char* minimal_profile() {
    return "client\n"
           "remote 127.0.0.1 1194 udp\n"
           "nobind\n"
           "dev tun\n"
           "route-nopull\n";
}

void evaluates_profile_without_network() {
    vpn::OpenVPN3Engine engine;
    const vpn::ProfileEvaluation evaluation =
        engine.evaluateProfile(minimal_profile());
    assert(evaluation.ok);
    assert(evaluation.remote_host == "127.0.0.1");
}

void creates_two_independent_client_objects() {
    vpn::OpenVPN3Engine engine;
    const auto first = engine.createSession("office-a");
    const auto second = engine.createSession("office-b");
    assert(first);
    assert(second);
    assert(first->sessionId() == "office-a");
    assert(second->sessionId() == "office-b");
    assert(first->sessionId() != second->sessionId());

    assert(first->setProfile(minimal_profile()).ok);
    assert(first->setCredentials(vpn::EngineCredentials{"用户", "密码", "", ""}));
}

void stopping_one_session_does_not_stop_the_other() {
    vpn::OpenVPN3Engine engine;
    const auto first = engine.createSession("office-a");
    const auto second = engine.createSession("office-b");
    assert(first && second);

    first->stop();
    assert(!first->isActive());
    assert(second->isActive());
}

}  // namespace

int main() {
    evaluates_profile_without_network();
    creates_two_independent_client_objects();
    stopping_one_session_does_not_stop_the_other();
    return 0;
}
