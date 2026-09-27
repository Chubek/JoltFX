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

add_executable(test_fallback_backends backends/test_fallback_backends.c)
target_link_libraries(test_fallback_backends PRIVATE
    jfx_backend_metal
    jfx_backend_d3d12
    jfx_backend_webgpu
)
add_test(NAME backend_fallback_conformance COMMAND test_fallback_backends)

add_executable(test_extensions extif/test_extensions.c)
target_link_libraries(test_extensions PRIVATE jfx_lua jfx_mruby)
add_test(NAME ext_conformance COMMAND test_extensions)

add_executable(test_desktop_frontend frontends/test_desktop_frontend.cpp)
target_link_libraries(test_desktop_frontend PRIVATE jfx_desktop_frontend)
add_test(NAME desktop_frontend COMMAND test_desktop_frontend)
