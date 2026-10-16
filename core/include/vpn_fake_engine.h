#ifndef VPN_FAKE_ENGINE_H
#define VPN_FAKE_ENGINE_H

#include <string>

namespace vpn {

class FakeEngine {
public:
    explicit FakeEngine(std::string session_id);

    FakeEngine(const FakeEngine&) = delete;
    FakeEngine& operator=(const FakeEngine&) = delete;
    FakeEngine(FakeEngine&&) noexcept = default;
    FakeEngine& operator=(FakeEngine&&) noexcept = default;

    bool start();
    void stop();
    bool active() const;

private:
    std::string session_id_;
    bool active_ = false;
};

}  // namespace vpn

#endif
