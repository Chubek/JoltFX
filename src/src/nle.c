#include "jfx/jfx_editor.h"
#include "jfx/jfx_audio.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_midi.h"
#include "jfx/jfx_automation.h"
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
    APP("{\"width\":%u,\"height\":%u,\"fpsNum\":%u,\"fpsDen\":%u,\"duration\":%llu,\"canUndo\":%s,\"canRedo\":%s,\"masterAudioGain\":%.9g,\"tracks\":[",
        jfx_timeline_width(t),jfx_timeline_height(t),jfx_timeline_fps_num(t),jfx_timeline_fps_den(t),
        (unsigned long long)jfx_timeline_duration(t),jfx_editor_can_undo(editor)?"true":"false",jfx_editor_can_redo(editor)?"true":"false",(double)jfx_timeline_master_audio_gain(t));
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        APP("%s{\"name\":",track?",":""); STR(jfx_timeline_track_name(t,track));
        APP(",\"muted\":%s,\"solo\":%s,\"opacity\":%.9g,\"audioGain\":%.9g,\"audioInserts\":[",
            jfx_timeline_track_muted(t,track)?"true":"false",jfx_timeline_track_solo(t,track)?"true":"false",
            (double)jfx_timeline_track_opacity(t,track),(double)jfx_timeline_track_audio_gain(t,track));
        for (uint32_t i=0;i<jfx_timeline_audio_insert_count(t,track);++i) {
            jfx_audio_insert_t in={.size=sizeof(in)};
            if (jfx_timeline_get_audio_insert(t,track,i,&in)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
            APP("%s{\"cid\":",i?",":""); STR(in.cid); APP(",\"path\":"); STR(in.path);
            APP(",\"enabled\":%s,\"parameters\":[",in.enabled?"true":"false");
            for (uint32_t p=0;p<in.parameter_count;++p) APP("%s{\"id\":%u,\"value\":%.17g}",p?",":"",in.parameters[p].id,in.parameters[p].value);
            const void *state=NULL; size_t bytes=0;
            if (jfx_timeline_get_audio_insert_state(t,track,i,&state,&bytes)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
            APP("],\"stateBytes\":%zu}",bytes);
        }
        APP("],\"audioAutomation\":[");
        for (uint32_t a=0;a<jfx_timeline_audio_automation_count(t,track);++a) {
            jfx_audio_automation_lane_t lane={.size=sizeof(lane)};
            if (jfx_timeline_get_audio_automation(t,track,a,&lane)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
            APP("%s{\"target\":%u,\"insert\":%u,\"parameter\":%u,\"interpolation\":%u,\"keys\":[",a?",":"",(unsigned)lane.target,lane.insert,lane.parameter,(unsigned)lane.interpolation);
            for (uint32_t k=0;k<lane.key_count;++k) APP("%s{\"frame\":%llu,\"value\":%.17g}",k?",":"",(unsigned long long)lane.keys[k].frame,lane.keys[k].value);
            APP("]}");
        }
        APP("],\"clips\":[");
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
            APP(",\"audio\":{\"enabled\":%s,\"gain\":%.9g,\"pan\":%.9g,\"fadeIn\":%llu,\"fadeOut\":%llu},\"midiNotes\":[",
                audio.enabled?"true":"false",(double)audio.gain,(double)audio.pan,
                (unsigned long long)audio.fade_in_frames,(unsigned long long)audio.fade_out_frames);
            for (uint32_t n=0;n<jfx_timeline_midi_note_count(t,track,clip);++n) {
                jfx_midi_note_t note={.size=sizeof(note)};
                if (jfx_timeline_get_midi_note(t,track,clip,n,&note)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
                APP("%s{\"pitch\":%u,\"channel\":%u,\"frame\":%llu,\"length\":%llu,\"velocity\":%.9g}",n?",":"",note.pitch,note.channel,(unsigned long long)note.frame,(unsigned long long)note.length,(double)note.velocity);
            }
            APP("]}");
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
