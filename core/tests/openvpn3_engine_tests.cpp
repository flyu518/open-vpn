#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "openvpn3_engine.h"
#include "openvpn3_engine_config.h"

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

void uses_shared_connection_policy() {
    static_assert(vpn::openvpn3_config::connection_timeout_seconds == vpn::project_config::connection_timeout_seconds);
    static_assert(vpn::openvpn3_config::retry_on_auth_failed == vpn::project_config::retry_on_auth_failed);
    assert(vpn::openvpn3_config::connection_timeout_seconds > 0);
    assert(vpn::openvpn3_config::connection_timeout_seconds <= 3600);
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

void evaluates_profile_files(int argc, char** argv) {
    if (argc < 2) return;

    vpn::OpenVPN3Engine engine;
    std::vector<std::unique_ptr<vpn::OpenVPN3Session>> sessions;
    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        assert(file);
        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        assert(!contents.empty());

        const auto evaluation = engine.evaluateProfile(contents);
        assert(evaluation.ok);
        assert(!evaluation.remote_host.empty());
        assert(!evaluation.remote_port.empty());
        assert(!evaluation.remote_protocol.empty());

        auto session = engine.createSession("profile-file-" + std::to_string(index));
        assert(session);
        assert(session->setProfile(contents).ok);
        sessions.push_back(std::move(session));
    }

    if (sessions.size() >= 2) {
        assert(sessions[0]->sessionId() != sessions[1]->sessionId());
        sessions[0]->stop();
        assert(!sessions[0]->isActive());
        assert(sessions[1]->isActive());
    }
}

}  // namespace

int main(int argc, char** argv) {
    uses_shared_connection_policy();
    evaluates_profile_without_network();
    creates_two_independent_client_objects();
    stopping_one_session_does_not_stop_the_other();
    evaluates_profile_files(argc, argv);
    return 0;
}
