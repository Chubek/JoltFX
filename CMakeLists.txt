cmake_minimum_required(VERSION 3.20)
project(JoltFX VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

option(JOLTFX_BUILD_TESTS "Build tests" ON)
option(JOLTFX_BUILD_EXAMPLES "Build examples" ON)
option(JOLTFX_BUILD_DOCS "Build documentation" OFF)

option(JFX_ASAN "Enable AddressSanitizer" OFF)
option(JFX_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
if(JFX_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(JFX_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()
include(cmake/modules/CompilerWarnings.cmake)

add_subdirectory(tilly)
add_subdirectory(joltscript)
add_subdirectory(backends)
add_subdirectory(core)
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

include(cmake/packaging/install_rules.cmake)
