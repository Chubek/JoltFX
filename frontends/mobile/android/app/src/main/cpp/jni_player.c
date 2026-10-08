#include <jni.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "jfx/mobile_player.h"
#include "jfx/jfx_color.h"
#include "tilly/containers.h"

static jfx_mobile_player_t *player_from_handle(jlong h) { return (jfx_mobile_player_t *)(uintptr_t)h; }
#define JNI(name) Java_org_joltfx_mobile_JoltPlayerActivity_##name
static void throw_result(JNIEnv *env,jfx_result_t result) {
    jclass type=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if (type) (*env)->ThrowNew(env,type,jfx_result_to_string(result));
}
JNIEXPORT jlong JNICALL JNI(nativeExportBegin)(JNIEnv *env,jobject self,jlong h,jstring path,jlong start,jlong count,jboolean audio) {
    (void)self; if (!path || start<0 || count<0) { throw_result(env,JFX_ERROR_INVALID_ARGUMENT); return 0; }
    const char *p=(*env)->GetStringUTFChars(env,path,NULL); if (!p) return 0;
    jfx_export_options_t o={.size=sizeof(o),.path=p,.start_frame=(uint64_t)start,.frame_count=(uint64_t)count,.audio=audio!=0};
    jfx_export_job_t *job=NULL; jfx_result_t r=jfx_mobile_player_export_begin(player_from_handle(h),&o,&job);
    (*env)->ReleaseStringUTFChars(env,path,p);
    if (r!=JFX_SUCCESS) throw_result(env,r);
    return (jlong)(uintptr_t)job;
}
JNIEXPORT jint JNICALL JNI(nativeExportStep)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; return jfx_export_step((jfx_export_job_t *)(uintptr_t)h,1);
}
JNIEXPORT jint JNICALL JNI(nativeExportState)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; return (jint)jfx_export_state((jfx_export_job_t *)(uintptr_t)h);
}
JNIEXPORT jlong JNICALL JNI(nativeExportCompleted)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; return (jlong)jfx_export_completed_frames((jfx_export_job_t *)(uintptr_t)h);
}
JNIEXPORT void JNICALL JNI(nativeExportDestroy)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; jfx_export_destroy((jfx_export_job_t *)(uintptr_t)h);
}
JNIEXPORT jlong JNICALL JNI(nativeAudioCreate)(JNIEnv *env,jobject self,jlong h) {
    (void)self; jfx_audio_mixer_t *m=NULL;
    jfx_result_t r=jfx_mobile_player_audio_mixer(player_from_handle(h),48000,&m);
    if (r!=JFX_SUCCESS) throw_result(env,r);
    return (jlong)(uintptr_t)m;
}
JNIEXPORT jfloatArray JNICALL JNI(nativeAudioRender)(JNIEnv *env,jobject self,jlong h,jlong sample,jint frames) {
    (void)self; if (sample<0 || frames<=0 || frames>65536) return NULL;
    float *pcm=tilly_container_alloc((size_t)frames*2*sizeof(float)); if (!pcm) return NULL;
    jfx_result_t r=jfx_audio_mixer_render((jfx_audio_mixer_t *)(uintptr_t)h,(uint64_t)sample,(size_t)frames,pcm,(size_t)frames*2);
    jfloatArray out=NULL;
    if (r==JFX_SUCCESS) { out=(*env)->NewFloatArray(env,frames*2); if (out) (*env)->SetFloatArrayRegion(env,out,0,frames*2,pcm); }
    tilly_container_free(pcm); if (r!=JFX_SUCCESS) throw_result(env,r); return out;
}
JNIEXPORT void JNICALL JNI(nativeAudioDestroy)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; jfx_audio_mixer_destroy((jfx_audio_mixer_t *)(uintptr_t)h);
}
JNIEXPORT jboolean JNICALL JNI(nativePlaying)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; jfx_mobile_player_state_t state={.size=sizeof(state)};
    return jfx_mobile_player_get_state(player_from_handle(h),&state)==JFX_SUCCESS && state.playing!=0?JNI_TRUE:JNI_FALSE;
}

