# Geometry libraries stay behind the engine's C boundary. Only VTK's math/core
# module is needed; do not build its rendering/windowing or language wrappers.
set(JFX_3D_EIGEN_ROOT "" CACHE PATH "Eigen >=3.4.1 headers (offline override)")
find_package(Boost 1.74 REQUIRED)
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
target_sources(jfx_core PRIVATE src/modeling3d.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/modeling3d.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g1>")
endif()
target_include_directories(jfx_core SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/glm"
    ${Boost_INCLUDE_DIRS}
    "${JFX_3D_EIGEN_ROOT}"
    "${BULLET_PHYSICS_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/libigl/include"
    "${PROJECT_SOURCE_DIR}/third_party/tinyply/source")
# CGAL's checkout is split into packages, each with its own include directory.
file(GLOB cgal_packages "${PROJECT_SOURCE_DIR}/third_party/cgal/*/include")
target_include_directories(jfx_core SYSTEM PRIVATE ${cgal_packages})
target_compile_definitions(jfx_core PRIVATE CGAL_DISABLE_GMP=1 CGAL_HEADER_ONLY=1 IGL_PARALLEL_FOR_FORCE_SERIAL=1)
set_property(TARGET jfx_core PROPERTY NO_SYSTEM_FROM_IMPORTED OFF)
target_link_libraries(jfx_core PRIVATE BulletDynamics BulletCollision LinearMath VTK::CommonCore)
# This VTK revision unconditionally configures CommonDataModel as well as Core.
# Its exported package references those archives, so build them for installation.
add_dependencies(jfx_core vtkCommonDataModel)
get_target_property(vtk_math_includes VTK::CommonCore INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(jfx_core SYSTEM PRIVATE ${vtk_math_includes})
