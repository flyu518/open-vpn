#include "vpn_engine.h"

#include <limits>
#include <new>

struct vpn_engine {
    vpn_capability_t capability;
    uint32_t max_active_sessions;
};

namespace {

bool is_valid_capability(vpn_capability_t capability) {
    return capability == VPN_CAPABILITY_DESKTOP_MULTI ||
           capability == VPN_CAPABILITY_MOBILE_SINGLE;
}

uint32_t max_sessions_for(vpn_capability_t capability) {
    return capability == VPN_CAPABILITY_MOBILE_SINGLE
               ? 1U
               : std::numeric_limits<uint32_t>::max();
}

}  // namespace

extern "C" int vpn_engine_create(vpn_engine_t** out_engine,
                                  vpn_capability_t capability) {
    if (out_engine == nullptr || !is_valid_capability(capability)) {
        return VPN_ERR_INVALID_ARGUMENT;
    }

    vpn_engine_t* engine = new (std::nothrow) vpn_engine{capability,
                                                          max_sessions_for(capability)};
    if (engine == nullptr) {
        return VPN_ERR_OUT_OF_MEMORY;
    }

    *out_engine = engine;
    return VPN_OK;
}

extern "C" void vpn_engine_destroy(vpn_engine_t* engine) {
    delete engine;
}

extern "C" vpn_capability_t vpn_engine_capability(const vpn_engine_t* engine) {
    return engine == nullptr ? static_cast<vpn_capability_t>(0) : engine->capability;
}

extern "C" uint32_t vpn_engine_max_active_sessions(const vpn_engine_t* engine) {
    return engine == nullptr ? 0U : engine->max_active_sessions;
}
