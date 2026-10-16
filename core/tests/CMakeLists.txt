add_executable(vpn_contract_tests
    contract_tests.cpp
)

target_link_libraries(vpn_contract_tests PRIVATE vpn_core)

add_test(NAME vpn_contract_tests COMMAND vpn_contract_tests)

add_executable(vpn_route_policy_tests
    route_policy_tests.cpp
)

target_link_libraries(vpn_route_policy_tests PRIVATE vpn_core)

add_test(NAME vpn_route_policy_tests COMMAND vpn_route_policy_tests)

add_executable(vpn_session_manager_tests
    session_manager_tests.cpp
)

target_link_libraries(vpn_session_manager_tests PRIVATE vpn_core)

add_test(NAME vpn_session_manager_tests COMMAND vpn_session_manager_tests)

add_executable(vpn_platform_adapter_contract_tests
    platform_adapter_contract_tests.cpp
)

target_include_directories(vpn_platform_adapter_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(vpn_platform_adapter_contract_tests PRIVATE vpn_core)

add_test(NAME vpn_platform_adapter_contract_tests COMMAND vpn_platform_adapter_contract_tests)

if(EXISTS "${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp")
    add_executable(vpn_openvpn3_engine_tests
        openvpn3_engine_tests.cpp
    )

    target_link_libraries(vpn_openvpn3_engine_tests PRIVATE vpn_core)

    add_test(NAME vpn_openvpn3_engine_tests COMMAND vpn_openvpn3_engine_tests)
endif()

if(APPLE)
    add_executable(vpn_macos_route_lifecycle_tests
        ../../platform/macos/tests/route_lifecycle_tests.mm
    )
    target_include_directories(vpn_macos_route_lifecycle_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(vpn_macos_route_lifecycle_tests PRIVATE vpn_macos_adapter)
    add_test(NAME vpn_macos_route_lifecycle_tests COMMAND vpn_macos_route_lifecycle_tests)
endif()

add_executable(vpn_windows_route_lifecycle_tests
    ../../platform/windows/tests/route_lifecycle_tests.cpp
)
target_include_directories(vpn_windows_route_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(vpn_windows_route_lifecycle_tests PRIVATE vpn_windows_adapter)
add_test(NAME vpn_windows_route_lifecycle_tests COMMAND vpn_windows_route_lifecycle_tests)

add_executable(vpn_security_recovery_tests
    security_recovery_tests.cpp
)
target_link_libraries(vpn_security_recovery_tests PRIVATE vpn_core)
add_test(NAME vpn_security_recovery_tests COMMAND vpn_security_recovery_tests)

add_executable(vpn_credential_codec_tests
    credential_codec_tests.cpp
)
target_include_directories(vpn_credential_codec_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/common
)
add_test(NAME vpn_credential_codec_tests COMMAND vpn_credential_codec_tests)
