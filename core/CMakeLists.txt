cmake_minimum_required(VERSION 3.13)

project(vpn_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vpn_core STATIC
    src/fake_engine.cpp
    src/vpn_engine.cpp
    src/vpn_session_manager.cpp
    src/vpn_route_policy.cpp
)

target_include_directories(vpn_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vpn_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(vpn_core PUBLIC ws2_32)
endif()

enable_testing()
add_subdirectory(tests)
