set(JFX_MRUBY_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime")
set(JFX_MRUBY_LIBRARY "${JFX_MRUBY_BUILD_DIR}/jfx/lib/libmruby.a")
set(JFX_MRUBY_CFLAGS "-fPIC")
set(JFX_MRUBY_WARNING_FLAGS "")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Upstream's deliberate one-byte strncat in its lexer trips GCC's heuristic.
    set(JFX_MRUBY_WARNING_FLAGS "-Wno-stringop-truncation")
endif()
if(JFX_ASAN)
    string(APPEND JFX_MRUBY_CFLAGS " -fsanitize=address -fno-omit-frame-pointer")
endif()
if(JFX_UBSAN)
    string(APPEND JFX_MRUBY_CFLAGS " -fsanitize=undefined")
endif()
add_custom_command(
    OUTPUT "${JFX_MRUBY_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env MRUBY_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb
            MRUBY_BUILD_DIR=${JFX_MRUBY_BUILD_DIR} CC=${CMAKE_C_COMPILER} AR=${CMAKE_AR}
            "JFX_MRUBY_CFLAGS=${JFX_MRUBY_CFLAGS}"
            "JFX_MRUBY_WARNING_FLAGS=${JFX_MRUBY_WARNING_FLAGS}"
            ${CMAKE_COMMAND} -E chdir ${PROJECT_SOURCE_DIR}/third_party/mruby ruby minirake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/mruby
    COMMENT "Building the vendored mruby runtime"
)
add_custom_target(jfx_mruby_runtime_build DEPENDS "${JFX_MRUBY_LIBRARY}")
include(GNUInstallDirs)
set(JFX_MRUBY_INSTALLED_ARCHIVE "${CMAKE_STATIC_LIBRARY_PREFIX}jfx_mruby_runtime${CMAKE_STATIC_LIBRARY_SUFFIX}")
add_library(jfx_mruby_runtime INTERFACE)
target_link_libraries(jfx_mruby_runtime INTERFACE
    "$<BUILD_INTERFACE:${JFX_MRUBY_LIBRARY}>"
    "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${JFX_MRUBY_INSTALLED_ARCHIVE}>")
add_dependencies(jfx_mruby_runtime jfx_mruby_runtime_build)
install(FILES "${JFX_MRUBY_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RENAME "${JFX_MRUBY_INSTALLED_ARCHIVE}")

add_library(jfx_mruby STATIC src/mruby_runtime.c)
target_include_directories(jfx_mruby PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jfx_mruby SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/mruby/include)
target_include_directories(jfx_mruby PRIVATE ../common/src)
target_compile_definitions(jfx_mruby PRIVATE MRB_USE_DEBUG_HOOK MRB_INT64 MRB_WORDBOX_NO_FLOAT_TRUNCATE)
target_link_libraries(jfx_mruby PUBLIC jfx_extif_common PRIVATE jfx_mruby_runtime tilly)
if(NOT MSVC)
    target_link_libraries(jfx_mruby PRIVATE m)
endif()
set_project_warnings(jfx_mruby)
