#include "engine_worker.h"

#include <exception>
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
    try {
        thread_ = std::thread([this, task = std::move(task)]() mutable {
            task();
            running_.store(false);
        });
    } catch (...) {
        running_.store(false);
        throw;
    }
    return true;
}

void EngineWorker::requestStop(std::function<void()> stop_task) {
    std::exception_ptr failure;
    if (stop_task) {
        try { stop_task(); } catch (...) { failure = std::current_exception(); }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
    if (failure) std::rethrow_exception(failure);
}

bool EngineWorker::running() const {
    return running_.load();
}

}  // namespace vpn
