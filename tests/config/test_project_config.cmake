cmake_minimum_required(VERSION 3.21)

get_filename_component(repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(reader "${repo}/cmake/ProjectConfig.cmake")
if(NOT EXISTS "${reader}")
    message(FATAL_ERROR "Shared project configuration reader is not implemented")
endif()
if(NOT DEFINED TEST_OUTPUT_DIR)
    set(TEST_OUTPUT_DIR "${repo}/build/project-config-tests")
endif()
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

function(query file key expected_success expected_text)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DOPENVPN_PROJECT_CONFIG_FILE=${file}"
        "-DOPENVPN_CONFIG_KEY=${key}"
        ${ARGN} -P "${reader}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(expected_success)
        string(STRIP "${output}" output)
        if(NOT status EQUAL 0 OR NOT output STREQUAL expected_text)
            message(FATAL_ERROR "Query ${key}: expected '${expected_text}', got '${output}' (${status}): ${error}")
        endif()
    elseif(status EQUAL 0)
        message(FATAL_ERROR "Invalid configuration unexpectedly accepted: ${file} / ${key}")
    endif()
endfunction()

set(default_config "${repo}/project-config.json")
file(READ "${default_config}" default_json)
string(JSON default_version GET "${default_json}" version)
string(JSON default_timeout GET "${default_json}" connectionTimeoutSeconds)
string(JSON default_retry GET "${default_json}" retryOnAuthFailed)
if(default_retry)
    set(default_retry_text "true")
else()
    set(default_retry_text "false")
endif()
query("${default_config}" version TRUE "${default_version}")
query("${default_config}" connectionTimeoutSeconds TRUE "${default_timeout}")
query("${default_config}" retryOnAuthFailed TRUE "${default_retry_text}")
query("${default_config}" version TRUE "${default_version}" "-DOPENVPN_EXPECTED_VERSION=${default_version}")
query("${default_config}" version FALSE "" "-DOPENVPN_EXPECTED_VERSION=${default_version}.invalid")
query("${default_config}" password FALSE "")
query("${TEST_OUTPUT_DIR}/absent.json" version FALSE "")

set(valid "{\"schemaVersion\":1,\"version\":\"2.3.4\",\"connectionTimeoutSeconds\":45,\"retryOnAuthFailed\":true}")
set(fixture "${TEST_OUTPUT_DIR}/config.json")
file(WRITE "${fixture}" "${valid}")
query("${fixture}" version TRUE "2.3.4")
query("${fixture}" connectionTimeoutSeconds TRUE "45")
query("${fixture}" retryOnAuthFailed TRUE "true")

foreach(invalid IN ITEMS
        "{}"
        "not-json"
        "{\"schemaVersion\":2,\"version\":\"2.3.4\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"01.0.0\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.65536\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0-rc.1\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":1,\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":0,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":3601,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":1.5,\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":\"30\",\"retryOnAuthFailed\":false}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":\"false\"}"
        "{\"schemaVersion\":1,\"version\":\"1.0.0\",\"connectionTimeoutSeconds\":30,\"retryOnAuthFailed\":0}")
    file(WRITE "${fixture}" "${invalid}")
    query("${fixture}" version FALSE "")
endforeach()

file(WRITE "${fixture}" "${valid}")
set(generator "${TEST_OUTPUT_DIR}/generate.cmake")
file(WRITE "${generator}"
    "include(\"${reader}\")\nopenvpn_generate_config_header(\"${TEST_OUTPUT_DIR}/generated\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DOPENVPN_PROJECT_CONFIG_FILE=${fixture}"
    -P "${generator}" RESULT_VARIABLE generated_status ERROR_VARIABLE generated_error)
if(NOT generated_status EQUAL 0)
    message(FATAL_ERROR "Header generation failed: ${generated_error}")
endif()
file(READ "${TEST_OUTPUT_DIR}/generated/openvpn_project_config.h" header)
foreach(required "version = \"2.3.4\"" "connection_timeout_seconds = 45" "retry_on_auth_failed = true")
    string(FIND "${header}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Generated header missing ${required}")
    endif()
endforeach()

# A development cache must not turn the single production source into another
# platform-specific configuration. Fixture overrides are script-mode only.
set(probe "${TEST_OUTPUT_DIR}/production-probe")
file(MAKE_DIRECTORY "${probe}")
file(WRITE "${probe}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.21)\nproject(SharedConfigProbe NONE)\ninclude(\"${reader}\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${probe}" -B "${probe}/build"
    "-DOPENVPN_PROJECT_CONFIG_FILE=${fixture}"
    RESULT_VARIABLE override_status OUTPUT_VARIABLE override_output ERROR_VARIABLE override_error)
if(override_status EQUAL 0)
    message(FATAL_ERROR "Production builds must reject alternate project configuration files")
endif()
message(STATUS "Shared project configuration tests passed")
