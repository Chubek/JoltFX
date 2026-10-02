# Integration tests: the CLI contract, the Hello World example, and the
# desktop frontend's real frame loop.

add_test(
    NAME cli_integration
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_cli.sh"
            "$<TARGET_FILE:joltfx_cli>" "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(cli_integration PROPERTIES LABELS "integration;cli" TIMEOUT 300)

find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(JFX_TEST_FFMPEG ffmpeg)
find_program(JFX_TEST_FFPROBE ffprobe)
if(Python3_Interpreter_FOUND AND JFX_TEST_FFMPEG AND JFX_TEST_FFPROBE)
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
