add_library(jfx_mobile_player STATIC src/mobile_player.c)
target_include_directories(jfx_mobile_player PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(jfx_mobile_player PUBLIC jfx_core jolt_effects)
set_project_warnings(jfx_mobile_player)

# Built by the root project only for an Android NDK toolchain. The Kotlin app
# owns the SurfaceView and calls this thin JNI bridge; it does not create its
# own worker threads.
if(ANDROID)
    add_library(jfx_android_jni SHARED android/app/src/main/cpp/jni_player.c)
    target_link_libraries(jfx_android_jni PRIVATE jfx_mobile_player)
    set_project_warnings(jfx_android_jni)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_subdirectory(ios)
endif()
