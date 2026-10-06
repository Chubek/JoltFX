# Exercise source selection in isolated, offline configure runs.
if(NOT DEFINED JFX_SOURCE_DIR OR NOT DEFINED JFX_TEST_DIR)
    message(FATAL_ERROR "JFX_SOURCE_DIR and JFX_TEST_DIR are required")
endif()
file(REMOVE_RECURSE "${JFX_TEST_DIR}")
file(MAKE_DIRECTORY "${JFX_TEST_DIR}/compatible/include" "${JFX_TEST_DIR}/incompatible/include")
file(WRITE "${JFX_TEST_DIR}/compatible/include/mruby.h" "mrb_state *mrb_open_allocf(mrb_allocf, void *);\n")
file(WRITE "${JFX_TEST_DIR}/incompatible/include/mruby.h" "mrb_state *mrb_open(void);\n")

function(check_source name vendor root expected success)
    set(source "${JFX_TEST_DIR}/${name}")
    file(MAKE_DIRECTORY "${source}")
    if(NOT vendor STREQUAL "missing")
        file(COPY "${JFX_TEST_DIR}/${vendor}/include" DESTINATION "${source}/third_party/mruby")
    endif()
    file(WRITE "${source}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(MrubySourceTest NONE)\n"
        "include(\"${JFX_SOURCE_DIR}/extif/mruby/ResolveSource.cmake\")\n"
        "if(NOT JFX_MRUBY_ROOT STREQUAL \"${expected}\")\n"
        "  message(FATAL_ERROR \"Unexpected mruby source: \${JFX_MRUBY_ROOT}\")\n"
        "endif()\n")
    set(args "-DFETCHCONTENT_SOURCE_DIR_JFX_MRUBY_SOURCE=${JFX_TEST_DIR}/compatible")
    if(NOT root STREQUAL "default")
        list(APPEND args "-DJFX_MRUBY_ROOT=${root}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build" ${args}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(success AND NOT result EQUAL 0)
        message(FATAL_ERROR "${name} failed:\n${output}\n${error}")
    elseif(NOT success)
        if(result EQUAL 0 OR NOT error MATCHES "${expected}")
            message(FATAL_ERROR "${name} did not reject the explicit root:\n${output}\n${error}")
        endif()
    endif()
endfunction()

check_source(vendored compatible default "${JFX_TEST_DIR}/vendored/third_party/mruby" TRUE)
check_source(fallback incompatible default "${JFX_TEST_DIR}/compatible" TRUE)
check_source(missing missing default "${JFX_TEST_DIR}/compatible" TRUE)
# Existing build caches contain the old default path; they must also recover.
check_source(cached incompatible "${JFX_TEST_DIR}/cached/third_party/mruby" "${JFX_TEST_DIR}/compatible" TRUE)
check_source(external incompatible "${JFX_TEST_DIR}/compatible" "${JFX_TEST_DIR}/compatible" TRUE)
check_source(bad_external compatible "${JFX_TEST_DIR}/incompatible" "lacks mrb_open_allocf" FALSE)
check_source(missing_external compatible "${JFX_TEST_DIR}/absent" "mruby headers not found" FALSE)
