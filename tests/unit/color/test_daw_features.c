#include "jfx/jfx_editor.h"
#include "jfx/jfx_midi.h"
#include "jfx/jfx_recording.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_automation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void notes_and_history(void) {
    jfx_editor_t *e=jfx_editor_create(2,2); assert(e);
    assert(jfx_editor_command(e,"sequence.new",2,2,25,1,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,JFX_CLIP_MIDI,0,25,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"midi.note.add",0,0,0,.75,"60 2 10 1")==JFX_SUCCESS);
    jfx_midi_note_t n={.size=sizeof(n)};
    assert(jfx_timeline_get_midi_note(jfx_editor_timeline(e),0,0,0,&n)==JFX_SUCCESS);
    assert(n.pitch==60 && n.frame==2 && n.length==10 && n.channel==1 && n.velocity==.75f);
    assert(jfx_editor_command(e,"clip.split",0,0,0,5,"")==JFX_SUCCESS);
    assert(jfx_timeline_midi_note_count(jfx_editor_timeline(e),0,1)==1);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(jfx_editor_timeline(e),0)==1);
    assert(jfx_editor_command(e,"midi.note.remove",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_midi_note_count(jfx_editor_timeline(e),0,0)==0);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    char text[32768]; size_t bytes=0;
    assert(jfx_editor_save(e,text,sizeof(text),&bytes)==JFX_SUCCESS);
    assert(jfx_editor_load(e,text,bytes,NULL,0)==JFX_SUCCESS);
    assert(jfx_timeline_get_midi_note(jfx_editor_timeline(e),0,0,0,&n)==JFX_SUCCESS && n.velocity==.75f);
    n.velocity=NAN; assert(jfx_timeline_add_midi_note(jfx_editor_timeline(e),0,0,&n)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_add_midi_note(NULL,0,0,&n)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_midi_note(NULL,0,0,0,&n)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_midi_note(NULL,0,0,0,&n)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_remove_midi_note(NULL,0,0,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"audio.insert.add",0,0,0,0,"12345678112233445566778801020304 missing.vst3")==JFX_SUCCESS);
    /* Hex chunking must round-trip binary zeros/high bytes without module loads. */
    const unsigned char state[]={ 'J','V','S','1',3,0,0,0,2,0,0,0,0,255,128,42,0 };
    assert(jfx_editor_command(e,"audio.insert.state",0,0,0,0,"4a565331030000000200000000ff802a00")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,.5,"gain 0 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,0,1,"gain 25 0")==JFX_SUCCESS);
    const void *blob=NULL; size_t length=0;
    assert(jfx_timeline_get_audio_insert_state(jfx_editor_timeline(e),0,0,&blob,&length)==JFX_SUCCESS && length==sizeof(state) && !memcmp(blob,state,length));
    assert(jfx_editor_save(e,text,sizeof(text),&bytes)==JFX_SUCCESS);
    assert(jfx_editor_load(e,text,bytes,NULL,0)==JFX_SUCCESS);
    jfx_audio_automation_lane_t lane={.size=sizeof(lane)};
    assert(jfx_timeline_get_audio_automation(jfx_editor_timeline(e),0,0,&lane)==JFX_SUCCESS && lane.key_count==2 && lane.keys[1].frame==25);
    assert(jfx_timeline_get_audio_automation(NULL,0,0,&lane)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_audio_automation(NULL,0,&lane)==JFX_ERROR_INVALID_ARGUMENT);
    lane.keys[1].frame=0; assert(jfx_timeline_set_audio_automation(jfx_editor_timeline(e),0,&lane)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_audio_insert_state(jfx_editor_timeline(e),0,0,&blob,&length)==JFX_SUCCESS && !memcmp(blob,state,length));
    assert(jfx_editor_command(e,"audio.insert.state",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_get_audio_insert_state(jfx_editor_timeline(e),0,0,&blob,&length)==JFX_SUCCESS && length==sizeof(state));
    assert(jfx_timeline_audio_insert_state(jfx_editor_timeline(e),0,0,state,4)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_audio_insert_state(NULL,0,0,state,sizeof(state))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vst3_validate_state(state,sizeof(state))==JFX_SUCCESS);
    assert(jfx_vst3_validate_state(state,4)==JFX_ERROR_INVALID_ARGUMENT);
    unsigned char large[8204]; memcpy(large,"JVS1",4); memset(large+4,0,8); large[5]=32;
    for (size_t i=12;i<sizeof(large);++i) large[i]=(unsigned char)(i*37);
    assert(jfx_timeline_audio_insert_state(jfx_editor_timeline(e),0,0,large,sizeof(large))==JFX_SUCCESS);
    assert(jfx_editor_save(e,text,sizeof(text),&bytes)==JFX_SUCCESS);
    assert(jfx_editor_load(e,text,bytes,NULL,0)==JFX_SUCCESS);
    assert(jfx_timeline_get_audio_insert_state(jfx_editor_timeline(e),0,0,&blob,&length)==JFX_SUCCESS && length==sizeof(large) && !memcmp(blob,large,length));
    assert(jfx_editor_command(e,"audio.insert.add",0,0,0,0,"12345678112233445566778801020304 second.vst3")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.automation.key",0,0,17,.25,"plugin 4 1")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"audio.insert.move",0,0,1,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_get_audio_insert_state(jfx_editor_timeline(e),0,1,&blob,&length)==JFX_SUCCESS && length==sizeof(large) && !memcmp(blob,large,length));
    assert(jfx_timeline_get_audio_automation(jfx_editor_timeline(e),0,1,&lane)==JFX_SUCCESS && lane.insert==1 && lane.parameter==17);
    assert(jfx_editor_command(e,"audio.insert.remove",0,1,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_audio_automation_count(jfx_editor_timeline(e),0)==1);
    assert(jfx_timeline_clear_audio_insert_parameters(NULL,0,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_get_audio_insert_state(NULL,0,0,&blob,&length)==JFX_ERROR_INVALID_ARGUMENT);
    const char *bad="fps 25 1\ntrack Test\naudio_insert 1 12345678112233445566778801020304 missing.vst3\naudio_insert_state 17\nstate_hex 4a565331030000000200000000\n";
    assert(jfx_editor_load(e,bad,strlen(bad),NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_editor_destroy(e);
}
static void capture(const char *path) {
    jfx_audio_recording_t *r=NULL;
    assert(jfx_audio_recording_begin(NULL,8000,&r)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_recording_begin(path,1,&r)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_recording_push(NULL,NULL,64)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_recording_finish(NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_audio_recording_begin(path,8000,&r)==JFX_SUCCESS);
    float pcm[128]; for (size_t i=0;i<128;++i) pcm[i]=i&1?-.5f:.25f;
    assert(jfx_audio_recording_push(r,pcm,64)==JFX_SUCCESS);
    pcm[0]=NAN; assert(jfx_audio_recording_push(r,pcm,64)==JFX_ERROR_INVALID_ARGUMENT); pcm[0]=.25f;
    assert(jfx_audio_recording_frames(r)==64);
    assert(jfx_audio_recording_push(r,pcm,64)==JFX_SUCCESS);
    assert(jfx_audio_recording_finish(r)==JFX_SUCCESS); jfx_audio_recording_destroy(r);
    jfx_timeline_t *t=jfx_timeline_create(2,2,25,1); assert(t); jfx_timeline_add_track(t,"take");
    jfx_clip_desc_t c={0}; c.source=JFX_CLIP_AUDIO; c.image_path=path; c.length_frames=25; c.enabled=true;
    assert(jfx_timeline_add_clip(t,0,&c)==0);
    assert(jfx_timeline_render_audio(t,0,8000,64,pcm,128)==JFX_SUCCESS && pcm[0]==.25f && pcm[1]==-.5f);
    jfx_audio_automation_lane_t lane={.size=sizeof(lane),.target=JFX_AUTOMATION_TRACK_GAIN,.interpolation=JFX_INTERP_LINEAR,.key_count=2,.keys={{0,0},{1,1}}};
    assert(jfx_timeline_set_audio_automation(t,0,&lane)==JFX_SUCCESS);
    assert(jfx_timeline_render_audio(t,0,8000,64,pcm,128)==JFX_SUCCESS);
    for (size_t i=0;i<64;++i) assert(fabsf(pcm[i*2]-.25f*(float)i/320)<1e-7f);
    lane.key_count=0; assert(jfx_timeline_set_audio_automation(t,0,&lane)==JFX_SUCCESS);
    assert(jfx_audio_recording_begin(path,8000,&r)==JFX_SUCCESS); jfx_audio_recording_cancel(r); jfx_audio_recording_destroy(r);
    assert(jfx_timeline_render_audio(t,0,8000,64,pcm,128)==JFX_SUCCESS && pcm[0]==.25f);
    jfx_timeline_destroy(t); assert(!remove(path));
}
int main(int argc,char **argv) { assert(argc==2); notes_and_history(); capture(argv[1]); return 0; }
