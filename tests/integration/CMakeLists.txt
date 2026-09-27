# Phase 3 integration tests: CLI contract + Hello World example.
# The shell suite drives the built CLI binary against the in-tree kernels.

add_test(
    NAME cli_integration
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_cli.sh"
            "$<TARGET_FILE:joltfx_cli>" "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(cli_integration PROPERTIES LABELS "integration;cli")

add_test(
    NAME example_hello_world
    COMMAND example_hello_world "${CMAKE_SOURCE_DIR}/examples/hello_world/effect.jolt"
)
set_tests_properties(example_hello_world PROPERTIES LABELS "integration;example")
