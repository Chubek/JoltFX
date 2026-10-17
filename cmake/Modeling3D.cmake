# Geometry libraries stay behind the engine's C boundary. Only VTK's math/core
# module is needed; do not build its rendering/windowing or language wrappers.
set(JFX_3D_EIGEN_ROOT "" CACHE PATH "Eigen >=3.4.1 headers (offline override)")
set(JFX_3D_BOOST_ROOT "" CACHE PATH "Standalone Boost >=1.74 headers for native/cross builds")
if(JFX_3D_BOOST_ROOT)
    # Never add a host /usr/include to an NDK/Emscripten compile: its libc
    # headers would shadow the target sysroot. Use an unpacked Boost tree.
    file(STRINGS "${JFX_3D_BOOST_ROOT}/boost/version.hpp" jfx_boost_version
        REGEX "^#define BOOST_VERSION [0-9]+$")
    string(REGEX REPLACE ".*BOOST_VERSION ([0-9]+).*" "\\1" jfx_boost_version "${jfx_boost_version}")
    if(NOT jfx_boost_version MATCHES "^[0-9]+$" OR jfx_boost_version LESS 107400)
        message(FATAL_ERROR "JFX_3D_BOOST_ROOT must contain Boost >=1.74 headers")
    endif()
    set(Boost_INCLUDE_DIRS "${JFX_3D_BOOST_ROOT}")
else()
    find_package(Boost 1.74 REQUIRED)
endif()
if(NOT JFX_3D_EIGEN_ROOT)
    # This checkout's Eigen predates libigl's reshaped/indexing API. Pin a
    # compatible header dependency; callers can supply it for offline builds.
    include(FetchContent)
    FetchContent_Declare(jfx_3d_eigen
        URL https://codeload.github.com/eigen-mirror/eigen/tar.gz/refs/tags/3.4.1
        URL_HASH SHA256=b93c667d1b69265cdb4d9f30ec21f8facbbe8b307cf34c0b9942834c6d4fdbe2)
    FetchContent_GetProperties(jfx_3d_eigen)
    if(NOT jfx_3d_eigen_POPULATED)
        FetchContent_Populate(jfx_3d_eigen)
    endif()
    set(JFX_3D_EIGEN_ROOT "${jfx_3d_eigen_SOURCE_DIR}")
