#ifndef VPN_ENGINE_H
#define VPN_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vpn_engine vpn_engine_t;

typedef enum vpn_capability {
    VPN_CAPABILITY_DESKTOP_MULTI = 1,
    VPN_CAPABILITY_MOBILE_SINGLE = 2
} vpn_capability_t;

typedef enum vpn_error_code {
    VPN_OK = 0,
    VPN_ERR_INVALID_ARGUMENT = 1,
    VPN_ERR_OUT_OF_MEMORY = 2,
    VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN = 1001,
    VPN_ERR_ROUTE_CONFLICT = 1002,
    VPN_ERR_FULL_TUNNEL_EXCLUSIVE = 1003
} vpn_error_code_t;

int vpn_engine_create(vpn_engine_t** out_engine, vpn_capability_t capability);
void vpn_engine_destroy(vpn_engine_t* engine);
vpn_capability_t vpn_engine_capability(const vpn_engine_t* engine);
uint32_t vpn_engine_max_active_sessions(const vpn_engine_t* engine);

#ifdef __cplusplus
}
#endif

#endif
