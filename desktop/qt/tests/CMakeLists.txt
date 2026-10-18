add_test(NAME openvpn_project_config_tests COMMAND "${CMAKE_COMMAND}"
    "-DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/project-config-tests"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/config/test_project_config.cmake")
if(WIN32)
    add_test(NAME openvpn_windows_version_metadata_tests
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/config/test_windows_version_metadata.ps1"
            -Executable "$<TARGET_FILE:openvpn_qt>")
    add_test(NAME openvpn_packaging_config_tests
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/config/test_packaging_config.ps1")
endif()
if(APPLE)
    add_test(NAME openvpn_macos_packaging_config_tests
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/config/test_packaging_config.sh" "${CMAKE_COMMAND}")
endif()

add_executable(openvpn_qt_model_tests
    test_session_list_model.cpp
    ../src/session_list_model.cpp
    ../src/session_list_model.h
    ../src/profile_store.cpp
    ../src/profile_store.h
)

target_include_directories(openvpn_qt_model_tests PRIVATE ../src)
target_link_libraries(openvpn_qt_model_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME openvpn_qt_model_tests COMMAND openvpn_qt_model_tests)

add_executable(openvpn_qt_engine_tests
    test_engine_client.cpp
    ../src/engine_client.cpp
    ../src/engine_client.h
)

target_include_directories(openvpn_qt_engine_tests PRIVATE ../src)
target_link_libraries(openvpn_qt_engine_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME openvpn_qt_engine_tests COMMAND openvpn_qt_engine_tests)

add_executable(openvpn_qt_controller_tests
    test_app_controller.cpp
    ../src/app_controller.cpp
    ../src/app_controller.h
    ../src/engine_client.cpp
    ../src/engine_client.h
    ../src/profile_store.cpp
    ../src/profile_store.h
    ../src/session_list_model.cpp
    ../src/session_list_model.h
)

target_include_directories(openvpn_qt_controller_tests PRIVATE ../src)
target_link_libraries(openvpn_qt_controller_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME openvpn_qt_controller_tests COMMAND openvpn_qt_controller_tests)

add_executable(openvpn_qt_qml_tests
    test_qml_interactions.cpp
    ../src/app_controller.cpp
    ../src/app_controller.h
    ../src/engine_client.cpp
    ../src/engine_client.h
    ../src/profile_store.cpp
    ../src/profile_store.h
    ../src/session_list_model.cpp
    ../src/session_list_model.h
)
qt_add_resources(openvpn_qt_qml_tests "openvpn_qml_test_assets"
    PREFIX "/"
    FILES
        ../assets/app-icon.png
)
target_compile_definitions(openvpn_qt_qml_tests PRIVATE
    OPENVPN_QT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    OPENVPN_QT_TESTING
)
target_include_directories(openvpn_qt_qml_tests PRIVATE ../src)
target_link_libraries(openvpn_qt_qml_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME openvpn_qt_qml_tests COMMAND openvpn_qt_qml_tests)
set_tests_properties(openvpn_qt_qml_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_executable(openvpn_qt_tray_tests
    test_tray_controller.cpp
    test_tray_controller.h
    ../src/tray_controller.cpp
    ../src/tray_controller.h
    ../src/app_controller.cpp
    ../src/app_controller.h
    ../src/engine_client.cpp
    ../src/engine_client.h
    ../src/profile_store.cpp
    ../src/profile_store.h
    ../src/session_list_model.cpp
    ../src/session_list_model.h
)
target_include_directories(openvpn_qt_tray_tests PRIVATE ../src)
qt_add_resources(openvpn_qt_tray_tests "openvpn_tray_test_assets"
    PREFIX "/openvpn"
    BASE "../assets"
    FILES ../assets/app-icon.png
)
target_link_libraries(openvpn_qt_tray_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME openvpn_qt_tray_tests COMMAND openvpn_qt_tray_tests)
set_tests_properties(openvpn_qt_tray_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
add_executable(openvpn_qt_traffic_tests test_traffic_history.cpp)
target_include_directories(openvpn_qt_traffic_tests PRIVATE ../src)
target_link_libraries(openvpn_qt_traffic_tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME openvpn_qt_traffic_tests COMMAND openvpn_qt_traffic_tests)

qt_add_resources(openvpn_qt_qml_tests "openvpn_blue_icon"
    PREFIX "/openvpn" BASE "../assets" FILES ../assets/app-icon.png)
qt_add_resources(openvpn_qt_tray_tests "openvpn_blue_tray_icon"
    PREFIX "/openvpn" BASE "../assets" FILES ../assets/app-icon.png)

if(WIN32)
    set(OPENVPN_QT_TEST_TEMP "${CMAKE_CURRENT_BINARY_DIR}/qt-test-temp")
    # CTest does not inherit the Qt environment used while configuring the
    # project. Keep the Windows test executables self-contained by pointing
    # them at the same Qt bin/plugins directories as the configured Qt6
    # package. Without the bin path Windows reports 0xc0000135 before a test
    # can even start because Qt6Core.dll is not on PATH.
    get_filename_component(OPENVPN_QT_TEST_QT_CMAKE_DIR "${Qt6_DIR}" ABSOLUTE)
    get_filename_component(OPENVPN_QT_TEST_QT_LIB_DIR "${OPENVPN_QT_TEST_QT_CMAKE_DIR}/../.." ABSOLUTE)
    get_filename_component(OPENVPN_QT_TEST_QT_ROOT "${OPENVPN_QT_TEST_QT_LIB_DIR}/.." ABSOLUTE)
    set(OPENVPN_QT_TEST_BIN "${OPENVPN_QT_TEST_QT_ROOT}/bin")
    set(OPENVPN_QT_TEST_PLUGIN_PATH "${OPENVPN_QT_TEST_QT_ROOT}/plugins")
    file(MAKE_DIRECTORY "${OPENVPN_QT_TEST_TEMP}")
    set_tests_properties(
        openvpn_qt_model_tests
        openvpn_qt_engine_tests
        openvpn_qt_controller_tests
        openvpn_qt_traffic_tests
        PROPERTIES
            ENVIRONMENT "TEMP=${OPENVPN_QT_TEST_TEMP};TMP=${OPENVPN_QT_TEST_TEMP};QT_PLUGIN_PATH=${OPENVPN_QT_TEST_PLUGIN_PATH}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${OPENVPN_QT_TEST_BIN}"
    )
    set_tests_properties(
        openvpn_qt_qml_tests
        openvpn_qt_tray_tests
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TEMP=${OPENVPN_QT_TEST_TEMP};TMP=${OPENVPN_QT_TEST_TEMP};QT_PLUGIN_PATH=${OPENVPN_QT_TEST_PLUGIN_PATH}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${OPENVPN_QT_TEST_BIN}"
    )
endif()