endif()
set(VTK_BUILD_TESTING OFF CACHE STRING "" FORCE)
set(VTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VTK_BUILD_ALL_MODULES OFF CACHE BOOL "" FORCE)
set(VTK_GROUP_ENABLE_StandAlone DONT_WANT CACHE STRING "" FORCE)
set(VTK_GROUP_ENABLE_Rendering DONT_WANT CACHE STRING "" FORCE)
set(VTK_GROUP_ENABLE_Imaging DONT_WANT CACHE STRING "" FORCE)
set(VTK_GROUP_ENABLE_Web DONT_WANT CACHE STRING "" FORCE)
set(VTK_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
set(VTK_WRAP_JAVA OFF CACHE BOOL "" FORCE)
set(VTK_ENABLE_WRAPPING OFF CACHE BOOL "" FORCE)
set(VTK_ENABLE_REMOTE_MODULES OFF CACHE BOOL "" FORCE)
set(VTK_MODULE_ENABLE_VTK_CommonCore YES CACHE STRING "" FORCE)
set(VTK_SMP_IMPLEMENTATION_TYPE Sequential CACHE STRING "" FORCE)
set(VTK_USE_PCH OFF CACHE BOOL "" FORCE)
set(VTK_VERSIONED_INSTALL ON CACHE BOOL "" FORCE)
# vtk_module_build otherwise inherits the parent project's name for export files,
# while vtk-config.cmake expects VTK-targets.cmake / VTK-vtk-module-properties.cmake.
include(GNUInstallDirs)
include("${PROJECT_SOURCE_DIR}/third_party/VTK/CMake/vtkVersion.cmake")
set(vtk_cmake_destination "${CMAKE_INSTALL_LIBDIR}/cmake/vtk-${VTK_MAJOR_VERSION}.${VTK_MINOR_VERSION}")
set(vtk_target_package PACKAGE VTK)
set(BUILD_SHARED_LIBS OFF)
set(jfx_saved_cxx_debug_flags "${CMAKE_CXX_FLAGS_DEBUG}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # VTK's fully instantiated template debug data can add hundreds of MiB to
    # every static consumer. Keep engine line tables, omit vendor debug data.
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g0")
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/VTK" "${PROJECT_BINARY_DIR}/third_party/vtk" EXCLUDE_FROM_ALL)
# The module's own export includes its private dependencies. Install the selected
# subset even though it is excluded from unrelated default/install traversal.
install(SCRIPT "${PROJECT_BINARY_DIR}/third_party/vtk/cmake_install.cmake")
install(FILES "${JFX_3D_EIGEN_ROOT}/COPYING.MPL2" DESTINATION share/doc/JoltFX/third_party/eigen)

# Build Bullet's portable CPU dynamics only.
set(BULLET_PHYSICS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/bullet3")
file(READ "${BULLET_PHYSICS_SOURCE_DIR}/VERSION" BULLET_VERSION)
string(STRIP "${BULLET_VERSION}" BULLET_VERSION)
set(INSTALL_LIBS OFF)
foreach(part LinearMath BulletCollision BulletDynamics)
    add_subdirectory("${BULLET_PHYSICS_SOURCE_DIR}/src/${part}" "${PROJECT_BINARY_DIR}/third_party/bullet/${part}" EXCLUDE_FROM_ALL)
    set_target_properties(${part} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
set(CMAKE_CXX_FLAGS_DEBUG "${jfx_saved_cxx_debug_flags}")
target_link_libraries(BulletCollision PUBLIC LinearMath)
target_link_libraries(BulletDynamics PUBLIC BulletCollision LinearMath)
target_sources(jfx_core PRIVATE src/modeling3d.cpp src/osg_viewport.cpp src/iap.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/modeling3d.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g1>")
endif()
# OpenSceneGraph blend (opt-in). The bridge compiles against GLM by default.
# With JFX_USE_OSG=ON the vendored OSG 3.7.0 core library is configured and
# built in isolation (ExternalProject, so its `uninstall` target and old
# CMake floor never collide with this build) and jfx_core links the real
# libosg: the trackball then runs osg::Quat and jfx_osg_have_osg() reports
# 1. Only the `osg` (+ OpenThreads) core targets are ever built: no viewers,
# plugins or applications. Costs nothing when OFF.
option(JFX_USE_OSG "Blend third_party/openscenegraph headers into the 3D viewport bridge" ON)
if(JFX_USE_OSG)
    include(ExternalProject)
    cmake_host_system_information(RESULT jfx_osg_jobs QUERY NUMBER_OF_LOGICAL_CORES)
    ExternalProject_Add(jfx_osg_vendored
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/openscenegraph"
        BINARY_DIR "${PROJECT_BINARY_DIR}/third_party/osg-vendored"
        INSTALL_DIR "${PROJECT_BINARY_DIR}/third_party/osg-vendored/install"
        CMAKE_ARGS
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_OSG_APPLICATIONS=OFF
            -DBUILD_OSG_EXAMPLES=OFF
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
            --target osg OpenThreads --config Release -j ${jfx_osg_jobs}
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS
            "<BINARY_DIR>/lib/libosg.so"
            "<BINARY_DIR>/lib/libOpenThreads.so"
        EXCLUDE_FROM_ALL
    )
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/third_party/osg-vendored/include")
    add_library(jfx_osg_core SHARED IMPORTED GLOBAL)
    set_target_properties(jfx_osg_core PROPERTIES
        IMPORTED_LOCATION
            "${PROJECT_BINARY_DIR}/third_party/osg-vendored/lib/libosg.so"
        INTERFACE_INCLUDE_DIRECTORIES
            "${PROJECT_SOURCE_DIR}/third_party/openscenegraph/include;${PROJECT_BINARY_DIR}/third_party/osg-vendored/include")
    add_dependencies(jfx_osg_core jfx_osg_vendored)
    target_include_directories(jfx_core SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/openscenegraph/include"
        "${PROJECT_BINARY_DIR}/third_party/osg-vendored/include")
    target_link_libraries(jfx_core PRIVATE jfx_osg_core)
    target_compile_definitions(jfx_core PUBLIC JFX_HAVE_OSG=1)
    set_target_properties(jfx_core PROPERTIES
        BUILD_RPATH "${PROJECT_BINARY_DIR}/third_party/osg-vendored/lib"
        INSTALL_RPATH "${PROJECT_BINARY_DIR}/third_party/osg-vendored/lib")
    message(STATUS "3D viewport: OpenSceneGraph libosg enabled (JFX_HAVE_OSG)")
else()
    message(STATUS "3D viewport: OpenSceneGraph fallback (GLM trackball, no OSG link)")
endif()
target_include_directories(jfx_core SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/glm"
    ${Boost_INCLUDE_DIRS}
    "${JFX_3D_EIGEN_ROOT}"
    "${BULLET_PHYSICS_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/libigl/include"
    "${PROJECT_SOURCE_DIR}/third_party/tinyply/source")
# The Vulkan offscreen scene renderer (jfx_scene3d_render_gpu) calls the
# backend directly; the header is only visible when that backend is built.
if(JFX_BACKEND_VULKAN)
    target_include_directories(jfx_core PRIVATE
        "${PROJECT_SOURCE_DIR}/backends/vulkan/include")
endif()
# CGAL's checkout is split into packages, each with its own include directory.
file(GLOB cgal_packages "${PROJECT_SOURCE_DIR}/third_party/cgal/*/include")
target_include_directories(jfx_core SYSTEM PRIVATE ${cgal_packages})
target_compile_definitions(jfx_core PRIVATE CGAL_DISABLE_GMP=1 CGAL_HEADER_ONLY=1 IGL_PARALLEL_FOR_FORCE_SERIAL=1)
set_property(TARGET jfx_core PROPERTY NO_SYSTEM_FROM_IMPORTED OFF)
target_link_libraries(jfx_core PRIVATE BulletDynamics BulletCollision LinearMath VTK::CommonCore)
# This VTK revision also configures CommonDataModel. Its exported package needs
# those archives for SDK installation, but application targets do not link them.
# Keep them in the default build without forcing web/Android app-only builds to
# compile an otherwise unused VTK module (or its host code-generation tools).
add_custom_target(jfx_scene_install_deps ALL DEPENDS vtkCommonDataModel)
get_target_property(vtk_math_includes VTK::CommonCore INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(jfx_core SYSTEM PRIVATE ${vtk_math_includes})
