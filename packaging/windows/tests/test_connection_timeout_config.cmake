if(NOT DEFINED ENGINE_SOURCE OR NOT DEFINED CONFIG_HEADER)
    message(FATAL_ERROR "ENGINE_SOURCE and CONFIG_HEADER are required")
endif()

file(READ "${ENGINE_SOURCE}" engine_source)
file(READ "${CONFIG_HEADER}" config_header)

string(FIND "${engine_source}" "openvpn3_config::connection_timeout_seconds" timeout_use)
string(FIND "${engine_source}" "openvpn3_config::retry_on_auth_failed" auth_retry_use)
string(REGEX MATCH "connection_timeout_seconds[ \\t]*=[ \\t]*[1-9][0-9]*" timeout_definition
    "${config_header}")

if(timeout_use EQUAL -1 OR auth_retry_use EQUAL -1 OR timeout_definition STREQUAL "")
    message(FATAL_ERROR "OpenVPN3 must use the shared finite connection timeout configuration")
endif()

message(STATUS "Shared OpenVPN3 connection timeout configuration verified")
