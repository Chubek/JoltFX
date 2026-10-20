cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(JoltFXSDK VERSION 1.0.0 LANGUAGES C CXX)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(JOLTFX_SDK_API_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src/include" CACHE PATH "JoltFX public header tree")
if(NOT EXISTS "${JOLTFX_SDK_API_ROOT}/jfx/jfx_plugin_sdk.h")
    message(FATAL_ERROR "Set JOLTFX_SDK_API_ROOT to JoltFX's public include directory")
endif()
add_library(jfx_sdk_headers INTERFACE)
set_target_properties(jfx_sdk_headers PROPERTIES EXPORT_NAME headers)
add_library(JoltFXSDK::headers ALIAS jfx_sdk_headers)
target_include_directories(jfx_sdk_headers INTERFACE
    $<BUILD_INTERFACE:${JOLTFX_SDK_API_ROOT}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
foreach(facet IN ITEMS backend facet frontend gui libraries module plugins renderer tools)
    add_library(jfx_sdk_${facet} INTERFACE)
    set_target_properties(jfx_sdk_${facet} PROPERTIES EXPORT_NAME ${facet})
    add_library(JoltFXSDK::${facet} ALIAS jfx_sdk_${facet})
    target_link_libraries(jfx_sdk_${facet} INTERFACE jfx_sdk_headers)
    target_include_directories(jfx_sdk_${facet} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${facet}/include>)
    install(DIRECTORY ${facet}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    list(APPEND sdk_targets jfx_sdk_${facet})
endforeach()
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
install(TARGETS jfx_sdk_headers jfx_sdk_loader ${sdk_targets} EXPORT JoltFXSDKTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT JoltFXSDKTargets NAMESPACE JoltFXSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
configure_package_config_file(cmake/JoltFXSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake"
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXSDKConfigVersion.cmake" cmake/JoltFXSDKModules.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXSDK)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk)
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/joltfx/sdk/examples)
include(cmake/JoltFXSDKModules.cmake)
if(JOLTFX_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS|watchOS|WASI)$")
    joltfx_sdk_add_plugin(jfx_sdk_gain examples/gain/gain.c)
endif()
