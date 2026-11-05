# Only the inspected, portable Clipper2 algorithms; no vendor examples, tests,
# source-tree configure_file, Z variant, or implicit dependency downloads.
add_library(jfx_ezdesign_clipper STATIC
    ${PROJECT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp
    ${PROJECT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib/src/clipper.offset.cpp
    ${PROJECT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib/src/clipper.rectclip.cpp)
target_include_directories(jfx_ezdesign_clipper SYSTEM PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib/include>)
set_target_properties(jfx_ezdesign_clipper PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(jfx_ezdesign STATIC model.cpp tools.cpp)
target_include_directories(jfx_ezdesign PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(jfx_ezdesign SYSTEM PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/glm>)
target_include_directories(jfx_ezdesign SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/earcut.hpp/include)
target_link_libraries(jfx_ezdesign PUBLIC tilly PRIVATE jfx_ezdesign_clipper)
set_project_warnings(jfx_ezdesign)
install(TARGETS jfx_ezdesign jfx_ezdesign_clipper EXPORT JoltFXTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${PROJECT_SOURCE_DIR}/third_party/earcut.hpp/LICENSE DESTINATION share/doc/JoltFX/third_party/earcut)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/clipper2/LICENSE DESTINATION share/doc/JoltFX/third_party/clipper2)
