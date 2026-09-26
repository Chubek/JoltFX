add_subdirectory(tillyz)

add_library(tilly STATIC
    src/tilly.c
    src/allocator.c
    src/logger.c
    src/module.c
)

target_include_directories(tilly PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tilly PUBLIC tillyz)

set_project_warnings(tilly)
