# Exercise dependency discovery without requiring an installed Wasmtime SDK.
file(REMOVE_RECURSE "${TEST_BINARY_DIR}")
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/sdk/include" "${TEST_BINARY_DIR}/sdk/lib"
    "${TEST_BINARY_DIR}/project")
file(WRITE "${TEST_BINARY_DIR}/sdk/include/wasmtime.h"
    "#define WASMTIME_VERSION_MAJOR 38\n#define WASMTIME_VERSION_MINOR 0\n#define WASMTIME_VERSION_PATCH 4\n")
file(WRITE "${TEST_BINARY_DIR}/sdk/lib/${LIBRARY_NAME}" "")
file(WRITE "${TEST_BINARY_DIR}/project/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(WasmtimeDiscovery C)
list(APPEND CMAKE_MODULE_PATH "${MODULE_DIR}")
find_package(Wasmtime 38 REQUIRED)
get_target_property(actual_include Wasmtime::wasmtime INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(actual_library Wasmtime::wasmtime IMPORTED_LOCATION)
if(NOT actual_include STREQUAL "${JFX_WASMTIME_ROOT}/include" OR
   NOT actual_library STREQUAL "${JFX_WASMTIME_ROOT}/lib/${LIBRARY_NAME}")
    message(FATAL_ERROR "Wasmtime target retained stale SDK paths")
endif()
]=])
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${TEST_BINARY_DIR}/project" -B "${TEST_BINARY_DIR}/build"
    "-DMODULE_DIR=${MODULE_DIR}" "-DJFX_WASMTIME_ROOT=${TEST_BINARY_DIR}/sdk"
    "-DLIBRARY_NAME=${LIBRARY_NAME}"
    "-DWasmtime_INCLUDE_DIR=${TEST_BINARY_DIR}/removed/include"
    "-DWasmtime_LIBRARY=${TEST_BINARY_DIR}/removed/lib/${LIBRARY_NAME}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Stale SDK recovery failed:\n${output}\n${errors}")
endif()
# Remove the SDK after a successful configure, reproducing temporary cleanup.
file(REMOVE_RECURSE "${TEST_BINARY_DIR}/sdk")
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${TEST_BINARY_DIR}/project" -B "${TEST_BINARY_DIR}/build"
    -DCMAKE_FIND_USE_CMAKE_PATH=OFF -DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF
    -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF
    -DCMAKE_FIND_USE_PACKAGE_ROOT_PATH=OFF
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(result EQUAL 0 OR NOT "${output}\n${errors}" MATCHES "JFX_WASMTIME_ROOT")
    message(FATAL_ERROR "Missing SDK must fail with setup guidance:\n${output}\n${errors}")
endif()
