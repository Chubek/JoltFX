add_library(jfx_frontend_common STATIC
    src/frontend_api.c
)

target_include_directories(jfx_frontend_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_frontend_common PUBLIC jfx_core)

set_project_warnings(jfx_frontend_common)
