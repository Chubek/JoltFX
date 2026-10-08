add_subdirectory(tillyz)
include("${PROJECT_SOURCE_DIR}/cmake/MemTKX.cmake")

add_library(tilly STATIC
    src/tilly.c
    src/allocator.cpp
    src/logger.c
    src/module.c
)

target_include_directories(tilly PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tilly PUBLIC tillyz)
target_link_libraries(tilly PRIVATE $<BUILD_INTERFACE:MemTKX::MemTKX>)
target_include_directories(tilly SYSTEM BEFORE PRIVATE "${JFX_MEMTKX_INCLUDE}")
set_target_properties(tilly PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)

set_project_warnings(tilly)

# Internal adapters are available only in the source build.
target_include_directories(tilly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>)
target_include_directories(tilly SYSTEM PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/klib>)
find_package(Threads REQUIRED)
target_link_libraries(tilly PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
