add_library(jfx_core STATIC
    src/engine.c
    src/scheduler.c
    src/memory.c
    src/event_bus.c
    src/plugin_host.c
    src/result.c
)

target_include_directories(jfx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_core PUBLIC tilly)
target_link_libraries(jfx_core PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(jfx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Engine owns backend selection (see engine.c); the backend must not link
# back into jfx_core. PRIVATE keeps the dependency out of consumers' link.
target_link_libraries(jfx_core PRIVATE
    jfx_backend_vulkan
    jfx_backend_metal
    jfx_backend_d3d12
    jfx_backend_webgpu
)

set_project_warnings(jfx_core)
