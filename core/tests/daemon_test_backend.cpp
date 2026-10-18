// Link-time substitutes used ONLY by daemon tests. No sockets, profiles,
// tun devices, platform helper, or credential APIs are accessible from this file.
#include "openvpn3_engine.h"
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include "platform/macos/secure_store/keychain_store.h"
#endif

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vpn {
class OpenVPN3Session::Impl {
public:
    std::string id;
    std::string profile;
    EngineEventSink sink;
    std::atomic<bool> stopped{false};
    std::thread worker;
};

OpenVPN3Session::OpenVPN3Session(std::string id, EngineEventSink sink)
    : impl_(std::make_unique<Impl>()) {
    impl_->id = std::move(id);
    impl_->sink = std::move(sink);
}
OpenVPN3Session::~OpenVPN3Session() {
    impl_->stopped = true;
    if (impl_->worker.joinable()) impl_->worker.join();
}
const std::string& OpenVPN3Session::sessionId() const { return impl_->id; }
bool OpenVPN3Session::isActive() const { return !impl_->stopped; }
SessionTrafficStats OpenVPN3Session::trafficStats() const {
    return impl_->id == "b" ? SessionTrafficStats{222, 22} : SessionTrafficStats{111, 11};
}
ProfileEvaluation OpenVPN3Session::setProfile(std::string_view profile) {
    impl_->profile = profile;
    if (profile != "fixture:connected" && profile != "fixture:warning" &&
        profile != "fixture:failure" && profile != "fixture:stop-error" &&
        profile != "fixture:slow-start" && profile != "fixture:burst" &&
        profile != "fixture:connecting" && profile != "fixture:inactive-failure" &&
        profile != "fixture:timeout" && profile != "fixture:auth-rejection" &&
        profile != "fixture:disconnected" && profile != "fixture:connect-error" && profile != "fixture:strict-conflict" && profile != "fixture:external-conflict") {
        throw std::runtime_error("测试替身禁止使用真实 VPN 配置");
    }
    return {true, "", "fixture.invalid", "", ""};
}
void OpenVPN3Session::setRouteConflictMode(RouteConflictMode) {}
void OpenVPN3Session::setRouteRules(const std::vector<RouteRule>&) {}
bool OpenVPN3Session::setCredentials(const EngineCredentials&) { return true; }
bool OpenVPN3Session::connect() {
    if (impl_->profile == "fixture:timeout" || impl_->profile == "fixture:auth-rejection") {
        impl_->sink({impl_->profile == "fixture:timeout" ? "CONNECTION_TIMEOUT" : "AUTH_FAILED",
                     "", false, false}); // Native events need not have error/fatal flags.
        impl_->sink({"DISCONNECTED", "", false, false});
        impl_->stopped = true;
        return true;
    }
    if (impl_->profile == "fixture:connecting") return true;
    if (impl_->profile == "fixture:connect-error") throw std::runtime_error("模拟启动失败");
    if (impl_->profile == "fixture:slow-start")
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    impl_->sink({"CONNECTED", "", false, false});
    if (impl_->profile == "fixture:warning") {
        for (int i = 0; i < 2; ++i)
            impl_->sink({"LOG", "SetupClient: transmitting tun setup list\n兼容模式：跳过 2 条冲突路由\n"
                "冲突：当前 10.20.0.0/16；前一个会话 " + std::to_string(getpid()) + ":a 使用 10.0.0.0/8\n"
                "冲突：当前 10.20.0.0/16；前一个会话 " + std::to_string(getpid()) + ":a 使用 10.0.0.0/8\n"
                "冲突：当前 2001:db8::/64；前一个会话 " + std::to_string(getpid()) + ":a 使用 2001:db8::/32\n", false, false});
    }
    if (impl_->profile == "fixture:strict-conflict") {
        impl_->sink({"LOG", "ovpnagent: request error: 路由与活动 VPN 重叠：候选会话 " +
            std::to_string(getpid()) + ":b 的 10.20.0.0/16 与活动会话 " + std::to_string(getpid()) +
            ":a 的 10.0.0.0/8 重叠", false, false});
        impl_->sink({"TUN_SETUP_FAILED", "ovpnagent: request error", true, true});
        return true;
    }
    if (impl_->profile == "fixture:external-conflict") {
        impl_->sink({"LOG", "兼容模式：跳过 1 条冲突路由\n冲突：当前 10.20.0.0/16；前一个会话 0:a 使用 10.0.0.0/8\n", false, false});
    }
    if (impl_->profile == "fixture:failure" || impl_->profile == "fixture:inactive-failure") {
        impl_->sink({"FATAL_ERROR", "password=synthetic-secret", true, true});
        impl_->sink({"DISCONNECTED", "", false, false});
    }
    if (impl_->profile == "fixture:disconnected")
        impl_->sink({"DISCONNECTED", "", false, false});
    if (impl_->profile == "fixture:disconnected" || impl_->profile == "fixture:inactive-failure")
        impl_->stopped = true;
    if (impl_->profile == "fixture:burst") {
        impl_->worker = std::thread([this] {
            for (int i = 0; i < 48 && !impl_->stopped; ++i) {
                impl_->sink({"LOG", std::string(196608, 'z'), false, false});
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }
    return true;
}
void OpenVPN3Session::stop() {
    impl_->stopped = true;
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->profile == "fixture:stop-error") {
        impl_->profile.clear();
        throw std::runtime_error("模拟断开失败");
    }
}
void OpenVPN3Session::pause() {}
void OpenVPN3Session::resume() {}
class OpenVPN3Engine::Impl {};
OpenVPN3Engine::OpenVPN3Engine() : impl_(std::make_unique<Impl>()) {}
OpenVPN3Engine::~OpenVPN3Engine() = default;
std::unique_ptr<OpenVPN3Session> OpenVPN3Engine::createSession(std::string id, EngineEventSink sink) {
    return std::unique_ptr<OpenVPN3Session>(new OpenVPN3Session(std::move(id), std::move(sink)));
}

#if defined(__APPLE__)
namespace platform::macos {
KeychainCredentialStore::KeychainCredentialStore(std::string service) : service_(std::move(service)) {}
CredentialStoreResult KeychainCredentialStore::load(std::string_view, Credentials*) {
    throw std::runtime_error("测试替身禁止读取凭据");
}
CredentialStoreResult KeychainCredentialStore::save(std::string_view, const Credentials&) {
    throw std::runtime_error("测试替身禁止写入凭据");
}
CredentialStoreResult KeychainCredentialStore::remove(std::string_view) {
    throw std::runtime_error("测试替身禁止删除凭据");
}
}  // namespace platform::macos
#endif
}  // namespace vpn
