#include "openvpn3_engine.h"

#include "engine_worker.h"
#include "openvpn3_engine_config.h"
#if defined(_WIN32)
#include "windows_driver_policy.h"
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "client/ovpncli.cpp"

namespace vpn {
namespace {

std::string session_id_for_profile(std::string_view session_id) {
    std::string result(session_id);
    for (char& character : result) {
        if (character == '\n' || character == '\r' || character == '\t' ||
            character == '"' || character == '\\') {
            character = '_';
        }
    }
    return result;
}

const char* route_mode_name(RouteConflictMode mode) {
    switch (mode) {
    case RouteConflictMode::Strict:
        return "strict";
    case RouteConflictMode::Compatible:
        return "compatible";
    }
    return "compatible";
}

const char* route_rule_action_name(RouteRuleAction action) {
    return action == RouteRuleAction::Exclude ? "exclude" : "prefer";
}

const char* route_rule_type_name(RouteRuleType type) {
    switch (type) {
    case RouteRuleType::Domain:
        return "domain";
    case RouteRuleType::DomainSuffix:
        return "domainSuffix";
    case RouteRuleType::Ip:
        return "ip";
    }
    return "ip";
}

std::string encode_route_rules(const std::vector<RouteRule>& rules) {
    std::string result;
    for (const auto& rule : rules) {
        if (rule.pattern.empty() || rule.pattern.find_first_of("|,@ \\t\\r\\n") != std::string::npos)
            continue;
        const std::string item = std::string(route_rule_action_name(rule.action)) + "@" +
                                 route_rule_type_name(rule.type) + ":" + rule.pattern;
        if (!result.empty()) result += ',';
        if (result.size() + item.size() > 900) break;
        result += item;
    }
    return result;
}

class ClientImpl final : public openvpn::ClientAPI::OpenVPNClient {
public:
    ClientImpl(std::string session_id, EngineEventSink event_sink, openvpn::Stop* stop)
        : session_id_(std::move(session_id)), event_sink_(std::move(event_sink)), stop_(stop) {}

    bool pause_on_connection_timeout() override {
        return false;
    }

    void event(const openvpn::ClientAPI::Event& event) override {
        if (event_sink_) {
            event_sink_(EngineEvent{event.name, event.info, event.error, event.fatal});
        }
    }

    void acc_event(
        const openvpn::ClientAPI::AppCustomControlMessageEvent& event) override {
        if (event_sink_) {
            event_sink_(EngineEvent{"ACC_" + event.protocol, event.payload, false, false});
        }
    }

    void log(const openvpn::ClientAPI::LogInfo& log_info) override {
        if (event_sink_) {
            event_sink_(EngineEvent{"LOG", log_info.text, false, false});
        }
    }

    void external_pki_cert_request(
        openvpn::ClientAPI::ExternalPKICertRequest& request) override {
        request.error = true;
        request.errorText = "外部 PKI 尚未接入";
    }

    void external_pki_sign_request(
        openvpn::ClientAPI::ExternalPKISignRequest& request) override {
        request.error = true;
        request.errorText = "外部 PKI 签名尚未接入";
    }

    bool socket_protect(openvpn_io::detail::socket_type,
                        std::string,
                        bool) override {
        return true;
    }

    const std::string& sessionId() const {
        return session_id_;
    }

    void reportFailure(const std::string& message) noexcept {
        try {
            if (event_sink_) event_sink_(EngineEvent{"FATAL_ERROR", message, true, true});
        } catch (...) {
            // An observer failure must not escape the worker's thread entry.
        }
    }

protected:
    openvpn::Stop* get_async_stop() override { return stop_; }

private:
    std::string session_id_;
    EngineEventSink event_sink_;
    openvpn::Stop* stop_;
};

}  // namespace

class OpenVPN3Session::Impl {
public:
    Impl(std::string session_id, EngineEventSink event_sink)
        : client(std::make_unique<ClientImpl>(std::move(session_id),
                                               std::move(event_sink), &stop)),
          active(true) {}

