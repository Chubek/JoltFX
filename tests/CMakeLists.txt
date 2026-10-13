add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(perf)

option(JFX_ANDROID_JNI_HOST_TESTS "Exercise the Android JNI bridge with a host JDK" OFF)
if(JFX_ANDROID_JNI_HOST_TESTS AND UNIX AND NOT CMAKE_CROSSCOMPILING AND TARGET jfx_mobile_player)
    add_subdirectory(../frontends/mobile/android/tests android-jni)
endif()

# Test registration lives next to each target in tests/unit and
# tests/integration, so a target and its add_test() cannot drift apart.
