# Allocation-free lib2geom Bernstein/point kernels shared with Ingagi's CGO
# bridge. This deliberately avoids pulling in the upstream GTK/GSL/toy build.
add_library(jfx_animation_geometry STATIC
    "${PROJECT_SOURCE_DIR}/ingagi/animation/geometry_bridge.cc")
target_compile_features(jfx_animation_geometry PRIVATE cxx_std_17)
target_include_directories(jfx_animation_geometry SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/lib2geom/include")
if(JFX_3D_BOOST_ROOT)
    target_include_directories(jfx_animation_geometry SYSTEM PRIVATE "${JFX_3D_BOOST_ROOT}")
endif()
set_target_properties(jfx_animation_geometry PROPERTIES POSITION_INDEPENDENT_CODE ON)
install(TARGETS jfx_animation_geometry EXPORT JoltFXTargets ARCHIVE DESTINATION lib)
install(FILES
    "${PROJECT_SOURCE_DIR}/third_party/lib2geom/LICENSE.md"
    "${PROJECT_SOURCE_DIR}/third_party/lib2geom/COPYING-MPL-1.1"
    "${PROJECT_SOURCE_DIR}/third_party/lib2geom/COPYING-LGPL-2.1"
    DESTINATION share/joltfx/licenses/lib2geom)
