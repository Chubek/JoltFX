find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(JFX_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
set(JFX_MICROPYTHON_ROOT "${PROJECT_SOURCE_DIR}/third_party/micropython")
set(JFX_MICROPYTHON_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JFX_MICROPYTHON_GENERATED}")
configure_file(port/mpconfigport.h "${JFX_MICROPYTHON_GENERATED}/mpconfigport.h" COPYONLY)
file(WRITE "${JFX_MICROPYTHON_GENERATED}/Makefile"
    "MICROPYTHON_TOP = ${JFX_MICROPYTHON_ROOT}\nSRC_QSTR += ${CMAKE_CURRENT_SOURCE_DIR}/port/definitions.c\ninclude $(MICROPYTHON_TOP)/ports/embed/embed.mk\n")
file(GLOB JFX_MICROPYTHON_HEADERS CONFIGURE_DEPENDS "${JFX_MICROPYTHON_ROOT}/py/*.h")
add_custom_command(OUTPUT "${JFX_MICROPYTHON_GENERATED}/genhdr/qstrdefs.generated.h"
    "${JFX_MICROPYTHON_GENERATED}/genhdr/moduledefs.h"
    "${JFX_MICROPYTHON_GENERATED}/genhdr/root_pointers.h"
    "${JFX_MICROPYTHON_GENERATED}/genhdr/mpversion.h"
    COMMAND ${JFX_MAKE_EXECUTABLE} -f Makefile
        "BUILD=${JFX_MICROPYTHON_GENERATED}" "CC=${CMAKE_C_COMPILER}"
        "PYTHON=${Python3_EXECUTABLE}" "PACKAGE_DIR=${JFX_MICROPYTHON_GENERATED}/embed"
    WORKING_DIRECTORY "${JFX_MICROPYTHON_GENERATED}"
    DEPENDS port/mpconfigport.h port/definitions.c ${JFX_MICROPYTHON_HEADERS}
    COMMENT "Generating the sandboxed MicroPython port headers")
add_custom_target(jfx_python_headers DEPENDS "${JFX_MICROPYTHON_GENERATED}/genhdr/qstrdefs.generated.h")
file(GLOB JFX_MICROPYTHON_SOURCES CONFIGURE_DEPENDS "${JFX_MICROPYTHON_ROOT}/py/*.c")
list(REMOVE_ITEM JFX_MICROPYTHON_SOURCES "${JFX_MICROPYTHON_ROOT}/py/emitnative.c")
add_library(jfx_python STATIC src/python_runtime.c ${JFX_MICROPYTHON_SOURCES})
add_dependencies(jfx_python jfx_python_headers)
target_include_directories(jfx_python PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(jfx_python SYSTEM PRIVATE ${JFX_MICROPYTHON_ROOT}
    "${JFX_MICROPYTHON_ROOT}/ports/embed" "${JFX_MICROPYTHON_GENERATED}")
target_include_directories(jfx_python PRIVATE ../common/src)
if(NOT MSVC)
    set_source_files_properties(${JFX_MICROPYTHON_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    if(JFX_ASAN)
        # These tiny stack-depth probes must address the native stack, rather
        # than ASan's fake stack. The interpreter and adapter remain instrumented.
        set_property(SOURCE "${JFX_MICROPYTHON_ROOT}/py/stackctrl.c"
            "${JFX_MICROPYTHON_ROOT}/py/cstack.c" APPEND PROPERTY COMPILE_OPTIONS "-fno-sanitize=address")
    endif()
    target_link_libraries(jfx_python PRIVATE m)
endif()
target_link_libraries(jfx_python PUBLIC jfx_extif_common)
set_project_warnings(jfx_python)
