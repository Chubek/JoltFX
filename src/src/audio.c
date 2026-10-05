#include "jfx/jfx_audio.h"
#include "joltscript/audio_io.h"
#include "joltscript/audio_task.h"
#include "joltscript/audio_kernels.h"
#include "tilly/allocator.h"
#include <string.h>
#include <limits.h>

static void *allocate(size_t n) { return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,_Alignof(max_align_t)); }
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
static jfx_result_t convert(uint64_t frame,uint32_t num,uint32_t den,uint32_t rate,uint64_t *out) {
    if (!num || !den || !out || rate<8000 || rate>192000) return JFX_ERROR_INVALID_ARGUMENT;
    uint64_t b=(uint64_t)den*rate,q=frame/num,rem=frame%num;
    /* Divide before multiplying. The only remaining product is two u32
     * remainders, so exact rational timing also works on MSVC and wasm32. */
    if (q && b>UINT64_MAX/q) return JFX_ERROR_INVALID_ARGUMENT;
    uint64_t total=q*b,whole=b/num;
    if (rem && whole>(UINT64_MAX-total)/rem) return JFX_ERROR_INVALID_ARGUMENT;
    total+=rem*whole;
    uint64_t fraction=rem*(b%num);
    uint64_t tail=fraction/num+(fraction%num!=0);
    if (total>INT64_MAX || tail>(uint64_t)INT64_MAX-total) return JFX_ERROR_INVALID_ARGUMENT;
    *out=total+tail; return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_audio_sample(const jfx_timeline_t *t,uint64_t frame,uint32_t rate,uint64_t *out) {
    if (!t) return JFX_ERROR_INVALID_ARGUMENT;
    return convert(frame,jfx_timeline_fps_num(t),jfx_timeline_fps_den(t),rate,out);
}
static jfx_result_t signed_clock(const jfx_timeline_t *t,int64_t delta,uint32_t rate,int64_t *out) {
    if (delta==INT64_MIN) return JFX_ERROR_INVALID_ARGUMENT;
    uint64_t magnitude=(uint64_t)(delta<0?-delta:delta),sample;
    jfx_result_t r=jfx_timeline_audio_sample(t,magnitude,rate,&sample);
    if (r!=JFX_SUCCESS) return r;
    if (delta<0) {
        uint64_t num=jfx_timeline_fps_num(t),b=(uint64_t)jfx_timeline_fps_den(t)*rate;
        bool fractional=((magnitude%num)*(b%num))%num!=0;
        *out=-(int64_t)(sample-(fractional?1u:0u));
    } else *out=(int64_t)sample;
    return JFX_SUCCESS;
}
typedef struct {
    char path[JFX_NODE_PATH_MAX];
    uint64_t start,end,in_point,length,fade_in,fade_out;
    int64_t reference;
    float gain,pan;
    bool video,no_audio;
    jolt_audio_reader_t *reader;
} audio_clip_t;
struct jfx_audio_mixer { audio_clip_t *clips; size_t count; uint32_t rate; jolt_program_t *program; };
void jfx_audio_mixer_destroy(jfx_audio_mixer_t *m) {
    if (!m) return;
    for (size_t i=0;i<m->count;++i) jolt_audio_reader_close(m->clips[i].reader);
    jolt_program_destroy(m->program); release(m->clips); release(m);
}
jfx_result_t jfx_audio_mixer_create(const jfx_timeline_t *t,uint32_t rate,jfx_audio_mixer_t **out) {
    if (!t || !out || rate<8000 || rate>192000) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_audio_mixer_t *m=allocate(sizeof(*m)); if (!m) return JFX_ERROR_OUT_OF_MEMORY;
    memset(m,0,sizeof(*m)); m->rate=rate;
    jolt_status_t compiled=jolt_audio_mix_compile(&m->program);
    if (compiled!=JOLT_OK) { release(m); return compiled==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_BACKEND_FAILURE; }
    size_t count=0; bool solo=false;
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        solo=solo || jfx_timeline_track_solo(t,track);
        count+=jfx_timeline_clip_count(t,track);
    }
    if (count) m->clips=allocate(count*sizeof(*m->clips));
    if (count && !m->clips) { jfx_audio_mixer_destroy(m); return JFX_ERROR_OUT_OF_MEMORY; }
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        if (jfx_timeline_track_muted(t,track) || (solo && !jfx_timeline_track_solo(t,track))) continue;
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            jfx_clip_audio_t a={.size=sizeof(a)};
            if (jfx_timeline_get_clip_audio(t,track,clip,&a)!=JFX_SUCCESS) goto bad;
            if (!a.enabled || !jfx_timeline_clip_enabled(t,track,clip) || a.gain==0 || jfx_timeline_track_audio_gain(t,track)==0) continue;
            audio_clip_t *c=&m->clips[m->count]; memset(c,0,sizeof(*c));
            const char *path=jfx_timeline_clip_path(t,track,clip);
            if (!path || strlen(path)>=sizeof(c->path)) goto bad;
            strcpy(c->path,path); c->video=jfx_timeline_clip_source(t,track,clip)==JFX_CLIP_VIDEO;
            c->gain=a.gain*jfx_timeline_track_audio_gain(t,track); c->pan=a.pan;
            uint64_t start=jfx_timeline_clip_start(t,track,clip),len=jfx_timeline_clip_length(t,track,clip);
            uint64_t in=jfx_timeline_clip_in_point(t,track,clip);
            int64_t offset=jfx_timeline_clip_key_offset(t,track,clip);
            int64_t source_bias,fade_bias;
            if (jfx_timeline_audio_sample(t,start,rate,&c->start)!=JFX_SUCCESS ||
                jfx_timeline_audio_sample(t,start+len,rate,&c->end)!=JFX_SUCCESS ||
                jfx_timeline_audio_sample(t,a.reference_frames,rate,&c->length)!=JFX_SUCCESS ||
                jfx_timeline_audio_sample(t,a.fade_in_frames,rate,&c->fade_in)!=JFX_SUCCESS ||
                jfx_timeline_audio_sample(t,a.fade_out_frames,rate,&c->fade_out)!=JFX_SUCCESS ||
                signed_clock(t,(int64_t)in-(int64_t)start,rate,&source_bias)!=JFX_SUCCESS ||
                (offset<0 && start>(uint64_t)(INT64_MAX+offset)) ||
                signed_clock(t,offset-(int64_t)start,rate,&fade_bias)!=JFX_SUCCESS) goto bad;
            if ((source_bias>0 && c->start>(uint64_t)INT64_MAX-(uint64_t)source_bias) ||
                (fade_bias>0 && c->start>(uint64_t)INT64_MAX-(uint64_t)fade_bias)) goto bad;
            c->in_point=(uint64_t)((int64_t)c->start+source_bias);
            c->reference=(int64_t)c->start+fade_bias;
            if (c->in_point>(uint64_t)INT64_MAX-(c->end-c->start)) goto bad;
            ++m->count;
        }
    }
    *out=m; return JFX_SUCCESS;
