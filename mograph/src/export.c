#include "jfx/jfx_export.h"
#include "joltscript/media_writer.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#if defined(_WIN32)
#include <windows.h>
#endif

struct jfx_export_job {
    jfx_editor_t *editor;
    jfx_audio_mixer_t *mixer;
    jolt_media_writer_t *writer;
    FILE *file;
    char *path,*temporary;
    uint8_t *pixels;
    float *pcm;
    uint64_t start,total,completed,audio_start,audio_cursor;
    uint32_t width,height,rate,fps_num,fps_den;
    bool sequence;
    jfx_export_state_t state;
    jfx_result_t result;
};
static void *allocate(size_t n) { return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,_Alignof(max_align_t)); }
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
static jfx_result_t mapped(jolt_status_t r) {
    return r==JOLT_OK?JFX_SUCCESS:r==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:
        r==JOLT_ERR_CAPABILITY?JFX_ERROR_NOT_IMPLEMENTED:r==JOLT_ERR_ARGUMENT?JFX_ERROR_INVALID_ARGUMENT:JFX_ERROR_BACKEND_FAILURE;
}
bool jfx_export_available(void) { return jolt_media_available(); }
bool jfx_export_codec_available(const char *name,bool audio) { return jolt_media_codec_available(name,audio); }
static void abort_output(jfx_export_job_t *j) {
    jolt_media_writer_close(j->writer); j->writer=NULL;
    if (j->file) { fclose(j->file); j->file=NULL; }
    if (j->temporary && *j->temporary) { remove(j->temporary); j->temporary[0]=0; }
}
void jfx_export_cancel(jfx_export_job_t *j) {
    if (!j || j->state!=JFX_EXPORT_RUNNING) return;
    abort_output(j); j->state=JFX_EXPORT_CANCELLED;
}
void jfx_export_destroy(jfx_export_job_t *j) {
    if (!j) return;
    abort_output(j); jfx_editor_destroy(j->editor); jfx_audio_mixer_destroy(j->mixer);
    release(j->pixels); release(j->pcm); release(j->path); release(j->temporary); release(j);
}
jfx_export_state_t jfx_export_state(const jfx_export_job_t *j) { return j?j->state:JFX_EXPORT_FAILED; }
uint64_t jfx_export_completed_frames(const jfx_export_job_t *j) { return j?j->completed:0; }
uint64_t jfx_export_total_frames(const jfx_export_job_t *j) { return j?j->total:0; }
static const char *container_for(const char *path) {
    const char *ext=strrchr(path,'.');
    if (!ext) return NULL;
    if (!strcmp(ext,".mp4")) return "mp4";
    if (!strcmp(ext,".mov")) return "mov";
    if (!strcmp(ext,".mkv")) return "matroska";
    if (!strcmp(ext,".webm")) return "webm";
    return NULL;
}
jfx_result_t jfx_export_begin(const jfx_editor_t *editor,const jfx_export_options_t *o,jfx_export_job_t **out) {
    if (!editor || !o || o->size<sizeof(*o) || !o->path || !*o->path || !out || strlen(o->path)>4096 ||
        strstr(o->path,"://") || o->width>4096 || o->height>4096 || o->start_frame>INT64_MAX || o->frame_count>INT64_MAX ||
        (o->sample_rate && (o->sample_rate<8000 || o->sample_rate>192000))) return JFX_ERROR_INVALID_ARGUMENT;
    const char *container=o->container?o->container:container_for(o->path);
    if (!container || (strcmp(container,"mp4") && strcmp(container,"mov") && strcmp(container,"matroska") && strcmp(container,"webm"))) return JFX_ERROR_INVALID_ARGUMENT;
    if (!jfx_export_available()) return JFX_ERROR_NOT_IMPLEMENTED;
    bool sequence=jfx_editor_kind(editor)==JFX_PROJECT_KIND_SEQUENCE;
    const jfx_timeline_t *timeline=jfx_editor_timeline((jfx_editor_t *)editor);
    uint64_t duration=sequence?jfx_timeline_duration(timeline):0;
    uint64_t count=o->frame_count?o->frame_count:(duration>o->start_frame?duration-o->start_frame:0);
    if (!count || count>10000000 || o->start_frame>(uint64_t)INT64_MAX-count) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t num=sequence?jfx_timeline_fps_num(timeline):(o->fps_num?o->fps_num:30);
    uint32_t den=sequence?jfx_timeline_fps_den(timeline):(o->fps_den?o->fps_den:1);
    if (!num || !den || num>INT_MAX || den>INT_MAX || (double)num/den<1 || (double)num/den>240 ||
        (sequence && ((o->fps_num && o->fps_num!=num) || (o->fps_den && o->fps_den!=den)))) return JFX_ERROR_INVALID_ARGUMENT;
    const char *video=o->video_codec?o->video_codec:!strcmp(container,"matroska")?"ffv1":!strcmp(container,"mov")?"prores":!strcmp(container,"webm")?"libvpx-vp9":"mpeg4";
    const char *audio=o->audio_codec?o->audio_codec:!strcmp(container,"mp4")?"aac":!strcmp(container,"webm")?"libopus":"pcm_s16le";
    if (!jfx_export_codec_available(video,false) || (sequence && o->audio && !jfx_export_codec_available(audio,true))) return JFX_ERROR_NOT_IMPLEMENTED;
    jfx_export_job_t *j=allocate(sizeof(*j)); if (!j) return JFX_ERROR_OUT_OF_MEMORY;
    memset(j,0,sizeof(*j)); j->state=JFX_EXPORT_RUNNING; j->start=o->start_frame; j->total=count;
    j->sequence=sequence; j->fps_num=num; j->fps_den=den;
    j->width=o->width?o->width:sequence?jfx_timeline_width(timeline):jfx_editor_graph_width(editor);
    j->height=o->height?o->height:sequence?jfx_timeline_height(timeline):jfx_editor_graph_height(editor);
    j->rate=o->sample_rate?o->sample_rate:48000;
    if (!j->width || !j->height || j->width>4096 || j->height>4096) { jfx_export_destroy(j); return JFX_ERROR_INVALID_ARGUMENT; }
    jfx_result_t result=JFX_ERROR_OUT_OF_MEMORY;
    char *text=allocate(JFX_PROJECT_MAX_BYTES); size_t length=0;
    if (!text) { jfx_export_destroy(j); return result; }
    result=jfx_editor_save(editor,text,JFX_PROJECT_MAX_BYTES,&length);
    j->editor=jfx_editor_create(j->width,j->height);
    if (result==JFX_SUCCESS && !j->editor) result=JFX_ERROR_OUT_OF_MEMORY;
    if (result==JFX_SUCCESS) result=jfx_editor_load(j->editor,text,length,NULL,0);
    release(text);
    if (result!=JFX_SUCCESS) goto fail;
    j->path=allocate(strlen(o->path)+1); j->temporary=allocate(strlen(o->path)+64);
    if (j->temporary) j->temporary[0]=0;
    j->pixels=allocate((size_t)j->width*j->height*4);
    if (!j->path || !j->temporary || !j->pixels) { result=JFX_ERROR_OUT_OF_MEMORY; goto fail; }
    strcpy(j->path,o->path); j->temporary[0]=0;
    if (sequence && o->audio) {
        timeline=jfx_editor_timeline(j->editor);
        result=jfx_audio_mixer_create(timeline,j->rate,&j->mixer);
        if (result==JFX_SUCCESS) result=jfx_timeline_audio_sample(timeline,j->start,j->rate,&j->audio_start);
        j->audio_cursor=j->audio_start;
        if (result!=JFX_SUCCESS) goto fail;
        j->pcm=allocate(JFX_AUDIO_MAX_BLOCK_FRAMES*2*sizeof(float));
        if (!j->pcm) { result=JFX_ERROR_OUT_OF_MEMORY; goto fail; }
    }
    for (unsigned slot=0;slot<1024;++slot) {
        snprintf(j->temporary,strlen(j->path)+64,"%s.jfx-part-%u",j->path,slot);
        errno=0; j->file=fopen(j->temporary,"wbx");
        if (j->file) break;
        if (errno!=EEXIST) { j->temporary[0]=0; result=JFX_ERROR_NOT_FOUND; goto fail; }
    }
    if (!j->file) { j->temporary[0]=0; result=JFX_ERROR_ALREADY_EXISTS; goto fail; }
    jolt_media_options_t media={.size=sizeof(media),.container=container,.video_codec=video,.audio_codec=audio,
        .width=j->width,.height=j->height,.fps_num=num,.fps_den=den,.sample_rate=j->rate,
        .audio=j->mixer!=NULL,.video_bitrate=o->video_bitrate,.audio_bitrate=o->audio_bitrate};
    result=mapped(jolt_media_writer_open(j->file,&media,&j->writer));
    if (result!=JFX_SUCCESS) goto fail;
    *out=j; return JFX_SUCCESS;
fail:
    jfx_export_destroy(j); return result;
}
jfx_result_t jfx_export_step(jfx_export_job_t *j,uint32_t max_frames) {
    if (!j || !max_frames || max_frames>1024) return JFX_ERROR_INVALID_ARGUMENT;
    if (j->state==JFX_EXPORT_COMPLETE) return JFX_SUCCESS;
    if (j->state==JFX_EXPORT_CANCELLED) return JFX_ERROR_INVALID_ARGUMENT;
    if (j->state==JFX_EXPORT_FAILED) return j->result;
    jfx_result_t r=JFX_SUCCESS;
    for (uint32_t i=0;i<max_frames && j->completed<j->total;++i) {
        uint64_t frame=j->start+j->completed;
        r=j->sequence?jfx_editor_render_frame(j->editor,frame,j->width,j->height,j->pixels,(size_t)j->width*j->height*4)
            :jfx_editor_render_graph(j->editor,UINT32_MAX,(double)frame*j->fps_den/j->fps_num,j->width,j->height,j->pixels,(size_t)j->width*j->height*4);
        if (r!=JFX_SUCCESS) goto failed;
        r=mapped(jolt_media_writer_video(j->writer,j->pixels)); if (r!=JFX_SUCCESS) goto failed;
        if (j->mixer) {
            uint64_t end;
            r=jfx_timeline_audio_sample(jfx_editor_timeline(j->editor),frame+1,j->rate,&end);
            if (r!=JFX_SUCCESS) goto failed;
            while (j->audio_cursor<end) {
                size_t n=end-j->audio_cursor>JFX_AUDIO_MAX_BLOCK_FRAMES?JFX_AUDIO_MAX_BLOCK_FRAMES:(size_t)(end-j->audio_cursor);
                r=jfx_audio_mixer_render(j->mixer,j->audio_cursor,n,j->pcm,n*2);
                if (r!=JFX_SUCCESS) goto failed;
                r=mapped(jolt_media_writer_audio(j->writer,j->pcm,n)); if (r!=JFX_SUCCESS) goto failed;
                j->audio_cursor+=n;
            }
        }
        ++j->completed;
    }
    if (j->completed==j->total) {
        r=mapped(jolt_media_writer_finish(j->writer)); if (r!=JFX_SUCCESS) goto failed;
        jolt_media_writer_close(j->writer); j->writer=NULL;
        int flush=fflush(j->file),closed=fclose(j->file); j->file=NULL;
        if (flush || closed) { r=JFX_ERROR_BACKEND_FAILURE; goto failed; }
#if defined(_WIN32)
        if (!MoveFileExA(j->temporary,j->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { r=JFX_ERROR_BACKEND_FAILURE; goto failed; }
#else
        if (rename(j->temporary,j->path)) { r=JFX_ERROR_BACKEND_FAILURE; goto failed; }
#endif
        j->temporary[0]=0; j->state=JFX_EXPORT_COMPLETE;
    }
    return JFX_SUCCESS;
failed:
    abort_output(j); j->state=JFX_EXPORT_FAILED; j->result=r; return r;
}
jfx_result_t jfx_editor_export_video(const jfx_editor_t *e,const jfx_export_options_t *o,jfx_media_progress_fn progress,void *user) {
    jfx_export_job_t *j=NULL; jfx_result_t r=jfx_export_begin(e,o,&j);
    if (r!=JFX_SUCCESS) return r;
    while (jfx_export_state(j)==JFX_EXPORT_RUNNING) {
        if (progress && !progress(user,j->completed,j->total)) { jfx_export_cancel(j); r=JFX_ERROR_INVALID_ARGUMENT; break; }
        r=jfx_export_step(j,1); if (r!=JFX_SUCCESS) break;
    }
    if (r==JFX_SUCCESS && progress) (void)progress(user,j->completed,j->total);
    jfx_export_destroy(j); return r;
}
