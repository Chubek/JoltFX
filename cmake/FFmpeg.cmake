# A small, reproducible offline build of the FFmpeg submodule. System packages
# can expose additional codecs; this profile includes no external dependencies.
include(ExternalProject)
include(GNUInstallDirs)
find_program(JFX_MEDIA_MAKE NAMES gmake make REQUIRED)
set(JFX_MEDIA_BUILD_JOBS 4 CACHE STRING "Parallel jobs for vendored FFmpeg")
set(ffmpeg_prefix "${CMAKE_BINARY_DIR}/third_party/ffmpeg-install")
file(MAKE_DIRECTORY "${ffmpeg_prefix}/include")
set(ffmpeg_cflags "${CMAKE_C_FLAGS} -O2 -fPIC")
set(ffmpeg_ldflags "${CMAKE_EXE_LINKER_FLAGS}")
set(ffmpeg_cross "")
if(EMSCRIPTEN)
    list(APPEND ffmpeg_cross --enable-cross-compile --target-os=none --arch=wasm32 --disable-inline-asm --disable-stripping)
elseif(ANDROID)
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(ffmpeg_arch aarch64)
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(ffmpeg_arch x86_64)
    else()
        message(FATAL_ERROR "Vendored FFmpeg profile supports Android arm64-v8a/x86_64")
    endif()
    list(APPEND ffmpeg_cross --enable-cross-compile --target-os=android "--arch=${ffmpeg_arch}")
    string(APPEND ffmpeg_cflags " --target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT}")
    string(APPEND ffmpeg_ldflags " --target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(ffmpeg_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT ffmpeg_arch)
        set(ffmpeg_arch arm64)
    endif()
    list(LENGTH ffmpeg_arch ffmpeg_arch_count)
    if(NOT ffmpeg_arch_count EQUAL 1)
        message(FATAL_ERROR "Bundled FFmpeg requires one iOS architecture per build directory")
    endif()
    set(ffmpeg_sysroot "${CMAKE_OSX_SYSROOT}")
    if(NOT IS_DIRECTORY "${ffmpeg_sysroot}")
        execute_process(COMMAND xcrun --sdk "${ffmpeg_sysroot}" --show-sdk-path
            OUTPUT_VARIABLE ffmpeg_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endif()
    set(ffmpeg_min "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT ffmpeg_min)
        set(ffmpeg_min 14.0)
    endif()
    if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(ffmpeg_version_flag "-mios-simulator-version-min=${ffmpeg_min}")
    else()
        set(ffmpeg_version_flag "-miphoneos-version-min=${ffmpeg_min}")
    endif()
    list(APPEND ffmpeg_cross --enable-cross-compile --target-os=darwin "--arch=${ffmpeg_arch}")
    string(APPEND ffmpeg_cflags " -arch ${ffmpeg_arch} -isysroot ${ffmpeg_sysroot} ${ffmpeg_version_flag}")
    string(APPEND ffmpeg_ldflags " -arch ${ffmpeg_arch} -isysroot ${ffmpeg_sysroot} ${ffmpeg_version_flag}")
elseif(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Set up a target FFmpeg package for this cross toolchain, or extend cmake/FFmpeg.cmake")
endif()
set(ffmpeg_byproducts "")
foreach(lib IN ITEMS avformat avcodec swscale swresample avutil)
    list(APPEND ffmpeg_byproducts "${ffmpeg_prefix}/lib/lib${lib}.a")
endforeach()
ExternalProject_Add(jfx_ffmpeg_build
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/ffmpeg"
    BINARY_DIR "${CMAKE_BINARY_DIR}/third_party/ffmpeg-build"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
    CONFIGURE_COMMAND <SOURCE_DIR>/configure "--prefix=${ffmpeg_prefix}"
        "--cc=${CMAKE_C_COMPILER}" "--ar=${CMAKE_AR}" "--ranlib=${CMAKE_RANLIB}" "--nm=${CMAKE_NM}"
        "--extra-cflags=${ffmpeg_cflags}" "--extra-ldflags=${ffmpeg_ldflags}"
        --disable-everything --disable-autodetect --disable-network --disable-doc --disable-programs
        --disable-avdevice --disable-avfilter --disable-pthreads --disable-w32threads --disable-os2threads
        --disable-asm --disable-x86asm --enable-pic --enable-static --disable-shared
        --enable-avformat --enable-avcodec --enable-avutil --enable-swscale --enable-swresample
        --enable-protocol=file --enable-muxer=mp4,mov,matroska,wav
        --enable-demuxer=mov,matroska,wav,flac,mp3,aac,ogg,image2
        --enable-encoder=mpeg4,aac,ffv1,prores,pcm_s16le
        --enable-decoder=mpeg4,h264,hevc,vp8,vp9,ffv1,prores,aac,mp3,flac,vorbis,opus,pcm_s16le,pcm_s24le,pcm_f32le
        --enable-parser=mpeg4video,h264,hevc,vp8,vp9,aac,mpegaudio,flac,opus,vorbis
        ${ffmpeg_cross}
    BUILD_COMMAND "${JFX_MEDIA_MAKE}" "-j${JFX_MEDIA_BUILD_JOBS}"
    INSTALL_COMMAND "${JFX_MEDIA_MAKE}" install
    BUILD_BYPRODUCTS ${ffmpeg_byproducts}
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON)
add_library(jfx_ffmpeg INTERFACE)
foreach(lib IN ITEMS avformat avcodec swscale swresample avutil)
    add_library(jfx_ffmpeg_${lib} STATIC IMPORTED GLOBAL)
    set_target_properties(jfx_ffmpeg_${lib} PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_prefix}/lib/lib${lib}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ffmpeg_prefix}/include")
    add_dependencies(jfx_ffmpeg_${lib} jfx_ffmpeg_build)
    target_link_libraries(jfx_ffmpeg INTERFACE
        $<BUILD_INTERFACE:jfx_ffmpeg_${lib}>
        $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/lib${lib}.a>)
    install(FILES "${ffmpeg_prefix}/lib/lib${lib}.a" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endforeach()
if(NOT MSVC)
    target_link_libraries(jfx_ffmpeg INTERFACE m)
endif()
