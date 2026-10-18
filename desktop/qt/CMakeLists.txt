cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectConfig.cmake")
project(OpenVPNQt VERSION "${OPENVPN_PROJECT_VERSION}" LANGUAGES CXX)

if(DEFINED OPENVPN_APP_VERSION AND NOT OPENVPN_APP_VERSION STREQUAL OPENVPN_PROJECT_VERSION)
    message(FATAL_ERROR "OPENVPN_APP_VERSION differs from project-config.json; clear the old cache with -UOPENVPN_APP_VERSION")
endif()
set(OPENVPN_APP_VERSION "${OPENVPN_PROJECT_VERSION}" CACHE STRING "OpenVPN 应用版本")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Test
    Widgets
)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(openvpn_qt
    src/app_controller.cpp
    src/app_controller.h
    src/engine_client.cpp
    src/engine_client.h
    src/profile_store.cpp
    src/profile_store.h
    src/session_list_model.cpp
    src/session_list_model.h
    src/traffic_history.h
    src/tray_controller.cpp
    src/tray_controller.h
    src/main.cpp
)
if(APPLE)
    target_sources(openvpn_qt PRIVATE assets/OpenVPN.icns)
    set_source_files_properties(assets/OpenVPN.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(openvpn_qt PROPERTIES MACOSX_BUNDLE_ICON_FILE OpenVPN.icns)
    set_target_properties(openvpn_qt PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist.in")
endif()

qt_add_qml_module(openvpn_qt
    URI OpenVPN
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/TrafficChart.qml
)

if(WIN32 AND CMAKE_GENERATOR MATCHES "^Visual Studio")
    # The application owns moc --collect-json and the registration command.
    # The separate helper target duplicates that command under Visual Studio,
    # so it must wait for the application just as Qt does for its Xcode path.
    add_dependencies(openvpn_qt_qmltyperegistration openvpn_qt)
endif()


qt_add_resources(openvpn_qt "openvpn_assets"
    PREFIX "/"
    FILES
        assets/app-icon.png
)
qt_add_resources(openvpn_qt "openvpn_tray_asset"
    PREFIX "/openvpn"
    BASE "assets"
    FILES assets/app-icon.png
)
if(WIN32)
    configure_file(assets/OpenVPNVersion.rc.in "${CMAKE_CURRENT_BINARY_DIR}/OpenVPNVersion.rc" @ONLY)
    target_sources(openvpn_qt PRIVATE assets/OpenVPN.rc "${CMAKE_CURRENT_BINARY_DIR}/OpenVPNVersion.rc")
    target_include_directories(openvpn_qt PRIVATE assets)
    # The desktop UI must be linked as a Windows GUI application. Without
    # this property, Windows creates a console window before Qt starts.
    set_target_properties(openvpn_qt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(openvpn_qt PRIVATE src)
target_compile_definitions(openvpn_qt PRIVATE
    OPENVPN_APP_VERSION="${OPENVPN_APP_VERSION}"
)
target_link_libraries(openvpn_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

set_target_properties(openvpn_qt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.openvpn.desktop"
    MACOSX_BUNDLE_BUNDLE_NAME "OpenVPN"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${OPENVPN_APP_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${OPENVPN_APP_VERSION}"
)

include(GNUInstallDirs)
install(TARGETS openvpn_qt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
option(OPENVPN_QT_BUILD_TESTS "构建 Qt 桌面单元测试" ON)
if(BUILD_TESTING AND OPENVPN_QT_BUILD_TESTS)
    add_subdirectory(tests)
    if(WIN32)
        add_test(
            NAME openvpn_qt_gui_subsystem_test
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/windows/tests/test_gui_subsystem.ps1"
                -Executable "$<TARGET_FILE:openvpn_qt>"
        )
        add_test(
            NAME openvpn_qt_tray_recovery_stability_test
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/windows/tests/test_tray_recovery_stability.ps1"
        )
        add_test(
            NAME openvpn_qt_daemon_bootstrap_source_test
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/windows/tests/test_daemon_bootstrap_source.ps1"
        )
    endif()
endif()
