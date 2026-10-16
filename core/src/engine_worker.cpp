#include "engine_worker.h"

#include <utility>

namespace vpn {

EngineWorker::~EngineWorker() {
    requestStop({});
}

bool EngineWorker::start(std::function<void()> task) {
    if (thread_.joinable() || !task) {
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this, task = std::move(task)]() mutable {
        task();
        running_.store(false);
    });
    return true;
}

void EngineWorker::requestStop(std::function<void()> stop_task) {
    if (stop_task && running_.load()) {
        stop_task();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

bool EngineWorker::running() const {
    return running_.load();
}

}  // namespace vpn
