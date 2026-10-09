#include "jfx/jfx_audio.h"
#include "cpu_numeric.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_midi.h"
#include "jfx/jfx_automation.h"
#include "joltscript/audio_io.h"
#include "joltscript/audio_task.h"
#include "joltscript/audio_kernels.h"
#include "tilly/allocator.h"
#include <string.h>
#include <limits.h>
#include <stdlib.h>
typedef struct { uint64_t on,off; uint8_t channel,pitch; float velocity; } audio_note_t;

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
    uint32_t track;
    jolt_audio_reader_t *reader;
} audio_clip_t;
typedef struct {
    float gain;
    bool audible;
    size_t count;
    uint32_t latency;
    jfx_audio_insert_t settings[JFX_AUDIO_MAX_INSERTS];
    jfx_vst3_instance_t *inserts[JFX_AUDIO_MAX_INSERTS];
    void *states[JFX_AUDIO_MAX_INSERTS]; size_t state_bytes[JFX_AUDIO_MAX_INSERTS];
    audio_note_t *notes; size_t note_count;
    bool chase;
    bool has_audio;
    float *dry_delay; uint32_t dry_latency,dry_cursor;
    jfx_audio_automation_lane_t *automation; size_t automation_count;
} audio_track_t;
struct jfx_audio_mixer {
    audio_clip_t *clips; size_t count; uint32_t rate; jolt_program_t *program;
    audio_track_t *tracks; uint32_t track_count;
    float master_gain;
    double tempo;
    uint64_t next_sample;
    bool started;
};
void jfx_audio_mixer_destroy(jfx_audio_mixer_t *m) {
    if (!m) return;
    for (size_t i=0;i<m->count;++i) jolt_audio_reader_close(m->clips[i].reader);
    for (uint32_t t=0;t<m->track_count;++t) {
        for (size_t i=0;i<m->tracks[t].count;++i) { jfx_vst3_destroy(m->tracks[t].inserts[i]); release(m->tracks[t].states[i]); }
        release(m->tracks[t].notes);
        release(m->tracks[t].automation);
        release(m->tracks[t].dry_delay);
    }
    jolt_program_destroy(m->program); release(m->clips); release(m->tracks); release(m);
}
static jfx_result_t open_inserts(jfx_audio_mixer_t *m,audio_track_t *track) {
    track->latency=0;
    release(track->dry_delay); track->dry_delay=NULL; track->dry_latency=track->dry_cursor=0;
    track->chase=true; bool first=true;
    for (size_t i=0;i<track->count;++i) {
        jfx_vst3_destroy(track->inserts[i]); track->inserts[i]=NULL;
        if (!track->settings[i].enabled) continue;
        jfx_result_t r=jfx_vst3_create_with_state(&track->settings[i],track->states[i],track->state_bytes[i],m->rate,JFX_AUDIO_MAX_BLOCK_FRAMES,&track->inserts[i]);
        if (r!=JFX_SUCCESS) return r;
        if (jfx_vst3_is_instrument(track->inserts[i]) && !first) return JFX_ERROR_NOT_IMPLEMENTED;
        if (track->note_count && first && (!jfx_vst3_is_instrument(track->inserts[i]) || !jfx_vst3_accepts_midi(track->inserts[i]))) return JFX_ERROR_NOT_IMPLEMENTED;
        first=false;
        if (track->has_audio && jfx_vst3_is_instrument(track->inserts[i])) {
            track->dry_latency=jfx_vst3_latency(track->inserts[i]);
            if (track->dry_latency) {
                track->dry_delay=allocate((size_t)track->dry_latency*2*sizeof(float));
                if (!track->dry_delay) return JFX_ERROR_OUT_OF_MEMORY;
                memset(track->dry_delay,0,(size_t)track->dry_latency*2*sizeof(float));
            }
        }
        jfx_vst3_set_tempo(track->inserts[i],m->tempo);
        track->latency+=jfx_vst3_latency(track->inserts[i]);
        if (track->latency>m->rate*2) return JFX_ERROR_NOT_IMPLEMENTED;
    }
    if (track->note_count && first) return JFX_ERROR_NOT_IMPLEMENTED;
    return JFX_SUCCESS;
}
jfx_result_t jfx_audio_mixer_create(const jfx_timeline_t *t,uint32_t rate,jfx_audio_mixer_t **out) {
    if (!t || !out || rate<8000 || rate>192000) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_audio_mixer_t *m=allocate(sizeof(*m)); if (!m) return JFX_ERROR_OUT_OF_MEMORY;
    memset(m,0,sizeof(*m)); m->rate=rate; m->master_gain=jfx_timeline_master_audio_gain(t); m->tempo=jfx_timeline_audio_tempo(t);
    m->track_count=(uint32_t)jfx_timeline_track_count(t);
    if (m->track_count) {
        m->tracks=allocate(m->track_count*sizeof(*m->tracks));
        if (!m->tracks) { release(m); return JFX_ERROR_OUT_OF_MEMORY; }
        memset(m->tracks,0,m->track_count*sizeof(*m->tracks));
    }
    jolt_status_t compiled=jolt_audio_mix_compile(&m->program);
    if (compiled!=JOLT_OK) { jfx_audio_mixer_destroy(m); return compiled==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_BACKEND_FAILURE; }
    size_t count=0; bool solo=false;
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        solo=solo || jfx_timeline_track_solo(t,track);
        count+=jfx_timeline_clip_count(t,track);
    }
    if (count) m->clips=allocate(count*sizeof(*m->clips));
    if (count && !m->clips) { jfx_audio_mixer_destroy(m); return JFX_ERROR_OUT_OF_MEMORY; }
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        if (jfx_timeline_track_muted(t,track) || (solo && !jfx_timeline_track_solo(t,track))) continue;
        audio_track_t *mt=&m->tracks[track]; mt->audible=true; mt->gain=jfx_timeline_track_audio_gain(t,track);
        mt->automation_count=jfx_timeline_audio_automation_count(t,track);
        if (mt->automation_count) {
            mt->automation=allocate(mt->automation_count*sizeof(*mt->automation));
            if (!mt->automation) { jfx_audio_mixer_destroy(m); return JFX_ERROR_OUT_OF_MEMORY; }
            for (uint32_t i=0;i<mt->automation_count;++i) {
                mt->automation[i].size=sizeof(*mt->automation);
                if (jfx_timeline_get_audio_automation(t,track,i,mt->automation+i)!=JFX_SUCCESS) goto bad;
                for (uint32_t k=0;k<mt->automation[i].key_count;++k) {
                    uint64_t sample=0;
                    if (jfx_timeline_audio_sample(t,mt->automation[i].keys[k].frame,rate,&sample)!=JFX_SUCCESS) goto bad;
                    mt->automation[i].keys[k].frame=sample;
                }
            }
        }
        mt->count=jfx_timeline_audio_insert_count(t,track);
        for (uint32_t i=0;i<mt->count;++i) {
            mt->settings[i].size=sizeof(mt->settings[i]);
            if (jfx_timeline_get_audio_insert(t,track,i,&mt->settings[i])!=JFX_SUCCESS) goto bad;
            const void *state=NULL; size_t bytes=0;
            if (jfx_timeline_get_audio_insert_state(t,track,i,&state,&bytes)!=JFX_SUCCESS) goto bad;
            if (bytes) {
                mt->states[i]=allocate(bytes); if (!mt->states[i]) { jfx_audio_mixer_destroy(m); return JFX_ERROR_OUT_OF_MEMORY; }
                memcpy(mt->states[i],state,bytes); mt->state_bytes[i]=bytes;
            }
        }
        size_t note_capacity=0;
        for (uint32_t c=0;c<jfx_timeline_clip_count(t,track);++c) note_capacity+=jfx_timeline_midi_note_count(t,track,c);
        if (note_capacity) {
            mt->notes=allocate(note_capacity*sizeof(*mt->notes));
            if (!mt->notes) { jfx_audio_mixer_destroy(m); return JFX_ERROR_OUT_OF_MEMORY; }
        }
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            jfx_clip_audio_t a={.size=sizeof(a)};
            if (jfx_timeline_get_clip_audio(t,track,clip,&a)!=JFX_SUCCESS) goto bad;
            bool gain_automated=false;
            for (size_t k=0;k<mt->automation_count;++k) if (mt->automation[k].target==JFX_AUTOMATION_TRACK_GAIN) gain_automated=true;
            if (!a.enabled || !jfx_timeline_clip_enabled(t,track,clip) || a.gain==0 || (mt->gain==0 && !gain_automated)) continue;
            if (jfx_timeline_clip_source(t,track,clip)==JFX_CLIP_MIDI) {
                uint64_t start=jfx_timeline_clip_start(t,track,clip),len=jfx_timeline_clip_length(t,track,clip),in=jfx_timeline_clip_in_point(t,track,clip);
                for (uint32_t n=0;n<jfx_timeline_midi_note_count(t,track,clip);++n) {
                    jfx_midi_note_t note={.size=sizeof(note)};
                    if (jfx_timeline_get_midi_note(t,track,clip,n,&note)!=JFX_SUCCESS) goto bad;
                    if (note.frame>=in+len || note.frame+note.length<=in) continue;
                    uint64_t on=note.frame<in?in:note.frame,off=note.frame+note.length>in+len?in+len:note.frame+note.length;
                    audio_note_t *out_note=&mt->notes[mt->note_count++];
                    if (jfx_timeline_audio_sample(t,start+on-in,rate,&out_note->on)!=JFX_SUCCESS ||
                        jfx_timeline_audio_sample(t,start+off-in,rate,&out_note->off)!=JFX_SUCCESS) goto bad;
                    if (out_note->off<=out_note->on) { --mt->note_count; continue; }
                    out_note->channel=note.channel; out_note->pitch=note.pitch; out_note->velocity=note.velocity*a.gain;
                    if (out_note->velocity>1) out_note->velocity=1;
                }
                continue;
            }
            audio_clip_t *c=&m->clips[m->count]; memset(c,0,sizeof(*c));
            const char *path=jfx_timeline_clip_path(t,track,clip);
            if (!path || strlen(path)>=sizeof(c->path)) goto bad;
            strcpy(c->path,path); c->video=jfx_timeline_clip_source(t,track,clip)==JFX_CLIP_VIDEO;
            c->gain=a.gain; c->pan=a.pan; c->track=track;
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
            mt->has_audio=true;
        }
        jfx_result_t opened=open_inserts(m,mt);
        if (opened!=JFX_SUCCESS) { jfx_audio_mixer_destroy(m); return opened; }
    }
    *out=m; return JFX_SUCCESS;
