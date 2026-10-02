#include "jfx/jfx_export.h"
#include "joltscript/video_io.h"
#include "joltscript/audio_io.h"
#include "joltscript/media_writer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static void sentinel(const char *path) { FILE *f=fopen(path,"wb"); assert(f); assert(fwrite("keep",1,4,f)==4); assert(!fclose(f)); }
static void preserved(const char *path) { char s[8]={0}; FILE *f=fopen(path,"rb"); assert(f); assert(fread(s,1,8,f)==4 && !memcmp(s,"keep",4)); fclose(f); }
static bool cancel(void *user,uint64_t completed,uint64_t total) { (void)user; assert(total==3); return completed<1; }
static void le(FILE *file,unsigned value,unsigned n) { for (unsigned i=0;i<n;++i) fputc((int)((value>>(i*8))&255),file); }
static void audio_roundtrip(jfx_editor_t *e,const char *path) {
    char wave[2048]; assert(snprintf(wave,sizeof(wave),"%s.wav",path)>0);
    FILE *file=fopen(wave,"wb"); assert(file); fwrite("RIFF",1,4,file); le(file,36+44100*4,4);
    fwrite("WAVEfmt ",1,8,file); le(file,16,4); le(file,1,2); le(file,2,2); le(file,44100,4);
    le(file,44100*4,4); le(file,4,2); le(file,16,2); fwrite("data",1,4,file); le(file,44100*4,4);
    for (unsigned i=0;i<44100;++i) { le(file,(unsigned)(int)(24000*sinf((float)i*0.12f)),2); le(file,(unsigned)(int)(12000*sinf((float)i*0.06f)),2); }
    assert(!fclose(file));
    assert(jfx_editor_command(e,"sequence.new",16,16,30000,1001,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"track.add",0,0,0,0,"Audio")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_AUDIO,0,3,wave)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.audio.gain",0,0,0,2,"")==JFX_SUCCESS);
    jfx_export_options_t options={.size=sizeof(options),.path=path,.container="matroska",.frame_count=3,.audio=true};
    assert(jfx_editor_export_video(e,&options,NULL,NULL)==JFX_SUCCESS);
    jolt_audio_reader_t *reader=NULL; assert(jolt_audio_reader_open(path,48000,&reader)==JOLT_OK);
    float expected[1024],actual[1024],blocks[1024];
    assert(jfx_timeline_render_audio(jfx_editor_timeline(e),300,48000,512,expected,1024)==JFX_SUCCESS);
    assert(jolt_audio_reader_read(reader,300,512,actual)==JOLT_OK);
    bool clipped=false;
    for (size_t i=0;i<1024;++i) { if (fabsf(expected[i])>1) clipped=true; assert(fabsf(actual[i]-fmaxf(-1,fminf(1,expected[i])))<0.00004f); }
    assert(clipped);
    assert(jolt_audio_reader_read(reader,300,197,blocks)==JOLT_OK);
    assert(jolt_audio_reader_read(reader,497,315,blocks+394)==JOLT_OK);
    assert(!memcmp(actual,blocks,sizeof(actual)));
    assert(jolt_audio_reader_read(reader,4805,512,actual)==JOLT_OK);
    for (size_t i=0;i<1024;++i) assert(actual[i]==0);
    /* Reading backwards after drain reinitializes the decoder/resampler. */
    assert(jolt_audio_reader_read(reader,300,512,actual)==JOLT_OK);
    assert(!memcmp(actual,blocks,sizeof(actual)));
    assert(jolt_audio_reader_read(NULL,0,1,actual)==JOLT_ERR_ARGUMENT);
    assert(jolt_audio_reader_read(reader,0,1,NULL)==JOLT_ERR_ARGUMENT);
    assert(jolt_audio_reader_read(reader,UINT64_MAX,1,actual)==JOLT_ERR_ARGUMENT);
    jolt_audio_reader_close(reader);
    assert(jolt_audio_reader_open(NULL,48000,&reader)==JOLT_ERR_ARGUMENT);
    assert(jolt_audio_reader_open(path,48000,NULL)==JOLT_ERR_ARGUMENT);
    assert(!remove(wave));
}
int main(int argc,char **argv) {
    assert(argc==2);
    jfx_editor_t *e=jfx_editor_create(16,16); assert(e);
    jfx_export_options_t o={.size=sizeof(o),.path=argv[1],.container="matroska",.frame_count=3,.audio=true};
    jfx_export_job_t *j=NULL;
    assert(jfx_export_begin(NULL,&o,&j)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_export_step(NULL,1)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_export_begin(e,NULL,&j)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_export_begin(e,&o,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_export_video(NULL,&o,NULL,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(!jfx_export_codec_available(NULL,false));
    jolt_media_writer_t *writer=NULL; jolt_media_options_t media={0};
    assert(jolt_media_writer_open(NULL,&media,&writer)==JOLT_ERR_ARGUMENT);
    assert(jolt_media_writer_video(NULL,NULL)==JOLT_ERR_ARGUMENT);
    assert(jolt_media_writer_audio(NULL,NULL,0)==JOLT_ERR_ARGUMENT);
    assert(jolt_media_writer_finish(NULL)==JOLT_ERR_ARGUMENT);
    if (!jfx_export_available()) {
        assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_NOT_IMPLEMENTED);
        jfx_editor_destroy(e); puts("FFmpeg-disabled export contract: passed"); return 0;
    }
    sentinel(argv[1]);
    assert(jfx_editor_export_video(e,&o,cancel,NULL)!=JFX_SUCCESS); preserved(argv[1]);
    /* Cancelling an old job cannot remove another job's reused temp slot. */
    jfx_export_job_t *cancelled=NULL;
    assert(jfx_export_begin(e,&o,&cancelled)==JFX_SUCCESS); jfx_export_cancel(cancelled);
    assert(jfx_export_state(cancelled)==JFX_EXPORT_CANCELLED);
    assert(jfx_export_begin(e,&o,&j)==JFX_SUCCESS);
    jfx_export_destroy(cancelled); assert(jfx_export_step(j,3)==JFX_SUCCESS); jfx_export_destroy(j);
    sentinel(argv[1]);
    assert(jfx_export_begin(e,&o,&j)==JFX_SUCCESS);
    uint8_t expected[16*16*4];
    assert(jfx_editor_render_frame(e,1,16,16,expected,sizeof(expected))==JFX_SUCCESS);
    assert(jfx_export_total_frames(j)==3 && jfx_export_completed_frames(j)==0);
    assert(jfx_export_step(j,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_export_step(j,1)==JFX_SUCCESS); preserved(argv[1]);
    assert(jfx_editor_command(e,"clip.enabled",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_export_step(j,2)==JFX_SUCCESS && jfx_export_state(j)==JFX_EXPORT_COMPLETE);
    assert(jfx_export_step(j,1)==JFX_SUCCESS); jfx_export_destroy(j);
    uint8_t rgba[16*16*4];
    assert(jolt_video_io_frame(argv[1],1.0/30,16,16,rgba,sizeof(rgba))==0);
    assert(!memcmp(rgba,expected,sizeof(rgba)));
    jolt_audio_reader_t *reader=NULL; assert(jolt_audio_reader_open(argv[1],48000,&reader)==JOLT_OK);
    float pcm[64]; assert(jolt_audio_reader_read(reader,100,32,pcm)==JOLT_OK);
    for (size_t i=0;i<64;++i) assert(pcm[i]==0);
    jolt_audio_reader_close(reader);
    o.video_codec="missing_encoder"; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_NOT_IMPLEMENTED);
    o.video_codec=NULL; sentinel(argv[1]);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_AUDIO,0,3,"/missing/audio.wav")==JFX_SUCCESS);
    assert(jfx_export_begin(e,&o,&j)==JFX_SUCCESS);
    assert(jfx_export_step(j,1)==JFX_ERROR_NOT_FOUND && jfx_export_state(j)==JFX_EXPORT_FAILED);
    jfx_export_destroy(j); preserved(argv[1]);
    assert(jfx_editor_command(e,"clip.remove",0,1,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"graph",0,0,0,0,"")==JFX_SUCCESS);
    o.audio=false; assert(jfx_editor_export_video(e,&o,NULL,NULL)==JFX_SUCCESS);
    assert(jolt_video_io_frame(argv[1],0,16,16,rgba,sizeof(rgba))==0);
    audio_roundtrip(e,argv[1]);
    jfx_editor_destroy(e); remove(argv[1]); puts("encoded export: passed"); return 0;
}
