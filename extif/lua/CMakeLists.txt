set(JFX_LUA_SOURCES
    ${PROJECT_SOURCE_DIR}/third_party/lua/lapi.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lauxlib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lbaselib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lcode.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lcorolib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lctype.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ldebug.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ldo.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ldump.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lfunc.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lgc.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/llex.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lmathlib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lmem.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lobject.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lopcodes.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lparser.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lstate.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lstring.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lstrlib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ltable.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ltablib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/ltm.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lundump.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lutf8lib.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lvm.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/lzio.c
    ${PROJECT_SOURCE_DIR}/third_party/lua/loadlib.c
)

add_library(jfx_lua STATIC src/lua_runtime.c ${JFX_LUA_SOURCES})

# The vendored Lua sources are compiled here but are not our code, and upstream
# is not clean under this project's warning set: its jump-table macro takes the
# address of a label, it formats through non-literals, and its collector
# dereferences pointers it has just proved may be null. Our adapter
# (src/lua_runtime.c) still gets the full set, where a real null dereference
# would be worth seeing.
set_source_files_properties(${JFX_LUA_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-pedantic;-Wno-format-nonliteral;-Wno-null-dereference")
target_include_directories(jfx_lua PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jfx_lua PRIVATE ${PROJECT_SOURCE_DIR}/third_party/lua)
target_link_libraries(jfx_lua PUBLIC jfx_extif_common PRIVATE tilly)
if(NOT MSVC)
    target_link_libraries(jfx_lua PRIVATE m ${CMAKE_DL_LIBS})
endif()
set_project_warnings(jfx_lua)
