add_library(jfx_core STATIC
    src/engine.c
    src/buffer.c
    src/texture.c
    src/kernel.c
    src/lut.c
    src/image.c
    src/compose.c
    src/composition.c
    src/color.c
    src/timeline.c
    src/editor.c
    src/nle.c
    src/audio.c
    src/recording.c
    src/vst3.cpp
    src/export.c
    src/project.c
    src/scheduler.c
    src/memory.c
    src/event_bus.c
    src/plugin_host.c
    src/animation.cpp
    src/result.c
    src/cpu_numeric.cpp
)
option(JFX_CPU_SIMD "Use vendored xsimd for shared CPU numeric kernels" ON)
if(NOT JFX_CPU_SIMD)
    target_compile_definitions(jfx_core PRIVATE JFX_CPU_SIMD_OFF=1)
endif()
target_include_directories(jfx_core SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/xsimd/include")

target_include_directories(jfx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# The engine compiles Joltscript and dispatches JBC1 bytecode, so the Glue
# layer sits underneath it. The Glue layer never links back.
target_link_libraries(jfx_core PUBLIC joltscript_glue jfx_api jolt_effects)
target_include_directories(jfx_core PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/sdl/include"
    "${CMAKE_SOURCE_DIR}/third_party/xsimd/include"
    "${CMAKE_SOURCE_DIR}/third_party/glm"
    "${CMAKE_SOURCE_DIR}/third_party/satie/include"
    "${CMAKE_SOURCE_DIR}/third_party/simdette/include"
    "${CMAKE_SOURCE_DIR}/third_party/LIEF/include"
)
include(${CMAKE_SOURCE_DIR}/cmake/ColorKernels.cmake)
target_include_directories(jfx_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

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
include(${CMAKE_SOURCE_DIR}/cmake/VST3.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Modeling3D.cmake)
