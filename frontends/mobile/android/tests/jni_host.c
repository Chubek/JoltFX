/* Embed a real JVM and register the production JNI functions. No fake JNIEnv
 * and no Android SDK needed; on-device UI tests remain a separate gate. */
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define JNI(name) Java_org_joltfx_mobile_JoltPlayerActivity_##name
extern jlong JNI(nativeCreate)(JNIEnv *,jobject,jint,jint,jdouble);
extern void JNI(nativeDestroy)(JNIEnv *,jobject,jlong);
extern jint JNI(nativeEdit)(JNIEnv *,jobject,jlong,jstring,jint,jint,jint,jdouble,jstring);
extern jstring JNI(nativeScene3DState)(JNIEnv *,jobject,jlong);
extern jint JNI(nativeProjectFile)(JNIEnv *,jobject,jlong,jstring,jboolean);
extern jintArray JNI(nativePixels)(JNIEnv *,jobject,jlong);
extern jint JNI(nativeSeek)(JNIEnv *,jobject,jlong,jdouble);

int main(int argc,char **argv) {
    if(argc!=2) return 2;
    char classpath[4096];
    int n=snprintf(classpath,sizeof(classpath),"-Djava.class.path=%s",argv[1]);
    if(n<0 || (size_t)n>=sizeof(classpath)) return 2;
    JavaVMOption options[]={{classpath,NULL},{"-Xcheck:jni",NULL}};
    JavaVMInitArgs args={JNI_VERSION_1_8,2,options,JNI_FALSE};
    JavaVM *vm=NULL; JNIEnv *env=NULL;
    if(JNI_CreateJavaVM(&vm,(void **)&env,&args)!=JNI_OK) return 1;
    jclass cls=(*env)->FindClass(env,"org/joltfx/mobile/JoltPlayerActivity");
    JNINativeMethod methods[]={
        {"nativeCreate","(IID)J",NULL},
        {"nativeDestroy","(J)V",NULL},
        {"nativeEdit","(JLjava/lang/String;IIIDLjava/lang/String;)I",NULL},
        {"nativeScene3DState","(J)Ljava/lang/String;",NULL},
        {"nativeProjectFile","(JLjava/lang/String;Z)I",NULL},
        {"nativePixels","(J)[I",NULL},
        {"nativeSeek","(JD)I",NULL},
    };
    void (*functions[])(void)={
        (void (*)(void))JNI(nativeCreate), (void (*)(void))JNI(nativeDestroy),
        (void (*)(void))JNI(nativeEdit), (void (*)(void))JNI(nativeScene3DState),
        (void (*)(void))JNI(nativeProjectFile), (void (*)(void))JNI(nativePixels),
        (void (*)(void))JNI(nativeSeek),
    };
    /* JNI specifies void* slots for function addresses. Copy the representation
     * explicitly rather than using a non-ISO function-to-object pointer cast. */
    _Static_assert(sizeof(functions[0])==sizeof(methods[0].fnPtr),"JNI function address size");
    for(size_t i=0;i<sizeof(methods)/sizeof(methods[0]);++i)
        memcpy(&methods[i].fnPtr,&functions[i],sizeof(methods[i].fnPtr));
    int result=1;
    if(cls && (*env)->RegisterNatives(env,cls,methods,(jint)(sizeof(methods)/sizeof(methods[0])))==JNI_OK) {
        jmethodID run=(*env)->GetStaticMethodID(env,cls,"run","()V");
        if(run) { (*env)->CallStaticVoidMethod(env,cls,run); result=(*env)->ExceptionCheck(env)?1:0; }
    }
    if((*env)->ExceptionCheck(env)) (*env)->ExceptionDescribe(env);
    (*vm)->DestroyJavaVM(vm);
    return result;
}
