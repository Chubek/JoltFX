#ifndef JFX_ANDROID_JNI_UTF8_H
#define JFX_ANDROID_JNI_UTF8_H

#include <jni.h>
#include <stdint.h>
#include <string.h>
#include "tilly/containers.h"

static void jfx_jni_throw(JNIEnv *env,const char *type,const char *message) {
    jclass cls=(*env)->FindClass(env,type);
    if(cls) { (*env)->ThrowNew(env,cls,message); (*env)->DeleteLocalRef(env,cls); }
}

/* GetStringUTFChars uses modified UTF-8 (six bytes for supplementary scalars).
 * Core, Joltscript, file APIs and JSON all require standard UTF-8 instead. */
static char *jfx_jni_utf8(JNIEnv *env,jstring value) {
    if(!value) return NULL;
    jsize length=(*env)->GetStringLength(env,value);
    if(length>8*1024*1024) {
        jfx_jni_throw(env,"java/lang/IllegalArgumentException","Text exceeds 8 MiB UTF-16 units");
        return NULL;
    }
    const jchar *chars=(*env)->GetStringChars(env,value,NULL);
    if(!chars) return NULL;
    char *bytes=tilly_container_alloc((size_t)length*3+1);
    size_t used=0;
    int invalid=0;
    if(bytes) for(jsize i=0;i<length;++i) {
        uint32_t cp=chars[i];
        if(cp>=0xd800 && cp<=0xdbff) {
            if(i+1>=length || chars[i+1]<0xdc00 || chars[i+1]>0xdfff) { invalid=1; break; }
            cp=0x10000+((cp-0xd800)<<10)+(chars[++i]-0xdc00);
        } else if(cp==0 || (cp>=0xdc00 && cp<=0xdfff)) { invalid=1; break; }
        if(cp<0x80) bytes[used++]=(char)cp;
        else if(cp<0x800) {
            bytes[used++]=(char)(0xc0|(cp>>6)); bytes[used++]=(char)(0x80|(cp&63));
        } else if(cp<0x10000) {
            bytes[used++]=(char)(0xe0|(cp>>12)); bytes[used++]=(char)(0x80|((cp>>6)&63)); bytes[used++]=(char)(0x80|(cp&63));
        } else {
            bytes[used++]=(char)(0xf0|(cp>>18)); bytes[used++]=(char)(0x80|((cp>>12)&63));
            bytes[used++]=(char)(0x80|((cp>>6)&63)); bytes[used++]=(char)(0x80|(cp&63));
        }
    }
    (*env)->ReleaseStringChars(env,value,chars);
    if(!bytes) jfx_jni_throw(env,"java/lang/OutOfMemoryError","UTF-8 conversion allocation failed");
    else if(invalid) {
        tilly_container_free(bytes); bytes=NULL;
        jfx_jni_throw(env,"java/lang/IllegalArgumentException","Text contains NUL or an unpaired surrogate");
    } else bytes[used]=0;
    return bytes;
}

static jstring jfx_jni_string(JNIEnv *env,const char *utf8) {
    if(!utf8) return NULL;
    size_t length=strlen(utf8);
    if(length>INT32_MAX) return NULL;
    jbyteArray bytes=(*env)->NewByteArray(env,(jsize)length);
    if(!bytes) return NULL;
    (*env)->SetByteArrayRegion(env,bytes,0,(jsize)length,(const jbyte *)utf8);
    jstring result=NULL;
    jclass cls=NULL;
    jstring encoding=NULL;
    if(!(*env)->ExceptionCheck(env)) cls=(*env)->FindClass(env,"java/lang/String");
    if(cls) {
        jmethodID constructor=(*env)->GetMethodID(env,cls,"<init>","([BLjava/lang/String;)V");
        if(constructor) encoding=(*env)->NewStringUTF(env,"UTF-8");
        if(encoding) result=(jstring)(*env)->NewObject(env,cls,constructor,bytes,encoding);
    }
    if(encoding) (*env)->DeleteLocalRef(env,encoding);
    if(cls) (*env)->DeleteLocalRef(env,cls);
    (*env)->DeleteLocalRef(env,bytes);
    return result;
}
#endif
