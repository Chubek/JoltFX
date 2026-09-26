add_library(tillyz STATIC
    src/bootstrap.c
)

target_include_directories(tillyz PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_project_warnings(tillyz)
