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
    "-DCMAKE_INSTALL_PREFIX=${TEST_DIR}/original" -DJOLTFX_SDK_BUILD_TESTS=ON)
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/sdk")
run("${CMAKE_CTEST_COMMAND}" --test-dir "${TEST_DIR}/sdk" --output-on-failure)
run("${CMAKE_COMMAND}" --install "${TEST_DIR}/sdk")
# Reinstall on repeated runs, then relocate before configuring consumers.
file(COPY "${TEST_DIR}/original/" DESTINATION "${TEST_DIR}/relocated")
file(REMOVE_RECURSE "${TEST_DIR}/original")
file(MAKE_DIRECTORY "${TEST_DIR}/consumer")
file(WRITE "${TEST_DIR}/consumer/main.c" "
#include <joltfx/sdk/backend.h>
#include <joltfx/sdk/audio.h>
#include <joltfx/sdk/events.h>
#include <joltfx/sdk/ingagi.h>
#include <joltfx/sdk/joltscript.h>
#include <joltfx/sdk/runtime.h>
#include <joltfx/sdk/timeline.h>
#include <joltfx/sdk/facet.h>
#include <joltfx/sdk/frontend.h>
#include <joltfx/sdk/gui.h>
#include <joltfx/sdk/libraries.h>
#include <joltfx/sdk/loader.h>
#include <joltfx/sdk/openfx.h>
#include <ofxImageEffect.h>
#include <joltfx/sdk/module.h>
#include <joltfx/sdk/plugins.h>
#include <joltfx/sdk/renderer.h>
#include <joltfx/sdk/tools.h>
int main(void) { return (int)jfx_renderer_module_close(0) + (int)jfx_ofx_host_close(0); }
")
file(WRITE "${TEST_DIR}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(SDKConsumer LANGUAGES C CXX)
if(SDK_SOURCE_DIR)
    add_subdirectory("${SDK_SOURCE_DIR}" sdk)
else()
    find_package(JoltFXSDK 1.2 REQUIRED CONFIG COMPONENTS
        headers audio backend events facet frontend gui ingagi joltscript libraries
        loader openfx module plugins renderer runtime timeline tools)
endif()
foreach(facet audio backend events facet frontend gui ingagi joltscript libraries
        loader openfx module plugins renderer runtime timeline tools)
    list(APPEND facets JoltFXSDK::${facet})
    foreach(language c cpp)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${facet}.${language}"
            "#include <joltfx/sdk/${facet}.h>\n")
        add_library(probe_${facet}_${language} OBJECT "${CMAKE_CURRENT_BINARY_DIR}/${facet}.${language}")
        target_link_libraries(probe_${facet}_${language} PRIVATE JoltFXSDK::${facet})
        set_target_properties(probe_${facet}_${language} PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    endforeach()
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ingagi_cpp.cpp" "#include <ingagi/Ingagi-Module.hpp>\n")
add_library(ingagi_cpp OBJECT "${CMAKE_CURRENT_BINARY_DIR}/ingagi_cpp.cpp")
target_link_libraries(ingagi_cpp PRIVATE JoltFXSDK::ingagi)
target_compile_features(ingagi_cpp PRIVATE cxx_std_17)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE ${facets})
set_target_properties(consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
configure_file(main.c main.cpp COPYONLY)
add_executable(consumer_cpp "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
target_link_libraries(consumer_cpp PRIVATE ${facets})
set_target_properties(consumer_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
]=])
run("${CMAKE_COMMAND}" -S "${TEST_DIR}/consumer" -B "${TEST_DIR}/consumer-build"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/consumer-build")
run("${TEST_DIR}/consumer-build/consumer")
run("${TEST_DIR}/consumer-build/consumer_cpp")
# Exercise BUILD_INTERFACE paths separately; installed include roots must not
# hide missing transitive requirements in an add_subdirectory consumer.
run("${CMAKE_COMMAND}" -S "${TEST_DIR}/consumer" -B "${TEST_DIR}/buildtree-consumer"
    "-DSDK_SOURCE_DIR=${SOURCE_DIR}/JoltFX-SDK")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/buildtree-consumer")
run("${TEST_DIR}/buildtree-consumer/consumer")
run("${TEST_DIR}/buildtree-consumer/consumer_cpp")
# Installed contracts must be exact copies of their canonical definitions.
foreach(pair
    "third_party/openfx/include/ofxImageEffect.h|openfx/ofxImageEffect.h"
    "ingagi/native_extension_api/Ingagi-Module.h|ingagi/Ingagi-Module.h"
    "ingagi/native_extension_api/Ingagi-Module.hpp|ingagi/Ingagi-Module.hpp"
    "joltscript/layers/glue/include/joltscript/compiler.h|joltscript/compiler.h"
    "joltscript/layers/execution/include/joltscript/value.h|joltscript/value.h")
    string(REPLACE "|" ";" paths "${pair}")
    list(GET paths 0 source)
    list(GET paths 1 installed)
    run("${CMAKE_COMMAND}" -E compare_files "${SOURCE_DIR}/${source}"
        "${TEST_DIR}/relocated/include/${installed}")
endforeach()
foreach(guide facets ingagi joltscript openfx)
    if(NOT EXISTS "${TEST_DIR}/relocated/share/doc/JoltFXSDK/sdk/docs/${guide}.md")
        message(FATAL_ERROR "Missing installed SDK guide: ${guide}")
    endif()
endforeach()
run("${CMAKE_COMMAND}" -E compare_files "${SOURCE_DIR}/third_party/openfx/LICENSE.md"
    "${TEST_DIR}/relocated/share/doc/JoltFXSDK/sdk/openfx/LICENSE.md")
# Detached SDK sources must also package successfully from installed contracts.
file(COPY "${SOURCE_DIR}/JoltFX-SDK/" DESTINATION "${TEST_DIR}/detached-source")
run("${CMAKE_COMMAND}" -S "${TEST_DIR}/detached-source" -B "${TEST_DIR}/detached-build"
    "-DJOLTFX_SDK_API_ROOT=${TEST_DIR}/relocated/include"
    "-DJOLTFX_SDK_GLUE_INCLUDE_DIR=${TEST_DIR}/relocated/include"
    "-DJOLTFX_SDK_EXECUTION_INCLUDE_DIR=${TEST_DIR}/relocated/include"
    "-DJOLTFX_SDK_INGAGI_API_DIR=${TEST_DIR}/relocated/include/ingagi"
    "-DJOLTFX_SDK_TILLY_INCLUDE_DIR=${TEST_DIR}/relocated/include"
    "-DJOLTFX_SDK_OPENFX_INCLUDE_DIR=${TEST_DIR}/relocated/include/openfx")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/detached-build")
run("${CMAKE_COMMAND}" --install "${TEST_DIR}/detached-build" --prefix "${TEST_DIR}/detached-install")
file(MAKE_DIRECTORY "${TEST_DIR}/unknown")
file(WRITE "${TEST_DIR}/unknown/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.20)
project(UnknownComponent NONE)
find_package(JoltFXSDK 1.1 REQUIRED CONFIG COMPONENTS nonexistent)
")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${TEST_DIR}/unknown" -B "${TEST_DIR}/unknown-build"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated" RESULT_VARIABLE unknown_result
    OUTPUT_QUIET ERROR_QUIET)
if(unknown_result EQUAL 0)
    message(FATAL_ERROR "Unknown required SDK component was accepted")
endif()
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/JoltFX-SDK/examples/gain" -B "${TEST_DIR}/gain"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/gain")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/plugins/JoltRenderer" -B "${TEST_DIR}/renderer"
    "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated" -DJFX_JOLT_RENDERER_SIMD=OFF)
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/renderer")
