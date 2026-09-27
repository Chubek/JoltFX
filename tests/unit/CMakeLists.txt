add_executable(test_tillyz
    tilly/test_tillyz.c
)

target_link_libraries(test_tillyz PRIVATE tillyz)

add_executable(test_core
    core/test_engine.c
)

target_link_libraries(test_core PRIVATE jfx_core)

add_library(test_plugin_module MODULE core/test_plugin_module.c)
target_include_directories(test_plugin_module PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
if(UNIX AND NOT APPLE)
    target_link_options(test_plugin_module PRIVATE -Wl,--allow-shlib-undefined)
endif()
set_project_warnings(test_plugin_module)

add_executable(test_plugin_host core/test_plugin_host.c)
target_link_libraries(test_plugin_host PRIVATE jfx_core)
set_target_properties(test_plugin_host PROPERTIES ENABLE_EXPORTS ON)
set_project_warnings(test_plugin_host)
add_test(NAME plugin_host COMMAND test_plugin_host $<TARGET_FILE:test_plugin_module>)

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

add_executable(test_mobile_frontend frontends/test_mobile_frontend.c)
target_link_libraries(test_mobile_frontend PRIVATE jfx_mobile_player)
add_test(NAME mobile_frontend COMMAND test_mobile_frontend)

add_executable(test_host_plugins frontends/test_host_plugins.c)
target_link_libraries(test_host_plugins PRIVATE jfx_ae_plugin jfx_premiere_plugin jfx_davinci_plugin)
add_test(NAME host_plugin_contract COMMAND test_host_plugins)

add_executable(test_web_session frontends/test_web_session.c)
target_link_libraries(test_web_session PRIVATE jfx_web_session)
add_test(NAME web_session COMMAND test_web_session)
