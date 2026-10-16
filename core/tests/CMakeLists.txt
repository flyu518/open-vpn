add_executable(vpn_contract_tests
    contract_tests.cpp
)

target_link_libraries(vpn_contract_tests PRIVATE vpn_core)

add_test(NAME vpn_contract_tests COMMAND vpn_contract_tests)
