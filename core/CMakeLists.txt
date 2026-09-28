add_library(jfx_core STATIC
    src/engine.c
    src/buffer.c
    src/texture.c
    src/kernel.c
    src/lut.c
    src/image.c
    src/compose.c
    src/timeline.c
    src/editor.c
    src/project.c
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

# The engine compiles Joltscript and dispatches JBC1 bytecode, so the Glue
# layer sits underneath it. The Glue layer never links back.
target_link_libraries(jfx_core PUBLIC joltscript_glue jfx_api)

# stb_image is vendored and header-only; image.c is the single translation unit
# that instantiates it.
target_include_directories(jfx_core SYSTEM PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/stb>)
target_link_libraries(jfx_core PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(jfx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Engine owns backend selection (see engine.c); the backends must not link back
# into jfx_core. PRIVATE keeps the dependency out of consumers' link line.
#
# The JFX_BACKEND_<NAME> definitions are PUBLIC so a consumer - including a
# test - can tell which backends this build actually contains instead of
# hardcoding a name that may have been switched off.
get_property(JFX_ENABLED_BACKENDS GLOBAL PROPERTY JFX_ENABLED_BACKENDS)
foreach(backend IN LISTS JFX_ENABLED_BACKENDS)
    string(TOUPPER "${backend}" backend_upper)
    target_link_libraries(jfx_core PRIVATE jfx_backend_${backend})
    target_compile_definitions(jfx_core PUBLIC JFX_BACKEND_${backend_upper}=1)
endforeach()

set_project_warnings(jfx_core)
