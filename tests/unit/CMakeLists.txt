add_executable(test_tillyz
    tilly/test_tillyz.c
)

target_link_libraries(test_tillyz PRIVATE tillyz)

add_executable(test_core
    core/test_engine.c
)

target_link_libraries(test_core PRIVATE jfx_core)

add_executable(test_containers containers/test_containers.c)
target_link_libraries(test_containers PRIVATE tilly)
add_test(NAME test_containers COMMAND test_containers)
add_executable(test_joltscript joltscript/test_vm.c)
target_link_libraries(test_joltscript PRIVATE joltscript_glue)
add_test(NAME test_joltscript COMMAND test_joltscript)

add_executable(test_pipeline joltscript/test_pipeline.c)
target_link_libraries(test_pipeline PRIVATE joltscript_glue)
add_test(NAME test_pipeline COMMAND test_pipeline)
add_executable(test_effects ../kernels/test_effects.c)
target_link_libraries(test_effects PRIVATE jolt_effects)
add_test(NAME kernel_color_conformance COMMAND test_effects)
add_custom_target(validate_kernels COMMAND $<TARGET_FILE:test_effects> DEPENDS test_effects)

add_executable(test_vulkan_backend backends/test_vulkan_backend.c)
target_link_libraries(test_vulkan_backend PRIVATE jfx_backend_vulkan)
add_test(NAME test_vulkan_backend COMMAND test_vulkan_backend)
