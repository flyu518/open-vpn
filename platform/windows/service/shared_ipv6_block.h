#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace vpn::windows {

// The official IPv6 sink routes are machine-wide, not adapter-specific.
// Sessions share ownership, and never remove a route that already existed.
class SharedIpv6Block {
  public:
    struct Api {
        std::function<bool(unsigned int)> create; // true only for a new route
        std::function<void(unsigned int)> remove;
    };
    class State {
      public:
        explicit State(Api api) : api_(std::move(api)) {
            owned_.reserve(3); // No allocations after an OS route has been created.
            try {
                for (unsigned int i = 0; i < 3; ++i)
                    if (api_.create(i)) owned_.push_back(i);
            } catch (...) {
                cleanup();
                throw;
            }
        }
        ~State() { cleanup(); }
      private:
        void cleanup() noexcept {
            for (const auto i : owned_) {
                try { api_.remove(i); } catch (...) {}
            }
            owned_.clear();
        }
        Api api_;
        std::vector<unsigned int> owned_;
    };
    using Lease = std::shared_ptr<State>;
    SharedIpv6Block();
    explicit SharedIpv6Block(Api api) : api_(std::move(api)) {}
    Lease acquire() {
        if (auto current = active_.lock()) return current;
        auto current = std::make_shared<State>(api_);
        active_ = current;
        return current;
    }
  private:
    Api api_;
    std::weak_ptr<State> active_;
};
} // namespace vpn::windows