    // Must outlive client: native Stop::Scope objects unregister in the base
    // client's destructor. A persistent stop also covers native startup, when
    // OpenVPNClient::stop() intentionally ignores foreign-thread calls.
    openvpn::Stop stop;
    std::unique_ptr<ClientImpl> client;
    EngineWorker worker;
    openvpn::ClientAPI::Config config;
    bool profile_set = false;
    bool credentials_set = false;
    RouteConflictMode route_conflict_mode = RouteConflictMode::Compatible;
    std::vector<RouteRule> route_rules;
    std::atomic<bool> active{false};
};

OpenVPN3Session::OpenVPN3Session(std::string session_id,
                                 EngineEventSink event_sink)
    : impl_(std::make_unique<Impl>(std::move(session_id), std::move(event_sink))) {}

OpenVPN3Session::~OpenVPN3Session() {
    try { stop(); } catch (...) {}
}

const std::string& OpenVPN3Session::sessionId() const {
    return impl_->client->sessionId();
}

bool OpenVPN3Session::isActive() const {
    return impl_ != nullptr && impl_->active;
}

SessionTrafficStats OpenVPN3Session::trafficStats() const {
    if (impl_ == nullptr || impl_->client == nullptr) {
        return {};
    }
    const openvpn::ClientAPI::TransportStats stats =
        impl_->client->transport_stats();
    return {stats.bytesIn, stats.bytesOut};
}

ProfileEvaluation OpenVPN3Session::setProfile(std::string_view profile_content) {
    impl_->config = openvpn::ClientAPI::Config{};
    impl_->config.content.assign(profile_content.data(), profile_content.size());
    impl_->config.content += "\nsetenv VPN_DESKTOP_SESSION_ID " +
                             session_id_for_profile(sessionId()) + "\n";
    impl_->config.content += "setenv VPN_DESKTOP_ROUTE_MODE " +
                             std::string(route_mode_name(impl_->route_conflict_mode)) + "\n";
    // OpenVPN3 defaults to retrying a connection indefinitely. A finite,
    // shared timeout keeps macOS and Windows in the same terminal state when
    // the agent, TUN device, network, or credentials cannot be established.
    impl_->config.connTimeout = openvpn3_config::connection_timeout_seconds;
    impl_->config.retryOnAuthFailed = openvpn3_config::retry_on_auth_failed;
    const std::string encoded_rules = encode_route_rules(impl_->route_rules);
    if (!encoded_rules.empty())
        impl_->config.content += "setenv VPN_DESKTOP_ROUTE_RULES " + encoded_rules + "\n";
#if defined(_WIN32)
    // OpenVPN3's Client::eval_config() copies these settings into internal
    // client state, so choose the adapter after a side-effect-free helper
    // evaluation but before evaluating the live client (see upstream OMI).
    // A standard Layer-3 tunnel gets a dynamically provisioned Wintun adapter
    // so concurrent sessions do not contend for TAP-Windows6's exclusive
    // device handle. Explicit driver choices and Layer-2 TAP profiles retain
    // their requested behavior.
    openvpn::ClientAPI::OpenVPNClientHelper helper;
    const auto driver_evaluation = helper.eval_config(impl_->config);
    impl_->config.wintun = !driver_evaluation.error &&
        windows_driver_policy::use_wintun(impl_->config.content,
                                          driver_evaluation.windowsDriver);
#endif
    const openvpn::ClientAPI::EvalConfig evaluation =
        impl_->client->eval_config(impl_->config);

    ProfileEvaluation result;
    result.ok = !evaluation.error;
    result.message = evaluation.message;
    result.remote_host = evaluation.remoteHost;
    result.remote_port = evaluation.remotePort;
    result.remote_protocol = evaluation.remoteProto;
    impl_->profile_set = result.ok;
    return result;
}

void OpenVPN3Session::setRouteConflictMode(RouteConflictMode mode) {
    if (impl_ != nullptr) {
        impl_->route_conflict_mode = mode;
    }
}

void OpenVPN3Session::setRouteRules(const std::vector<RouteRule>& rules) {
    if (impl_ != nullptr) impl_->route_rules = rules;
}

bool OpenVPN3Session::setCredentials(const EngineCredentials& credentials) {
    openvpn::ClientAPI::ProvideCreds native_credentials;
    native_credentials.username = credentials.username;
    native_credentials.password = credentials.password;
    native_credentials.response = credentials.response;
    native_credentials.dynamicChallengeCookie = credentials.dynamic_challenge_cookie;

    const openvpn::ClientAPI::Status status =
        impl_->client->provide_creds(native_credentials);
    impl_->credentials_set = !status.error;
    return impl_->credentials_set;
}

bool OpenVPN3Session::connect() {
    if (!impl_->profile_set || impl_->worker.running()) {
        return false;
    }

    impl_->active = true;
    try {
        const bool started = impl_->worker.start([this]() {
            try {
                const auto status = impl_->client->connect();
                impl_->active = false;
                if (status.error) impl_->client->reportFailure(status.message);
            } catch (...) {
                impl_->active = false;
                impl_->client->reportFailure("VPN 引擎异常退出");
            }
        });
        if (!started) impl_->active = false;
        return started;
    } catch (...) {
        impl_->active = false;
        throw;
    }
}

void OpenVPN3Session::stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->worker.requestStop([this]() { impl_->stop.stop(); });
    impl_->active = false;
}

void OpenVPN3Session::pause() {
    if (impl_ != nullptr && impl_->worker.running()) {
        impl_->client->pause("用户请求暂停");
    }
}

void OpenVPN3Session::resume() {
    if (impl_ != nullptr && impl_->worker.running()) {
        impl_->client->resume();
    }
}

class OpenVPN3Engine::Impl {
public:
    openvpn::ClientAPI::OpenVPNClientHelper helper;
};

OpenVPN3Engine::OpenVPN3Engine()
    : impl_(std::make_unique<Impl>()) {}

OpenVPN3Engine::~OpenVPN3Engine() = default;

ProfileEvaluation OpenVPN3Engine::evaluateProfile(std::string_view profile_content) {
    openvpn::ClientAPI::Config config;
    config.content.assign(profile_content.data(), profile_content.size());
    const openvpn::ClientAPI::EvalConfig evaluation = impl_->helper.eval_config(config);

    ProfileEvaluation result;
    result.ok = !evaluation.error;
    result.message = evaluation.message;
    result.remote_host = evaluation.remoteHost;
    result.remote_port = evaluation.remotePort;
    result.remote_protocol = evaluation.remoteProto;
    return result;
}

std::unique_ptr<OpenVPN3Session> OpenVPN3Engine::createSession(
    std::string session_id,
    EngineEventSink event_sink) {
    return std::unique_ptr<OpenVPN3Session>(
        new OpenVPN3Session(std::move(session_id), std::move(event_sink)));
}

}  // namespace vpn
