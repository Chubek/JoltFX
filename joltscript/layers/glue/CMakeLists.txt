add_library(joltscript_glue STATIC
    src/bindings.c
    src/compiler.c
)

target_include_directories(joltscript_glue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# The Glue layer sits *below* the engine: the compiler and the ABI registry must
# not depend on jfx_core, or the engine could not depend on them in order to run
# kernels. jfx_core links this target, never the other way round.
target_link_libraries(joltscript_glue PUBLIC
    joltscript_execution
)

set_project_warnings(joltscript_glue)
