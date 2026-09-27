add_library(jfx_backend_software STATIC
    src/software_backend.c
)

target_include_directories(jfx_backend_software PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_backend_software PUBLIC joltscript_glue)
target_link_libraries(jfx_backend_software PRIVATE tilly)
if(NOT MSVC)
    target_link_libraries(jfx_backend_software PUBLIC m)
endif()
set_project_warnings(jfx_backend_software)
