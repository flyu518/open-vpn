cmake_minimum_required(VERSION 3.21)

project(OpenVPNQt VERSION 0.2.13 LANGUAGES CXX)

set(OPENVPN_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "OpenVPN 应用版本")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS
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


qt_add_resources(openvpn_qt "openvpn_assets"
    PREFIX "/"
    FILES
        assets/app-icon.png
)
qt_add_resources(openvpn_qt "openvpn_tray_asset"
    PREFIX "/openvpn"
    BASE "assets"
    FILES assets/tray.svg assets/tray-mask.svg assets/app-icon.png
)
if(WIN32)
    target_sources(openvpn_qt PRIVATE assets/OpenVPN.rc)
    target_include_directories(openvpn_qt PRIVATE assets)
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
endif()
