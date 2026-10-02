#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/jfx_editor.h"

static void edits(void) {
    jfx_editor_t *e=jfx_editor_create(4,2);
    assert(e);
    assert(jfx_editor_command(e,"clip.split",0,0,0,100,"")==JFX_SUCCESS);
    jfx_timeline_t *t=jfx_editor_timeline(e);
    assert(jfx_timeline_clip_count(t,0)==2);
    assert(jfx_timeline_clip_length(t,0,0)==100);
    assert(jfx_timeline_clip_start(t,0,1)==100);
    assert(jfx_timeline_clip_in_point(t,0,1)==100);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(jfx_editor_timeline(e),0)==1);
    assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(jfx_editor_timeline(e),0)==2);
    assert(jfx_editor_command(e,"clip.move",0,1,0,400,"")==JFX_SUCCESS);
    t=jfx_editor_timeline(e);
    assert(jfx_timeline_clip_start(t,0,1)==400);
    assert(jfx_timeline_clip_in_point(t,0,1)==100);
    assert(jfx_editor_command(e,"clip.slip",0,1,0,25,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_in_point(t,0,1)==125);
    assert(jfx_editor_command(e,"clip.slip",0,1,0,-126,"")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_clip_in_point(t,0,1)==125);
    assert(jfx_editor_command(e,"clip.duplicate",0,1,0,600,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(t,0)==3);
    assert(jfx_editor_command(e,"clip.ripple_delete",0,1,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_start(t,0,1)==400);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    t=jfx_editor_timeline(e);
    assert(jfx_timeline_clip_count(t,0)==3);
    assert(jfx_editor_command(e,"clip.opacity",0,1,0,0.5,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    jfx_editor_destroy(e);
}

static void ownership_and_timing(void) {
    jfx_timeline_t *t=jfx_timeline_create(4,2,30000,1001);
    assert(t);
    assert(jfx_timeline_add_track(t,"V1")==0);
    assert(jfx_timeline_add_track(t,"V2")==1);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_SOLID; d.name="A clip #1";
    d.source_params[3]=1; d.start_frame=10; d.length_frames=100; d.in_point=50;
    d.enabled=true; d.opacity=1;
    assert(jfx_timeline_add_clip(t,0,&d)==0);
    assert(jfx_timeline_add_effect(t,0,0,"grade_lut")==0);
    assert(jfx_timeline_set_effect_string(t,0,0,0,0,"a path #1.cube")==JFX_SUCCESS);
    assert(jfx_timeline_add_key(t,0,0,0,0,0,0)==JFX_SUCCESS);
    assert(jfx_timeline_add_key(t,0,0,0,0,100,1)==JFX_SUCCESS);
    assert(jfx_timeline_split_clip(t,0,0,60)==JFX_SUCCESS);
    assert(jfx_timeline_clip_in_point(t,0,1)==100);
    assert(jfx_timeline_effect_param_on_timeline(t,0,1,0,0,60)==0.5f);
    assert(jfx_timeline_duplicate_clip(t,0,1,1,200)==JFX_SUCCESS);
    assert(jfx_timeline_effect_param_on_timeline(t,1,0,0,0,200)==0.5f);
    assert(jfx_timeline_set_effect_string(t,0,1,0,0,"changed")==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_effect_string(t,1,0,0,0),"a path #1.cube"));
    assert(jfx_timeline_position_clip(t,1,0,1,0)==JFX_SUCCESS);
    assert(jfx_timeline_effect_param_on_timeline(t,1,0,0,0,0)==0.5f);
    assert(jfx_timeline_position_clip(t,1,0,0,300)==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(t,1)==0);
    assert(jfx_timeline_clip_in_point(t,0,2)==100);
    assert(jfx_timeline_set_track_muted(t,0,true)==JFX_SUCCESS);
    assert(jfx_timeline_set_track_solo(t,0,true)==JFX_SUCCESS);
    assert(jfx_timeline_set_track_opacity(t,0,0.625f)==JFX_SUCCESS);
    assert(jfx_timeline_set_clip_enabled(t,0,2,false)==JFX_SUCCESS);
    assert(jfx_timeline_set_clip_blend(t,0,2,JFX_BLEND_SCREEN)==JFX_SUCCESS);
    assert(jfx_timeline_set_effect_blend(t,0,2,0,JFX_BLEND_MULTIPLY)==JFX_SUCCESS);
    assert(jfx_timeline_set_effect_interp(t,0,2,0,JFX_INTERP_HOLD)==JFX_SUCCESS);
    char doc[16384],error[256]; size_t n;
    assert(jfx_project_save_sequence(t,doc,sizeof(doc),&n)==JFX_SUCCESS);
    jfx_timeline_t *loaded=NULL;
    assert(jfx_project_load_sequence(doc,n,&loaded,error,sizeof(error))==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_clip_name(loaded,0,0),"A clip #1"));
    assert(jfx_timeline_track_muted(loaded,0) && jfx_timeline_track_solo(loaded,0));
    assert(jfx_timeline_track_opacity(loaded,0)==0.625f);
    assert(jfx_timeline_clip_in_point(loaded,0,2)==100);
    assert(!jfx_timeline_clip_enabled(loaded,0,2));
    assert(jfx_timeline_clip_blend(loaded,0,2)==JFX_BLEND_SCREEN);
    assert(jfx_timeline_effect_blend(loaded,0,2,0)==JFX_BLEND_MULTIPLY);
    assert(jfx_timeline_effect_interp(loaded,0,2,0)==JFX_INTERP_HOLD);
    assert(jfx_timeline_move_track(loaded,0,1)==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_track_name(loaded,1),"V1"));
    d.start_frame=UINT64_MAX; assert(jfx_timeline_add_clip(loaded,0,&d)==UINT32_MAX);
    assert(jfx_timeline_split_clip(loaded,1,0,10)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(loaded); jfx_timeline_destroy(t);
}

static void ripple_conflict(void) {
    jfx_timeline_t *t=jfx_timeline_create(2,2,30,1);
    assert(t && jfx_timeline_add_track(t,"V1")==0);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_SOLID; d.length_frames=100; d.opacity=1;
    assert(jfx_timeline_add_clip(t,0,&d)==0);
    d.start_frame=50; assert(jfx_timeline_add_clip(t,0,&d)==1);
    assert(jfx_timeline_ripple_delete(t,0,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_clip_count(t,0)==2 && jfx_timeline_clip_start(t,0,1)==50);
    jfx_timeline_destroy(t);
}

static void pixels_and_history(void) {
    jfx_editor_t *e=jfx_editor_create(1,1); assert(e);
    assert(jfx_editor_command(e,"effect.add",0,0,0,0,"exposure")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"effect.key.add",0,0,0,0,"0 0")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"effect.key.add",0,0,0,1,"0 120")==JFX_SUCCESS);
    unsigned char before[4],after[4];
    assert(jfx_editor_render(e,3,1,1,before,sizeof(before))==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.split",0,0,0,60,"")==JFX_SUCCESS);
    assert(jfx_editor_render(e,3,1,1,after,sizeof(after))==JFX_SUCCESS);
    assert(!memcmp(before,after,sizeof(before)));
    assert(jfx_editor_command(e,"clip.move",0,1,0,90,"")==JFX_SUCCESS);
    assert(jfx_editor_render(e,4,1,1,after,sizeof(after))==JFX_SUCCESS);
    assert(!memcmp(before,after,sizeof(before)));
    assert(jfx_editor_command(e,"clip.opacity",0,1,0,0.5,"")==JFX_SUCCESS);
    assert(jfx_editor_render(e,4,1,1,after,sizeof(after))==JFX_SUCCESS);
    assert(after[3]==128);
    char state[4096]; assert(jfx_editor_sequence_state(e,state,sizeof(state))==JFX_SUCCESS);
    assert(strstr(state,"\"inPoint\":60") && strstr(state,"\"canUndo\":true"));
    assert(jfx_editor_sequence_state(e,state,1)==JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_editor_sequence_state(NULL,state,sizeof(state))==JFX_ERROR_INVALID_ARGUMENT);
    for (int i=0;i<40;++i) assert(jfx_editor_command(e,"clip.name",0,0,0,0,"Rename")==JFX_SUCCESS);
    for (int i=0;i<32;++i) assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(!jfx_editor_can_undo(e) && jfx_editor_can_redo(e));
    for (int i=0;i<32;++i) assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"track.remove",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_track_count(jfx_editor_timeline(e))==0);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    jfx_editor_destroy(e);
}

static void limits(void) {
    jfx_timeline_t *t=jfx_timeline_create(1,1,30,1); assert(t);
    assert(jfx_timeline_add_track(t,"V1")==0);
    jfx_clip_desc_t d={0}; d.source=JFX_CLIP_SOLID; d.length_frames=10; d.opacity=1; d.enabled=true;
    for (int i=0;i<JFX_TIMELINE_MAX_CLIPS_PER_TRACK;++i) {
        d.start_frame=(uint64_t)i*10;
        assert(jfx_timeline_add_clip(t,0,&d)==(uint32_t)i);
    }
    assert(jfx_timeline_split_clip(t,0,0,5)==JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_timeline_clip_length(t,0,0)==10);
    assert(jfx_timeline_ripple_insert(t,0,10,20)==JFX_SUCCESS);
    assert(jfx_timeline_clip_start(t,0,1)==30);
    assert(jfx_timeline_ripple_insert(t,0,35,20)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_clip_start(t,0,1)==30);
    jfx_timeline_destroy(t);
    char error[128]; jfx_timeline_t *out=NULL;
    const char *large="size 1 1\nfps 30 1\ntrack V1\nclip solid 16777217 1 1 0 0 1 0 0 0 0 Big\n";
    assert(jfx_project_load_sequence(large,strlen(large),&out,error,sizeof(error))==JFX_SUCCESS);
    assert(jfx_timeline_clip_start(out,0,0)==16777217); jfx_timeline_destroy(out); out=NULL;
    const char *bad="size 1 1\nfps 30 1\ntrack V1\nclip solid -1 1 1 0 0 1 0 0 0 0 Big\n";
    assert(jfx_project_load_sequence(bad,strlen(bad),&out,error,sizeof(error))==JFX_ERROR_INVALID_ARGUMENT);
    assert(out==NULL);
    const char *state="size 1 1\nfps 30 1\ntrack V1\nclip solid 0 10 1 0 0 1 0 0 0 0 Shot\nclip_state 5 1 1 0\nclip_state 7 1 1 0\n";
    assert(jfx_project_load_sequence(state,strlen(state),&out,error,sizeof(error))==JFX_SUCCESS);
    assert(jfx_timeline_clip_in_point(out,0,0)==7);
    jfx_timeline_destroy(out);
}

static void smooth_trim_and_exact_frame(void) {
    jfx_editor_t *e=jfx_editor_create(2,1); assert(e);
    assert(jfx_editor_command(e,"sequence.new",2,1,30000,1001,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.add",0,0,0,100,"")==JFX_SUCCESS);
    jfx_timeline_t *t=jfx_editor_timeline(e);
    assert(jfx_timeline_add_effect(t,0,0,"opacity")==0);
    assert(jfx_timeline_add_key(t,0,0,0,0,0,1)==JFX_SUCCESS);
    assert(jfx_timeline_add_key(t,0,0,0,0,99,0)==JFX_SUCCESS);
    assert(jfx_timeline_set_effect_interp(t,0,0,0,JFX_INTERP_SMOOTH)==JFX_SUCCESS);
    jfx_editor_clear_history(e);
    uint8_t frames[100][8],out[8],scaled[4];
    for (uint64_t f=0;f<100;++f) {
        assert(jfx_editor_render_frame(e,f,2,1,frames[f],8)==JFX_SUCCESS);
        assert(jfx_editor_render(e,(double)f*1001/30000,2,1,out,8)==JFX_SUCCESS);
        assert(!memcmp(out,frames[f],8));
    }
    assert(jfx_editor_command(e,"clip.split",0,0,0,40,"")==JFX_SUCCESS);
    for (uint64_t f=0;f<100;++f) {
        assert(jfx_editor_render_frame(e,f,2,1,out,8)==JFX_SUCCESS);
        assert(!memcmp(out,frames[f],8));
    }
    assert(jfx_editor_command(e,"clip.trim",0,1,50,50,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.move",0,1,0,200,"")==JFX_SUCCESS);
    assert(jfx_editor_render_frame(e,215,1,1,scaled,4)==JFX_SUCCESS);
    assert(!memcmp(scaled,frames[65],4));
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_render_frame(e,65,2,1,out,8)==JFX_SUCCESS);
    assert(!memcmp(out,frames[65],8));
    assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_render_frame(e,215,2,1,out,8)==JFX_SUCCESS);
    assert(!memcmp(out,frames[65],8));
    assert(jfx_editor_write_frame(e,0,0,1,"unused")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_render_frame(e,0,2,1,out,1)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"effect.key.add",0,1,0,0.75,"0 215")==JFX_SUCCESS);
    t=jfx_editor_timeline(e);
    float value; assert(jfx_timeline_key_at(t,0,1,0,0,65,&value) && value==0.75f);
    jfx_editor_destroy(e);
}

int main(void) { edits(); ownership_and_timing(); ripple_conflict(); pixels_and_history(); limits(); smooth_trim_and_exact_frame(); return 0; }