JNIEXPORT jlong JNICALL JNI(nativeCreate)(JNIEnv *env,jobject self,jint w,jint h,jdouble duration) {
    (void)env; (void)self;
    jfx_mobile_player_t *p=NULL;
    jfx_mobile_player_config_t cfg={.size=sizeof(cfg),.width=(uint32_t)w,.height=(uint32_t)h,
        .duration_seconds=duration,.backend_name="vulkan"};
    return jfx_mobile_player_create(&cfg,&p)==JFX_SUCCESS?(jlong)(uintptr_t)p:0;
}
JNIEXPORT void JNICALL JNI(nativeDestroy)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; jfx_mobile_player_destroy(player_from_handle(h));
}
JNIEXPORT jint JNICALL JNI(nativeTap)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; return (jint)jfx_mobile_player_tap(player_from_handle(h));
}
JNIEXPORT jint JNICALL JNI(nativeSwipe)(JNIEnv *env,jobject self,jlong h,jdouble v) {
    (void)env; (void)self; return (jint)jfx_mobile_player_swipe(player_from_handle(h),v);
}
JNIEXPORT jint JNICALL JNI(nativePinch)(JNIEnv *env,jobject self,jlong h,jdouble v) {
    (void)env; (void)self; return (jint)jfx_mobile_player_pinch(player_from_handle(h),v);
}
JNIEXPORT jint JNICALL JNI(nativeRender)(JNIEnv *env,jobject self,jlong h,jdouble v) {
    (void)env; (void)self; return (jint)jfx_mobile_player_render(player_from_handle(h),v);
}
JNIEXPORT jint JNICALL JNI(nativeEdit)(JNIEnv *env,jobject self,jlong h,jstring op,
    jint a,jint b,jint c,jdouble value,jstring text) {
    (void)self;
    if (!op || !text || a<0 || b<0 || c<0) return JFX_ERROR_INVALID_ARGUMENT;
    const char *o=(*env)->GetStringUTFChars(env,op,NULL);
    if (!o) return JFX_ERROR_OUT_OF_MEMORY;
    const char *t=(*env)->GetStringUTFChars(env,text,NULL);
    jfx_result_t r=t?jfx_mobile_player_edit(player_from_handle(h),o,(uint32_t)a,(uint32_t)b,(uint32_t)c,value,t):JFX_ERROR_OUT_OF_MEMORY;
    if (t) (*env)->ReleaseStringUTFChars(env,text,t);
    (*env)->ReleaseStringUTFChars(env,op,o); return (jint)r;
}
static jintArray pixels(JNIEnv *env,jlong h,int graph,jint node) {
    jfx_mobile_player_t *p=player_from_handle(h);
    jfx_mobile_player_state_t state={.size=sizeof(state)};
    if (jfx_mobile_player_get_state(p,&state)!=JFX_SUCCESS) return NULL;
    size_t count=320*180;
    uint8_t *rgba=tilly_container_alloc(count*4);
    jint *argb=tilly_container_alloc(count*sizeof(jint));
    jintArray out=NULL;
    jfx_result_t r=rgba?(graph?jfx_mobile_player_render_graph(p,(uint32_t)node,state.time_seconds,320,180,rgba,count*4)
        :jfx_editor_render(jfx_mobile_player_editor(p),state.time_seconds,320,180,rgba,count*4)):JFX_ERROR_OUT_OF_MEMORY;
    if (rgba && argb && r==JFX_SUCCESS) {
        for (size_t i=0;i<count;++i) argb[i]=(jint)(((uint32_t)rgba[i*4+3]<<24)|((uint32_t)rgba[i*4]<<16)|((uint32_t)rgba[i*4+1]<<8)|rgba[i*4+2]);
        out=(*env)->NewIntArray(env,(jsize)count);
        if (out) (*env)->SetIntArrayRegion(env,out,0,(jsize)count,argb);
    }
    tilly_container_free(rgba); tilly_container_free(argb); return out;
}
JNIEXPORT jintArray JNICALL JNI(nativePixels)(JNIEnv *env,jobject self,jlong h) {
    (void)self; return pixels(env,h,0,0);
}
JNIEXPORT jintArray JNICALL JNI(nativeGraphPixels)(JNIEnv *env,jobject self,jlong h,jint node) {
    (void)self; return node<0?NULL:pixels(env,h,1,node);
}
JNIEXPORT jstring JNICALL JNI(nativeNodeCatalog)(JNIEnv *env,jobject self) {
    (void)self; size_t cap=1024*1024; char *json=tilly_container_alloc(cap); jstring out=NULL;
    if (json && jfx_node_catalog(json,cap)==JFX_SUCCESS) out=(*env)->NewStringUTF(env,json);
    tilly_container_free(json); return out;
}
JNIEXPORT jstring JNICALL JNI(nativeGraphState)(JNIEnv *env,jobject self,jlong h) {
    (void)self; size_t cap=1024*1024; char *json=tilly_container_alloc(cap); jstring out=NULL;
    if (json && jfx_mobile_player_graph_state(player_from_handle(h),json,cap)==JFX_SUCCESS) out=(*env)->NewStringUTF(env,json);
    tilly_container_free(json); return out;
}
JNIEXPORT jstring JNICALL JNI(nativeScene3DState)(JNIEnv *env,jobject self,jlong h) {
    (void)self; size_t cap=4*1024*1024; char *json=tilly_container_alloc(cap); jstring out=NULL;
    if (json && jfx_mobile_player_scene3d_state(player_from_handle(h),json,cap)==JFX_SUCCESS) out=(*env)->NewStringUTF(env,json);
    tilly_container_free(json); return out;
}
JNIEXPORT jint JNICALL JNI(nativeWriteGraph)(JNIEnv *env,jobject self,jlong h,jdouble seconds,jstring path) {
    (void)self; if (!path) return JFX_ERROR_INVALID_ARGUMENT;
    const char *p=(*env)->GetStringUTFChars(env,path,NULL); if (!p) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r=jfx_mobile_player_write_graph(player_from_handle(h),UINT32_MAX,seconds,p);
    (*env)->ReleaseStringUTFChars(env,path,p); return r;
}
JNIEXPORT jstring JNICALL JNI(nativeColorCatalog)(JNIEnv *env,jobject self) {
    (void)self; char *json=tilly_container_alloc(65536); jstring out=NULL;
    if (json && jfx_color_catalog(json,65536)==JFX_SUCCESS) out=(*env)->NewStringUTF(env,json);
    tilly_container_free(json); return out;
}
static uint32_t color_index(jfx_timeline_t *t,jint a,jint b,jint c,jint section) {
    if (a<0 || b<0 || c<0) return UINT32_MAX;
    uint32_t ordinal=0;
    for (uint32_t i=0;i<jfx_timeline_effect_count(t,(uint32_t)a,(uint32_t)b);++i)
        if ((int)jfx_color_section(jfx_timeline_effect_kind_desc(t,(uint32_t)a,(uint32_t)b,i))==section && ordinal++==(uint32_t)c) return i;
    return UINT32_MAX;
}
JNIEXPORT jstring JNICALL JNI(nativeColorKind)(JNIEnv *env,jobject self,jlong h,jint a,jint b,jint c,jint section) {
    (void)self; jfx_timeline_t *t=jfx_editor_timeline(jfx_mobile_player_editor(player_from_handle(h)));
    uint32_t i=color_index(t,a,b,c,section);
    const char *name=jfx_timeline_effect_kind(t,(uint32_t)a,(uint32_t)b,i);
    return (*env)->NewStringUTF(env,name?name:"");
}
JNIEXPORT jdouble JNICALL JNI(nativeColorValue)(JNIEnv *env,jobject self,jlong h,jint a,jint b,jint c,jint section,jint param) {
    (void)env; (void)self; jfx_timeline_t *t=jfx_editor_timeline(jfx_mobile_player_editor(player_from_handle(h)));
    uint32_t i=color_index(t,a,b,c,section);
    return (jdouble)jfx_timeline_effect_param(t,(uint32_t)a,(uint32_t)b,i,(size_t)param);
}
JNIEXPORT jstring JNICALL JNI(nativeSequenceState)(JNIEnv *env,jobject self,jlong h) {
    (void)self; size_t cap=4*1024*1024; char *json=tilly_container_alloc(cap); jstring out=NULL;
    if (json && jfx_mobile_player_sequence_state(player_from_handle(h),json,cap)==JFX_SUCCESS) out=(*env)->NewStringUTF(env,json);
    tilly_container_free(json); return out;
}
JNIEXPORT jint JNICALL JNI(nativeSeek)(JNIEnv *env,jobject self,jlong h,jdouble seconds) {
    (void)env; (void)self; return jfx_mobile_player_seek(player_from_handle(h),seconds);
}
JNIEXPORT jdouble JNICALL JNI(nativeTime)(JNIEnv *env,jobject self,jlong h) {
    (void)env; (void)self; jfx_mobile_player_state_t state={.size=sizeof(state)};
    return jfx_mobile_player_get_state(player_from_handle(h),&state)==JFX_SUCCESS?state.time_seconds:0;
}
JNIEXPORT jint JNICALL JNI(nativeWriteFrame)(JNIEnv *env,jobject self,jlong h,jlong frame,jstring path) {
    (void)self; if (!path || frame<0) return JFX_ERROR_INVALID_ARGUMENT;
    const char *p=(*env)->GetStringUTFChars(env,path,NULL); if (!p) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r=jfx_mobile_player_write_frame(player_from_handle(h),(uint64_t)frame,p);
    (*env)->ReleaseStringUTFChars(env,path,p); return r;
}
JNIEXPORT jint JNICALL JNI(nativeProjectFile)(JNIEnv *env,jobject self,jlong h,jstring path,jboolean save) {
    (void)self; if (!path) return JFX_ERROR_INVALID_ARGUMENT;
    const char *p=(*env)->GetStringUTFChars(env,path,NULL); if (!p) return JFX_ERROR_OUT_OF_MEMORY;
    FILE *file=fopen(p,save?"wb":"rb"); (*env)->ReleaseStringUTFChars(env,path,p);
    if (!file) return JFX_ERROR_NOT_FOUND;
    char *text=tilly_container_alloc(JFX_PROJECT_MAX_BYTES+1); size_t n=0;
    jfx_result_t r=JFX_ERROR_OUT_OF_MEMORY;
    if (text) {
        if (save) {
            r=jfx_mobile_player_save_document(player_from_handle(h),text,JFX_PROJECT_MAX_BYTES,&n);
            if (r==JFX_SUCCESS && fwrite(text,1,n,file)!=n) r=JFX_ERROR_BACKEND_FAILURE;
        } else {
            n=fread(text,1,JFX_PROJECT_MAX_BYTES+1,file);
            r=ferror(file)?JFX_ERROR_BACKEND_FAILURE:jfx_mobile_player_load_document(player_from_handle(h),text,n,NULL,0);
        }
    }
    if (fclose(file) && r==JFX_SUCCESS) r=JFX_ERROR_BACKEND_FAILURE;
    tilly_container_free(text); return r;
}
