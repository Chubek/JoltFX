set(JFX_QUICKJS_ROOT "${PROJECT_SOURCE_DIR}/third_party/quickjs")
set(JFX_QUICKJS_SOURCES
    ${JFX_QUICKJS_ROOT}/quickjs.c ${JFX_QUICKJS_ROOT}/dtoa.c
    ${JFX_QUICKJS_ROOT}/libregexp.c ${JFX_QUICKJS_ROOT}/libunicode.c
    ${JFX_QUICKJS_ROOT}/cutils.c)
file(STRINGS "${JFX_QUICKJS_ROOT}/VERSION" JFX_QUICKJS_VERSION LIMIT_COUNT 1)
add_library(jfx_quickjs STATIC src/quickjs_runtime.c ${JFX_QUICKJS_SOURCES})
target_include_directories(jfx_quickjs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(jfx_quickjs SYSTEM PRIVATE ${JFX_QUICKJS_ROOT})
target_include_directories(jfx_quickjs PRIVATE ../common/src)
target_compile_definitions(jfx_quickjs PRIVATE _GNU_SOURCE CONFIG_VERSION="${JFX_QUICKJS_VERSION}")
if(NOT MSVC)
    set_source_files_properties(${JFX_QUICKJS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(jfx_quickjs PRIVATE m)
endif()
target_link_libraries(jfx_quickjs PUBLIC jfx_extif_common)
set_project_warnings(jfx_quickjs)
