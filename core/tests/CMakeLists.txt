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
