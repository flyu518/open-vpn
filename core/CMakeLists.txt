cmake_minimum_required(VERSION 3.13)

project(vpn_core LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vpn_windows_adapter STATIC
    ../platform/windows/adapter/adapter.cpp
)
target_include_directories(vpn_windows_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vpn_windows_adapter PUBLIC vpn_core)
set_target_properties(vpn_windows_adapter PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(vpn_windows_service
        ../platform/windows/service/main.cpp
    )
    target_link_libraries(vpn_windows_service PRIVATE vpn_windows_adapter)
    set_target_properties(vpn_windows_service PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()

add_library(vpn_core STATIC
    src/fake_engine.cpp
    src/engine_worker.cpp
    src/vpn_engine.cpp
    src/vpn_session_manager.cpp
    src/vpn_route_policy.cpp
)

set(OPENVPN3_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openvpn3"
    CACHE PATH "OpenVPN3 source directory")
set(XXHASH_ROOT_DIR "" CACHE PATH "xxHash source or install root")

if(EXISTS "${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp")
    target_sources(vpn_core PRIVATE
        src/openvpn3_engine.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
    )

    target_compile_definitions(vpn_core PRIVATE
        ASIO_STANDALONE
        USE_ASIO
        HAVE_LZ4
        USE_OPENSSL
        ENABLE_EXTERNAL_PKI
        HAVE_JSONCPP
        OPENVPN_COMMAND_AGENT
        OVPNAGENT_NAME=vpn
        OPENVPN_CORE_API_VISIBILITY_HIDDEN
    )

    target_include_directories(vpn_core PRIVATE
        ${OPENVPN3_SOURCE_DIR}
        ${OPENVPN3_SOURCE_DIR}/client
        ${XXHASH_ROOT_DIR}
        /usr/local/opt/asio/include
        /usr/local/opt/fmt/include
        /usr/local/opt/jsoncpp/include
        /usr/local/opt/lz4/include
        /usr/local/opt/openssl/include
    )

    find_path(XXHASH_INCLUDE_DIR
        NAMES xxhash.h
        HINTS
            ${XXHASH_ROOT_DIR}
            /usr/local/opt/xxhash/include
    )
    if(NOT XXHASH_INCLUDE_DIR)
        message(FATAL_ERROR "找不到 xxhash.h，请设置 XXHASH_ROOT_DIR")
    endif()
    target_include_directories(vpn_core PRIVATE ${XXHASH_INCLUDE_DIR})

    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    find_library(LZ4_LIBRARY NAMES lz4 PATHS /usr/local/opt/lz4/lib REQUIRED)
    find_library(FMT_LIBRARY NAMES fmt PATHS /usr/local/opt/fmt/lib REQUIRED)
    find_library(JSONCPP_LIBRARY NAMES jsoncpp PATHS /usr/local/opt/jsoncpp/lib REQUIRED)

    target_link_libraries(vpn_core PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        ${LZ4_LIBRARY}
        ${FMT_LIBRARY}
        ${JSONCPP_LIBRARY}
        Threads::Threads
    )

    if(APPLE)
        find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
        find_library(IO_KIT IOKit REQUIRED)
        find_library(CORE_SERVICES CoreServices REQUIRED)
        find_library(SYSTEM_CONFIGURATION SystemConfiguration REQUIRED)
        target_link_libraries(vpn_core PRIVATE
            ${CORE_FOUNDATION}
            ${IO_KIT}
            ${CORE_SERVICES}
            ${SYSTEM_CONFIGURATION}
        )
    endif()
endif()

if(APPLE)
    add_library(vpn_macos_adapter STATIC
        ../platform/macos/adapter/adapter.mm
    )
    target_include_directories(vpn_macos_adapter
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(vpn_macos_adapter PUBLIC vpn_core)

    if(EXISTS "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/mac/ovpnagent.cpp")
        add_executable(vpn_macos_agent
            ../platform/macos/helper/main.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
        )
        target_compile_definitions(vpn_macos_agent PRIVATE
            ASIO_STANDALONE
            USE_ASIO
            HAVE_LZ4
            USE_OPENSSL
            ENABLE_EXTERNAL_PKI
            HAVE_JSONCPP
            OPENVPN_COMMAND_AGENT
            OVPNAGENT_NAME=vpn
            OPENVPN_CORE_API_VISIBILITY_HIDDEN
        )
        target_include_directories(vpn_macos_agent PRIVATE
            ${OPENVPN3_SOURCE_DIR}
            ${OPENVPN3_SOURCE_DIR}/client
            ${XXHASH_ROOT_DIR}
            ${XXHASH_INCLUDE_DIR}
            /usr/local/opt/asio/include
            /usr/local/opt/fmt/include
            /usr/local/opt/jsoncpp/include
            /usr/local/opt/lz4/include
            /usr/local/opt/openssl/include
        )
        target_link_libraries(vpn_macos_agent PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
            ${LZ4_LIBRARY}
            ${FMT_LIBRARY}
            ${JSONCPP_LIBRARY}
            Threads::Threads
            ${CORE_FOUNDATION}
            ${IO_KIT}
            ${CORE_SERVICES}
            ${SYSTEM_CONFIGURATION}
        )
        target_compile_options(vpn_macos_agent PRIVATE -Wno-deprecated-declarations)
    endif()
endif()

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
