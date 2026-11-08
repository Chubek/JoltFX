#include "jfx/jfx_nle.h"
#include "joltscript/compiler.h"
#include "joltscript/media_analysis.h"
#include "joltscript/audio_io.h"
#include "joltscript/video_io.h"
#include "joltscript/subtitle_io.h"
#include "tilly/memory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *jfx_nle_ffmpeg_version(void) { return jolt_media_ffmpeg_version(); }
bool jfx_nle_subtitles_available(void) { return jolt_subtitle_available()!=0; }
const char *jfx_nle_encoder_at(size_t index,bool audio) { return jolt_media_encoder_at(index,audio); }
jfx_result_t jfx_nle_probe(const char *path,jfx_nle_media_info_t *out) {
    if (!path || !*path || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    jolt_media_info_t info={.size=sizeof(info)};
    int r=jolt_media_probe(path,&info);
    if (r) return r==-2?JFX_ERROR_NOT_IMPLEMENTED:JFX_ERROR_NOT_FOUND;
    jfx_nle_media_info_t value={.size=sizeof(value),.width=info.width,.height=info.height,
        .fps_num=info.fps_num,.fps_den=info.fps_den,.duration_seconds=info.duration,.video=info.video!=0,.audio=info.audio!=0};
    memcpy(value.format,info.format,64); memcpy(value.video_codec,info.video_codec,64); memcpy(value.audio_codec,info.audio_codec,64);
    *out=value; return JFX_SUCCESS;
}
jfx_result_t jfx_nle_convert_begin(const char *source,const jfx_export_options_t *o,jfx_export_job_t **out) {
    if (!source || !o || o->size<sizeof(*o) || !o->path || !out || !strcmp(source,o->path) ||
        (o->fps_num==0)!=(o->fps_den==0)) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_nle_media_info_t info={.size=sizeof(info)};
    jfx_result_t r=jfx_nle_probe(source,&info); if (r!=JFX_SUCCESS) return r;
    uint32_t width=o->width?o->width:info.video?info.width:16;
    uint32_t height=o->height?o->height:info.video?info.height:16;
    uint32_t num=o->fps_num?o->fps_num:info.fps_num?info.fps_num:30;
    uint32_t den=o->fps_den?o->fps_den:info.fps_den?info.fps_den:1;
    double frames=ceil(info.duration_seconds*(double)num/den);
    if (!isfinite(frames) || frames<1 || frames>10000000 || width>4096 || height>4096 || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    uint64_t duration=(uint64_t)frames;
    if (o->start_frame>=duration || o->frame_count>duration-o->start_frame) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_editor_t *editor=jfx_editor_create(width,height);
    if (!editor) return JFX_ERROR_OUT_OF_MEMORY;
    r=jfx_editor_command(editor,"sequence.new",width,height,num,den,"");
    if (r==JFX_SUCCESS) r=jfx_editor_command(editor,"clip.add",0,info.video?JFX_CLIP_VIDEO:JFX_CLIP_AUDIO,0,frames,source);
    jfx_export_options_t options=*o; options.fps_num=num; options.fps_den=den; options.audio=o->audio && info.audio;
    if (r==JFX_SUCCESS) r=jfx_export_begin(editor,&options,out);
    jfx_editor_destroy(editor); return r;
}

jfx_result_t jfx_nle_analyze_pcm(const float *pcm,size_t frames,uint32_t rate,jfx_nle_audio_analysis_t *out) {
    if (!out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_nle_audio_analysis_t value={.size=sizeof(value)}; uint64_t onset=UINT64_MAX;
    jolt_status_t s=jolt_media_analyze(pcm,frames,rate,&value.integrated_lufs,&value.sample_peak,&onset,&value.confidence);
    if (s!=JOLT_OK) return s==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_INVALID_ARGUMENT;
    value.clap_detected=onset!=UINT64_MAX; value.clap_seconds=value.clap_detected?(double)onset/rate:0;
    *out=value; return JFX_SUCCESS;
}
jfx_result_t jfx_nle_analyze_file(const char *path,double seconds,jfx_nle_audio_analysis_t *out) {
    if (!path || !*path || !out || out->size<sizeof(*out) || !isfinite(seconds) || seconds<.1 || seconds>60) return JFX_ERROR_INVALID_ARGUMENT;
    jolt_audio_reader_t *reader=NULL;
    jolt_status_t s=jolt_audio_reader_open(path,48000,&reader);
    if (s!=JOLT_OK) return s==JOLT_ERR_CAPABILITY?JFX_ERROR_NOT_IMPLEMENTED:JFX_ERROR_NOT_FOUND;
    size_t frames=(size_t)(seconds*48000);
    float *pcm=tilly_mem_alloc(frames*2*sizeof(float));
    if (!pcm) { jolt_audio_reader_close(reader); return JFX_ERROR_OUT_OF_MEMORY; }
    for (size_t start=0;start<frames && s==JOLT_OK;start+=4096) {
        size_t n=frames-start; if (n>4096) n=4096;
        s=jolt_audio_reader_read(reader,start,n,pcm+start*2);
    }
    jolt_audio_reader_close(reader);
    jfx_result_t r=s==JOLT_OK?jfx_nle_analyze_pcm(pcm,frames,48000,out):JFX_ERROR_BACKEND_FAILURE;
    tilly_mem_free(pcm); return r;
}
jfx_result_t jfx_nle_sync_clap(jfx_editor_t *e,uint32_t rt,uint32_t rc,uint32_t tt,uint32_t tc,double rs,double ts) {
    if (!e || !isfinite(rs) || !isfinite(ts) || rs<0 || ts<0 || rs>60 || ts>60 || (rt==tt && rc==tc)) return JFX_ERROR_INVALID_ARGUMENT;
    const jfx_timeline_t *t=jfx_editor_timeline(e);
    if (rc>=jfx_timeline_clip_count(t,rt) || tc>=jfx_timeline_clip_count(t,tt)) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_clip_source_t a=jfx_timeline_clip_source(t,rt,rc),b=jfx_timeline_clip_source(t,tt,tc);
    if ((a!=JFX_CLIP_VIDEO && a!=JFX_CLIP_AUDIO) || (b!=JFX_CLIP_VIDEO && b!=JFX_CLIP_AUDIO)) return JFX_ERROR_INVALID_ARGUMENT;
    double frame=round((double)jfx_timeline_clip_start(t,rt,rc)+(rs-ts)*jfx_timeline_fps(t)
        -(double)jfx_timeline_clip_in_point(t,rt,rc)+(double)jfx_timeline_clip_in_point(t,tt,tc));
    if (frame<0 || frame>1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t r=jfx_editor_begin_edit(e,JFX_PROJECT_KIND_SEQUENCE);
    if (r!=JFX_SUCCESS) return r;
    jfx_editor_edit_label(e,"Synchronize clap");
    r=jfx_editor_command(e,"clip.move",tt,tc,tt,frame,"");
    if (r==JFX_SUCCESS) r=jfx_editor_commit_edit(e);
    if (r!=JFX_SUCCESS) jfx_editor_cancel_edit(e);
    return r;
}

static bool sequence_op(const char *op) {
    return !strncmp(op,"clip.",5) || !strncmp(op,"track.",6) ||
        !strncmp(op,"effect.",7) || !strncmp(op,"grade.",6) ||
        !strncmp(op,"calibration.",12) || !strncmp(op,"audio.",6) ||
        !strncmp(op,"midi.",5);
}
static bool number(const jolt_value_t *v,double *out) {
    switch (v->type) {
    case JOLT_TYPE_I32: case JOLT_TYPE_I64: *out=(double)v->as.integer; break;
    case JOLT_TYPE_U8: case JOLT_TYPE_U16: case JOLT_TYPE_U32: case JOLT_TYPE_U64: *out=(double)v->as.unsigned_integer; break;
    case JOLT_TYPE_F32: case JOLT_TYPE_F64: *out=v->as.number; break;
    default: return false;
    }
    return isfinite(*out) && *out>=-1.e9 && *out<=1.e9;
}
static bool text_value(const jolt_value_t *v,char *out,size_t cap) {
    if (v->type!=JOLT_TYPE_STRING || v->as.string.size>=cap ||
        memchr(v->as.string.data,0,v->as.string.size)) return false;
    memcpy(out,v->as.string.data,v->as.string.size); out[v->as.string.size]=0; return true;
}
jfx_result_t jfx_nle_run_script(jfx_editor_t *e,const char *source,char *error,size_t cap) {
    if (error && cap) *error=0;
    if (!e || !source || !*source || strlen(source)>65536) return JFX_ERROR_INVALID_ARGUMENT;
    jolt_program_t *program=NULL; jolt_diagnostic_t d={.size=sizeof(d)};
    jolt_status_t status=jolt_compile(source,&program,&d);
    if (status!=JOLT_OK) {
        if (error && cap) snprintf(error,cap,"%zu:%zu: %s",d.line,d.column,d.message);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jolt_vm_t *vm=jolt_vm_create();
    if (!vm) { jolt_program_destroy(program); return JFX_ERROR_OUT_OF_MEMORY; }
    jolt_value_t output={0}; jolt_run_options_t options={.size=sizeof(options),.steps=100000};
    status=jolt_program_run_values(vm,program,NULL,0,&output,1,&options);
    jolt_program_destroy(program);
    jfx_result_t r=JFX_ERROR_INVALID_ARGUMENT; bool active=false; size_t index=0;
    if (status!=JOLT_OK || (output.type!=JOLT_TYPE_VECTOR && output.type!=JOLT_TYPE_LIST) || output.as.collection.size>256) goto done;
    r=jfx_editor_begin_edit(e,JFX_PROJECT_KIND_SEQUENCE);
    if (r!=JFX_SUCCESS) goto done;
    active=true; jfx_editor_edit_label(e,"Joltscript edit");
    for (;index<output.as.collection.size;++index) {
        const jolt_value_t *row=output.as.collection.items+index;
        r=JFX_ERROR_INVALID_ARGUMENT;
        if (row->type!=JOLT_TYPE_VECTOR || row->as.collection.size!=6) goto done;
        const jolt_value_t *v=row->as.collection.items;
        char op[64],text[JFX_NODE_PATH_MAX]; double nums[4];
        if (!text_value(v,op,sizeof(op)) || !sequence_op(op) || !text_value(v+5,text,sizeof(text))) goto done;
        for (size_t k=0;k<4;++k) if (!number(v+k+1,nums+k) || (k<3 && (nums[k]<0 || floor(nums[k])!=nums[k]))) goto done;
        r=jfx_editor_command(e,op,(uint32_t)nums[0],(uint32_t)nums[1],(uint32_t)nums[2],nums[3],text);
        if (r!=JFX_SUCCESS) goto done;
    }
    r=jfx_editor_commit_edit(e); if (r==JFX_SUCCESS) active=false;
done:
    if (active) jfx_editor_cancel_edit(e);
    if (r!=JFX_SUCCESS && error && cap) snprintf(error,cap,"Joltscript edit failed at command %zu: %s (VM %d)",index+1,jfx_result_to_string(r),(int)status);
    jolt_vm_destroy(vm); return r;
}
jfx_result_t jfx_nle_relink(jfx_editor_t *e,const char *old_path,const char *new_path) {
    if (!e || !old_path || !*old_path || !new_path || !*new_path ||
        strlen(old_path)>=JFX_NODE_PATH_MAX || strlen(new_path)>=JFX_NODE_PATH_MAX || strstr(new_path,"://")) return JFX_ERROR_INVALID_ARGUMENT;
    /* Copy borrowed paths before any command can invalidate them. */
    char old_copy[JFX_NODE_PATH_MAX],new_copy[JFX_NODE_PATH_MAX];
    strcpy(old_copy,old_path); strcpy(new_copy,new_path);
    FILE *file=fopen(new_copy,"rb"); if (!file) return JFX_ERROR_NOT_FOUND; fclose(file);
    jfx_result_t r=jfx_editor_begin_edit(e,JFX_PROJECT_KIND_SEQUENCE);
    if (r!=JFX_SUCCESS) return r;
    jfx_editor_edit_label(e,"Relink media"); bool found=false;
    const jfx_timeline_t *t=jfx_editor_timeline(e);
    for (uint32_t a=0;a<jfx_timeline_track_count(t);++a) for (uint32_t b=0;b<jfx_timeline_clip_count(t,a);++b) {
        const char *path=jfx_timeline_clip_path(t,a,b);
        if (!path || strcmp(path,old_copy)) continue;
        found=true; r=jfx_editor_command(e,"clip.path",a,b,0,0,new_copy);
        if (r!=JFX_SUCCESS) goto fail;
    }
    if (!found) { r=JFX_ERROR_NOT_FOUND; goto fail; }
    r=jfx_editor_commit_edit(e); if (r==JFX_SUCCESS) return r;
fail:
    jfx_editor_cancel_edit(e); return r;
}
