if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "需要 SOURCE_DIR 和 BINARY_DIR")
endif()

file(READ "${SOURCE_DIR}/core/CMakeLists.txt" CORE_CMAKE)
foreach(TARGET_NAME openvpn_core openvpn_windows_service)
    string(FIND "${CORE_CMAKE}"
        "target_compile_definitions(${TARGET_NAME} PRIVATE" TARGET_BLOCK_START)
    if(TARGET_BLOCK_START EQUAL -1)
        message(FATAL_ERROR "未找到 ${TARGET_NAME} 的 Windows 编译定义")
    endif()
    string(SUBSTRING "${CORE_CMAKE}" ${TARGET_BLOCK_START} -1 TARGET_BLOCK_REMAINDER)
    string(FIND "${TARGET_BLOCK_REMAINDER}" ")" TARGET_BLOCK_END)
    if(TARGET_BLOCK_END EQUAL -1)
        message(FATAL_ERROR "无法读取 ${TARGET_NAME} 的编译定义")
    endif()
    string(SUBSTRING "${TARGET_BLOCK_REMAINDER}" 0 ${TARGET_BLOCK_END} TARGET_BLOCK)
    string(FIND "${TARGET_BLOCK}" "TAP_WIN_COMPONENT_ID=tap0901" TAP_COMPONENT_AT)
    if(TAP_COMPONENT_AT EQUAL -1)
        message(FATAL_ERROR "${TARGET_NAME} 缺少 TAP-Windows6 组件 ID tap0901")
    endif()
endforeach()

if(DEFINED OPENVPN3_SOURCE_DIR AND EXISTS "${OPENVPN3_SOURCE_DIR}")
    set(OPENVPN3_DIR "${OPENVPN3_SOURCE_DIR}")
else()
    set(OPENVPN3_DIR "${SOURCE_DIR}/third_party/openvpn3")
endif()
set(OUTPUT_DIR "${BINARY_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_patch SCRIPT INPUT RELATIVE_OUTPUT)
    set(output "${OUTPUT_DIR}/${RELATIVE_OUTPUT}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_SOURCE=${OPENVPN3_DIR}/${INPUT}"
            "-DOUTPUT_SOURCE=${output}"
            -P "${SOURCE_DIR}/${SCRIPT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "补丁脚本失败：${SCRIPT}\n${stdout}\n${stderr}")
    endif()
    if(NOT EXISTS "${output}")
        message(FATAL_ERROR "补丁脚本没有生成输出：${output}")
    endif()
endfunction()

run_patch(
    platform/windows/service/generate_cliopt.cmake
    openvpn/client/cliopt.hpp
    cliopt.hpp)
run_patch(
    platform/windows/service/generate_cmdagent.cmake
    openvpn/client/win/cmdagent.hpp
    cmdagent.hpp)
run_patch(
    platform/windows/service/generate_ovpncli.cmake
    client/ovpncli.cpp
    ovpncli.cpp)

file(READ "${OUTPUT_DIR}/cliopt.hpp" CLIOPT)
file(READ "${OUTPUT_DIR}/cmdagent.hpp" CMDAGENT)
file(READ "${OUTPUT_DIR}/ovpncli.cpp" OVPNCLI)

foreach(marker
        "load_transport_config(&opt)"
        "VPN_DESKTOP_SESSION_ID"
        "session_name_override"
        "wintun = config.wintun"
        "bool wintun = false"
        "config.clientconf.wintun ? TunWin::Wintun : TunWin::TapWindows6")
    string(FIND "${CLIOPT}" "${marker}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "cliopt 补丁缺少标记：${marker}")
    endif()
endforeach()

foreach(marker
        "desktop_session_name"
        "GetCurrentProcessId"
        "jreq[\"session_name\"] = config->session_name"
        "session_name = pull.session_name"
        "add_bypass_route(IP::Addr endpoint,")
    string(FIND "${CMDAGENT}" "${marker}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Windows command agent 补丁缺少标记：${marker}")
    endif()
endforeach()

foreach(marker
        "set_session_name_from_options"
        "WinCommandAgent::add_bypass_route(endpoint, session_name)"
        "socket_protect.set_session_name_from_options(options)")
    string(FIND "${OVPNCLI}" "${marker}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "ovpncli 补丁缺少标记：${marker}")
    endif()
endforeach()

message(STATUS "Windows client patch tests passed")
