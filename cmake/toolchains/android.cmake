# Cross-compilation toolchain for Android, driven by the NDK.
#
# Requires ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) to point at an NDK r26+ and
# ANDROID_ABI to select the target ABI. cmake(1) must be the NDK's own, because
# the NDK ships the matching CMake:
#
#   cmake -S . -B build-android \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android.cmake \
#       -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT \
#       -DANDROID_ABI=arm64-v8a \
#       -DANDROID_PLATFORM=android-26 \
#       -DJFX_FRONTEND_DESKTOP=OFF -DJFX_FRONTEND_WEB=OFF
#   cmake --build build-android
#
# The result is libjfx_android_jni.so for the JNI bridge that the Kotlin
# SurfaceView activity loads.

if(NOT DEFINED ANDROID_NDK_ROOT)
    if(DEFINED ENV{ANDROID_NDK_ROOT})
        set(ANDROID_NDK_ROOT "$ENV{ANDROID_NDK_ROOT}")
    elseif(DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK_ROOT "$ENV{ANDROID_NDK_HOME}")
    endif()
endif()

if(NOT ANDROID_NDK_ROOT)
    message(FATAL_ERROR
        "cmake/toolchains/android.cmake: set ANDROID_NDK_ROOT to an NDK r26 or newer")
endif()
if(NOT ANDROID_ABI)
    set(ANDROID_ABI "arm64-v8a")
endif()
if(NOT ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-26")
endif()

# The NDK's own CMake package dir carries the Android platform modules.
set(ANDROID_NDK "${ANDROID_NDK_ROOT}")
list(APPEND CMAKE_MODULE_PATH "${ANDROID_NDK}/build/cmake/modules")

set(ANDROID_STL "c++_shared")

# Vulkan is the Android graphics API; the other backends have no Android
# surface, so they are switched off here rather than built as CPU shims.
set(JFX_BACKEND_VULKAN ON CACHE BOOL "" FORCE)
set(JFX_BACKEND_METAL OFF CACHE BOOL "" FORCE)
set(JFX_BACKEND_D3D12 OFF CACHE BOOL "" FORCE)
set(JFX_BACKEND_WEBGPU OFF CACHE BOOL "" FORCE)

# The desktop editor and the web player are not Android surfaces; the mobile
# player and the JNI bridge are the point of this build.
set(JFX_FRONTEND_DESKTOP OFF CACHE BOOL "" FORCE)
set(JFX_FRONTEND_WEB OFF CACHE BOOL "" FORCE)
set(JFX_FRONTEND_MOBILE ON CACHE BOOL "" FORCE)
set(JFX_PLUGIN_HOST_BRIDGES OFF CACHE BOOL "" FORCE)
