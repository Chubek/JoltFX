set(JFX_IMGUI_SOURCES
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
)

add_library(jfx_desktop_frontend STATIC
    src/desktop_frontend.cpp
    ${JFX_IMGUI_SOURCES}
)
target_include_directories(jfx_desktop_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jfx_desktop_frontend PRIVATE ${PROJECT_SOURCE_DIR}/third_party/imgui)
target_link_libraries(jfx_desktop_frontend PUBLIC jfx_core)
set_project_warnings(jfx_desktop_frontend)

add_executable(jfx_desktop src/main.cpp)
target_link_libraries(jfx_desktop PRIVATE jfx_desktop_frontend)
set_project_warnings(jfx_desktop)
