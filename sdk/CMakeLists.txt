include(GNUInstallDirs)
add_library(jfx_plugin_sdk INTERFACE)
add_library(JoltFX::plugin_sdk ALIAS jfx_plugin_sdk)
set_target_properties(jfx_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)
target_include_directories(jfx_plugin_sdk INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/mograph/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/JoltFXPlugin.cmake")

if(JOLTFX_BUILD_EXAMPLES AND NOT EMSCRIPTEN)
    jfx_add_plugin(jfx_example_plugin SOURCES examples/tint/tint.c)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/JoltFXPluginSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXPluginSDKConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXPluginSDK")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/JoltFXPluginSDKConfigVersion.cmake"
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(TARGETS jfx_plugin_sdk EXPORT JoltFXPluginSDKTargets)
install(EXPORT JoltFXPluginSDKTargets NAMESPACE JoltFX::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXPluginSDK")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/JoltFXPluginSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/JoltFXPluginSDKConfigVersion.cmake" cmake/JoltFXPlugin.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/JoltFXPluginSDK")
install(DIRECTORY examples/tint DESTINATION "${CMAKE_INSTALL_DATADIR}/joltfx/plugin-sdk/examples")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/joltfx/plugin-sdk")
