cmake_minimum_required(VERSION 3.20)
project(JoltFX VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EMSCRIPTEN)
    # The MemTKX C boundary translates C++ allocation failures into NULL/status.
    # Emscripten must retain catches in both the adapter and C++ frontends.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
    add_link_options(-fexceptions)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

option(JOLTFX_BUILD_TESTS "Build tests" ON)
option(JOLTFX_BUILD_EXAMPLES "Build examples" ON)
option(JOLTFX_BUILD_DOCS "Build documentation" OFF)

option(JFX_ASAN "Enable AddressSanitizer" OFF)
option(JFX_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Backends. A backend that is switched off is neither built nor selectable by
# the engine's registry, and `jfx_engine_init` rejects its name. The Vulkan
# backend is the reference implementation and defaults on; the other three
# default off because they are portable CPU-path adapters whose native dispatch
# needs a platform SDK, and building all of them adds three near-identical
# libraries to every build for no behaviour.
option(JFX_BACKEND_VULKAN "Build the Vulkan backend" ON)
option(JFX_BACKEND_METAL "Build the Metal backend (macOS/iOS)" ON)
option(JFX_BACKEND_D3D12 "Build the D3D12 backend (Windows)" ON)
option(JFX_BACKEND_WEBGPU "Build the WebGPU backend" ON)

# Extension runtimes.
option(JFX_EXT_LUA "Build the Lua extension runtime" ON)
option(JFX_EXT_MRUBY "Build the mruby extension runtime" ON)
option(JFX_EXT_QUICKJS "Build the QuickJS extension runtime" OFF)
option(JFX_EXT_PYTHON "Build the MicroPython extension runtime" OFF)
option(JFX_EXT_WASM "Build the vendored WAMR WebAssembly extension runtime" OFF)

# Frontends.
option(JFX_FRONTEND_CLI "Build the command-line frontend" ON)
option(JFX_FRONTEND_DESKTOP "Build the desktop frontend" ON)
option(JFX_FRONTEND_WEB "Build the web/WASM session library" ON)
option(JFX_FRONTEND_MOBILE "Build the mobile player library" ON)
option(JFX_PLUGIN_HOST_BRIDGES "Build the host-application plugin bridges" ON)

if(JFX_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(JFX_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()
include(cmake/modules/CompilerWarnings.cmake)

# Header-only carrier for the public engine API. Declared before the backends
# because they implement the backend HAL, whose signatures use jfx_result_t,
# without linking the engine that owns the backend selection.
add_library(jfx_api INTERFACE)
target_include_directories(jfx_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/include>
    $<INSTALL_INTERFACE:include>
)

add_subdirectory(tilly)
add_subdirectory(joltscript)
add_subdirectory(backends)
add_subdirectory(src)
add_subdirectory(sdk)
add_subdirectory(extif)
add_subdirectory(kernels)
add_subdirectory(frontends)

if(JOLTFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JOLTFX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(JOLTFX_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating API documentation")
    else()
        message(WARNING "JOLTFX_BUILD_DOCS is ON but Doxygen was not found")
    endif()
endif()

include(cmake/packaging/install_rules.cmake)
