add_library(jfx_web_session STATIC src/jfx_wasm.c)
target_include_directories(jfx_web_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(jfx_web_session PUBLIC jfx_core jolt_effects)
set_project_warnings(jfx_web_session)
