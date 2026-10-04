set(JFX_WASMTIME_ROOT "" CACHE PATH "Wasmtime C API installation prefix (v38 or newer)")
find_package(Wasmtime 38 REQUIRED)
add_library(jfx_wasm STATIC src/wasm_runtime.c)
target_include_directories(jfx_wasm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(jfx_wasm PRIVATE ../common/src)
target_link_libraries(jfx_wasm PUBLIC jfx_extif_common PRIVATE Wasmtime::wasmtime)
set_project_warnings(jfx_wasm)
