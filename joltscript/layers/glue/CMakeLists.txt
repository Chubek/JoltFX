add_library(joltscript_glue STATIC
    src/bindings.c
    src/compiler.c
    src/image_program.c
)

target_include_directories(joltscript_glue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# The Glue layer sits *below* the engine: the compiler and the ABI registry must
# not depend on jfx_core, or the engine could not depend on them in order to run
# kernels. jfx_core links this target, never the other way round.
target_link_libraries(joltscript_glue PUBLIC
    joltscript_execution
)

set_project_warnings(joltscript_glue)

# Production LUT readers; the bundled parsers remain usable on small/WASM builds.
option(JFX_COLOR_OCIO "Enable OpenColorIO's registered LUT formats" ON)
option(JFX_COLOR_OCIO_BUNDLED "Build third_party/opencolorio when no system package is available" OFF)
target_sources(joltscript_glue PRIVATE src/color_io.cpp)
if(JFX_COLOR_OCIO)
    find_package(OpenColorIO 2 CONFIG QUIET)
    if(NOT TARGET OpenColorIO::OpenColorIO AND JFX_COLOR_OCIO_BUNDLED)
        set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ language standard")
        set(OCIO_BUILD_APPS OFF CACHE BOOL "" FORCE)
        set(OCIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(OCIO_BUILD_GPU_TESTS OFF CACHE BOOL "" FORCE)
        set(OCIO_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
        set(OCIO_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        # OCIO links its minizip dependency to ZLIB::ZLIB. Keep the external
        # minizip build on that same ABI rather than auto-detecting zlib-ng.
        list(APPEND minizip-ng_CMAKE_ARGS -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB-NG=TRUE)
        add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/opencolorio" "${CMAKE_BINARY_DIR}/third_party/opencolorio" EXCLUDE_FROM_ALL)
        if(TARGET OpenColorIO AND NOT TARGET OpenColorIO::OpenColorIO)
            add_library(OpenColorIO::OpenColorIO ALIAS OpenColorIO)
        endif()
    endif()
    if(TARGET OpenColorIO::OpenColorIO)
        target_link_libraries(joltscript_glue PRIVATE OpenColorIO::OpenColorIO)
        target_compile_definitions(joltscript_glue PRIVATE JFX_HAVE_OCIO=1)
    else()
        message(STATUS "OpenColorIO not found: built-in LUT readers active; use JFX_COLOR_OCIO_BUNDLED=ON for the vendored library")
    endif()
endif()

add_library(jfx_miniaudio OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c")
target_include_directories(jfx_miniaudio SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/miniaudio")
target_compile_definitions(jfx_miniaudio PUBLIC MA_NO_DEVICE_IO MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_THREADING MA_NO_ENCODING)
set_target_properties(jfx_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(joltscript_glue PRIVATE $<BUILD_INTERFACE:jfx_miniaudio>)

option(JFX_VIDEO_FFMPEG "Enable FFmpeg media decoding and encoded export" ON)
option(JFX_MEDIA_FFMPEG_BUNDLED "Build vendored FFmpeg instead of using system libraries" OFF)
target_sources(joltscript_glue PRIVATE src/video_io.c src/audio_io.c src/media_writer.c)
if(JFX_VIDEO_FFMPEG)
    if(JFX_MEDIA_FFMPEG_BUNDLED)
        include("${PROJECT_SOURCE_DIR}/cmake/FFmpeg.cmake")
        target_link_libraries(joltscript_glue PRIVATE jfx_ffmpeg)
        target_compile_definitions(joltscript_glue PRIVATE JFX_HAVE_FFMPEG=1)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
            pkg_check_modules(JFX_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
        endif()
        if(TARGET PkgConfig::JFX_FFMPEG)
            target_link_libraries(joltscript_glue PRIVATE PkgConfig::JFX_FFMPEG)
            target_compile_definitions(joltscript_glue PRIVATE JFX_HAVE_FFMPEG=1)
        else()
            message(STATUS "FFmpeg development libraries not found: use JFX_MEDIA_FFMPEG_BUNDLED=ON for encoded export")
        endif()
    endif()
endif()
