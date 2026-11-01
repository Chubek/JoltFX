cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(JoltFXSDK VERSION 1.1.0 LANGUAGES C CXX)
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
foreach(contract IN ITEMS
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
install(DIRECTORY loader/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${JOLTFX_SDK_API_ROOT}/jfx" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${JOLTFX_SDK_GLUE_INCLUDE_DIR}/joltscript"
    "${JOLTFX_SDK_EXECUTION_INCLUDE_DIR}/joltscript" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.h"
    "${JOLTFX_SDK_INGAGI_API_DIR}/Ingagi-Module.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ingagi)
install(FILES "${JOLTFX_SDK_TILLY_INCLUDE_DIR}/tilly/allocator.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tilly)
install(TARGETS jfx_sdk_headers jfx_sdk_loader ${sdk_targets} EXPORT JoltFXSDKTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT JoltFXSDKTargets NAMESPACE JoltFXSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
configure_package_config_file(cmake/JoltFXSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake"
    VERSION 1.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake" cmake/JoltFXSDKModules.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk/docs)
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/joltfx/sdk/examples)
include(cmake/JoltFXSDKModules.cmake)
if(JOLTFX_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS|watchOS|WASI)$")
    joltfx_sdk_add_plugin(jfx_sdk_gain examples/gain/gain.c)
endif()
