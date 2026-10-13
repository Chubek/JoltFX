cmake_minimum_required(VERSION 3.22)
project(JfxFilamentBridge LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(FILAMENT_SKIP_SAMPLES ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SDL2 ON CACHE BOOL "" FORCE)
set(FILAMENT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(FILAMENT_ENABLE_MATDBG OFF CACHE BOOL "" FORCE)
set(FILAMENT_ENABLE_FGVIEWER OFF CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_VULKAN OFF CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_XCB OFF CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_XLIB OFF CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_EGL_ON_LINUX ON CACHE BOOL "" FORCE)
set(FILAMENT_ENABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(FILAMENT_DISABLE_MATOPT ON CACHE BOOL "" FORCE)
set(USE_STATIC_LIBCXX OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The vendored headless EGL platform creates an OpenGL ES context. Compile
    # its built-in materials for that shader model as well as our surface.
    set(FILAMENT_LINUX_IS_MOBILE ON CACHE BOOL "" FORCE)
    # This remains a native host build: matc must be built locally even though
    # the graphics shader model is ES/mobile.
    set(IS_HOST_PLATFORM TRUE)
    # This standalone bridge uses the host's libstdc++, whose std::mutex has no
    # libc++ capability annotations. Do not enable libc++-specific attributes.
    add_compile_options(-U_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
endif()
add_subdirectory("${FILAMENT_ROOT}" vendor EXCLUDE_FROM_ALL)
# Upstream BlobDictionary.h relies on a transitive fixed-width integer include
# that recent libstdc++ no longer supplies. Keep the workaround SDK-local.
target_compile_options(filamat PRIVATE -include cstdint)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(APPLE)
    set(jfx_filament_api metal)
    set(jfx_filament_profile desktop)
else()
    set(jfx_filament_api opengl)
    set(jfx_filament_profile mobile)
endif()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/surface.filamat"
    COMMAND matc -p ${jfx_filament_profile} -a ${jfx_filament_api} -o "${CMAKE_CURRENT_BINARY_DIR}/surface.filamat" "${CMAKE_CURRENT_SOURCE_DIR}/surface.mat"
    DEPENDS matc surface.mat VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/surface_material.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_material.py"
        "${CMAKE_CURRENT_BINARY_DIR}/surface.filamat" "${CMAKE_CURRENT_BINARY_DIR}/surface_material.h"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/surface.filamat" embed_material.py VERBATIM)
add_library(jfx_filament_bridge SHARED renderer.cpp "${CMAKE_CURRENT_BINARY_DIR}/surface_material.h")
set_target_properties(jfx_filament_bridge PROPERTIES OUTPUT_NAME jfx_filament
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(jfx_filament_bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(jfx_filament_bridge PRIVATE filament)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Resolve ES entry points inside the bridge, rather than relying on the
    # host application's graphics libraries.
    find_library(JFX_FILAMENT_GLES NAMES GLESv2 REQUIRED)
    target_link_libraries(jfx_filament_bridge PRIVATE "${JFX_FILAMENT_GLES}")
endif()
if(JFX_FILAMENT_ASAN)
    target_compile_options(jfx_filament_bridge PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(jfx_filament_bridge PRIVATE -fsanitize=address)
endif()
if(JFX_FILAMENT_UBSAN)
    target_compile_options(jfx_filament_bridge PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(jfx_filament_bridge PRIVATE -fsanitize=undefined)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(jfx_filament_bridge PRIVATE -Wl,--exclude-libs,ALL)
endif()
