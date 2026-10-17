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
        "vpn_route_policy_validate"
        "default_session_name"
        "parent()->set_client_process(session_key(),")
    string(FIND "${GENERATED}" "${MARKER}" MARKER_AT)
    if(MARKER_AT EQUAL -1)
        message(FATAL_ERROR "生成结果缺少 Windows 多会话标记：${MARKER}")
    endif()
endforeach()

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

set(UPSTREAM_SOURCE "${SOURCE_DIR}/third_party/openvpn3/openvpn/ovpnagent/win/ovpnagent.cpp")
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
endif()

message(STATUS "Windows agent 多会话生成器 fixture 和缺失边界测试通过")
