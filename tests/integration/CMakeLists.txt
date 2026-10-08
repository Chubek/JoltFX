# Integration tests: the CLI contract, the Hello World example, and the
# desktop frontend's real frame loop.

if(TARGET joltfx_cli)
add_test(
    NAME cli_integration
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_cli.sh"
            "$<TARGET_FILE:joltfx_cli>" "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(cli_integration PROPERTIES LABELS "integration;cli" TIMEOUT 300)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME memory_allocation_policy COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/scripts/check-memory-policy.py" "${PROJECT_SOURCE_DIR}")
    add_test(NAME memory_tool_edges COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/memory/test_tools.py"
        "$<TARGET_FILE:joltfmt>" "$<TARGET_FILE:joltdoc>" "$<TARGET_FILE:joltscript-lsp>")
    set_tests_properties(memory_allocation_policy memory_tool_edges PROPERTIES LABELS "integration;memory" TIMEOUT 60)
    if(TARGET joltfx_cli)
        add_test(NAME memory_image_cli COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/memory/test_image_cli.py" "$<TARGET_FILE:joltfx_cli>")
        set_tests_properties(memory_image_cli PROPERTIES LABELS "integration;memory" TIMEOUT 60)
    endif()
    if(JFX_ASAN)
        add_test(NAME memory_sanitizer_detection COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/memory/test_detection.py" "$<TARGET_FILE:test_memory>")
        set_tests_properties(memory_sanitizer_detection PROPERTIES LABELS "integration;memory" TIMEOUT 60)
    endif()
endif()
if(JFX_EXT_WASM)
    add_test(NAME cmake_wamr_config COMMAND "${CMAKE_COMMAND}"
        "-DADAPTER_DIR=${PROJECT_SOURCE_DIR}/extif/wasm"
        "-DWAMR_ROOT=${JFX_WAMR_ROOT}"
        "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/wamr-config-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/extif/test_wamr_config.cmake")
    set_tests_properties(cmake_wamr_config PROPERTIES LABELS "integration;cmake" TIMEOUT 60)
endif()
if(JFX_EXT_MRUBY)
    add_test(NAME mruby_source_selection COMMAND "${CMAKE_COMMAND}"
        "-DJFX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DJFX_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/mruby-source-selection"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/extif/test_mruby_source.cmake")
    set_tests_properties(mruby_source_selection PROPERTIES LABELS "integration;cmake" TIMEOUT 60)
    add_test(NAME cmake_mruby_config COMMAND "${CMAKE_COMMAND}"
        "-DADAPTER_DIR=${PROJECT_SOURCE_DIR}/extif/mruby"
        "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/mruby-config-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/extif/test_mruby_config.cmake")
    set_tests_properties(cmake_mruby_config PROPERTIES LABELS "integration;cmake" TIMEOUT 60)
endif()
find_program(JFX_TEST_FFMPEG ffmpeg)
find_program(JFX_TEST_FFPROBE ffprobe)
if(Python3_Interpreter_FOUND AND TARGET joltfx_cli)
    add_test(NAME modeling3d_cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_modeling3d.py" "$<TARGET_FILE:joltfx_cli>")
    add_test(NAME ext_conformance_cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/extif/test_scripts.py" "$<TARGET_FILE:joltfx_cli>" "${PROJECT_SOURCE_DIR}")
    set_tests_properties(ext_conformance_cli PROPERTIES LABELS "integration;cli;conformance" TIMEOUT 180)
endif()
if(Python3_Interpreter_FOUND AND TARGET joltfx_cli AND TARGET jfx_example_plugin)
    set(plugin_cli_args "$<TARGET_FILE:joltfx_cli>" "$<TARGET_FILE:jfx_example_plugin>")
    if(JFX_TEST_FFPROBE)
        list(APPEND plugin_cli_args "${JFX_TEST_FFPROBE}")
    endif()
    add_test(NAME plugin_cli_integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/test_plugin_cli.py" ${plugin_cli_args})
    set_tests_properties(plugin_cli_integration PROPERTIES LABELS "integration;cli;conformance" TIMEOUT 120)
endif()
if(Python3_Interpreter_FOUND AND TARGET joltfx_cli AND JFX_TEST_FFMPEG AND JFX_TEST_FFPROBE)
    add_test(NAME media_cli_integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/media/test_media.py" "$<TARGET_FILE:joltfx_cli>"
        "${JFX_TEST_FFMPEG}" "${JFX_TEST_FFPROBE}")
    set_tests_properties(media_cli_integration PROPERTIES LABELS "integration;cli;media" TIMEOUT 180 SKIP_RETURN_CODE 77)
endif()

if(JOLTFX_BUILD_EXAMPLES)
    add_test(
        NAME example_hello_world
        COMMAND example_hello_world "${CMAKE_SOURCE_DIR}/examples/hello_world/effect.jolt"
    )
    set_tests_properties(example_hello_world PROPERTIES LABELS "integration;example" TIMEOUT 120)
endif()

# The desktop frontend used to compose exactly one frame and exit, so nothing
# exercised the window, the input pump or the present. These drive a bounded
# number of real frames and fail if the loop does not actually run them.
# Headless smoke mode covers the UI path on machines with no display server;
# the windowed run is added only when this build has a windowing backend.
if(JFX_FRONTEND_DESKTOP)
    add_test(
        NAME desktop_headless
        COMMAND jfx_desktop --headless-smoke
    )
    set_tests_properties(desktop_headless PROPERTIES LABELS "integration;frontend" TIMEOUT 120)

    get_property(desktop_window_enabled GLOBAL PROPERTY JFX_DESKTOP_WINDOW_ENABLED)
    if(desktop_window_enabled)
        add_test(
            NAME desktop_window
            COMMAND jfx_desktop --frames 30 --width 640 --height 480
                    --effect brightness --param 0.5
        )
        # No display server (CI container, SSH session) must skip, not fail.
        set_tests_properties(desktop_window PROPERTIES
            LABELS "integration;frontend;window"
            TIMEOUT 180
            SKIP_RETURN_CODE 77
        )
    endif()
endif()

# Child CLI/tool processes inherit the object-level MemTKX leak census.
get_property(memory_tests DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS memory_tests)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "TILLY_CHECK_LEAKS=1")
endforeach()
