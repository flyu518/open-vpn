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
