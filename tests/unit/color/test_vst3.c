#include "jfx/jfx_editor.h"
#include "jfx/jfx_export.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_midi.h"
#include "jfx/jfx_automation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void le(FILE *f, unsigned v, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        fputc((int) ((v >> (8 * i)) & 255), f);
}
static void wave(const char *path)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite("RIFF", 1, 4, f);
    le(f, 36 + 16000, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    le(f, 16, 4);
    le(f, 1, 2);
    le(f, 1, 2);
    le(f, 8000, 4);
    le(f, 16000, 4);
    le(f, 2, 2);
    le(f, 16, 2);
    fwrite("data", 1, 4, f);
    le(f, 16000, 4);
    for (unsigned i = 0; i < 8000; ++i)
        le(f, 1000 + i % 2000, 2);
    assert(!fclose(f));
}
static void model(void)
{
    jfx_editor_t *e = jfx_editor_create(2, 2);
    assert(e);
    assert(jfx_editor_command(e, "sequence.new", 2, 2, 25, 1, "") == JFX_SUCCESS);
    const char *text = "12345678112233445566778801020304 missing folder/test.vst3";
    assert(jfx_editor_command(e, "audio.insert.add", 0, 0, 0, 0, text) == JFX_SUCCESS);
    assert(jfx_editor_command(e, "audio.insert.param", 0, 0, 15, .123456789123, "") == JFX_SUCCESS);
    assert(jfx_editor_command(e, "audio.insert.enabled", 0, 0, 0, 0, "") == JFX_SUCCESS);
    assert(jfx_editor_command(e, "audio.tempo", 0, 0, 0, 125.5, "") == JFX_SUCCESS);
    assert(jfx_editor_command(e, "audio.master.gain", 0, 0, 0, .5, "") == JFX_SUCCESS);
    char saved[8192];
    size_t n = 0;
    assert(jfx_editor_save(e, saved, sizeof(saved), &n) == JFX_SUCCESS);
    assert(jfx_editor_command(e, "undo", 0, 0, 0, 0, "") == JFX_SUCCESS);
    assert(jfx_timeline_master_audio_gain(jfx_editor_timeline(e)) == 1);
    assert(jfx_editor_load(e, saved, n, NULL, 0) == JFX_SUCCESS);
    jfx_audio_insert_t in = {.size = sizeof(in)};
    assert(jfx_timeline_get_audio_insert(jfx_editor_timeline(e), 0, 0, &in) == JFX_SUCCESS &&
           !in.enabled);
    assert(!strcmp(in.path, "missing folder/test.vst3") && in.parameters[0].value == .123456789123);
    assert(jfx_timeline_audio_tempo(jfx_editor_timeline(e)) == 125.5);
    jfx_audio_mixer_t *m = NULL;
    assert(jfx_audio_mixer_create(jfx_editor_timeline(e), 8000, &m) == JFX_SUCCESS);
    jfx_audio_mixer_destroy(m);
    assert(jfx_timeline_audio_insert_parameter(jfx_editor_timeline(e), 0, 0, 0, NAN) ==
           JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_remove_audio_insert(NULL, 0, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_add_audio_insert(NULL, 0, &in) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_move_audio_insert(jfx_editor_timeline(e), 0, 0, 1) ==
           JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_master_audio_gain(NULL, 1) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_enable_audio_insert(NULL, 0, 0, true) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_audio_insert_parameter(NULL, 0, 0, 0, .5) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_audio_tempo(NULL, 120) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_audio_tempo(jfx_editor_timeline(e), NAN) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_audio_insert(NULL, 0, 0, &in) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_add_audio_insert(jfx_editor_timeline(e), 0, &in) == JFX_SUCCESS);
    assert(jfx_timeline_audio_insert_parameter(jfx_editor_timeline(e), 0, 1, 15, .75) ==
           JFX_SUCCESS);
    assert(jfx_timeline_move_audio_insert(jfx_editor_timeline(e), 0, 1, 0) == JFX_SUCCESS);
    assert(jfx_timeline_get_audio_insert(jfx_editor_timeline(e), 0, 0, &in) == JFX_SUCCESS &&
           in.parameters[0].value == .75);
    assert(jfx_timeline_remove_audio_insert(jfx_editor_timeline(e), 0, 0) == JFX_SUCCESS);
    for (uint32_t p = 100; p < 163; ++p)
        assert(jfx_timeline_audio_insert_parameter(jfx_editor_timeline(e), 0, 0, p, .5) ==
               JFX_SUCCESS);
    assert(jfx_timeline_audio_insert_parameter(jfx_editor_timeline(e), 0, 0, 999, .5) ==
           JFX_ERROR_OUT_OF_MEMORY);
    jfx_editor_destroy(e);
}
static unsigned edits=0,resizes=0;
static void edit_callback(void *user,jfx_vst3_edit_phase_t phase,uint32_t id,double value) {
    assert(user==&edits && id==0);
    if (phase==JFX_VST3_EDIT_VALUE) assert(value==.25);
    ++edits;
}
static jfx_result_t resize_callback(void *user,uint32_t width,uint32_t height) {
    assert(user==&resizes && width>0 && height>0); ++resizes; return JFX_SUCCESS;
}
static void extended_host(const char *module,const jfx_vst3_class_t *classes,const char *wave_path) {
    jfx_audio_insert_t in={.size=sizeof(in),.enabled=true}; strcpy(in.path,module); strcpy(in.cid,classes[0].cid);
    jfx_vst3_instance_t *p=NULL; assert(jfx_vst3_create(&in,8000,64,&p)==JFX_SUCCESS);
    uint32_t w=0,h=0;
    assert(jfx_vst3_editor_size(NULL,&w,&h)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_editor_size(p,&w,&h)==JFX_SUCCESS && w==400 && h==200);
    assert(jfx_vst3_set_edit_callback(p,edit_callback,&edits)==JFX_SUCCESS);
    assert(jfx_vst3_editor_open(p,(void *)(uintptr_t)1,"Unsupported",resize_callback,&resizes)==JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_vst3_editor_open(p,(void *)(uintptr_t)1,"X11EmbedWindowID",resize_callback,&resizes)==JFX_SUCCESS && resizes==1);
    assert(jfx_vst3_editor_resize(p,500,300)==JFX_SUCCESS && resizes==2);
    jfx_vst3_editor_wheel(p,1); assert(edits==3);
    jfx_vst3_editor_focus(p,true); jfx_vst3_editor_key(p,true,'a',0,0); jfx_vst3_pump();
    float pcm[128]; for (unsigned i=0;i<128;++i) pcm[i]=1;
    assert(jfx_vst3_process(p,0,64,pcm)==JFX_SUCCESS && pcm[0]==.25f);
    assert(jfx_vst3_add_parameter_point(p,0,0,0)==JFX_SUCCESS);
    assert(jfx_vst3_add_parameter_point(p,0,63,1)==JFX_SUCCESS);
    for (unsigned i=0;i<128;++i) pcm[i]=1;
    assert(jfx_vst3_process(p,64,64,pcm)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(fabsf(pcm[i*2]-(float)i/63)<1e-6f);
    assert(jfx_vst3_set_parameter(p,0,.25)==JFX_SUCCESS);
    assert(jfx_vst3_process(p,128,64,pcm)==JFX_SUCCESS);
    unsigned char state[128]; size_t bytes=0;
    assert(jfx_vst3_save_state(NULL,state,sizeof(state),&bytes)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_save_state(p,NULL,0,&bytes)==JFX_SUCCESS && bytes==28);
    memset(state,0xa5,sizeof(state)); assert(jfx_vst3_save_state(p,state,1,&bytes)==JFX_ERROR_OUT_OF_MEMORY && state[0]==0xa5);
    assert(jfx_vst3_save_state(p,state,sizeof(state),&bytes)==JFX_SUCCESS);
    jfx_vst3_destroy(p); p=NULL;
    assert(jfx_vst3_create_with_state(&in,state,bytes,8000,64,&p)==JFX_SUCCESS);
    jfx_vst3_parameter_t param={.size=sizeof(param)}; assert(jfx_vst3_parameter(p,0,&param)==JFX_SUCCESS && param.value==.25);
    jfx_vst3_destroy(p);
    strcpy(in.cid,classes[4].cid);
    assert(jfx_vst3_create(&in,8000,64,&p)==JFX_SUCCESS && jfx_vst3_is_instrument(p) && jfx_vst3_accepts_midi(p));
    jfx_midi_event_t events[]={{sizeof(jfx_midi_event_t),3,JFX_MIDI_NOTE_ON,0,60,.75f,42},{sizeof(jfx_midi_event_t),17,JFX_MIDI_NOTE_OFF,0,60,0,42}};
    assert(jfx_vst3_process_events(p,0,64,pcm,events,2)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(pcm[i*2]==(i>=3 && i<17?.75f:0));
    events[0].sample_offset=64; assert(jfx_vst3_process_events(p,0,64,pcm,events,2)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_set_parameter(NULL,0,.5)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_set_parameter(p,0,NAN)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_vst3_destroy(p);
    jfx_editor_t *e=jfx_editor_create(2,2); assert(e);
    assert(jfx_editor_command(e,"sequence.new",2,2,25,1,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_MIDI,0,25,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"midi.note.add",0,0,0,.5,"60 1 3 0")==JFX_SUCCESS);
    assert(jfx_timeline_add_audio_insert(jfx_editor_timeline(e),0,&in)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,0,"plugin 0 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,1,"plugin 25 0")==JFX_SUCCESS);
    jfx_audio_mixer_t *automated=NULL; assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&automated)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(automated,640,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(fabsf(pcm[i*2]-.5f*(float)(640+i)/8000)<1e-6f);
    jfx_audio_mixer_destroy(automated);
    char bounce[1024]; assert(strlen(wave_path)+10<sizeof(bounce)); snprintf(bounce,sizeof(bounce),"%s.midi.wav",wave_path);
    jfx_export_options_t options={.size=sizeof(options),.path=bounce,.container="wav",.sample_rate=8000,.audio=true};
    jfx_export_job_t *job=NULL; assert(jfx_export_begin(e,&options,&job)==JFX_SUCCESS);
    jfx_audio_automation_lane_t hold={.size=sizeof(hold),.target=JFX_AUTOMATION_PLUGIN_PARAMETER,.insert=0,.parameter=0,.interpolation=JFX_INTERP_HOLD,.key_count=2,.keys={{0,.25},{2,.75}}};
    assert(jfx_timeline_set_audio_automation(jfx_editor_timeline(e),0,&hold)==JFX_SUCCESS);
    assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&automated)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(automated,630,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(pcm[i*2]==(i<10?.125f:.375f));
    jfx_audio_mixer_destroy(automated);
    hold.key_count=0; assert(jfx_timeline_set_audio_automation(jfx_editor_timeline(e),0,&hold)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,0,"plugin 0 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,1,"plugin 25 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.remove",0,0,0,0,"plugin 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.remove",0,0,0,0,"plugin 25")==JFX_SUCCESS);
    while (jfx_export_state(job)==JFX_EXPORT_RUNNING) assert(jfx_export_step(job,4)==JFX_SUCCESS);
    assert(jfx_export_state(job)==JFX_EXPORT_COMPLETE); jfx_export_destroy(job);
    jfx_timeline_t *roundtrip=jfx_timeline_create(2,2,25,1); assert(roundtrip); jfx_timeline_add_track(roundtrip,"mixdown");
    jfx_clip_desc_t decoded={0}; decoded.source=JFX_CLIP_AUDIO; decoded.image_path=bounce; decoded.length_frames=25; decoded.enabled=true;
    assert(jfx_timeline_add_clip(roundtrip,0,&decoded)==0);
    assert(jfx_timeline_render_audio(roundtrip,640,8000,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(fabsf(pcm[i*2]-.5f*(float)(640+i)/8000)<1e-6f);
    jfx_timeline_destroy(roundtrip); assert(!remove(bounce));
    jfx_audio_mixer_t *m=NULL; assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&m)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,310,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(pcm[i*2]==(i>=10?.5f:0));
    /* Seek into a held note: fresh instance plus note chase. */
    assert(jfx_audio_mixer_render(m,640,64,pcm,128)==JFX_SUCCESS && pcm[0]==.5f);
    assert(jfx_editor_command(e,"midi.note.remove",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,1270,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(pcm[i*2]==(i<10?.5f:0));
    jfx_audio_mixer_destroy(m);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_AUDIO,0,25,wave_path)==JFX_SUCCESS);
    assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&m)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,640,64,pcm,128)==JFX_SUCCESS && fabsf(pcm[0]-1640.0f/32768)<1e-7f);
    jfx_audio_mixer_destroy(m);
    assert(jfx_timeline_remove_audio_insert(jfx_editor_timeline(e),0,0)==JFX_SUCCESS);
    strcpy(in.cid,classes[5].cid); assert(jfx_timeline_add_audio_insert(jfx_editor_timeline(e),0,&in)==JFX_SUCCESS);
    assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&m)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,640,64,pcm,128)==JFX_SUCCESS && fabsf(pcm[0]-1640.0f/32768)<1e-7f);
    assert(jfx_audio_mixer_render(m,704,64,pcm,128)==JFX_SUCCESS && fabsf(pcm[0]-1704.0f/32768)<1e-7f);
    jfx_audio_mixer_destroy(m);
    strcpy(in.cid,classes[0].cid); assert(jfx_timeline_add_audio_insert(jfx_editor_timeline(e),0,&in)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,1,0,0,"plugin 0 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,1,0,1,"plugin 25 0")==JFX_SUCCESS);
    assert(jfx_audio_mixer_create(jfx_editor_timeline(e),8000,&m)==JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m,640,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) assert(fabsf(pcm[i*2]-(float)(1640+i)/32768*(float)(640+i)/8000)<1e-7f);
    jfx_audio_mixer_destroy(m); jfx_editor_destroy(e);
}
int main(int argc, char **argv)
{
    model();
    if (argc == 1) {
        assert(!jfx_vst3_available());
        return 0;
    }
    assert(argc == 3);
    assert(jfx_vst3_available());
    wave(argv[2]);
    jfx_vst3_class_t classes[6];
    size_t count = 0;
    assert(jfx_vst3_classes(NULL, classes, 4, &count) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_classes(argv[1], NULL, 0, &count) == JFX_SUCCESS && count == 6);
    assert(jfx_vst3_classes(argv[1], classes, 1, &count) == JFX_ERROR_OUT_OF_MEMORY && count == 6);
    assert(jfx_vst3_classes(argv[1], classes, 6, &count) == JFX_SUCCESS && count == 6);
    extended_host(argv[1],classes,argv[2]);
    jfx_audio_insert_t in = {
        .size = sizeof(in), .enabled = true, .parameter_count = 1, .parameters = {{0, .5}}};
    strcpy(in.path, argv[1]);
    strcpy(in.cid, classes[0].cid);
    jfx_vst3_instance_t *plugin = NULL, *second = NULL;
    assert(jfx_vst3_create(NULL, 8000, 64, &plugin) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_create(&in, 8000, 64, &plugin) == JFX_SUCCESS);
    assert(jfx_vst3_create(&in, 8000, 64, &second) ==
           JFX_SUCCESS); /* One module entry, two instances. */
    jfx_vst3_parameter_t param = {.size = sizeof(param)};
    assert(jfx_vst3_parameter_count(plugin) == 2 &&
           jfx_vst3_parameter(plugin, 0, &param) == JFX_SUCCESS && param.value == .5);
    assert(jfx_vst3_parameter(plugin, 2, &param) == JFX_ERROR_INVALID_ARGUMENT);
    float pcm[128];
    for (size_t i = 0; i < 128; ++i)
        pcm[i] = 1;
    assert(jfx_vst3_process(plugin, 0, 65, pcm) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_process(plugin, 0, 64, pcm) == JFX_SUCCESS && pcm[0] == .5f);
    jfx_vst3_destroy(plugin);
    jfx_vst3_destroy(second);
    in.parameter_count = 2;
    in.parameters[1] = (jfx_audio_plugin_value_t){1, 1};
    assert(jfx_vst3_create(&in, 8000, 64, &plugin) == JFX_SUCCESS);
    for (size_t i = 0; i < 128; ++i)
        pcm[i] = 42;
    assert(jfx_vst3_process(plugin, 0, 64, pcm) == JFX_ERROR_NOT_IMPLEMENTED && pcm[0] == 42);
    jfx_vst3_destroy(plugin);
    in.parameter_count = 1;
    strcpy(in.cid, classes[3].cid);
    assert(jfx_vst3_create(&in, 8000, 64, &plugin) == JFX_SUCCESS);
    assert(jfx_vst3_parameter(plugin, 0, &param) == JFX_SUCCESS && param.value == .5);
    assert(jfx_vst3_set_tempo(plugin, 137.5) == JFX_SUCCESS);
    assert(jfx_vst3_set_tempo(plugin, NAN) == JFX_ERROR_INVALID_ARGUMENT);
    for (size_t i = 0; i < 128; ++i)
        pcm[i] = 1;
    assert(jfx_vst3_process(plugin, 0, 64, pcm) == JFX_SUCCESS && pcm[0] == .5f);
    jfx_vst3_destroy(plugin);
    strcpy(in.cid, classes[2].cid);
    assert(jfx_vst3_create(&in, 8000, 64, &plugin) == JFX_ERROR_NOT_IMPLEMENTED);
    jfx_timeline_t *t = jfx_timeline_create(2, 2, 25, 1);
    assert(t);
    jfx_timeline_add_track(t, "Wet");
    jfx_timeline_add_track(t, "Dry");
    jfx_clip_desc_t clip = {0};
    clip.source = JFX_CLIP_AUDIO;
    clip.image_path = argv[2];
    clip.enabled = true;
    clip.length_frames = 25;
    assert(jfx_timeline_add_clip(t, 0, &clip) == 0 && jfx_timeline_add_clip(t, 1, &clip) == 0);
    float dry[128], wet[128], split[128];
    assert(jfx_timeline_render_audio(t, 200, 8000, 64, dry, 128) == JFX_SUCCESS);
    strcpy(in.cid, classes[1].cid);
    assert(jfx_timeline_add_audio_insert(t, 0, &in) == JFX_SUCCESS);
    strcpy(in.cid, classes[0].cid);
    assert(jfx_timeline_add_audio_insert(t, 0, &in) == JFX_SUCCESS);
    assert(jfx_timeline_set_master_audio_gain(t, .5) == JFX_SUCCESS);
    assert(jfx_timeline_set_audio_tempo(t, 133.7) == JFX_SUCCESS);
    jfx_audio_mixer_t *m = NULL;
    assert(jfx_audio_mixer_create(t, 8000, &m) == JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m, 200, 64, wet, 128) == JFX_SUCCESS);
    for (size_t i = 0; i < 128; ++i)
        assert(fabsf(wet[i] - dry[i] * .3125f) <
               .000001f); /* compensated delayed .25 wet + 1 dry, master .5 */
    assert(jfx_audio_mixer_render(m, 200, 17, split, 34) == JFX_SUCCESS);
    assert(jfx_audio_mixer_render(m, 217, 47, split + 34, 94) == JFX_SUCCESS &&
           !memcmp(wet, split, sizeof(wet)));
    jfx_timeline_destroy(t); /* Snapshot owns insert settings and module lifetime. */
    assert(jfx_audio_mixer_render(m, 200, 64, split, 128) == JFX_SUCCESS &&
           !memcmp(wet, split, sizeof(wet)));
    jfx_audio_mixer_destroy(m);
    /* Incremental WAV mixdown owns a snapshot, includes inserts, and retains
     * the previous target when cancelled or a media read fails. */
    jfx_editor_t *e = jfx_editor_create(2, 2);
    assert(e);
    assert(jfx_editor_command(e, "sequence.new", 2, 2, 25, 1, "") == JFX_SUCCESS);
    assert(jfx_editor_command(e, "clip.add", 0, JFX_CLIP_AUDIO, 0, 25, argv[2]) == JFX_SUCCESS);
    assert(jfx_timeline_add_audio_insert(jfx_editor_timeline(e), 0, &in) == JFX_SUCCESS);
    char output[2048];
    snprintf(output, sizeof(output), "%s.mix.wav", argv[2]);
    jfx_export_options_t options = {
        .size = sizeof(options), .path = output, .audio = true, .sample_rate = 8000};
    jfx_export_job_t *job = NULL;
    assert(jfx_export_begin(e, &options, &job) == JFX_SUCCESS);
    assert(jfx_timeline_audio_insert_parameter(jfx_editor_timeline(e), 0, 0, 0, 0) == JFX_SUCCESS);
    while (jfx_export_state(job) == JFX_EXPORT_RUNNING)
        assert(jfx_export_step(job, 1) == JFX_SUCCESS);
    jfx_export_destroy(job);
    FILE *file = fopen(output, "rb");
    assert(file);
    unsigned char header[44];
    assert(fread(header, 1, 44, file) == 44);
    assert(!memcmp(header, "RIFF", 4) && header[20] == 3 && header[22] == 2 && header[34] == 32);
    float sample;
    assert(fread(&sample, sizeof(sample), 1, file) == 1 &&
           fabsf(sample - 500.0f / 32768) < .000001f);
    assert(!fseek(file, 0, SEEK_END) && ftell(file) == 44 + 8000 * 8);
    assert(!fclose(file));
    assert(jfx_export_begin(e, &options, &job) == JFX_SUCCESS);
    assert(jfx_export_step(job, 1) == JFX_SUCCESS);
    jfx_export_cancel(job);
    assert(jfx_export_state(job) == JFX_EXPORT_CANCELLED);
    jfx_export_destroy(job);
    file = fopen(output, "rb");
    assert(file);
    assert(!fseek(file, 44, SEEK_SET) && fread(&sample, sizeof(sample), 1, file) == 1 &&
           fabsf(sample - 500.0f / 32768) < .000001f);
    assert(!fclose(file));
    assert(jfx_editor_command(e, "clip.add", 0, JFX_CLIP_AUDIO, 0, 25, "missing.wav") ==
           JFX_SUCCESS);
    assert(jfx_export_begin(e, &options, &job) == JFX_SUCCESS);
    assert(jfx_export_step(job, 1) == JFX_ERROR_NOT_FOUND &&
           jfx_export_state(job) == JFX_EXPORT_FAILED);
    jfx_export_destroy(job);
    file = fopen(output, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END) && ftell(file) == 44 + 8000 * 8);
    assert(!fclose(file));
    assert(!remove(output));
    jfx_editor_destroy(e);
    assert(!remove(argv[2]));
    return 0;
}
