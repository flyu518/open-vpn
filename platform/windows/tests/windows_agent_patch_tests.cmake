if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "需要 SOURCE_DIR 和 BINARY_DIR")
endif()

set(GENERATOR "${SOURCE_DIR}/platform/windows/service/generate_multisession_agent.cmake")
set(FIXTURE "${SOURCE_DIR}/platform/windows/tests/windows_agent_source_fixture.cpp.txt")
set(OUTPUT_DIR "${BINARY_DIR}/generated")
set(OUTPUT_SOURCE "${OUTPUT_DIR}/windows-agent-generated.cpp")
set(MISSING_FIXTURE "${BINARY_DIR}/missing-boundary-fixture.cpp")
set(MISSING_OUTPUT "${BINARY_DIR}/missing-boundary-output.cpp")

if(NOT EXISTS "${GENERATOR}" OR NOT EXISTS "${FIXTURE}")
    message(FATAL_ERROR "Windows agent generator fixture 不存在")
endif()

file(MAKE_DIRECTORY "${BINARY_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_SOURCE=${FIXTURE}"
        "-DOUTPUT_SOURCE=${OUTPUT_SOURCE}"
        -P "${GENERATOR}"
    RESULT_VARIABLE PATCH_RESULT
    OUTPUT_VARIABLE PATCH_OUTPUT
    ERROR_VARIABLE PATCH_ERROR
)
if(NOT PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "完整 Windows agent fixture 生成失败：${PATCH_OUTPUT}${PATCH_ERROR}")
endif()

file(READ "${OUTPUT_SOURCE}" GENERATED)
foreach(MARKER
        "struct WindowsAgentSessionState"
        "std::map<SessionKey, WindowsAgentSessionState> session_states"
        "WindowsAgentSessionState &session_state(const SessionKey &key)"
        "destroy_tuns_for_pid"
        "adopt_pending_tun"
        "parent()->adopt_pending_tun(session_pid, session_key(), pending_tap, os)"
        "confirm event and remote TAP handle"
        "session_states.find(key)"
        "std::map<SessionKey, vpn::RoutePolicy> route_policies"
        "wintun_adapter_lease.h"
        "session_wintun_setup.h"
        "vpn::windows::SharedIpv6Block::Lease ipv6_block"
        "ipv6_blocks.acquire()"
        "Win::ScopedHANDLE handle(state.tun->establish"
        "state.ipv6_block.reset()"
        "new SessionWintunSetup(io_context_,"
        "std::unique_ptr<vpn::windows::WintunAdapterLease> wintun_adapter"
        "tun_type == TunWin::Wintun && !state.wintun_adapter"
        "Wintun adapters visible to agent:"
        "vpn_route_policy_validate"
        "default_session_name"
        "parent()->set_client_process(session_key(),")
    string(FIND "${GENERATED}" "${MARKER}" MARKER_AT)
    if(MARKER_AT EQUAL -1)
        message(FATAL_ERROR "生成结果缺少 Windows 多会话标记：${MARKER}")
    endif()
endforeach()

string(FIND "${GENERATED}"
    "Win::ScopedHANDLE establish_tun(const SessionKey &key," ESTABLISH_START)
string(FIND "${GENERATED}" "    bool destroy_tun(const SessionKey &key," DESTROY_START)
if(ESTABLISH_START EQUAL -1 OR DESTROY_START EQUAL -1 OR
   DESTROY_START LESS_EQUAL ESTABLISH_START)
    message(FATAL_ERROR "无法定位 Windows 会话 establish_tun 方法")
endif()
math(EXPR ESTABLISH_LENGTH "${DESTROY_START} - ${ESTABLISH_START}")
string(SUBSTRING "${GENERATED}" ${ESTABLISH_START} ${ESTABLISH_LENGTH}
    ESTABLISH_METHOD)
string(FIND "${ESTABLISH_METHOD}"
    "tun_type == TunWin::Wintun && !state.wintun_adapter" ESTABLISH_LEASE_AT)
