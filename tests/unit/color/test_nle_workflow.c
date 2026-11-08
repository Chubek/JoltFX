#include "jfx/jfx_nle.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "tilly/memory.h"

int main(void) {
    jfx_editor_t *e=jfx_editor_create(16,16); assert(e);
    assert(jfx_editor_command(e,"clip.name",0,0,0,0,"Shot")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.opacity",0,0,0,1,"")==JFX_SUCCESS);
    assert(jfx_editor_can_redo(e));
    assert(!jfx_editor_can_undo(e));
    assert(jfx_editor_begin_edit(e,JFX_PROJECT_KIND_SEQUENCE)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.opacity",0,0,0,.5,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.opacity",0,0,0,1,"")==JFX_SUCCESS);
    assert(jfx_editor_commit_edit(e)==JFX_SUCCESS);
    assert(jfx_editor_can_redo(e));
    char error[256];
    const char *script="(defkernel edit [] [[\"clip.name\" 0 0 0 0 \"Scripted\"] [\"clip.split\" 0 0 0 40 \"\"]])";
    assert(jfx_nle_run_script(e,script,error,sizeof(error))==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(jfx_editor_timeline(e),0)==2);
    assert(!strcmp(jfx_editor_history_label(e,false,0),"Joltscript edit"));
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_timeline_clip_count(jfx_editor_timeline(e),0)==1);
    script="(defkernel edit [] [[\"clip.name\" 0 0 0 0 \"Must roll back\"] [\"clip.split\" 0 0 0 9999 \"\"]])";
    assert(jfx_nle_run_script(e,script,error,sizeof(error))!=JFX_SUCCESS);
    assert(strcmp(jfx_timeline_clip_name(jfx_editor_timeline(e),0,0),"Must roll back"));
    assert(jfx_editor_can_redo(e));
    assert(jfx_nle_run_script(e,"(defkernel bad [] [[\"undo\" 0 0 0 0 \"\"]])",error,sizeof(error))!=JFX_SUCCESS);
    assert(jfx_nle_run_script(NULL,"",error,sizeof(error))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"clip.duplicate",0,0,0,30,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.transition",0,1,0,20,"morph_cut")==JFX_SUCCESS);
    char document[16384]; size_t written=0;
    assert(jfx_editor_save(e,document,sizeof(document),&written)==JFX_SUCCESS);
    assert(strstr(document,"clip_transition morph_cut 20"));
    assert(jfx_editor_load(e,document,written,error,sizeof(error))==JFX_SUCCESS);
    uint8_t pixels[16*16*4];
    jfx_result_t rendered=jfx_editor_render_frame(e,40,16,16,pixels,sizeof(pixels));
    if (rendered!=JFX_SUCCESS) fprintf(stderr,"transition render: %s\n",jfx_result_to_string(rendered));
    assert(rendered==JFX_SUCCESS);
    assert(pixels[3]==255);
    uint8_t split_pixels[sizeof(pixels)];
    assert(jfx_editor_command(e,"clip.split",0,1,0,40,"")==JFX_SUCCESS);
    assert(jfx_editor_render_frame(e,40,16,16,split_pixels,sizeof(split_pixels))==JFX_SUCCESS);
    assert(!memcmp(pixels,split_pixels,sizeof(pixels)));
    uint8_t from[4]={255,0,0,255},to[4]={0,255,0,255},mixed[4]={0};
    assert(jfx_nle_transition("morph_cut",from,to,1,1,0,mixed,4)==JFX_SUCCESS && !memcmp(from,mixed,4));
    assert(jfx_nle_transition("morph_cut",from,to,1,1,1,mixed,4)==JFX_SUCCESS && !memcmp(to,mixed,4));
    assert(jfx_nle_transition("morph_cut",from,to,1,1,.5,mixed,4)==JFX_SUCCESS && mixed[0]>=127 && mixed[1]>=127);
    assert(jfx_nle_transition("invalid",from,to,1,1,.5,mixed,4)==JFX_ERROR_INVALID_ARGUMENT);
    to[3]=0;
    assert(jfx_nle_transition("morph_cut",from,to,1,1,.5,to,4)==JFX_SUCCESS);
    assert(to[0]==255 && to[1]==0 && to[3]>=127 && to[3]<=128);
    uint8_t moving[8*8*4],moved[sizeof(moving)],morph[sizeof(moving)],dissolve[sizeof(moving)];
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        size_t p=(size_t)(y*8+x)*4;
        for (int c=0;c<3;++c) {
            moving[p+(size_t)c]=(uint8_t)(255*exp(-((x-3.0)*(x-3.0)+(y-3.0)*(y-3.0))/4));
            moved[p+(size_t)c]=(uint8_t)(255*exp(-((x-4.0)*(x-4.0)+(y-3.0)*(y-3.0))/4));
        }
        moving[p+3]=moved[p+3]=255;
    }
    assert(jfx_nle_transition("morph_cut",moving,moved,8,8,.5,morph,sizeof(morph))==JFX_SUCCESS);
    assert(jfx_nle_transition("cross_dissolve",moving,moved,8,8,.5,dissolve,sizeof(dissolve))==JFX_SUCCESS);
    assert(memcmp(morph,dissolve,sizeof(morph)));
    size_t frames=48000;
    float *pcm=tilly_mem_calloc(frames*2,sizeof(float)); assert(pcm);
    jfx_nle_audio_analysis_t analysis={.size=sizeof(analysis)};
    assert(jfx_nle_analyze_pcm(pcm,frames,48000,&analysis)==JFX_SUCCESS && !analysis.clap_detected && isinf(analysis.integrated_lufs));
    pcm[12000*2]=pcm[12000*2+1]=.9f;
    assert(jfx_nle_analyze_pcm(pcm,frames,48000,&analysis)==JFX_SUCCESS && analysis.clap_detected);
    assert(fabs(analysis.clap_seconds-.25)<.001 && analysis.sample_peak>.89);
    pcm[24000*2]=pcm[24000*2+1]=.9f;
    assert(jfx_nle_analyze_pcm(pcm,frames,48000,&analysis)==JFX_SUCCESS && !analysis.clap_detected);
    memset(pcm,0,frames*2*sizeof(float));
    for (size_t i=12000;i<frames;++i) pcm[i*2]=pcm[i*2+1]=.5f;
    assert(jfx_nle_analyze_pcm(pcm,frames,48000,&analysis)==JFX_SUCCESS && !analysis.clap_detected);
    pcm[0]=NAN;
    assert(jfx_nle_analyze_pcm(pcm,frames,48000,&analysis)==JFX_ERROR_INVALID_ARGUMENT);
    tilly_mem_free(pcm);
    jfx_editor_destroy(e);
    return 0;
}
