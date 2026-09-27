add_library(jfx_backend_metal STATIC src/metal_backend.c)
target_include_directories(jfx_backend_metal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(jfx_backend_metal PUBLIC jfx_backend_software)
set_project_warnings(jfx_backend_metal)
