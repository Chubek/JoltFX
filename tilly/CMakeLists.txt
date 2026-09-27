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

# Internal adapters are available only in the source build.
target_include_directories(tilly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>)
target_include_directories(tilly SYSTEM PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/klib>)
find_package(Threads REQUIRED)
target_link_libraries(tilly PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
