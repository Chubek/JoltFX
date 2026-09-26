add_library(jfx_extif_common STATIC
    src/ffi_runtime.c
)

target_include_directories(jfx_extif_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_extif_common PUBLIC jfx_core)

set_project_warnings(jfx_extif_common)
