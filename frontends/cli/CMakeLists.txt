add_executable(joltfx_cli
    src/main.c
    src/commands.c
)

target_link_libraries(joltfx_cli PRIVATE
    jfx_core
    jfx_frontend_common
    joltscript_glue
    jolt_effects
    jfx_backend_vulkan
)

set_target_properties(joltfx_cli PROPERTIES OUTPUT_NAME joltfx)

set_project_warnings(joltfx_cli)
