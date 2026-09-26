add_library(jfx_core STATIC
    src/engine.c
    src/scheduler.c
    src/memory.c
    src/event_bus.c
)

target_include_directories(jfx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_core PUBLIC tilly)

set_project_warnings(jfx_core)
