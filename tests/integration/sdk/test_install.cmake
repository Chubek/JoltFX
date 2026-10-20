foreach(required SOURCE_DIR TEST_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGV}\n${output}\n${error}")
    endif()
endfunction()
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/JoltFX-SDK" -B "${TEST_DIR}/sdk"
    "-DCMAKE_INSTALL_PREFIX=${TEST_DIR}/original")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/sdk")
run("${CMAKE_COMMAND}" --install "${TEST_DIR}/sdk")
# Reinstall on repeated runs, then relocate before configuring consumers.
file(COPY "${TEST_DIR}/original/" DESTINATION "${TEST_DIR}/relocated")
file(MAKE_DIRECTORY "${TEST_DIR}/consumer")
file(WRITE "${TEST_DIR}/consumer/main.c" "
#include <joltfx/sdk/backend.h>
#include <joltfx/sdk/facet.h>
#include <joltfx/sdk/frontend.h>
#include <joltfx/sdk/gui.h>
#include <joltfx/sdk/libraries.h>
#include <joltfx/sdk/loader.h>
#include <joltfx/sdk/module.h>
#include <joltfx/sdk/plugins.h>
#include <joltfx/sdk/renderer.h>
#include <joltfx/sdk/tools.h>
int main(void) { return (int)jfx_renderer_module_close(0); }
")
file(WRITE "${TEST_DIR}/consumer/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.20)
project(SDKConsumer LANGUAGES C CXX)
find_package(JoltFXSDK 1 REQUIRED CONFIG COMPONENTS renderer loader plugins)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE JoltFXSDK::loader)
set_target_properties(consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
configure_file(main.c main.cpp COPYONLY)
add_executable(consumer_cpp \"\${CMAKE_CURRENT_BINARY_DIR}/main.cpp\")
target_link_libraries(consumer_cpp PRIVATE JoltFXSDK::loader)
set_target_properties(consumer_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
")
run("${CMAKE_COMMAND}" -S "${TEST_DIR}/consumer" -B "${TEST_DIR}/consumer-build"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/consumer-build")
run("${TEST_DIR}/consumer-build/consumer")
run("${TEST_DIR}/consumer-build/consumer_cpp")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/JoltFX-SDK/examples/gain" -B "${TEST_DIR}/gain"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/gain")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/plugins/JoltRenderer" -B "${TEST_DIR}/renderer"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated" -DJFX_JOLT_RENDERER_SIMD=OFF)
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/renderer")
