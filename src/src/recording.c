#include "jfx/jfx_recording.h"
#include "cpu_numeric.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#if defined(_WIN32)
#include <windows.h>
#endif
struct jfx_audio_recording { FILE *file; char *path,*temporary; uint64_t frames; uint32_t rate; bool finished,failed; };
static void *allocate(size_t n) { return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,_Alignof(max_align_t)); }
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
void jfx_audio_recording_cancel(jfx_audio_recording_t *r) {
    if (!r || r->finished) return;
    if (r->file) { fclose(r->file); r->file=NULL; }
    if (r->temporary && *r->temporary) { remove(r->temporary); *r->temporary=0; }
    r->failed=true;
}
void jfx_audio_recording_destroy(jfx_audio_recording_t *r) {
    if (!r) return;
    jfx_audio_recording_cancel(r); release(r->path); release(r->temporary); release(r);
}
static bool header(jfx_audio_recording_t *r) {
    uint32_t bytes=(uint32_t)r->frames*8;
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,3,0,2,0,0,0,0,0,0,0,0,0,8,0,32,0,'d','a','t','a',0,0,0,0};
    for (unsigned b=0;b<4;++b) { h[4+b]=(unsigned char)((bytes+36)>>(8*b)); h[24+b]=(unsigned char)(r->rate>>(8*b)); h[28+b]=(unsigned char)((r->rate*8)>>(8*b)); h[40+b]=(unsigned char)(bytes>>(8*b)); }
    return !fseek(r->file,0,SEEK_SET) && fwrite(h,1,sizeof(h),r->file)==sizeof(h);
}
jfx_result_t jfx_audio_recording_begin(const char *path,uint32_t rate,jfx_audio_recording_t **out) {
    if (!path || !*path || strlen(path)>4096 || strstr(path,"://") || !out || rate<8000 || rate>192000) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_audio_recording_t *r=allocate(sizeof(*r)); if (!r) return JFX_ERROR_OUT_OF_MEMORY;
    memset(r,0,sizeof(*r)); r->rate=rate; size_t n=strlen(path);
    r->path=allocate(n+1); r->temporary=allocate(n+64);
    if (r->temporary) *r->temporary=0;
    if (!r->path || !r->temporary) { jfx_audio_recording_destroy(r); return JFX_ERROR_OUT_OF_MEMORY; }
    memcpy(r->path,path,n+1);
    for (unsigned slot=0;slot<1024;++slot) {
        snprintf(r->temporary,n+64,"%s.jfx-take-%u",path,slot); errno=0; r->file=fopen(r->temporary,"wbx");
        if (r->file) break;
        *r->temporary=0;
        if (errno!=EEXIST) { jfx_audio_recording_destroy(r); return JFX_ERROR_NOT_FOUND; }
    }
    if (!r->file) { jfx_audio_recording_destroy(r); return JFX_ERROR_ALREADY_EXISTS; }
    if (!header(r)) { jfx_audio_recording_destroy(r); return JFX_ERROR_BACKEND_FAILURE; }
    *out=r; return JFX_SUCCESS;
}
jfx_result_t jfx_audio_recording_push(jfx_audio_recording_t *r,const float *pcm,size_t frames) {
    if (!r || !r->file || r->failed || !pcm || !frames || frames>JFX_AUDIO_MAX_BLOCK_FRAMES || frames>(UINT32_MAX-36u)/8u-r->frames) return JFX_ERROR_INVALID_ARGUMENT;
    if (!jfx_cpu_finite(pcm,frames*2)) return JFX_ERROR_INVALID_ARGUMENT;
    unsigned char bytes[4096]; size_t cursor=0;
    while (cursor<frames*2) {
        size_t n=frames*2-cursor; if (n>1024) n=1024;
        for (size_t i=0;i<n;++i) { uint32_t bits; memcpy(&bits,pcm+cursor+i,4); for (unsigned b=0;b<4;++b) bytes[i*4+b]=(unsigned char)(bits>>(b*8)); }
        if (fwrite(bytes,4,n,r->file)!=n) { jfx_audio_recording_cancel(r); return JFX_ERROR_BACKEND_FAILURE; }
        cursor+=n;
    }
    r->frames+=frames; return JFX_SUCCESS;
}
uint64_t jfx_audio_recording_frames(const jfx_audio_recording_t *r) { return r?r->frames:0; }
jfx_result_t jfx_audio_recording_finish(jfx_audio_recording_t *r) {
    if (!r || r->failed || (!r->file && !r->finished)) return JFX_ERROR_INVALID_ARGUMENT;
    if (r->finished) return JFX_SUCCESS;
    bool ok=header(r); if (fflush(r->file)) ok=false; if (fclose(r->file)) ok=false; r->file=NULL;
#if defined(_WIN32)
    if (ok) ok=MoveFileExA(r->temporary,r->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if (ok) ok=rename(r->temporary,r->path)==0;
#endif
    if (!ok) { jfx_audio_recording_cancel(r); return JFX_ERROR_BACKEND_FAILURE; }
    *r->temporary=0; r->finished=true; return JFX_SUCCESS;
}