bad:
    jfx_audio_mixer_destroy(m); return JFX_ERROR_INVALID_ARGUMENT;
}
static jfx_result_t audio_result(jolt_status_t r) {
    return r==JOLT_OK?JFX_SUCCESS:r==JOLT_ERR_MEMORY || r==JOLT_ERR_BUDGET?JFX_ERROR_OUT_OF_MEMORY:
        r==JOLT_ERR_NOT_FOUND?JFX_ERROR_NOT_FOUND:r==JOLT_ERR_CAPABILITY?JFX_ERROR_NOT_IMPLEMENTED:JFX_ERROR_BACKEND_FAILURE;
}
jfx_result_t jfx_audio_mixer_render(jfx_audio_mixer_t *m,uint64_t start,size_t frames,float *out,size_t capacity) {
    if (!m || !out || !frames || frames>JFX_AUDIO_MAX_BLOCK_FRAMES || capacity<frames*2 ||
        start>(uint64_t)INT64_MAX-frames) return JFX_ERROR_INVALID_ARGUMENT;
    float *mix=allocate(frames*2*sizeof(float)),*source=allocate(frames*2*sizeof(float));
    if (!mix || !source) { release(mix); release(source); return JFX_ERROR_OUT_OF_MEMORY; }
    memset(mix,0,frames*2*sizeof(float)); jfx_result_t result=JFX_SUCCESS;
    size_t code_size; const uint8_t *code=jolt_program_data(m->program,&code_size);
    for (size_t i=0;i<m->count;++i) {
        audio_clip_t *c=&m->clips[i];
        if (start>=c->end || start+frames<=c->start || c->no_audio) {
            jolt_audio_reader_close(c->reader); c->reader=NULL; continue;
        }
        uint64_t begin=start>c->start?start:c->start,end=start+frames<c->end?start+frames:c->end;
        size_t n=(size_t)(end-begin),dest=(size_t)(begin-start);
        jolt_status_t r=JOLT_OK;
        if (!c->reader) r=jolt_audio_reader_open(c->path,m->rate,&c->reader);
        if (r==JOLT_AUDIO_NO_STREAM && c->video) { c->no_audio=true; continue; }
        if (r==JOLT_OK) r=jolt_audio_reader_read(c->reader,c->in_point+begin-c->start,n,source);
        uint64_t local=begin-c->start;
        if (local>(uint64_t)INT64_MAX || (c->reference>0 && local>(uint64_t)INT64_MAX-(uint64_t)c->reference)) r=JOLT_ERR_ARGUMENT;
        if (r==JOLT_OK) r=jolt_audio_accumulate(code,code_size,source,n,c->gain,c->pan,c->reference+(int64_t)local,
            c->length,c->fade_in,c->fade_out,mix+dest*2);
        if (r!=JOLT_OK) { result=audio_result(r); break; }
    }
    if (result==JFX_SUCCESS) memcpy(out,mix,frames*2*sizeof(float));
    release(source); release(mix); return result;
}
jfx_result_t jfx_timeline_render_audio(const jfx_timeline_t *t,uint64_t start,uint32_t rate,size_t frames,float *out,size_t capacity) {
    if (!out || !frames || frames>JFX_AUDIO_MAX_BLOCK_FRAMES || capacity<frames*2) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_audio_mixer_t *m=NULL; jfx_result_t r=jfx_audio_mixer_create(t,rate,&m);
    if (r==JFX_SUCCESS) r=jfx_audio_mixer_render(m,start,frames,out,capacity);
    jfx_audio_mixer_destroy(m); return r;
}
