cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(JoltFXSDK VERSION 1.2.0 LANGUAGES C CXX)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(JOLTFX_SDK_API_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src/include" CACHE PATH "JoltFX public header tree")
if(NOT EXISTS "${JOLTFX_SDK_API_ROOT}/jfx/jfx_plugin_sdk.h")
    message(FATAL_ERROR "Set JOLTFX_SDK_API_ROOT to JoltFX's public include directory")
endif()
set(JOLTFX_SDK_GLUE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../joltscript/layers/glue/include" CACHE PATH "Joltscript Glue public include directory")
set(JOLTFX_SDK_EXECUTION_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../joltscript/layers/execution/include" CACHE PATH "Joltscript Execution public include directory")
set(JOLTFX_SDK_INGAGI_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ingagi/native_extension_api" CACHE PATH "Ingagi native embedding headers")
set(JOLTFX_SDK_TILLY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tilly/include" CACHE PATH "Tilly public include directory")
set(JOLTFX_SDK_OPENFX_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openfx/include" CACHE PATH "OpenFX canonical headers")
foreach(contract IN ITEMS
        "${JOLTFX_SDK_OPENFX_INCLUDE_DIR}/ofxImageEffect.h"
        "${JOLTFX_SDK_TILLY_INCLUDE_DIR}/tilly/allocator.h"
        "${JOLTFX_SDK_GLUE_INCLUDE_DIR}/joltscript/compiler.h"
        "${JOLTFX_SDK_EXECUTION_INCLUDE_DIR}/joltscript/value.h"
        "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.h"
        "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.hpp")
    if(NOT EXISTS "${contract}")
        message(FATAL_ERROR "Missing SDK contract: ${contract}. Set the JOLTFX_SDK_* header roots; see README.md.")
    endif()
endforeach()
# Preserve the canonical Ingagi headers and their installed include spelling in
# build-tree consumers too. configure_file tracks changes to the source headers.
foreach(header Ingagi-Module.h Ingagi-Module.hpp)
    configure_file("${JOLTFX_SDK_INGAGI_API_DIR}/${header}"
        "${CMAKE_CURRENT_BINARY_DIR}/include/ingagi/${header}" COPYONLY)
endforeach()
add_library(jfx_sdk_headers INTERFACE)
set_target_properties(jfx_sdk_headers PROPERTIES EXPORT_NAME headers)
add_library(JoltFXSDK::headers ALIAS jfx_sdk_headers)
target_include_directories(jfx_sdk_headers INTERFACE
    $<BUILD_INTERFACE:${JOLTFX_SDK_API_ROOT}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set(sdk_facets audio backend events facet frontend gui ingagi joltscript libraries
    module plugins renderer runtime timeline tools)
foreach(facet IN LISTS sdk_facets)
    add_library(jfx_sdk_${facet} INTERFACE)
    set_target_properties(jfx_sdk_${facet} PROPERTIES EXPORT_NAME ${facet})
    add_library(JoltFXSDK::${facet} ALIAS jfx_sdk_${facet})
    target_link_libraries(jfx_sdk_${facet} INTERFACE jfx_sdk_headers)
    target_include_directories(jfx_sdk_${facet} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${facet}/include>)
    install(DIRECTORY ${facet}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    list(APPEND sdk_targets jfx_sdk_${facet})
endforeach()
target_include_directories(jfx_sdk_ingagi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
target_include_directories(jfx_sdk_joltscript INTERFACE
    $<BUILD_INTERFACE:${JOLTFX_SDK_GLUE_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${JOLTFX_SDK_EXECUTION_INCLUDE_DIR}>)
target_compile_features(jfx_sdk_ingagi INTERFACE c_std_11)
target_include_directories(jfx_sdk_runtime INTERFACE
    $<BUILD_INTERFACE:${JOLTFX_SDK_TILLY_INCLUDE_DIR}>)
add_library(jfx_sdk_loader STATIC loader/src/renderer_loader.c)
set_target_properties(jfx_sdk_loader PROPERTIES EXPORT_NAME loader POSITION_INDEPENDENT_CODE ON)
add_library(JoltFXSDK::loader ALIAS jfx_sdk_loader)
target_link_libraries(jfx_sdk_loader PUBLIC jfx_sdk_renderer PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(jfx_sdk_loader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/loader/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(jfx_sdk_loader PRIVATE c_std_11)
add_library(jfx_sdk_openfx STATIC openfx/src/ofx_host.c)
set_target_properties(jfx_sdk_openfx PROPERTIES EXPORT_NAME openfx POSITION_INDEPENDENT_CODE ON)
add_library(JoltFXSDK::openfx ALIAS jfx_sdk_openfx)
target_link_libraries(jfx_sdk_openfx PUBLIC jfx_sdk_headers PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(jfx_sdk_openfx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/openfx/include>
    $<BUILD_INTERFACE:${JOLTFX_SDK_OPENFX_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openfx>)
target_compile_features(jfx_sdk_openfx PRIVATE c_std_11)
install(DIRECTORY openfx/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${JOLTFX_SDK_OPENFX_INCLUDE_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openfx
    FILES_MATCHING PATTERN "*.h")
install(FILES openfx/LICENSE.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk/openfx)
option(JOLTFX_SDK_BUILD_TESTS "Build standalone SDK tests" OFF)
if(JOLTFX_SDK_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
    enable_testing()
    add_library(jfx_sdk_ofx_fixture MODULE openfx/tests/fixture.c)
    target_include_directories(jfx_sdk_ofx_fixture PRIVATE "${JOLTFX_SDK_OPENFX_INCLUDE_DIR}")
    add_executable(jfx_sdk_ofx_inspect_test openfx/tests/inspect.c)
    target_link_libraries(jfx_sdk_ofx_inspect_test PRIVATE jfx_sdk_openfx)
    if(MSVC)
        target_compile_options(jfx_sdk_ofx_inspect_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(jfx_sdk_ofx_inspect_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME sdk_openfx_inspect COMMAND jfx_sdk_ofx_inspect_test $<TARGET_FILE:jfx_sdk_ofx_fixture>)
    foreach(mode RANGE 0 2)
        add_library(jfx_sdk_peek_fixture_${mode} MODULE loader/tests/fixture.c)
        target_link_libraries(jfx_sdk_peek_fixture_${mode} PRIVATE jfx_sdk_loader)
        target_compile_definitions(jfx_sdk_peek_fixture_${mode} PRIVATE FIXTURE_MODE=${mode})
    endforeach()
    add_executable(jfx_sdk_peek_test loader/tests/peek.c)
    target_link_libraries(jfx_sdk_peek_test PRIVATE jfx_sdk_loader)
    if(MSVC)
        target_compile_options(jfx_sdk_peek_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(jfx_sdk_peek_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME sdk_loader_peek COMMAND jfx_sdk_peek_test
        $<TARGET_FILE:jfx_sdk_peek_fixture_0> $<TARGET_FILE:jfx_sdk_peek_fixture_1>
        $<TARGET_FILE:jfx_sdk_peek_fixture_2> $<TARGET_FILE:jfx_sdk_ofx_fixture>)
endif()
install(DIRECTORY loader/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${JOLTFX_SDK_API_ROOT}/jfx" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${JOLTFX_SDK_GLUE_INCLUDE_DIR}/joltscript"
    "${JOLTFX_SDK_EXECUTION_INCLUDE_DIR}/joltscript" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.h"
    "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ingagi)
install(FILES "${JOLTFX_SDK_TILLY_INCLUDE_DIR}/tilly/allocator.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tilly)
install(TARGETS jfx_sdk_headers jfx_sdk_loader jfx_sdk_openfx ${sdk_targets} EXPORT JoltFXSDKTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT JoltFXSDKTargets NAMESPACE JoltFXSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
configure_package_config_file(cmake/JoltFXSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake"
    VERSION 1.2.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake" cmake/JoltFXSDKModules.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
install(FILES README.md FUTURE_OF_SDK.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk/docs)
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/joltfx/sdk/examples)
include(cmake/JoltFXSDKModules.cmake)
if(JOLTFX_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS|watchOS|WASI)$")
    joltfx_sdk_add_plugin(jfx_sdk_gain examples/gain/gain.c)
endif()
