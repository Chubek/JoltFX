set(JFX_MRUBY_LIBRARY "${PROJECT_SOURCE_DIR}/third_party/mruby/build/jfx/lib/libmruby.a")
add_custom_command(
    OUTPUT "${JFX_MRUBY_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env MRUBY_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb
            ${CMAKE_COMMAND} -E chdir ${PROJECT_SOURCE_DIR}/third_party/mruby ruby minirake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/mruby
    COMMENT "Building the vendored mruby runtime"
)
add_custom_target(jfx_mruby_runtime_build DEPENDS "${JFX_MRUBY_LIBRARY}")
add_library(jfx_mruby_runtime STATIC IMPORTED GLOBAL)
set_target_properties(jfx_mruby_runtime PROPERTIES IMPORTED_LOCATION "${JFX_MRUBY_LIBRARY}")
add_dependencies(jfx_mruby_runtime jfx_mruby_runtime_build)

add_library(jfx_mruby STATIC src/mruby_runtime.c)
target_include_directories(jfx_mruby PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jfx_mruby PRIVATE ${PROJECT_SOURCE_DIR}/third_party/mruby/include)
target_compile_definitions(jfx_mruby PRIVATE MRB_USE_DEBUG_HOOK)
target_link_libraries(jfx_mruby PUBLIC jfx_extif_common PRIVATE jfx_mruby_runtime tilly)
if(NOT MSVC)
    target_link_libraries(jfx_mruby PRIVATE m)
endif()
set_project_warnings(jfx_mruby)
