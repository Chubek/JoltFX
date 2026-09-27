# Test target helpers.
#
# JoltFX's tests are plain `assert()` programs. `assert(expr)` is a statement
# whose expression must run for its side effects, so compiling a test with
# NDEBUG (which RelWithDebInfo and Release do) deletes the calls entirely and
# the test passes while checking nothing. That is a silent, total loss of test
# coverage in exactly the configurations most likely to be shipped, so test
# targets always have NDEBUG disabled.
#
# `jfx_test_target` is the single place that guarantees it.

function(jfx_test_target name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    if(ARG_LIBRARIES)
        target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    set_project_warnings(${name})
endfunction()
