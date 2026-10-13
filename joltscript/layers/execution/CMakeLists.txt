add_library(joltscript_execution STATIC
    src/vm.c
    src/bytecode.c
    src/typed_program.c
    src/pipeline.c
    src/image_task.c
    src/audio_task.c
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(stdlib_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../stdlib")
set(stdlib_sources)
foreach(module math graphics geometry time collections strings)
    list(APPEND stdlib_sources "${stdlib_dir}/${module}.jolt")
endforeach()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stdlib_catalog.h"
    COMMAND "${Python3_EXECUTABLE}" "${stdlib_dir}/generate.py" "${CMAKE_CURRENT_BINARY_DIR}/stdlib_catalog.h"
    DEPENDS "${stdlib_dir}/generate.py" ${stdlib_sources} VERBATIM)
target_sources(joltscript_execution PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/stdlib_catalog.h")
target_include_directories(joltscript_execution PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
function(jolt_add_pcre2)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(PCRE2_BUILD_TESTS OFF)
    set(PCRE2_BUILD_PCRE2GREP OFF)
    set(PCRE2_SUPPORT_JIT OFF)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/PCRE2" "${CMAKE_CURRENT_BINARY_DIR}/pcre2" EXCLUDE_FROM_ALL)
endfunction()
if(NOT TARGET pcre2-8-static)
    jolt_add_pcre2()
endif()
target_link_libraries(joltscript_execution PRIVATE pcre2-8-static)
target_compile_definitions(joltscript_execution PRIVATE PCRE2_STATIC)
install(FILES ${stdlib_sources} DESTINATION share/joltfx/stdlib)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/PCRE2/LICENCE"
    DESTINATION share/doc/joltfx/third_party/pcre2)

target_include_directories(joltscript_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_execution PUBLIC tilly)
# The vendored klib headers are included as SYSTEM so their macros (notably the
# khash initialiser, which narrows size_t to khint32_t) do not raise this
# project's -Wconversion in every translation unit that uses them.
target_include_directories(joltscript_execution SYSTEM PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/klib>)

set_project_warnings(joltscript_execution)

if(NOT MSVC)
    target_link_libraries(joltscript_execution PUBLIC m)
endif()
