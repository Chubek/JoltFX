# The extension adapter uses the Wasmtime 38+ C API. Consumers resolve their
# own installation instead of inheriting a path from the producer's machine.
find_path(Wasmtime_INCLUDE_DIR wasmtime.h HINTS "${JFX_WASMTIME_ROOT}/include")
find_library(Wasmtime_LIBRARY NAMES wasmtime HINTS "${JFX_WASMTIME_ROOT}/lib")
if(Wasmtime_INCLUDE_DIR)
    foreach(part IN ITEMS MAJOR MINOR PATCH)
        file(STRINGS "${Wasmtime_INCLUDE_DIR}/wasmtime.h" version_line
            REGEX "^#define WASMTIME_VERSION_${part} [0-9]+$")
        string(REGEX REPLACE ".* ([0-9]+)$" "\\1" Wasmtime_VERSION_${part} "${version_line}")
    endforeach()
    set(Wasmtime_VERSION "${Wasmtime_VERSION_MAJOR}.${Wasmtime_VERSION_MINOR}.${Wasmtime_VERSION_PATCH}")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Wasmtime REQUIRED_VARS Wasmtime_INCLUDE_DIR Wasmtime_LIBRARY
    VERSION_VAR Wasmtime_VERSION)
if(Wasmtime_FOUND AND NOT TARGET Wasmtime::wasmtime)
    add_library(Wasmtime::wasmtime UNKNOWN IMPORTED)
    set_target_properties(Wasmtime::wasmtime PROPERTIES
        IMPORTED_LOCATION "${Wasmtime_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${Wasmtime_INCLUDE_DIR}")
    if(UNIX AND Wasmtime_LIBRARY MATCHES "\\.a$")
        find_package(Threads REQUIRED)
        set_property(TARGET Wasmtime::wasmtime PROPERTY INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS};m")
    endif()
endif()
mark_as_advanced(Wasmtime_INCLUDE_DIR Wasmtime_LIBRARY)
