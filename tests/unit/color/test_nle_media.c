#include "jfx/jfx_nle.h"
#include "joltscript/video_io.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void keep(const char *path) { FILE *f=fopen(path,"wb"); assert(f); assert(fwrite("keep",1,4,f)==4); assert(!fclose(f)); }
static void unchanged(const char *path) { char s[8]={0}; FILE *f=fopen(path,"rb"); assert(f); assert(fread(s,1,8,f)==4); fclose(f); assert(!strcmp(s,"keep")); }
static void finish(jfx_export_job_t *job) {
    while (jfx_export_state(job)==JFX_EXPORT_RUNNING) assert(jfx_export_step(job,1)==JFX_SUCCESS);
    assert(jfx_export_state(job)==JFX_EXPORT_COMPLETE); jfx_export_destroy(job);
}
int main(int argc,char **argv) {
    assert(argc==2); char source[1024],output[1024],caption[1024];
    snprintf(source,sizeof(source),"%s-source.mkv",argv[1]); snprintf(output,sizeof(output),"%s-output.mov",argv[1]);
    snprintf(caption,sizeof(caption),"%s-caption.ass",argv[1]);
    jfx_editor_t *e=jfx_editor_create(64,64); assert(e);
    char tc[64]; assert(jfx_nle_timecode(1800,30000,1001,true,tc,sizeof(tc))==JFX_SUCCESS);
    assert(!strcmp(tc,"00:01:00;02"));
    assert(jfx_nle_timecode(0,25,1,true,tc,sizeof(tc))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_nle_timecode(0,0,1,false,tc,sizeof(tc))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_nle_probe(NULL,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_nle_relink(NULL,"a","b")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"sequence.new",64,64,30000,1001,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_VIDEO,20,30,source)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.slip",0,0,0,5,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.name",0,0,0,0,"A \"quoted\" shot")==JFX_SUCCESS);
    char json[32768]; size_t length=0;
    assert(jfx_nle_export_otio(e,json,sizeof(json),&length)==JFX_SUCCESS);
    assert(strstr(json,"Gap.1") && strstr(json,"Clip.2"));
    assert(jfx_nle_import_otio(e,json,length,64,64,30000,1001)==JFX_SUCCESS);
    assert(!jfx_editor_can_undo(e));
    jfx_timeline_t *t=jfx_editor_timeline(e);
    assert(jfx_timeline_clip_start(t,0,0)==20 && jfx_timeline_clip_in_point(t,0,0)==5 && jfx_timeline_clip_length(t,0,0)==30);
    assert(!strcmp(jfx_timeline_clip_name(t,0,0),"A \"quoted\" shot"));
    assert(jfx_nle_import_otio(e,"{}",2,64,64,30,1)!=JFX_SUCCESS);
    const char *offset="{\"OTIO_SCHEMA\":\"Timeline.1\",\"global_start_time\":{\"OTIO_SCHEMA\":\"RationalTime.1\",\"value\":30,\"rate\":30},\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[]}}";
    assert(jfx_nle_import_otio(e,offset,strlen(offset),64,64,30,1)==JFX_ERROR_NOT_IMPLEMENTED);
    const char *gap="{\"OTIO_SCHEMA\":\"Timeline.1\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[{\"OTIO_SCHEMA\":\"Track.1\",\"kind\":\"Video\",\"children\":[{\"OTIO_SCHEMA\":\"Gap.1\",\"source_range\":{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":{\"OTIO_SCHEMA\":\"RationalTime.1\",\"value\":0,\"rate\":30},\"duration\":{\"OTIO_SCHEMA\":\"RationalTime.1\",\"value\":30,\"rate\":30}}}]}]}}";
    assert(jfx_nle_import_otio(e,gap,strlen(gap),64,64,30,1)==JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_timeline_clip_start(jfx_editor_timeline(e),0,0)==20);
    assert(jfx_editor_command(e,"effect.add",0,0,0,0,"invert")==JFX_SUCCESS);
    assert(jfx_nle_export_otio(e,json,sizeof(json),&length)==JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    keep(output);
    assert(jfx_nle_relink(e,source,output)==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_clip_path(jfx_editor_timeline(e),0,0),output));
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_clip_path(jfx_editor_timeline(e),0,0),source));
    assert(jfx_nle_relink(e,source,"/not/a/media/file")==JFX_ERROR_NOT_FOUND);
    assert(jfx_editor_command(e,"track.add",0,0,0,0,"Microphone")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",1,JFX_CLIP_AUDIO,0,30,source)==JFX_SUCCESS);
    assert(jfx_nle_sync_clap(e,0,0,1,0,.5,.25)==JFX_SUCCESS);
    assert(jfx_timeline_clip_start(jfx_editor_timeline(e),1,0)==22);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_start(jfx_editor_timeline(e),1,0)==0);
    assert(jfx_nle_sync_clap(e,0,0,1,0,0,10)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"sequence.new",64,64,30,1,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_SOLID,0,4,"")==JFX_SUCCESS);
    uint8_t before[64*64*4],after[64*64*4];
    assert(jfx_editor_render_frame(e,0,64,64,before,sizeof(before))==JFX_SUCCESS);
    if (jfx_nle_subtitles_available()) {
        FILE *f=fopen(caption,"wb"); assert(f);
        const char *ass="[Script Info]\nScriptType: v4.00+\nPlayResX: 64\nPlayResY: 64\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,0,0,0,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,TEST\n";
        assert(fwrite(ass,1,strlen(ass),f)==strlen(ass)); assert(!fclose(f));
        assert(jfx_editor_command(e,"clip.subtitles",0,0,0,0,caption)==JFX_SUCCESS);
        assert(jfx_editor_render_frame(e,0,64,64,after,sizeof(after))==JFX_SUCCESS);
        assert(memcmp(before,after,sizeof(after)));
        assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
        remove(caption);
    }
    if (jfx_export_available()) {
        assert(jfx_nle_encoder_at(0,false));
        jfx_export_options_t options={.size=sizeof(options),.path=source,.container="matroska",.frame_count=4,.audio=true};
        jfx_export_job_t *job=NULL; assert(jfx_export_begin(e,&options,&job)==JFX_SUCCESS); finish(job);
        jfx_nle_media_info_t info={.size=sizeof(info)}; assert(jfx_nle_probe(source,&info)==JFX_SUCCESS);
        assert(info.width==64 && info.height==64 && info.video && info.audio && info.duration_seconds>0);
        options.path=output; options.container="mov"; options.frame_count=3;
        assert(jfx_nle_convert_begin(source,&options,&job)==JFX_SUCCESS);
        assert(jfx_export_step(job,1)==JFX_SUCCESS); jfx_export_cancel(job); jfx_export_destroy(job); unchanged(output);
        assert(jfx_nle_convert_begin(source,&options,&job)==JFX_SUCCESS); finish(job);
        assert(jfx_nle_probe(output,&info)==JFX_SUCCESS && !strcmp(info.video_codec,"prores"));
        assert(jolt_video_io_frame(output,0,64,64,after,sizeof(after))==0);
        for (size_t i=0;i<sizeof(after);++i) assert(abs((int)before[i]-(int)after[i])<8);
        jfx_nle_audio_analysis_t analysis={.size=sizeof(analysis)};
        assert(jfx_nle_analyze_file(source,.1,&analysis)==JFX_SUCCESS && !analysis.clap_detected);
        options.path=source; assert(jfx_nle_convert_begin(source,&options,&job)==JFX_ERROR_INVALID_ARGUMENT);
        remove(source);
    }
    remove(output); jfx_editor_destroy(e); return 0;
}
