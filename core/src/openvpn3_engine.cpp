#include "openvpn3_engine.h"

#include "engine_worker.h"

#include <memory>
#include <mutex>
#include <utility>

#include "client/ovpncli.cpp"

namespace vpn {
namespace {

class ClientImpl final : public openvpn::ClientAPI::OpenVPNClient {
public:
    ClientImpl(std::string session_id, EngineEventSink event_sink)
        : session_id_(std::move(session_id)), event_sink_(std::move(event_sink)) {}

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

private:
    std::string session_id_;
    EngineEventSink event_sink_;
};

}  // namespace

class OpenVPN3Session::Impl {
public:
    Impl(std::string session_id, EngineEventSink event_sink)
        : client(std::make_unique<ClientImpl>(std::move(session_id),
                                               std::move(event_sink))),
          active(true) {}

    std::unique_ptr<ClientImpl> client;
    EngineWorker worker;
    openvpn::ClientAPI::Config config;
    bool profile_set = false;
    bool credentials_set = false;
    bool active = false;
};

OpenVPN3Session::OpenVPN3Session(std::string session_id,
                                 EngineEventSink event_sink)
    : impl_(std::make_unique<Impl>(std::move(session_id), std::move(event_sink))) {}

OpenVPN3Session::~OpenVPN3Session() {
    stop();
}

const std::string& OpenVPN3Session::sessionId() const {
    return impl_->client->sessionId();
}

bool OpenVPN3Session::isActive() const {
    return impl_ != nullptr && impl_->active;
}

ProfileEvaluation OpenVPN3Session::setProfile(std::string_view profile_content) {
    impl_->config = openvpn::ClientAPI::Config{};
    impl_->config.content.assign(profile_content.data(), profile_content.size());
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
    return impl_->worker.start([this]() {
        const openvpn::ClientAPI::Status status = impl_->client->connect();
        if (status.error) {
            impl_->active = false;
        }
    });
}

void OpenVPN3Session::stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->worker.requestStop([this]() { impl_->client->stop(); });
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