bad:
    jfx_audio_mixer_destroy(m); return JFX_ERROR_INVALID_ARGUMENT;
}
static jfx_result_t audio_result(jolt_status_t r) {
    return r==JOLT_OK?JFX_SUCCESS:r==JOLT_ERR_MEMORY || r==JOLT_ERR_BUDGET?JFX_ERROR_OUT_OF_MEMORY:
        r==JOLT_ERR_NOT_FOUND?JFX_ERROR_NOT_FOUND:r==JOLT_ERR_CAPABILITY?JFX_ERROR_NOT_IMPLEMENTED:JFX_ERROR_BACKEND_FAILURE;
}
static jfx_result_t render_track(jfx_audio_mixer_t *m,uint32_t track,uint64_t start,size_t frames,float *mix,float *source) {
    memset(mix,0,frames*2*sizeof(float));
    size_t code_size; const uint8_t *code=jolt_program_data(m->program,&code_size);
    for (size_t i=0;i<m->count;++i) {
        audio_clip_t *c=&m->clips[i];
        if (c->track!=track) continue;
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
        if (r!=JOLT_OK) return audio_result(r);
    }
    return JFX_SUCCESS;
}
static int event_order(const void *a,const void *b) {
    const jfx_midi_event_t *x=a,*y=b;
    if (x->sample_offset!=y->sample_offset) return x->sample_offset<y->sample_offset?-1:1;
    return (int)x->type-(int)y->type;
}
static double automation_value(const jfx_audio_automation_lane_t *lane,uint64_t sample) {
    const jfx_audio_automation_key_t *keys=lane->keys; uint32_t k=1;
    if (sample<=keys[0].frame) return keys[0].value;
    while (k<lane->key_count && sample>=keys[k].frame) ++k;
    if (k==lane->key_count || lane->interpolation==JFX_INTERP_HOLD) return keys[k-1].value;
    double alpha=(double)(sample-keys[k-1].frame)/(double)(keys[k].frame-keys[k-1].frame);
    if (lane->interpolation==JFX_INTERP_SMOOTH) alpha=alpha*alpha*(3-2*alpha);
    return keys[k-1].value+(keys[k].value-keys[k-1].value)*alpha;
}
static jfx_result_t automate_plugin(audio_track_t *track,uint32_t insert,int64_t start,size_t frames) {
    jfx_vst3_instance_t *p=track->inserts[insert];
    for (size_t a=0;a<track->automation_count;++a) {
        const jfx_audio_automation_lane_t *lane=track->automation+a;
        if (lane->target!=JFX_AUTOMATION_PLUGIN_PARAMETER || lane->insert!=insert) continue;
        jfx_result_t r=jfx_vst3_add_parameter_point(p,lane->parameter,0,automation_value(lane,start>0?(uint64_t)start:0));
        if (r!=JFX_SUCCESS) return r;
        /* Linear ramps use exact endpoints/knots. Hold sends the old value
         * immediately before each step. Smooth adds bounded sampling points. */
        uint32_t offset=1,step=frames>4096?((uint32_t)frames+127)/128:64;
        while (offset<frames) {
            uint32_t next=(uint32_t)frames-1;
            if (lane->interpolation==JFX_INTERP_SMOOTH && offset+step-1<next) next=offset+step-1;
            for (uint32_t k=0;k<lane->key_count;++k) if ((int64_t)lane->keys[k].frame>start && (int64_t)lane->keys[k].frame<start+(int64_t)frames) {
                uint32_t point=(uint32_t)((int64_t)lane->keys[k].frame-start);
                if (point>=offset && point<next) next=point;
                if (lane->interpolation==JFX_INTERP_HOLD && point>offset && point-1<next) next=point-1;
            }
            int64_t clock=start+next;
            r=jfx_vst3_add_parameter_point(p,lane->parameter,next,automation_value(lane,clock>0?(uint64_t)clock:0));
            if (r!=JFX_SUCCESS) return r;
            offset=next+1;
        }
    }
    return JFX_SUCCESS;
}
static jfx_result_t process_track(audio_track_t *track,uint64_t start,size_t frames,float *pcm) {
    jfx_midi_event_t events[JFX_VST3_MAX_EVENTS]; size_t count=0;
    for (size_t n=0;n<track->note_count;++n) {
        audio_note_t *note=track->notes+n;
        bool on=(note->on>=start && note->on<start+frames) || (track->chase && note->on<start && note->off>start);
        bool off=note->off>=start && note->off<start+frames;
        if (count+(size_t)on+(size_t)off>JFX_VST3_MAX_EVENTS) return JFX_ERROR_OUT_OF_MEMORY;
        if (on) events[count++]=(jfx_midi_event_t){sizeof(jfx_midi_event_t),note->on<start?0:(uint32_t)(note->on-start),JFX_MIDI_NOTE_ON,note->channel,note->pitch,note->velocity,(int32_t)n};
        if (off) events[count++]=(jfx_midi_event_t){sizeof(jfx_midi_event_t),(uint32_t)(note->off-start),JFX_MIDI_NOTE_OFF,note->channel,note->pitch,0,(int32_t)n};
    }
    qsort(events,count,sizeof(*events),event_order); track->chase=false;
    uint32_t preceding_latency=0;
    for (size_t i=0;i<track->count;++i) if (track->inserts[i]) {
        float *dry=NULL;
        if (track->has_audio && jfx_vst3_is_instrument(track->inserts[i])) {
            dry=allocate(frames*2*sizeof(float)); if (!dry) return JFX_ERROR_OUT_OF_MEMORY;
            memcpy(dry,pcm,frames*2*sizeof(float));
        }
        jfx_result_t r=automate_plugin(track,(uint32_t)i,(int64_t)start-(int64_t)preceding_latency,frames);
        if (r==JFX_SUCCESS) r=jfx_vst3_process_events(track->inserts[i],start,frames,pcm,events,count);
        if (r==JFX_SUCCESS && dry) for (size_t n=0;n<frames;++n) {
            for (unsigned c=0;c<2;++c) {
                if (track->dry_latency) {
                    size_t slot=(size_t)track->dry_cursor*2+c; pcm[n*2+c]+=track->dry_delay[slot]; track->dry_delay[slot]=dry[n*2+c];
                } else pcm[n*2+c]+=dry[n*2+c];
            }
            if (track->dry_latency) track->dry_cursor=(track->dry_cursor+1)%track->dry_latency;
        }
        release(dry);
        if (r!=JFX_SUCCESS) return r;
        count=0;
        preceding_latency+=jfx_vst3_latency(track->inserts[i]);
    }
    return JFX_SUCCESS;
}
jfx_result_t jfx_audio_mixer_render(jfx_audio_mixer_t *m,uint64_t start,size_t frames,float *out,size_t capacity) {
    if (!m || !out || !frames || frames>JFX_AUDIO_MAX_BLOCK_FRAMES || capacity<frames*2 ||
        start>(uint64_t)INT64_MAX-frames-m->rate*2u) return JFX_ERROR_INVALID_ARGUMENT;
    size_t scratch=frames;
    for (uint32_t t=0;t<m->track_count;++t) if (m->tracks[t].latency>scratch)
        scratch=m->tracks[t].latency>JFX_AUDIO_MAX_BLOCK_FRAMES?JFX_AUDIO_MAX_BLOCK_FRAMES:m->tracks[t].latency;
    float *mix=allocate(frames*2*sizeof(float)),*source=allocate(scratch*2*sizeof(float)),*channel=allocate(scratch*2*sizeof(float));
    if (!mix || !source || !channel) { release(mix); release(source); release(channel); return JFX_ERROR_OUT_OF_MEMORY; }
    memset(mix,0,frames*2*sizeof(float)); jfx_result_t result=JFX_SUCCESS;
    bool discontinuity=!m->started || start!=m->next_sample;
    for (uint32_t t=0;t<m->track_count && result==JFX_SUCCESS;++t) {
        audio_track_t *track=&m->tracks[t]; if (!track->audible) continue;
        if (discontinuity) {
            if (m->started) result=open_inserts(m,track);
            /* Feed each rack's combined delay before emitting output, aligning
             * delayed tracks with zero-latency tracks without clipping headroom. */
            uint32_t primed=0;
            while (result==JFX_SUCCESS && primed<track->latency) {
                size_t n=track->latency-primed; if (n>scratch) n=scratch;
                result=render_track(m,t,start+primed,n,channel,source);
                if (result==JFX_SUCCESS) result=process_track(track,start+primed,n,channel);
                primed+=(uint32_t)n;
            }
        }
        if (result==JFX_SUCCESS) result=render_track(m,t,start+track->latency,frames,channel,source);
        if (result==JFX_SUCCESS) result=process_track(track,start+track->latency,frames,channel);
        bool automated=false;
        for (size_t a=0;a<track->automation_count;++a) if (track->automation[a].target==JFX_AUTOMATION_TRACK_GAIN) automated=true;
        if (result==JFX_SUCCESS && !automated) jfx_cpu_accumulate(mix,channel,frames*2,track->gain,m->master_gain);
        else if (result==JFX_SUCCESS) for (size_t i=0;i<frames;++i) {
            double gain=track->gain;
            for (size_t a=0;a<track->automation_count;++a) if (track->automation[a].target==JFX_AUTOMATION_TRACK_GAIN) gain=automation_value(track->automation+a,start+i);
            for (unsigned c=0;c<2;++c) mix[i*2+c]+=channel[i*2+c]*(float)gain*m->master_gain;
        }
    }
    if (result==JFX_SUCCESS) memcpy(out,mix,frames*2*sizeof(float));
    m->started=true; m->next_sample=result==JFX_SUCCESS?start+frames:UINT64_MAX;
    release(source); release(channel); release(mix); return result;
}
jfx_result_t jfx_timeline_render_audio(const jfx_timeline_t *t,uint64_t start,uint32_t rate,size_t frames,float *out,size_t capacity) {
    if (!out || !frames || frames>JFX_AUDIO_MAX_BLOCK_FRAMES || capacity<frames*2) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_audio_mixer_t *m=NULL; jfx_result_t r=jfx_audio_mixer_create(t,rate,&m);
    if (r==JFX_SUCCESS) r=jfx_audio_mixer_render(m,start,frames,out,capacity);
    jfx_audio_mixer_destroy(m); return r;
}
