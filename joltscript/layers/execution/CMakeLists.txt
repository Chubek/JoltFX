add_library(joltscript_execution STATIC
    src/vm.c
    src/bytecode.c
    src/typed_program.c
    src/pipeline.c
    src/image_task.c
    src/audio_task.c
)

set(_jfx_joltscript_sljit_default OFF)
set(_jfx_sljit_processors
    "(x86_64|AMD64|i[3-6]86|aarch64|arm64|armv[6-9]|ppc|powerpc|mips|riscv|s390x|loongarch)")
if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "${_jfx_sljit_processors}")
    set(_jfx_joltscript_sljit_default ON)
endif()
option(JFX_JOLTSCRIPT_SLJIT
    "Build the vendored SLJIT backend for Joltscript JBC1 bytecode"
    ${_jfx_joltscript_sljit_default})

if(JFX_JOLTSCRIPT_SLJIT)
    find_package(Threads REQUIRED)
    add_library(jfx_sljit STATIC "${PROJECT_SOURCE_DIR}/third_party/sljit/sljit_src/sljitLir.c")
    target_include_directories(jfx_sljit SYSTEM PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/sljit/sljit_src>)
    target_compile_definitions(jfx_sljit PRIVATE
        SLJIT_DEBUG=0
        SLJIT_VERBOSE=0
        SLJIT_ARGUMENT_CHECKS=0)
    target_link_libraries(jfx_sljit PUBLIC Threads::Threads)
    target_sources(joltscript_execution PRIVATE src/jit_sljit.c)
    target_link_libraries(joltscript_execution PRIVATE jfx_sljit)
    target_compile_definitions(joltscript_execution PRIVATE JFX_JOLTSCRIPT_SLJIT=1)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/sljit/LICENSE"
        DESTINATION share/doc/joltfx/third_party/sljit)
endif()

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
