#include "openvpn3_engine.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    std::promise<void> starting;
    std::atomic<bool> failed{false};
    vpn::OpenVPN3Engine engine;
    auto session = engine.createSession("test", [&](const vpn::EngineEvent& event) {
        if (event.name == "STARTING") starting.set_value();
        if (event.name == "FATAL_ERROR" && event.error && event.fatal) failed = true;
    });
    if (!session->setProfile("fixture:" + mode).ok || !session->connect()) return 3;
    if (mode == "early-stop") {
        if (starting.get_future().wait_for(std::chrono::seconds(1)) != std::future_status::ready)
            return 4;
        const auto before = std::chrono::steady_clock::now();
        session->stop();
        if (std::chrono::steady_clock::now() - before > std::chrono::seconds(1) || failed) {
            std::cerr << "引擎启动完成前丢失了停止请求\n";
            return 1;
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (session->isActive() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool still_active = session->isActive();
        session->stop();
        if (still_active || (mode != "success" && !failed)) {
            std::cerr << "引擎退出后活动状态未更新，或未发出失败事件\n";
            return 1;
        }
    }
    return 0;
}
