// Inert native boundary for testing the real OpenVPN3Session/EngineWorker code.
// Models the upstream startup window: stop() is ignored before foreign-thread
// access is enabled, while get_async_stop() registers a persistent Stop scope.
// No networking, tun setup, filesystem access or credential backend exists here.
#include "openvpn/common/stop.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace openvpn_io::detail { using socket_type = int; }
namespace openvpn::ClientAPI {
struct Config { std::string content; };
struct EvalConfig {
    bool error = false;
    std::string message, remoteHost, remotePort, remoteProto;
};
struct ProvideCreds { std::string username, password, response, dynamicChallengeCookie; };
struct Status { bool error = false; std::string message; };
struct Event { std::string name, info; bool error = false, fatal = false; };
struct AppCustomControlMessageEvent { std::string protocol, payload; };
struct LogInfo { std::string text; };
struct ExternalPKICertRequest { bool error = false; std::string errorText; };
using ExternalPKISignRequest = ExternalPKICertRequest;
struct TransportStats { long long bytesIn = 0, bytesOut = 0; };
class OpenVPNClientHelper {
public:
    EvalConfig eval_config(const Config&) { return {}; }
};
class OpenVPNClient {
public:
    virtual ~OpenVPNClient() = default;
    virtual bool pause_on_connection_timeout() = 0;
    virtual void event(const Event&) = 0;
    virtual void acc_event(const AppCustomControlMessageEvent&) = 0;
    virtual void log(const LogInfo&) = 0;
    virtual void external_pki_cert_request(ExternalPKICertRequest&) = 0;
    virtual void external_pki_sign_request(ExternalPKISignRequest&) = 0;
    virtual bool socket_protect(int, std::string, bool) = 0;
    EvalConfig eval_config(const Config& config) { profile_ = config.content; return {}; }
    Status provide_creds(const ProvideCreds&) { return {}; }
    TransportStats transport_stats() const { return {}; }
    Status connect() {
        if (profile_.find("fixture:throw") == 0) throw std::runtime_error("模拟引擎异常");
        if (profile_.find("fixture:status-error") == 0) return {true, "模拟初始化失败"};
        if (profile_.find("fixture:success") == 0) return {};
        event({"STARTING", "", false, false});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        scope_ = std::make_unique<Stop::Scope>(get_async_stop(), [this] { cancel(); });
        ready_ = true;
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, std::chrono::seconds(3), [this] { return cancelled_; }))
            return {true, "引擎启动期间丢失了停止请求"};
        return {};
    }
    void stop() { if (ready_) cancel(); }
    void pause(const std::string&) {}
    void resume() {}
protected:
    virtual Stop* get_async_stop() { return nullptr; }
private:
    void cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        condition_.notify_all();
    }
    std::string profile_;
    std::atomic<bool> ready_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool cancelled_ = false;
    std::unique_ptr<Stop::Scope> scope_;
};
}  // namespace openvpn::ClientAPI
