# The native bridge is independent of the optional Ingagi scripting workspace.
# GPU shader export remains browser-hosted; desktop uses the real Go CPU backend.
include(GNUInstallDirs)
set(JFX_MONKEYTRACE_GOROOT "/usr/lib/go" CACHE PATH "Go distribution for MonkeyTrace")
find_program(JFX_MONKEYTRACE_GO go HINTS "${JFX_MONKEYTRACE_GOROOT}/bin")
set(jfx_monkeytrace_default OFF)
if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$"
   AND JFX_MONKEYTRACE_GO AND EXISTS "${JFX_MONKEYTRACE_GOROOT}/src/runtime")
    set(jfx_monkeytrace_default ON)
endif()
option(JFX_MONKEYTRACE "Use MonkeyTrace as the default desktop 3D renderer" ${jfx_monkeytrace_default})
if(JFX_MONKEYTRACE)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
        message(FATAL_ERROR "MonkeyTrace's native bridge currently supports Linux/macOS host builds; use JFX_MONKEYTRACE=OFF for cross builds")
    endif()
    if(NOT JFX_MONKEYTRACE_GO OR NOT EXISTS "${JFX_MONKEYTRACE_GOROOT}/src/runtime")
        message(FATAL_ERROR "Set JFX_MONKEYTRACE_GOROOT to a Go 1.21+ distribution or disable JFX_MONKEYTRACE")
    endif()
    file(GLOB_RECURSE monkeytrace_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/ingagi/*.go" "${PROJECT_SOURCE_DIR}/ingagi/*.c"
        "${PROJECT_SOURCE_DIR}/ingagi/*.cc" "${PROJECT_SOURCE_DIR}/ingagi/*.h"
        "${PROJECT_SOURCE_DIR}/ingagi/*.ing" "${PROJECT_SOURCE_DIR}/ingagi/*.html"
        "${PROJECT_SOURCE_DIR}/ingagi/*.sexp")
    file(GLOB_RECURSE monkeytrace_math_headers CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/third_party/glm/*.hpp"
        "${PROJECT_SOURCE_DIR}/third_party/glm/*.inl"
        "${PROJECT_SOURCE_DIR}/third_party/simdette/*.hpp")
    set(monkeytrace_library "${PROJECT_BINARY_DIR}/monkeytrace/libmonkeytrace${CMAKE_SHARED_LIBRARY_SUFFIX}")
    add_custom_command(OUTPUT "${monkeytrace_library}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/monkeytrace"
        COMMAND ${CMAKE_COMMAND} -E env "GOROOT=${JFX_MONKEYTRACE_GOROOT}" "GOTOOLCHAIN=local"
            "GOPROXY=off" "GOCACHE=${PROJECT_BINARY_DIR}/monkeytrace/gocache"
            "CGO_ENABLED=1" "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
            ${JFX_MONKEYTRACE_GO} build -buildmode=c-shared -o "${monkeytrace_library}" ./monkeytrace/native
        DEPENDS ${monkeytrace_sources} ${monkeytrace_math_headers} "${PROJECT_SOURCE_DIR}/ingagi/go.mod"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/ingagi"
        COMMENT "Building MonkeyTrace native CPU renderer" VERBATIM)
    add_custom_target(monkeytrace_build DEPENDS "${monkeytrace_library}")
    add_library(monkeytrace_native SHARED IMPORTED GLOBAL)
    set_target_properties(monkeytrace_native PROPERTIES IMPORTED_LOCATION "${monkeytrace_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/ingagi/monkeytrace/native")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(monkeytrace_native PROPERTIES IMPORTED_NO_SONAME TRUE)
    endif()
    add_dependencies(monkeytrace_native monkeytrace_build)
    # Installed applications find the bridge beside the SDK libraries, including
    # relocatable non-system prefixes. Preserve any caller-provided RPATHs.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    else()
        list(APPEND CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(FILES "${monkeytrace_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${PROJECT_SOURCE_DIR}/ingagi/monkeytrace/native/monkeytrace.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/monkeytrace")
endif()
