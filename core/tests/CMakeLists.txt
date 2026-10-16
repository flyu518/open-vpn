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
