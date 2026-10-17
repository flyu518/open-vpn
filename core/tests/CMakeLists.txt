add_executable(openvpn_contract_tests
    contract_tests.cpp
)

target_link_libraries(openvpn_contract_tests PRIVATE openvpn_core)

add_test(NAME openvpn_contract_tests COMMAND openvpn_contract_tests)

add_executable(openvpn_route_policy_tests
    route_policy_tests.cpp
)

target_link_libraries(openvpn_route_policy_tests PRIVATE openvpn_core)

add_test(NAME openvpn_route_policy_tests COMMAND openvpn_route_policy_tests)

add_executable(openvpn_session_manager_tests
    session_manager_tests.cpp
)

target_link_libraries(openvpn_session_manager_tests PRIVATE openvpn_core)

add_test(NAME openvpn_session_manager_tests COMMAND openvpn_session_manager_tests)

add_executable(openvpn_platform_adapter_contract_tests
    platform_adapter_contract_tests.cpp
)

target_include_directories(openvpn_platform_adapter_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(openvpn_platform_adapter_contract_tests PRIVATE openvpn_core)

add_test(NAME openvpn_platform_adapter_contract_tests COMMAND openvpn_platform_adapter_contract_tests)

if(EXISTS "${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp")
    add_executable(openvpn_openvpn3_engine_tests
        openvpn3_engine_tests.cpp
    )

    target_link_libraries(openvpn_openvpn3_engine_tests PRIVATE openvpn_core)
    if(WIN32)
        target_link_libraries(openvpn_openvpn3_engine_tests PRIVATE
            ws2_32
            fwpuclnt
            wininet
            setupapi
            rpcrt4
            wtsapi32
            iphlpapi
        )
    endif()

    add_test(NAME openvpn_openvpn3_engine_tests COMMAND openvpn_openvpn3_engine_tests)
endif()

if(APPLE)
    add_executable(openvpn_macos_route_lifecycle_tests
        ../../platform/macos/tests/route_lifecycle_tests.mm
    )
    target_include_directories(openvpn_macos_route_lifecycle_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(openvpn_macos_route_lifecycle_tests PRIVATE openvpn_macos_adapter)
    add_test(NAME openvpn_macos_route_lifecycle_tests COMMAND openvpn_macos_route_lifecycle_tests)
endif()

add_executable(openvpn_windows_route_lifecycle_tests
    ../../platform/windows/tests/route_lifecycle_tests.cpp
)
target_include_directories(openvpn_windows_route_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(openvpn_windows_route_lifecycle_tests PRIVATE openvpn_windows_adapter)
add_test(NAME openvpn_windows_route_lifecycle_tests COMMAND openvpn_windows_route_lifecycle_tests)

add_executable(openvpn_security_recovery_tests
    security_recovery_tests.cpp
)
target_link_libraries(openvpn_security_recovery_tests PRIVATE openvpn_core)
add_test(NAME openvpn_security_recovery_tests COMMAND openvpn_security_recovery_tests)

add_executable(openvpn_credential_codec_tests
    credential_codec_tests.cpp
)
target_include_directories(openvpn_credential_codec_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
)
add_test(NAME openvpn_credential_codec_tests COMMAND openvpn_credential_codec_tests)

# 平台无关的控制器 fixture：Linux/macOS 均可验证 Windows 的桌面多会话策略，
# 不链接原生 VPN 引擎、系统凭据存储或平台网络 API。
add_executable(openvpn_daemon_session_limit_tests
    daemon_session_limit_tests.cpp
    daemon_test_backend.cpp
    ../tools/daemon_session_controller.cpp
    ../src/vpn_route_policy.cpp
    ../src/vpn_security.cpp)
target_include_directories(openvpn_daemon_session_limit_tests PRIVATE
    ../include ../tools ../.. ${JSONCPP_INCLUDE_DIR})
target_link_libraries(openvpn_daemon_session_limit_tests PRIVATE
    ${JSONCPP_LIBRARY} Threads::Threads)
if(WIN32)
    target_link_libraries(openvpn_daemon_session_limit_tests PRIVATE ws2_32)
endif()
add_test(NAME openvpn_daemon_session_limit_tests COMMAND openvpn_daemon_session_limit_tests)
set_tests_properties(openvpn_daemon_session_limit_tests PROPERTIES TIMEOUT 15)
if(WIN32 AND JSONCPP_LIBRARY)
    get_filename_component(VPN_JSONCPP_LIB_DIR "${JSONCPP_LIBRARY}" DIRECTORY)
    get_filename_component(VPN_VCPKG_ROOT "${VPN_JSONCPP_LIB_DIR}" DIRECTORY)
    set(VPN_WINDOWS_TEST_ENVIRONMENT "PATH=${VPN_VCPKG_ROOT}/bin;$ENV{PATH}")
    set_tests_properties(
        openvpn_daemon_session_limit_tests
        openvpn_windows_route_lifecycle_tests
        PROPERTIES ENVIRONMENT "${VPN_WINDOWS_TEST_ENVIRONMENT}")
    if(TARGET openvpn_openvpn3_engine_tests)
        set_tests_properties(openvpn_openvpn3_engine_tests PROPERTIES
            ENVIRONMENT "${VPN_WINDOWS_TEST_ENVIRONMENT}")
    endif()
endif()

add_test(NAME openvpn_windows_agent_patch_tests
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../..
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/windows-agent-patch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/windows/tests/windows_agent_patch_tests.cmake)
set_tests_properties(openvpn_windows_agent_patch_tests PROPERTIES TIMEOUT 15)

add_test(NAME openvpn_windows_client_patch_tests
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../..
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/windows-client-patch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/windows/tests/windows_client_patch_tests.cmake)
set_tests_properties(openvpn_windows_client_patch_tests PROPERTIES TIMEOUT 15)

# The existing tests use assert(), including calls with side effects. Keep those
# checks enabled in the requested Release build as well.
get_property(VPN_TEST_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(TEST_TARGET IN LISTS VPN_TEST_TARGETS)
    if(MSVC)
        target_compile_options(${TEST_TARGET} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TEST_TARGET} PRIVATE -UNDEBUG)
    endif()
endforeach()

if(APPLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(openvpn_engine_lifecycle_tests
        engine_lifecycle_tests.cpp
        ../src/openvpn3_engine.cpp
        ../src/engine_worker.cpp)
    target_include_directories(openvpn_engine_lifecycle_tests PRIVATE
        engine_fixture ../include ${OPENVPN3_SOURCE_DIR} ${FMT_INCLUDE_DIR})
    target_link_libraries(openvpn_engine_lifecycle_tests PRIVATE Threads::Threads ${FMT_LIBRARY})
    foreach(MODE early-stop success status-error throw)
        add_test(NAME openvpn_engine_${MODE}_test COMMAND openvpn_engine_lifecycle_tests ${MODE})
        set_tests_properties(openvpn_engine_${MODE}_test PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(openvpn_daemon_fixture
        daemon_fixture_main.cpp
        daemon_test_backend.cpp
        ../tools/daemon_session_controller.cpp
        ../src/vpn_route_policy.cpp
        ../src/vpn_security.cpp
    )
    target_include_directories(openvpn_daemon_fixture PRIVATE
        ../include ../.. ${JSONCPP_INCLUDE_DIR})
    target_link_libraries(openvpn_daemon_fixture PRIVATE ${JSONCPP_LIBRARY} Threads::Threads)
    foreach(SUITE production fixture)
        add_test(NAME openvpn_daemon_${SUITE}_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/daemon_process_tests.py
                --daemon $<TARGET_FILE:openvpn_daemon>
                --fixture-daemon $<TARGET_FILE:openvpn_daemon_fixture>
                --suite ${SUITE})
        set_tests_properties(openvpn_daemon_${SUITE}_tests PROPERTIES TIMEOUT 120)
    endforeach()
endif()
