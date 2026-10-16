#include "vpn_fake_engine.h"

#include <utility>

namespace vpn {

FakeEngine::FakeEngine(std::string session_id)
    : session_id_(std::move(session_id)) {}

bool FakeEngine::start() {
    active_ = true;
    return true;
}

void FakeEngine::stop() {
    active_ = false;
}

bool FakeEngine::active() const {
    return active_;
}

}  // namespace vpn
