#include <jni.h>
#include <stdint.h>

#include "jfx/mobile_player.h"

static jfx_mobile_player_t *player_from_handle(jlong handle) {
    return (jfx_mobile_player_t *)(uintptr_t)handle;
}

JNIEXPORT jlong JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativeCreate(JNIEnv *environment, jobject instance,
    jint width, jint height, jdouble duration) {
    (void)environment;
    (void)instance;
    jfx_mobile_player_t *player = NULL;
    jfx_mobile_player_config_t config = {
        .size = sizeof(config),
        .width = (uint32_t)width,
        .height = (uint32_t)height,
        .duration_seconds = duration,
        .backend_name = "vulkan",
    };
    return jfx_mobile_player_create(&config, &player) == JFX_SUCCESS ?
        (jlong)(uintptr_t)player : 0;
}

JNIEXPORT void JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativeDestroy(JNIEnv *environment, jobject instance,
    jlong handle) {
    (void)environment;
    (void)instance;
    jfx_mobile_player_destroy(player_from_handle(handle));
}

JNIEXPORT jint JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativeTap(JNIEnv *environment, jobject instance,
    jlong handle) {
    (void)environment;
    (void)instance;
    return (jint)jfx_mobile_player_tap(player_from_handle(handle));
}

JNIEXPORT jint JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativeSwipe(JNIEnv *environment, jobject instance,
    jlong handle, jdouble pixels) {
    (void)environment;
    (void)instance;
    return (jint)jfx_mobile_player_swipe(player_from_handle(handle), pixels);
}

JNIEXPORT jint JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativePinch(JNIEnv *environment, jobject instance,
    jlong handle, jdouble factor) {
    (void)environment;
    (void)instance;
    return (jint)jfx_mobile_player_pinch(player_from_handle(handle), factor);
}

JNIEXPORT jint JNICALL
Java_org_joltfx_mobile_JoltPlayerActivity_nativeRender(JNIEnv *environment, jobject instance,
    jlong handle, jdouble elapsed) {
    (void)environment;
    (void)instance;
    return (jint)jfx_mobile_player_render(player_from_handle(handle), elapsed);
}
