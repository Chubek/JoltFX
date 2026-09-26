add_library(joltscript_glue STATIC
    src/bindings.c
)

target_include_directories(joltscript_glue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_glue PUBLIC
    joltscript_execution
    jfx_core
)

set_project_warnings(joltscript_glue)
