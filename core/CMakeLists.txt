cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ProjectConfig.cmake")
set(OPENVPN_GENERATED_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
openvpn_generate_config_header("${OPENVPN_GENERATED_CONFIG_DIR}")

project(OpenVPNCore LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

if(MSVC)
    add_compile_options(/utf-8)
endif()

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
    add_executable(openvpn_windows_ipv6_probe EXCLUDE_FROM_ALL
        tools/windows_ipv6_probe.cpp ../platform/windows/service/shared_ipv6_block.cpp)
    target_include_directories(openvpn_windows_ipv6_probe PRIVATE ../platform/windows/service)
    target_link_libraries(openvpn_windows_ipv6_probe PRIVATE iphlpapi ws2_32 advapi32 rpcrt4)
    add_executable(openvpn_windows_service
        ../platform/windows/service/main.cpp
    )
    target_link_libraries(openvpn_windows_service PRIVATE openvpn_windows_adapter)
    set_target_properties(openvpn_windows_service PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

    add_executable(openvpn_windows_daemon_launcher WIN32
        tools/windows_daemon_launcher.cpp
    )
    set_target_properties(openvpn_windows_daemon_launcher PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

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
target_include_directories(openvpn_core PUBLIC "${OPENVPN_GENERATED_CONFIG_DIR}")

set(OPENVPN3_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openvpn3"
    CACHE PATH "OpenVPN3 source directory")
set(XXHASH_ROOT_DIR "" CACHE PATH "xxHash source or install root")
set(OPENVPN_BREW_PREFIX "/usr/local" CACHE PATH "Homebrew prefix for native dependencies")
set(OPENVPN_BREW_OPT "${OPENVPN_BREW_PREFIX}/opt")

if(EXISTS "${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp")
    set(VPN_OPENVPN3_PATCHED_INCLUDE
        "${CMAKE_CURRENT_BINARY_DIR}/openvpn3_patched_include")
    if(WIN32)
        set(VPN_OPENVPN3_CMDAGENT_SOURCE
            "${OPENVPN3_SOURCE_DIR}/openvpn/client/win/cmdagent.hpp")
        set(VPN_OPENVPN3_CMDAGENT_GENERATED
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/win/cmdagent.hpp")
        set(VPN_OPENVPN3_CMDAGENT_DIRECTORY
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/win")
        set(VPN_OPENVPN3_CMDAGENT_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_cmdagent.cmake")
    else()
        set(VPN_OPENVPN3_CMDAGENT_SOURCE
            "${OPENVPN3_SOURCE_DIR}/openvpn/client/unix/cmdagent.hpp")
        set(VPN_OPENVPN3_CMDAGENT_GENERATED
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/unix/cmdagent.hpp")
        set(VPN_OPENVPN3_CMDAGENT_DIRECTORY
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/unix")
        set(VPN_OPENVPN3_CMDAGENT_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cmdagent.cmake")
    endif()
    set(VPN_OPENVPN3_CLIOPT_GENERATED
        "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client/cliopt.hpp")
    if(WIN32)
        set(VPN_OPENVPN3_CLIOPT_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_cliopt.cmake")
    else()
        set(VPN_OPENVPN3_CLIOPT_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/../platform/macos/helper/generate_cliopt.cmake")
    endif()
    add_custom_command(
        OUTPUT "${VPN_OPENVPN3_CMDAGENT_GENERATED}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VPN_OPENVPN3_CMDAGENT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT_SOURCE=${VPN_OPENVPN3_CMDAGENT_SOURCE}"
            "-DOUTPUT_SOURCE=${VPN_OPENVPN3_CMDAGENT_GENERATED}"
            -P ${VPN_OPENVPN3_CMDAGENT_SCRIPT}
        DEPENDS
            ${VPN_OPENVPN3_CMDAGENT_SOURCE}
            ${VPN_OPENVPN3_CMDAGENT_SCRIPT}
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${VPN_OPENVPN3_CLIOPT_GENERATED}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/client"
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/openvpn/client/cliopt.hpp"
            "-DOUTPUT_SOURCE=${VPN_OPENVPN3_CLIOPT_GENERATED}"
            -P ${VPN_OPENVPN3_CLIOPT_SCRIPT}
        DEPENDS
            ${OPENVPN3_SOURCE_DIR}/openvpn/client/cliopt.hpp
            ${VPN_OPENVPN3_CLIOPT_SCRIPT}
        VERBATIM
    )
    add_custom_target(openvpn_openvpn3_cmdagent_source
        DEPENDS
            "${VPN_OPENVPN3_CMDAGENT_GENERATED}"
            "${VPN_OPENVPN3_CLIOPT_GENERATED}")

    if(WIN32)
        set(VPN_WINDOWS_DNS_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_session_dns.cmake")
        set(VPN_WINDOWS_DNS_OUTPUTS
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/tun/win/session_nrpt.hpp"
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/openvpn/tun/win/client/tunsetup.hpp")
        add_custom_command(
            OUTPUT ${VPN_WINDOWS_DNS_OUTPUTS}
            COMMAND ${CMAKE_COMMAND}
                "-DOPENVPN3_SOURCE_DIR=${OPENVPN3_SOURCE_DIR}"
                "-DOUTPUT_DIRECTORY=${VPN_OPENVPN3_PATCHED_INCLUDE}"
                -P ${VPN_WINDOWS_DNS_SCRIPT}
            DEPENDS ${VPN_WINDOWS_DNS_SCRIPT}
                ${OPENVPN3_SOURCE_DIR}/openvpn/tun/win/nrpt.hpp
                ${OPENVPN3_SOURCE_DIR}/openvpn/tun/win/client/tunsetup.hpp
            VERBATIM)
        add_custom_target(openvpn_windows_session_dns_source
            DEPENDS ${VPN_WINDOWS_DNS_OUTPUTS})
        add_dependencies(openvpn_windows_service openvpn_windows_session_dns_source)
        add_dependencies(openvpn_openvpn3_cmdagent_source openvpn_windows_session_dns_source)
        set(VPN_OPENVPN3_OVPNCLI_GENERATED
            "${VPN_OPENVPN3_PATCHED_INCLUDE}/client/ovpncli.cpp")
        add_custom_command(
            OUTPUT "${VPN_OPENVPN3_OVPNCLI_GENERATED}"
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp"
                "-DOUTPUT_SOURCE=${VPN_OPENVPN3_OVPNCLI_GENERATED}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_ovpncli.cmake
            DEPENDS
                ${OPENVPN3_SOURCE_DIR}/client/ovpncli.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_ovpncli.cmake
            VERBATIM
        )
        add_custom_target(openvpn_windows_ovpncli_source
            DEPENDS "${VPN_OPENVPN3_OVPNCLI_GENERATED}")
        add_dependencies(openvpn_openvpn3_cmdagent_source
            openvpn_windows_ovpncli_source)
    endif()

    target_sources(openvpn_core PRIVATE
        src/openvpn3_engine.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
        ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
    )
    add_dependencies(openvpn_core openvpn_openvpn3_cmdagent_source)

    target_compile_definitions(openvpn_core PRIVATE
        ASIO_STANDALONE
        ASIO_DISABLE_LOCAL_SOCKETS
        USE_ASIO
        HAVE_LZ4
        USE_OPENSSL
        ENABLE_EXTERNAL_PKI
        HAVE_JSONCPP
        OPENVPN_COMMAND_AGENT
        OVPNAGENT_NAME=openvpn
        TAP_WIN_COMPONENT_ID=tap0901
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
    if(NOT EXISTS "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp")
        message(FATAL_ERROR
            "Windows 构建必须提供锁定版本的 OpenVPN3 Windows agent 源码："
            "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp")
    endif()
    if(EXISTS "${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp")
        set(VPN_WINDOWS_AGENT_GENERATED
            "${CMAKE_CURRENT_BINARY_DIR}/openvpn_windows_agent_generated.cpp")
        add_custom_command(
            OUTPUT "${VPN_WINDOWS_AGENT_GENERATED}"
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT_SOURCE=${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp"
                "-DOUTPUT_SOURCE=${VPN_WINDOWS_AGENT_GENERATED}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_multisession_agent.cmake
            DEPENDS
                ${OPENVPN3_SOURCE_DIR}/openvpn/ovpnagent/win/ovpnagent.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service/generate_multisession_agent.cmake
                ${CMAKE_CURRENT_SOURCE_DIR}/include/vpn_route_policy.h
            VERBATIM
        )
        add_custom_target(openvpn_windows_agent_source
            DEPENDS "${VPN_WINDOWS_AGENT_GENERATED}")
        add_dependencies(openvpn_windows_service openvpn_windows_agent_source)
        target_sources(openvpn_windows_service PRIVATE
            "${VPN_WINDOWS_AGENT_GENERATED}"
            ../platform/windows/service/wintun_adapter_lease.cpp
            ../platform/windows/service/shared_ipv6_block.cpp
        )
        target_sources(openvpn_windows_service PRIVATE
            ${OPENVPN3_SOURCE_DIR}/openvpn/crypto/data_epoch.cpp
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_helper.c
            ${OPENVPN3_SOURCE_DIR}/openvpn/openssl/xkey/xkey_provider.c
        )
        target_compile_definitions(openvpn_windows_service PRIVATE
            ASIO_STANDALONE
            ASIO_DISABLE_LOCAL_SOCKETS
            USE_ASIO
            HAVE_LZ4
            USE_OPENSSL
            ENABLE_EXTERNAL_PKI
            HAVE_JSONCPP
            OPENVPN_COMMAND_AGENT
            OVPNAGENT_NAME=openvpn
            TAP_WIN_COMPONENT_ID=tap0901
            OPENVPN_CORE_API_VISIBILITY_HIDDEN
            OPENVPN_WINDOWS_AGENT_GENERATED
            OPENVPN_WINDOWS_AGENT_SOURCE
        )
        target_include_directories(openvpn_windows_service PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/service
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
            fwpuclnt
            wininet
            setupapi
            rpcrt4
            wtsapi32
        )

        # Explicit standalone driver check; never run automatically by CTest.
        add_executable(openvpn_windows_wintun_probe EXCLUDE_FROM_ALL
            tools/windows_wintun_probe.cpp
            ../platform/windows/service/wintun_adapter_lease.cpp)
        target_include_directories(openvpn_windows_wintun_probe PRIVATE
            $<TARGET_PROPERTY:openvpn_windows_service,INCLUDE_DIRECTORIES>)
        target_compile_definitions(openvpn_windows_wintun_probe PRIVATE
            $<TARGET_PROPERTY:openvpn_windows_service,COMPILE_DEFINITIONS>)
        target_link_libraries(openvpn_windows_wintun_probe PRIVATE
            $<TARGET_PROPERTY:openvpn_windows_service,LINK_LIBRARIES>)
    endif()

    add_library(openvpn_windows_secure_store STATIC
        ../platform/windows/secure_store/credential_store.cpp
    )
    target_include_directories(openvpn_windows_secure_store PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/common
    )
    target_compile_definitions(openvpn_windows_secure_store PRIVATE
        NOMINMAX
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

    add_executable(openvpn_windows_connection_probe EXCLUDE_FROM_ALL
        tools/windows_connection_probe.cpp
        tools/daemon_session_controller.cpp)
    target_include_directories(openvpn_windows_connection_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${JSONCPP_INCLUDE_DIR})
    target_link_libraries(openvpn_windows_connection_probe PRIVATE
        openvpn_core openvpn_windows_secure_store ws2_32 advapi32 iphlpapi credui)

    # The Windows binaries link against the dynamic vcpkg/OpenSSL runtime.
    # Keep the build output directly runnable when launched from Explorer or
    # the desktop UI without a developer-shell PATH.
    function(vpn_copy_windows_runtime_dlls target)
        set(runtime_dlls)
        if(DEFINED OPENSSL_ROOT_DIR)
            list(APPEND runtime_dlls
                "${OPENSSL_ROOT_DIR}/bin/libcrypto-3-x64.dll"
                "${OPENSSL_ROOT_DIR}/bin/libssl-3-x64.dll")
        endif()
        foreach(library_variable IN ITEMS LZ4_LIBRARY JSONCPP_LIBRARY FMT_LIBRARY)
            if(DEFINED ${library_variable} AND EXISTS "${${library_variable}}")
                get_filename_component(library_dir "${${library_variable}}" DIRECTORY)
                get_filename_component(runtime_root "${library_dir}" DIRECTORY)
                if(library_variable STREQUAL "LZ4_LIBRARY")
                    list(APPEND runtime_dlls "${runtime_root}/bin/lz4.dll")
                elseif(library_variable STREQUAL "JSONCPP_LIBRARY")
                    list(APPEND runtime_dlls "${runtime_root}/bin/jsoncpp.dll")
                elseif(library_variable STREQUAL "FMT_LIBRARY")
                    list(APPEND runtime_dlls "${runtime_root}/bin/fmt.dll")
                endif()
            endif()
        endforeach()
        list(REMOVE_DUPLICATES runtime_dlls)
        foreach(runtime_dll IN LISTS runtime_dlls)
            if(EXISTS "${runtime_dll}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_dll}"
                        "$<TARGET_FILE_DIR:${target}>")
            endif()
        endforeach()
    endfunction()

    vpn_copy_windows_runtime_dlls(openvpn_windows_daemon)
    if(TARGET openvpn_windows_service)
        vpn_copy_windows_runtime_dlls(openvpn_windows_service)
    endif()
endif()

target_include_directories(openvpn_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(openvpn_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(openvpn_core PUBLIC
        ws2_32
        fwpuclnt
        wininet
        setupapi
        rpcrt4
        wtsapi32
    )
endif()

enable_testing()
add_subdirectory(tests)
