#include "daemon_session_controller.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using vpn::daemon::SessionController;
constexpr const char* limit_message =
    "当前Windows版本暂不支持同时连接多条VPN，请先断开已有连接";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ignore_notification(const Json::Value&) {}

Json::Value connect_request(const std::string& id, const std::string& profile = "fixture:connected") {
    Json::Value request(Json::objectValue);
    request["command"] = "connect";
    request["sessionId"] = id;
    request["profile"] = profile;
    request["credentials"]["username"] = "synthetic";
    request["credentials"]["password"] = "synthetic";
    return request;
}

Json::Value request(SessionController& controller, const Json::Value& value) {
    return vpn::daemon::process_request(vpn::daemon::write_json(value),
        [&](const Json::Value& parsed) { return controller.handleRequest(parsed); });
}

Json::Value command(SessionController& controller, const char* name, const char* id = "") {
    Json::Value value(Json::objectValue);
    value["command"] = name;
    value["sessionId"] = id;
    return request(controller, value);
}

void require_limited(SessionController& controller, const Json::Value& attempt) {
    const auto result = request(controller, attempt);
    require(!result["ok"].asBool(), "第二个活动会话未被阻止");
    require(result["message"].asString() == limit_message, "单连接限制未返回指定中文文案");
    require(command(controller, "ping")["ok"].asBool(), "被拒绝的请求影响了后续 ping");
}

void connected_session_is_preserved() {
    SessionController controller(ignore_notification, nullptr, 1);
    require(request(controller, connect_request("a"))["ok"].asBool(), "首个连接失败");
    require_limited(controller, connect_request("b"));
    const auto sessions = command(controller, "snapshot")["sessions"];
    require(sessions.size() == 1 && sessions[0]["sessionId"] == "a", "首个会话被替换或出现第二个会话");
    require(sessions[0]["state"] == "connected" && sessions[0]["downloadBytes"].asInt64() == 111,
            "被拒绝的请求改变了首个连接状态或统计");
    require(command(controller, "disconnect", "a")["ok"].asBool(), "断开首个连接失败");
    require(request(controller, connect_request("b"))["ok"].asBool(), "断开完成后未释放连接名额");
}

void connecting_session_occupies_slot() {
    SessionController controller(ignore_notification, nullptr, 1);
    const auto first = request(controller, connect_request("a", "fixture:connecting"));
    require(first["ok"].asBool() && first["session"]["state"] == "connecting", "连接中状态设置失败");
    require_limited(controller, connect_request("b"));
    require(command(controller, "snapshot")["sessions"][0]["state"] == "connecting",
            "第二个请求中断了首个连接的建立过程");
}

class CredentialProbe final : public vpn::CredentialStore {
public:
    int accesses = 0;
    vpn::CredentialStoreResult load(std::string_view, vpn::Credentials*) override {
        ++accesses;
        return {vpn::CredentialStoreStatus::NotFound, "测试凭据不存在"};
    }
    vpn::CredentialStoreResult save(std::string_view, const vpn::Credentials&) override {
        ++accesses;
        throw std::runtime_error("本测试禁止保存凭据");
    }
    vpn::CredentialStoreResult remove(std::string_view) override {
        ++accesses;
        throw std::runtime_error("本测试禁止删除凭据");
    }
};

void rejects_before_credentials_or_engine_setup() {
    CredentialProbe store;
    SessionController controller(ignore_notification, &store, 1);
    require(request(controller, connect_request("a"))["ok"].asBool(), "首个连接失败");
    // 该 profile 会被引擎替身拒绝；必须在尝试读取凭据或设置 profile 前返回容量错误。
    auto second = connect_request("b", "forbidden-profile");
    second.removeMember("credentials");
    require_limited(controller, second);
    require(store.accesses == 0, "被拒绝的连接访问了凭据存储");
    require_limited(controller, connect_request("b", "forbidden-profile"));
}

void concurrent_connects_reserve_one_slot() {
    SessionController controller(ignore_notification, nullptr, 1);
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<Json::Value> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] {
            ready.wait();
            results[i] = request(controller, connect_request(std::to_string(i), "fixture:slow-start"));
        });
    }
    start.set_value();
    for (auto& thread : threads) thread.join();
    unsigned accepted = 0;
    for (const auto& result : results) {
        if (result["ok"].asBool()) ++accepted;
        else require(result["message"].asString() == limit_message, "并发拒绝未返回容量错误");
    }
    require(accepted == 1 && command(controller, "snapshot")["sessions"].size() == 1,
            "并发 connect 绕过了单连接限制");
}

void failed_but_running_engine_keeps_slot() {
    SessionController controller(ignore_notification, nullptr, 1);
    const auto first = request(controller, connect_request("a", "fixture:failure"));
    require(first["ok"].asBool() && first["session"]["state"] == "failed", "失败状态设置失败");
    // 显示为 failed 不代表原生引擎已完成退出，不能仅按 UI 状态释放名额。
    require_limited(controller, connect_request("b"));
}

void inactive_terminal_records_do_not_consume_slots() {
    for (const auto* profile : {"fixture:inactive-failure", "fixture:disconnected"}) {
        SessionController controller(ignore_notification, nullptr, 1);
        require(request(controller, connect_request("a", profile))["ok"].asBool(), "终止状态设置失败");
        require(request(controller, connect_request("b"))["ok"].asBool(), "已退出的历史记录占用了活动名额");
        require_limited(controller, connect_request("c"));
    }
}

void failed_start_releases_slot() {
    SessionController controller(ignore_notification, nullptr, 1);
    require(!request(controller, connect_request("a", "fixture:connect-error"))["ok"].asBool(),
            "模拟启动失败未被处理");
    require(request(controller, connect_request("b"))["ok"].asBool(), "启动失败后名额未释放");
}

void default_configuration_remains_unlimited() {
    SessionController controller(ignore_notification, nullptr);
    require(request(controller, connect_request("a"))["ok"].asBool(), "默认配置的首个连接失败");
    require(request(controller, connect_request("b", "fixture:connecting"))["ok"].asBool(),
            "默认配置错误限制了第二个连接");
    require(request(controller, connect_request("c"))["ok"].asBool(), "默认配置错误限制了第三个连接");
    require(command(controller, "snapshot")["sessions"].size() == 3, "默认配置丢失了独立会话");
}

}  // namespace

int main() {
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"保留首个连接及断开后释放名额", connected_session_is_preserved},
        {"连接中占用活动名额", connecting_session_occupies_slot},
        {"在凭据访问和引擎初始化前拒绝", rejects_before_credentials_or_engine_setup},
        {"并发连接只允许一个成功", concurrent_connects_reserve_one_slot},
        {"失败但引擎尚未退出时保持名额", failed_but_running_engine_keeps_slot},
        {"已退出的历史记录不占活动名额", inactive_terminal_records_do_not_consume_slots},
        {"启动失败后释放名额", failed_start_releases_slot},
        {"默认配置保持不限连接数", default_configuration_remains_unlimited},
    };
    int failures = 0;
    for (const auto& test : cases) {
        try {
            test.run();
            std::cout << "通过：" << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "失败：" << test.name << "：" << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
