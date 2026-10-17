cmake_minimum_required(VERSION 3.13)

project(OpenVPNCore LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(openvpn_windows_adapter STATIC
    ../platform/windows/adapter/adapter.cpp
)
target_include_directories(openvpn_windows_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(openvpn_windows_adapter PUBLIC openvpn_core)
set_target_properties(openvpn_windows_adapter PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(openvpn_windows_service
        ../platform/windows/service/main.cpp
    )
    target_link_libraries(openvpn_windows_service PRIVATE openvpn_windows_adapter)
    set_target_properties(openvpn_windows_service PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()

add_library(openvpn_core STATIC
    src/fake_engine.cpp
    src/engine_worker.cpp
    src/vpn_engine.cpp
    src/openvpn3_engine.cpp
    src/vpn_session_manager.cpp
    src/vpn_route_policy.cpp
    src/vpn_security.cpp
    src/vpn_recovery.cpp
)

set(OPENVPN3_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openvpn3"
    CACHE PATH "OpenVPN3 source directory")
set(XXHASH_ROOT_DIR "" CACHE PATH "xxHash source or install root")
set(OPENVPN_BREW_PREFIX "/usr/local" CACHE PATH "Homebrew prefix for native dependencies")
set(OPENVPN_BREW_OPT "${OPENVPN_BREW_PREFIX}/opt")

if(EXISTS "${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp")
    set(VPN_OPENVPN3_PATCHED_INCLUDE
        "${CMAKE_CURRENT_BINARY_DIR}/openvpn3_patched_include")
    set(VPN_OPENVPN3_CMDAGENT_GENERATED
        "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/unix/cmdagent.hpp")
    set(VPN_OPENVPN3_CLIOPT_GENERATED
        "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/cliopt.hpp")
    add_custom_command(
        OUTPUT "${VPN_OPENVPN3_CMDAGENT_GENERATED}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/unix"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/openvpn/client/unix/cmdagent.hpp
            -DOUTPUT_SOURCE=${VPN_OPENVPN3_CMDAGENT_GENERATED}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cmdagent.cmake
        DEPENDS
            ${OPENVPN3_SOURCE_DIR}/openvpn/client/unix/cmdagent.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cmdagent.cmake
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${VPN_OPENVPN3_CLIOPT_GENERATED}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/openvpn/client/cliopt.hpp
            -DOUTPUT_SOURCE=${VPN_OPENVPN3_CLIOPT_GENERATED}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cliopt.cmake
        DEPENDS
            ${OPENVPN3_SOURCE_DIR}/openvpn/client/cliopt.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cliopt.cmake
        VERBATIM
    )
    add_custom_target(openvpn_openvpn3_cmdagent_source
        DEPENDS
            "${VPN_OPENVPN3_CMDAGENT_GENERATED}"
            "${VPN_OPENVPN3_CLIOPT_GENERATED}")

    target_sources(openvpn_core PRIVATE
        src/openvpn3_engine.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
    )
    add_dependencies(openvpn_core openvpn_openvpn3_cmdagent_source)

    target_compile_definitions(openvpn_core PRIVATE
        ASIO_STANDALONE
        USE_ASIO
        HAVE_LZ4
        USE_OPENSSL
        ENABLE_EXTERNAL_PKI
        HAVE_JSONCPP
        OPENVPN_COMMAND_AGENT
        OVPNAGENT_NAME=openvpn
        OPENVPN_CORE_API_VISIBILITY_HIDDEN
    )

    find_path(ASIO_INCLUDE_DIR NAMES asio.hpp
        HINTS "${OPENVPN_BREW_OPT}/asio/include")
    find_path(FMT_INCLUDE_DIR NAMES fmt/format.h
        HINTS "${OPENVPN_BREW_OPT}/fmt/include")
    find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h
        HINTS "${OPENVPN_BREW_OPT}/jsoncpp/include")
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h
        HINTS "${OPENVPN_BREW_OPT}/lz4/include")
    find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h
        HINTS "${OPENVPN_BREW_OPT}/openssl/include")
    foreach(REQUIRED_INCLUDE ASIO_INCLUDE_DIR FMT_INCLUDE_DIR JSONCPP_INCLUDE_DIR LZ4_INCLUDE_DIR OPENSSL_INCLUDE_DIR)
        if(NOT ${REQUIRED_INCLUDE})
            message(FATAL_ERROR "找不到 OpenVPN3 依赖头文件：${REQUIRED_INCLUDE}")
        endif()
    endforeach()

    target_include_directories(openvpn_core PRIVATE
        ${VPN_OPENVPN3_PATCHED_INCLUDE}
        ${OPENVPN3_SOURCE_DIR}
        ${OPENVPN3_SOURCE_DIR}/client
        ${XXHASH_ROOT_DIR}
        ${ASIO_INCLUDE_DIR}
        ${FMT_INCLUDE_DIR}
        ${JSONCPP_INCLUDE_DIR}
        ${LZ4_INCLUDE_DIR}
        ${OPENSSL_INCLUDE_DIR}
    )

    find_path(XXHASH_INCLUDE_DIR
        NAMES xxhash.h
        HINTS
            ${XXHASH_ROOT_DIR}
            "${OPENVPN_BREW_OPT}/xxhash/include"
    )
    if(NOT XXHASH_INCLUDE_DIR)
        message(FATAL_ERROR "找不到 xxhash.h，请设置 XXHASH_ROOT_DIR")
    endif()
    target_include_directories(openvpn_core PRIVATE ${XXHASH_INCLUDE_DIR})

    find_package(OpenSSL REQUIRED)
    find_library(LZ4_LIBRARY NAMES lz4 HINTS "${OPENVPN_BREW_OPT}/lz4/lib")
    find_library(FMT_LIBRARY NAMES fmt HINTS "${OPENVPN_BREW_OPT}/fmt/lib")
    find_library(JSONCPP_LIBRARY NAMES jsoncpp HINTS "${OPENVPN_BREW_OPT}/jsoncpp/lib")
    foreach(REQUIRED_LIBRARY LZ4_LIBRARY FMT_LIBRARY JSONCPP_LIBRARY)
        if(NOT ${REQUIRED_LIBRARY})
            message(FATAL_ERROR "找不到 OpenVPN3 依赖库：${REQUIRED_LIBRARY}")
        endif()
    endforeach()

    target_link_libraries(openvpn_core PUBLIC
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
        target_link_libraries(openvpn_core PRIVATE
            ${CORE_FOUNDATION}
            ${IO_KIT}
            ${CORE_SERVICES}
            ${SYSTEM_CONFIGURATION}
        )
    endif()
endif()

if(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(SECURITY Security REQUIRED)
    add_library(openvpn_macos_secure_store STATIC
        ../platform/macos/secure_store/keychain_store.mm
    )
    target_include_directories(openvpn_macos_secure_store PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
    )
    target_link_libraries(openvpn_macos_secure_store PUBLIC
        ${CORE_FOUNDATION}
        ${SECURITY}
    )
    set_target_properties(openvpn_macos_secure_store PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_library(openvpn_macos_adapter STATIC
        ../platform/macos/adapter/adapter.mm
    )
    target_include_directories(openvpn_macos_adapter
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(openvpn_macos_adapter PUBLIC openvpn_core)

    if(EXISTS "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/mac/ovpnagent.cpp")
        set(VPN_MACOS_AGENT_GENERATED
            "${CMAKE_CURRENT_BINARY_DIR}/openvpn_macos_agent_generated.cpp")
        add_custom_command(
            OUTPUT "${VPN_MACOS_AGENT_GENERATED}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/mac/ovpnagent.cpp
                -DOUTPUT_SOURCE=${VPN_MACOS_AGENT_GENERATED}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_multisession_agent.cmake
            DEPENDS
                ${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/mac/ovpnagent.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_multisession_agent.cmake
            VERBATIM
        )
        add_custom_target(openvpn_macos_agent_source DEPENDS "${VPN_MACOS_AGENT_GENERATED}")
        add_executable(openvpn_macos_agent
            ../platform/macos/helper/main.cpp
            src/vpn_route_policy.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
        )
        add_dependencies(openvpn_macos_agent openvpn_macos_agent_source)
        target_compile_definitions(openvpn_macos_agent PRIVATE
            ASIO_STANDALONE
            USE_ASIO
            HAVE_LZ4
            USE_OPENSSL
            ENABLE_EXTERNAL_PKI
            HAVE_JSONCPP
            OPENVPN_COMMAND_AGENT
            OVPNAGENT_NAME=openvpn
            OPENVPN_CORE_API_VISIBILITY_HIDDEN
        )
        target_include_directories(openvpn_macos_agent PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${OPENVPN3_SOURCE_DIR}
            ${OPENVPN3_SOURCE_DIR}/client
            ${XXHASH_ROOT_DIR}
            ${XXHASH_INCLUDE_DIR}
            "${OPENVPN_BREW_OPT}/asio/include"
            "${OPENVPN_BREW_OPT}/fmt/include"
            "${OPENVPN_BREW_OPT}/jsoncpp/include"
            "${OPENVPN_BREW_OPT}/lz4/include"
            "${OPENVPN_BREW_OPT}/openssl/include"
        )
        target_link_libraries(openvpn_macos_agent PRIVATE
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
        target_compile_options(openvpn_macos_agent PRIVATE -Wno-deprecated-declarations)
    endif()

    add_executable(openvpn_smoke_cli
        tools/smoke_main.cpp
    )
    target_include_directories(openvpn_smoke_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(openvpn_smoke_cli PRIVATE openvpn_core)
    set_target_properties(openvpn_smoke_cli PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(openvpn_daemon
        tools/daemon_main.cpp
        tools/daemon_session_controller.cpp
    )
    target_include_directories(openvpn_daemon PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        "${OPENVPN_BREW_OPT}/jsoncpp/include"
    )
    target_link_libraries(openvpn_daemon PRIVATE openvpn_core)
    target_link_libraries(openvpn_daemon PRIVATE openvpn_macos_secure_store)
    set_target_properties(openvpn_daemon PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endif()

if(WIN32)
    if(EXISTS "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp")
        target_sources(openvpn_windows_service PRIVATE
            ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
        )
        target_compile_definitions(openvpn_windows_service PRIVATE
            ASIO_STANDALONE
            USE_ASIO
            HAVE_LZ4
            USE_OPENSSL
            ENABLE_EXTERNAL_PKI
            HAVE_JSONCPP
            OPENVPN_COMMAND_AGENT
            OVPNAGENT_NAME=openvpn
            OPENVPN_CORE_API_VISIBILITY_HIDDEN
        )
        target_include_directories(openvpn_windows_service PRIVATE
            ${VPN_OPENVPN3_PATCHED_INCLUDE}
            ${OPENVPN3_SOURCE_DIR}
            ${OPENVPN3_SOURCE_DIR}/client
            ${XXHASH_INCLUDE_DIR}
            ${ASIO_INCLUDE_DIR}
            ${FMT_INCLUDE_DIR}
            ${JSONCPP_INCLUDE_DIR}
            ${LZ4_INCLUDE_DIR}
            ${OPENSSL_INCLUDE_DIR}
        )
        target_link_libraries(openvpn_windows_service PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
            ${LZ4_LIBRARY}
            ${FMT_LIBRARY}
            ${JSONCPP_LIBRARY}
            Threads::Threads
            ws2_32
            advapi32
            iphlpapi
        )
    endif()

    add_library(openvpn_windows_secure_store STATIC
        ../platform/windows/secure_store/credential_store.cpp
    )
    target_include_directories(openvpn_windows_secure_store PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
    )
    target_link_libraries(openvpn_windows_secure_store PUBLIC advapi32)
    set_target_properties(openvpn_windows_secure_store PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(openvpn_windows_daemon
        tools/windows_daemon_main.cpp
        tools/daemon_session_controller.cpp
    )
    target_include_directories(openvpn_windows_daemon PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${JSONCPP_INCLUDE_DIR}
    )
    target_link_libraries(openvpn_windows_daemon PRIVATE
        openvpn_core
        openvpn_windows_secure_store
        ws2_32
        advapi32
        iphlpapi
    )
    set_target_properties(openvpn_windows_daemon PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endif()

target_include_directories(openvpn_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(openvpn_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(openvpn_core PUBLIC ws2_32)
endif()

enable_testing()
add_subdirectory(tests)
