#include "jfx/jfx_audio.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_export.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void u16(FILE *f, unsigned v) { fputc((int)(v&255),f); fputc((int)((v>>8)&255),f); }
static void u32(FILE *f, unsigned v) { u16(f,v); u16(f,v>>16); }
static void fixture(const char *path,bool varying) {
    FILE *f=fopen(path,"wb"); assert(f);
    fwrite("RIFF",1,4,f); u32(f,36+16000); fwrite("WAVEfmt ",1,8,f); u32(f,16);
    u16(f,1); u16(f,1); u32(f,8000); u32(f,16000); u16(f,2); u16(f,16);
    fwrite("data",1,4,f); u32(f,16000);
    for (unsigned i=0;i<8000;++i) u16(f,varying?(unsigned)(int)(12000*sinf((float)i*0.31f)):(i<4000?8192:16384));
    assert(!fclose(f));
}
static void near(float a,float b) { assert(fabsf(a-b)<0.001f); }
static void rational_timing(const char *path) {
    jfx_timeline_t *t=jfx_timeline_create(2,2,30000,1001); assert(t);
    assert(jfx_timeline_add_track(t,"fractional")==0);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_AUDIO; d.image_path=path;
    d.start_frame=1; d.length_frames=28; d.in_point=3; d.enabled=true; d.opacity=0;
    assert(jfx_timeline_add_clip(t,0,&d)==0);
    uint64_t boundary=0;
    assert(jfx_timeline_audio_sample(t,7,44100,&boundary)==JFX_SUCCESS && boundary==10301);
    assert(jfx_timeline_audio_sample(t,30000,44100,&boundary)==JFX_SUCCESS && boundary==44144100);
    assert(jfx_timeline_audio_sample(t,UINT64_MAX,44100,&boundary)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_clip_audio_t audio={.size=sizeof(audio)}; assert(jfx_timeline_get_clip_audio(t,0,0,&audio)==JFX_SUCCESS);
    audio.fade_in_frames=10; audio.fade_out_frames=8;
    assert(jfx_timeline_set_clip_audio(t,0,0,&audio)==JFX_SUCCESS);
    float original[1024],split[1024];
    assert(jfx_timeline_render_audio(t,10000,44100,512,original,1024)==JFX_SUCCESS);
    assert(jfx_timeline_split_clip(t,0,0,7)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,10000,44100,512,split,1024)==JFX_SUCCESS);
    assert(!memcmp(original,split,sizeof(split)));
    assert(jfx_timeline_render_audio(t,13000,44100,512,original,1024)==JFX_SUCCESS);
    assert(jfx_timeline_trim_clip(t,0,1,8,21)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,13000,44100,512,split,1024)==JFX_SUCCESS);
    assert(!memcmp(original,split,sizeof(split)));
    jfx_audio_mixer_t *m=NULL; assert(jfx_audio_mixer_create(t,44100,&m)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,13000,197,split,394)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,13197,315,split+394,630)==JFX_SUCCESS);
    assert(!memcmp(original,split,sizeof(split)));
    assert(jfx_audio_mixer_render(m,13000,512,split,1024)==JFX_SUCCESS);
    assert(!memcmp(original,split,sizeof(split)));
    assert(jfx_timeline_set_track_muted(t,0,true)==JFX_SUCCESS);
    jfx_timeline_destroy(t);
    /* The mixer keeps its own model/media snapshot. */
    assert(jfx_audio_mixer_render(m,13000,512,split,1024)==JFX_SUCCESS);
    assert(!memcmp(original,split,sizeof(split))); jfx_audio_mixer_destroy(m);
}
static void overlaps(const char *path) {
    jfx_timeline_t *t=jfx_timeline_create(2,2,25,1); assert(t);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_AUDIO; d.image_path=path;
    d.length_frames=50; d.enabled=true; d.opacity=0;
    assert(jfx_timeline_add_track(t,"one")==0 && jfx_timeline_add_track(t,"two")==1);
    assert(jfx_timeline_add_clip(t,0,&d)==0 && jfx_timeline_add_clip(t,1,&d)==0);
    jfx_clip_audio_t a={.size=sizeof(a)}; assert(jfx_timeline_get_clip_audio(t,0,0,&a)==JFX_SUCCESS);
    a.gain=4; assert(jfx_timeline_set_clip_audio(t,0,0,&a)==JFX_SUCCESS);
    float pcm[128]; assert(jfx_timeline_render_audio(t,5000,8000,64,pcm,128)==JFX_SUCCESS);
    near(pcm[0],2.5f); /* Unclipped sum, independent of visual opacity. */
    assert(jfx_timeline_set_track_solo(t,1,true)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,5000,8000,64,pcm,128)==JFX_SUCCESS); near(pcm[0],0.5f);
    assert(jfx_timeline_set_track_muted(t,1,true)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,5000,8000,64,pcm,128)==JFX_SUCCESS); near(pcm[0],0);
    assert(jfx_timeline_set_track_solo(t,1,false)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,9000,8000,64,pcm,128)==JFX_SUCCESS); near(pcm[0],0);
    jfx_timeline_destroy(t);
}
static void wav_bounce(const char *source) {
    char path[2048]; assert(snprintf(path,sizeof(path),"%s.bounce.wav",source)>0);
    jfx_editor_t *e=jfx_editor_create(2,2); assert(e);
    assert(jfx_editor_command(e,"sequence.new",2,2,25,1,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_AUDIO,0,25,source)==JFX_SUCCESS);
    jfx_export_options_t options={.size=sizeof(options),.path=path,.sample_rate=8000,
        .start_frame=2,.frame_count=5,.audio=true};
    jfx_export_job_t *job=NULL;
    assert(jfx_export_begin(e,&options,&job)==JFX_SUCCESS); /* Also with FFmpeg/VST3 disabled. */
    jfx_editor_destroy(e);
    while (jfx_export_state(job)==JFX_EXPORT_RUNNING) assert(jfx_export_step(job,3)==JFX_SUCCESS);
    assert(jfx_export_completed_frames(job)==5); jfx_export_destroy(job);
    FILE *file=fopen(path,"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END) && ftell(file)==44+1600*8); assert(!fclose(file));
    /* Decode the generated float WAV through the ordinary source path. */
    jfx_timeline_t *t=jfx_timeline_create(2,2,25,1); assert(t);
    assert(jfx_timeline_add_track(t,"bounce")==0);
    jfx_clip_desc_t clip={0}; clip.source=JFX_CLIP_AUDIO; clip.image_path=path;
    clip.enabled=true; clip.length_frames=5;
    assert(jfx_timeline_add_clip(t,0,&clip)==0);
    float pcm[128]; assert(jfx_timeline_render_audio(t,0,8000,64,pcm,128)==JFX_SUCCESS);
    for (size_t i=0;i<128;++i) near(pcm[i],.25f);
    jfx_timeline_destroy(t); assert(!remove(path));
}
int main(int argc,char **argv) {
    assert(argc==2); fixture(argv[1],false);
    wav_bounce(argv[1]);
    char ramp[2048]; assert(snprintf(ramp,sizeof(ramp),"%s.ramp.wav",argv[1])>0);
    fixture(ramp,true); rational_timing(ramp); assert(!remove(ramp)); overlaps(argv[1]);
    jfx_timeline_t *t=jfx_timeline_create(2,2,25,1); assert(t);
    assert(jfx_timeline_add_track(t,"A1")==0);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_AUDIO; d.image_path=argv[1];
    d.length_frames=25; d.start_frame=5; d.enabled=true; d.opacity=1;
    assert(jfx_timeline_add_clip(t,0,&d)==0);
    jfx_clip_audio_t a={.size=sizeof(a)};
    assert(jfx_timeline_get_clip_audio(t,0,0,&a)==JFX_SUCCESS && a.enabled && a.gain==1);
    uint64_t s=0; assert(jfx_timeline_audio_sample(t,5,8000,&s)==JFX_SUCCESS && s==1600);
    float pcm[128], sentinel[128]; for (size_t i=0;i<128;++i) sentinel[i]=pcm[i]=42;
    assert(jfx_timeline_render_audio(t,1584,8000,64,pcm,128)==JFX_SUCCESS);
    for (size_t i=0;i<32;++i) near(pcm[i],0);
    for (size_t i=32;i<128;++i) near(pcm[i],0.25f);
    a.gain=2; a.pan=1; a.fade_in_frames=5;
    assert(jfx_timeline_set_clip_audio(t,0,0,&a)==JFX_SUCCESS);
    assert(jfx_timeline_set_track_audio_gain(t,0,0.5f)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,2400,8000,64,pcm,128)==JFX_SUCCESS);
    near(pcm[0],0); near(pcm[1],0.125f);
    float before[128]; memcpy(before,pcm,sizeof(pcm));
    assert(jfx_timeline_split_clip(t,0,0,7)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,2400,8000,64,pcm,128)==JFX_SUCCESS);
    assert(!memcmp(before,pcm,sizeof(pcm)));
    char text[4096]; size_t n=0;
    assert(jfx_project_save_sequence(t,text,sizeof(text),&n)==JFX_SUCCESS);
    jfx_editor_t *e=jfx_editor_create(2,2); assert(e);
    assert(jfx_editor_load(e,text,n,NULL,0)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.audio.gain",0,1,0,3,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_get_clip_audio(jfx_editor_timeline(e),0,1,&a)==JFX_SUCCESS);
    assert(a.gain==2 && a.reference_frames==25);
    assert(jfx_timeline_render_audio(jfx_editor_timeline(e),2400,8000,64,pcm,128)==JFX_SUCCESS);
    assert(!memcmp(before,pcm,sizeof(pcm)));
    assert(jfx_timeline_render_audio(t,0,8000,0,pcm,128)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_render_audio(NULL,0,8000,64,pcm,128)==JFX_ERROR_INVALID_ARGUMENT);
    a.gain=NAN; assert(jfx_timeline_set_clip_audio(t,0,0,&a)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_track_audio_gain(t,0,-1)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_clip_audio(NULL,0,0,&a)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_clip_audio(t,0,0,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_clip_audio(t,0,0,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_clip_audio(NULL,0,0,&a)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_track_audio_gain(NULL,0,1)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_audio_sample(NULL,0,48000,&s)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_audio_sample(t,0,48000,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_audio_sample(t,0,1,&s)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_audio_mixer_t *m=NULL;
    assert(jfx_audio_mixer_create(NULL,48000,&m)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_mixer_create(t,48000,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_mixer_render(NULL,0,1,pcm,128)==JFX_ERROR_INVALID_ARGUMENT);
    d.image_path="/missing/audio.wav"; d.start_frame=5;
    assert(jfx_timeline_add_clip(t,0,&d)==2);
    memcpy(pcm,sentinel,sizeof(pcm));
    assert(jfx_timeline_render_audio(t,2400,8000,64,pcm,128)!=JFX_SUCCESS);
    assert(!memcmp(pcm,sentinel,sizeof(pcm)));
    assert(jfx_timeline_set_track_muted(t,0,true)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,2400,8000,64,pcm,128)==JFX_SUCCESS);
    for (size_t i=0;i<128;++i) near(pcm[i],0);
    jfx_editor_destroy(e); jfx_timeline_destroy(t); remove(argv[1]);
    puts("audio mixing: passed"); return 0;
}
