find_package(Java 17 REQUIRED COMPONENTS Development)
find_package(JNI REQUIRED COMPONENTS JVM)
set(jni_test_class "${CMAKE_CURRENT_BINARY_DIR}/org/joltfx/mobile/JoltPlayerActivity.class")
add_custom_command(OUTPUT "${jni_test_class}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" -encoding UTF-8 -d "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/JoltPlayerActivity.java"
    DEPENDS JoltPlayerActivity.java VERBATIM)
add_custom_target(android_jni_test_class DEPENDS "${jni_test_class}")
add_executable(test_android_jni jni_host.c ../app/src/main/cpp/jni_player.c)
target_link_libraries(test_android_jni PRIVATE jfx_mobile_player JNI::JVM)
set_project_warnings(test_android_jni)
add_dependencies(test_android_jni android_jni_test_class)
add_test(NAME android_jni COMMAND test_android_jni "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(android_jni PROPERTIES LABELS "frontend;conformance")
