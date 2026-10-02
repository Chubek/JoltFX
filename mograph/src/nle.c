#include "jfx/jfx_editor.h"
#include "jfx/jfx_audio.h"
#include <stdarg.h>
#include <stdio.h>
#include "tilly/containers.h"
#include "tilly/attributes.h"

TILLY_PRINTF_LIKE(4, 5)
static bool append(char *out,size_t cap,size_t *used,const char *fmt,...) {
    if (*used>=cap) return false;
    va_list args; va_start(args,fmt); int n=vsnprintf(out+*used,cap-*used,fmt,args); va_end(args);
    if (n<0 || (size_t)n>=cap-*used) return false;
    *used+=(size_t)n; return true;
}
static bool string(char *out,size_t cap,size_t *used,const char *text) {
    if (!append(out,cap,used,"\"")) return false;
    for (const unsigned char *p=(const unsigned char *)(text?text:"");*p;++p) {
        if (*p=='"' || *p=='\\') { if (!append(out,cap,used,"\\%c",*p)) return false; }
        else if (*p<32) { if (!append(out,cap,used,"\\u%04x",(unsigned)*p)) return false; }
        else if (!append(out,cap,used,"%c",*p)) return false;
    }
    return append(out,cap,used,"\"");
}
jfx_result_t jfx_editor_sequence_state(const jfx_editor_t *editor,char *out,size_t cap) {
    if (!editor || !out || !cap) return JFX_ERROR_INVALID_ARGUMENT;
    const jfx_timeline_t *t=jfx_editor_timeline((jfx_editor_t *)editor);
    size_t used=0;
#define APP(...) do { if (!append(out,cap,&used,__VA_ARGS__)) return JFX_ERROR_OUT_OF_MEMORY; } while (0)
#define STR(s) do { if (!string(out,cap,&used,s)) return JFX_ERROR_OUT_OF_MEMORY; } while (0)
    APP("{\"width\":%u,\"height\":%u,\"fpsNum\":%u,\"fpsDen\":%u,\"duration\":%llu,\"canUndo\":%s,\"canRedo\":%s,\"tracks\":[",
        jfx_timeline_width(t),jfx_timeline_height(t),jfx_timeline_fps_num(t),jfx_timeline_fps_den(t),
        (unsigned long long)jfx_timeline_duration(t),jfx_editor_can_undo(editor)?"true":"false",jfx_editor_can_redo(editor)?"true":"false");
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        APP("%s{\"name\":",track?",":""); STR(jfx_timeline_track_name(t,track));
        APP(",\"muted\":%s,\"solo\":%s,\"opacity\":%.9g,\"audioGain\":%.9g,\"clips\":[",
            jfx_timeline_track_muted(t,track)?"true":"false",jfx_timeline_track_solo(t,track)?"true":"false",
            (double)jfx_timeline_track_opacity(t,track),(double)jfx_timeline_track_audio_gain(t,track));
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            APP("%s{\"name\":",clip?",":""); STR(jfx_timeline_clip_name(t,track,clip));
            APP(",\"source\":"); STR(jfx_clip_source_name(jfx_timeline_clip_source(t,track,clip)));
            APP(",\"path\":"); STR(jfx_timeline_clip_path(t,track,clip));
            APP(",\"start\":%llu,\"length\":%llu,\"inPoint\":%llu,\"enabled\":%s,\"opacity\":%.9g,\"effects\":%zu",
                (unsigned long long)jfx_timeline_clip_start(t,track,clip),
                (unsigned long long)jfx_timeline_clip_length(t,track,clip),
                (unsigned long long)jfx_timeline_clip_in_point(t,track,clip),
                jfx_timeline_clip_enabled(t,track,clip)?"true":"false",
                (double)jfx_timeline_clip_opacity(t,track,clip),jfx_timeline_effect_count(t,track,clip));
            jfx_clip_audio_t audio={.size=sizeof(audio)};
            if (jfx_timeline_get_clip_audio(t,track,clip,&audio)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
            APP(",\"audio\":{\"enabled\":%s,\"gain\":%.9g,\"pan\":%.9g,\"fadeIn\":%llu,\"fadeOut\":%llu}}",
                audio.enabled?"true":"false",(double)audio.gain,(double)audio.pan,
                (unsigned long long)audio.fade_in_frames,(unsigned long long)audio.fade_out_frames);
        }
        APP("]}");
    }
    APP("]}");
#undef APP
#undef STR
    return JFX_SUCCESS;
}

jfx_result_t jfx_editor_write_frame(jfx_editor_t *e,uint64_t frame,uint32_t w,uint32_t h,const char *path) {
    if (!e || !path || !*path || !w || !h || w>4096 || h>4096) return JFX_ERROR_INVALID_ARGUMENT;
    size_t count=(size_t)w*h;
    uint8_t *pixels=tilly_container_alloc(count*4);
    if (!pixels) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r=jfx_editor_render_frame(e,frame,w,h,pixels,count*4);
    if (r==JFX_SUCCESS) {
        /* Pack RGB in place, ahead of the read cursor. */
        for (size_t p=0;p<count;++p) for (size_t c=0;c<3;++c) pixels[p*3+c]=pixels[p*4+c];
        FILE *file=fopen(path,"wb");
        if (!file) r=JFX_ERROR_NOT_FOUND;
        else {
            if (fprintf(file,"P6\n%u %u\n255\n",w,h)<0 || fwrite(pixels,3,count,file)!=count) r=JFX_ERROR_BACKEND_FAILURE;
            if (fclose(file)) r=JFX_ERROR_BACKEND_FAILURE;
        }
    }
    tilly_container_free(pixels); return r;
}
