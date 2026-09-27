set(JFX_IMGUI_DIR "${PROJECT_SOURCE_DIR}/third_party/imgui")

set(JFX_IMGUI_SOURCES
    ${JFX_IMGUI_DIR}/imgui.cpp
    ${JFX_IMGUI_DIR}/imgui_draw.cpp
    ${JFX_IMGUI_DIR}/imgui_tables.cpp
    ${JFX_IMGUI_DIR}/imgui_widgets.cpp
)

# The windowed path needs SDL2 for the window and input, and the ImGui SDL2 and
# OpenGL3 backends to actually rasterize the composed draw data. Both are
# optional: without them the frontend still builds and runs headless, so a
# machine with no display toolchain can still build and test.
option(JFX_DESKTOP_WINDOW "Build the JoltFX desktop window (needs SDL2 and OpenGL)" ON)

set(JFX_DESKTOP_WINDOW_ENABLED OFF)
if(JFX_DESKTOP_WINDOW)
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        # Debian/Ubuntu ship SDL2's CMake config in a non-default prefix.
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 QUIET sdl2)
        endif()
    endif()
    if(SDL2_FOUND OR SDL2_VERSION)
        set(JFX_DESKTOP_WINDOW_ENABLED ON)
        list(APPEND JFX_IMGUI_SOURCES
            ${JFX_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
            ${JFX_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )
        message(STATUS "Desktop window: SDL2 ${SDL2_VERSION} (or pkg-config) found")
    else()
        message(STATUS "Desktop window: SDL2 not found, building the headless frontend only")
    endif()
endif()

# Publish the result so sibling directories (notably tests/) can register a
# windowed integration test only when one is actually buildable.
set_property(GLOBAL PROPERTY JFX_DESKTOP_WINDOW_ENABLED ${JFX_DESKTOP_WINDOW_ENABLED})

add_library(jfx_desktop_frontend STATIC
    src/desktop_frontend.cpp
    src/host_window.cpp
    ${JFX_IMGUI_SOURCES}
)
target_include_directories(jfx_desktop_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jfx_desktop_frontend PRIVATE ${JFX_IMGUI_DIR})
target_link_libraries(jfx_desktop_frontend PUBLIC jfx_core jolt_effects)
set_project_warnings(jfx_desktop_frontend)

# The vendored ImGui sources are compiled as part of this target but are not our
# code, so they are held to the warning set they ship with rather than to ours.
# Upstream is not clean under -Wconversion and we are not going to fork it to
# become so; our own sources still get the full set.
set_source_files_properties(${JFX_IMGUI_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-format-nonliteral;-Wno-null-dereference")

if(JFX_DESKTOP_WINDOW_ENABLED)
    if(SDL2_FOUND)
        target_link_libraries(jfx_desktop_frontend PRIVATE SDL2::SDL2 SDL2::SDL2main)
    else()
        target_include_directories(jfx_desktop_frontend PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_directories(jfx_desktop_frontend PRIVATE ${SDL2_LIBRARY_DIRS})
        target_link_libraries(jfx_desktop_frontend PRIVATE ${SDL2_LIBRARIES})
        target_link_options(jfx_desktop_frontend PRIVATE ${SDL2_LDFLAGS})
    endif()
    if(WIN32)
        target_link_libraries(jfx_desktop_frontend PRIVATE opengl32)
    else()
        find_package(OpenGL REQUIRED COMPONENTS OpenGL)
        target_link_libraries(jfx_desktop_frontend PRIVATE OpenGL::GL)
    endif()
    target_compile_definitions(jfx_desktop_frontend PRIVATE JFX_DESKTOP_HAVE_SDL2=1)
endif()

add_executable(jfx_desktop src/main.cpp)
target_link_libraries(jfx_desktop PRIVATE jfx_desktop_frontend)
set_project_warnings(jfx_desktop)