string(FIND "${ESTABLISH_METHOD}" "state.tun->establish" TUN_ESTABLISH_AT)
if(ESTABLISH_LEASE_AT EQUAL -1 OR TUN_ESTABLISH_AT EQUAL -1 OR
   ESTABLISH_LEASE_AT GREATER TUN_ESTABLISH_AT)
    message(FATAL_ERROR
        "Wintun 会话必须在 establish_tun 创建或访问 TUN 设备前持有独立适配器")
endif()

string(FIND "${GENERATED}" "parent()->destroy_tun(os)" OLD_DESTROY_CALL_AT)
if(NOT OLD_DESTROY_CALL_AT EQUAL -1)
    message(FATAL_ERROR "生成结果仍保留单实例 destroy_tun 调用")
endif()

file(READ "${FIXTURE}" FIXTURE_SOURCE)
string(REPLACE "using namespace openvpn;" "using namespace openvpn_removed;"
               MISSING_SOURCE "${FIXTURE_SOURCE}")
file(WRITE "${MISSING_FIXTURE}" "${MISSING_SOURCE}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_SOURCE=${MISSING_FIXTURE}"
        "-DOUTPUT_SOURCE=${MISSING_OUTPUT}"
        -P "${GENERATOR}"
    RESULT_VARIABLE MISSING_RESULT
    OUTPUT_VARIABLE MISSING_OUTPUT_LOG
    ERROR_VARIABLE MISSING_ERROR_LOG
)
if(MISSING_RESULT EQUAL 0)
    message(FATAL_ERROR "缺少边界片段时 Windows agent 生成器没有失败")
endif()

if(DEFINED OPENVPN3_SOURCE_DIR AND EXISTS "${OPENVPN3_SOURCE_DIR}")
    set(OPENVPN3_DIR "${OPENVPN3_SOURCE_DIR}")
else()
    set(OPENVPN3_DIR "${SOURCE_DIR}/third_party/openvpn3")
endif()

set(UPSTREAM_SOURCE "${OPENVPN3_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp")
if(EXISTS "${UPSTREAM_SOURCE}")
    set(UPSTREAM_OUTPUT "${BINARY_DIR}/upstream/windows-agent-generated.cpp")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_SOURCE=${UPSTREAM_SOURCE}"
            "-DOUTPUT_SOURCE=${UPSTREAM_OUTPUT}"
            -P "${GENERATOR}"
        RESULT_VARIABLE UPSTREAM_RESULT
        OUTPUT_VARIABLE UPSTREAM_OUTPUT_LOG
        ERROR_VARIABLE UPSTREAM_ERROR_LOG
    )
    if(NOT UPSTREAM_RESULT EQUAL 0)
        message(FATAL_ERROR "锁定版 OpenVPN3 Windows agent 生成失败：${UPSTREAM_OUTPUT_LOG}${UPSTREAM_ERROR_LOG}")
    endif()
    file(READ "${UPSTREAM_OUTPUT}" UPSTREAM_GENERATED)
    foreach(MARKER
            "std::map<SessionKey, WindowsAgentSessionState> session_states"
            "parent()->get_client_process(session_key())"
            "parent()->establish_tun(session_key(), *tbc"
            "parent()->destroy_tun(session_key(), os)")
        string(FIND "${UPSTREAM_GENERATED}" "${MARKER}" UPSTREAM_MARKER_AT)
        if(UPSTREAM_MARKER_AT EQUAL -1)
            message(FATAL_ERROR "锁定版 OpenVPN3 Windows agent 生成结果缺少标记：${MARKER}")
        endif()
    endforeach()
    string(FIND "${UPSTREAM_GENERATED}" "TunWin::NRPT::delete_rules(0)" GLOBAL_DNS_CLEANUP)
    string(FIND "${UPSTREAM_GENERATED}" "TunWin::SessionNRPT::delete_rules(std::string())" SESSION_DNS_CLEANUP)
    if(NOT GLOBAL_DNS_CLEANUP EQUAL -1 OR SESSION_DNS_CLEANUP EQUAL -1)
        message(FATAL_ERROR "Windows agent must not remove another product's DNS rules")
    endif()
endif()

message(STATUS "Windows agent 多会话生成器 fixture 和缺失边界测试通过")
