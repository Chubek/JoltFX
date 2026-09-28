add_library(joltscript_glue STATIC
    src/bindings.c
    src/compiler.c
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
target_sources(joltscript_glue PRIVATE src/color_io.cpp)
if(JFX_COLOR_OCIO)
    find_package(OpenColorIO 2 CONFIG QUIET)
    if(TARGET OpenColorIO::OpenColorIO)
        target_link_libraries(joltscript_glue PRIVATE OpenColorIO::OpenColorIO)
        target_compile_definitions(joltscript_glue PRIVATE JFX_HAVE_OCIO=1)
    else()
        message(STATUS "OpenColorIO not found: extended LUT formats unavailable")
    endif()
endif()

option(JFX_VIDEO_FFMPEG "Enable FFmpeg video clip decoding" ON)
target_sources(joltscript_glue PRIVATE src/video_io.c)
if(JFX_VIDEO_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JFX_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    endif()
    if(TARGET PkgConfig::JFX_FFMPEG)
        target_link_libraries(joltscript_glue PRIVATE PkgConfig::JFX_FFMPEG)
        target_compile_definitions(joltscript_glue PRIVATE JFX_HAVE_FFMPEG=1)
    else()
        message(STATUS "FFmpeg development libraries not found: video decoding unavailable")
    endif()
endif()
