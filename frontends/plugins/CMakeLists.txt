add_library(jfx_host_plugin_common STATIC src/host_plugin.c)
target_include_directories(jfx_host_plugin_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(jfx_host_plugin_common PUBLIC jfx_core)
set_project_warnings(jfx_host_plugin_common)

add_library(jfx_ae_plugin STATIC after_effects/src/after_effects.c)
target_link_libraries(jfx_ae_plugin PUBLIC jfx_host_plugin_common)
set_project_warnings(jfx_ae_plugin)

add_library(jfx_premiere_plugin STATIC premiere/src/premiere.c)
target_link_libraries(jfx_premiere_plugin PUBLIC jfx_host_plugin_common)
set_project_warnings(jfx_premiere_plugin)

add_library(jfx_davinci_plugin STATIC davinci/src/davinci.c)
target_link_libraries(jfx_davinci_plugin PUBLIC jfx_host_plugin_common)
set_project_warnings(jfx_davinci_plugin)
