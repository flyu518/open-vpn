#ifndef VPN_ENGINE_WORKER_H
#define VPN_ENGINE_WORKER_H

#include <atomic>
#include <functional>
#include <thread>

namespace vpn {

class EngineWorker {
public:
    EngineWorker() = default;
    ~EngineWorker();

    EngineWorker(const EngineWorker&) = delete;
    EngineWorker& operator=(const EngineWorker&) = delete;

    bool start(std::function<void()> task);
    void requestStop(std::function<void()> stop_task);
    bool running() const;

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace vpn

#endif
