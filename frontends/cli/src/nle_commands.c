#include "commands.h"
#include "jfx/jfx_nle.h"
#include "tilly/memory.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
static char *read_text(const char *path,size_t *length) {
    FILE *f=fopen(path,"rb"); if (!f) return NULL;
    char *s=tilly_mem_alloc(JFX_PROJECT_MAX_BYTES+1);
    if (!s) { fclose(f); return NULL; }
    size_t n=fread(s,1,JFX_PROJECT_MAX_BYTES+1,f); bool failed=ferror(f)!=0; fclose(f);
    if (failed || n>JFX_PROJECT_MAX_BYTES || memchr(s,0,n)) { tilly_mem_free(s); return NULL; }
    s[n]=0; *length=n; return s;
}
static bool write_text(const char *path,const char *text,size_t n) {
    if (strlen(path)>4096) return false;
    char temporary[4160]; FILE *f=NULL;
    for (unsigned i=0;i<1024;++i) {
        snprintf(temporary,sizeof(temporary),"%s.jfx-part-%u",path,i);
        f=fopen(temporary,"wbx"); if (f || errno!=EEXIST) break;
    }
    if (!f) return false;
    bool ok=fwrite(text,1,n,f)==n; if (fclose(f)) ok=false;
#ifdef _WIN32
    if (ok && !MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) ok=false;
#else
    if (ok && rename(temporary,path)) ok=false;
#endif
    if (!ok) remove(temporary);
    return ok;
}
int cmd_nle_tools(int argc,char **argv) {
    if (!argc) return -1;
    if (!strcmp(argv[0],"encoders")) {
        if (argc!=1) return 1;
        printf("FFmpeg %s\n",jfx_nle_ffmpeg_version());
        for (unsigned audio=0;audio<2;++audio) for (size_t i=0;;++i) {
            const char *name=jfx_nle_encoder_at(i,audio!=0); if (!name) break;
            printf("%s %s\n",audio?"audio":"video",name);
        }
        return 0;
    }
    jfx_result_t r=JFX_ERROR_INVALID_ARGUMENT;
    if (!strcmp(argv[0],"probe")) {
        if (argc!=2) return 1;
        jfx_nle_media_info_t info={.size=sizeof(info)}; r=jfx_nle_probe(argv[1],&info);
        if (r==JFX_SUCCESS) printf("%s %ux%u %u/%u fps %.6f seconds video=%s audio=%s\n",info.format,info.width,info.height,info.fps_num,info.fps_den,info.duration_seconds,info.video_codec,info.audio_codec);
    } else if (!strcmp(argv[0],"analyze")) {
        if (argc!=2 && argc!=3) return 1;
        char *end=NULL; double seconds=argc==3?strtod(argv[2],&end):10;
        if (argc==3 && (!end || end==argv[2] || *end)) return 1;
        jfx_nle_audio_analysis_t info={.size=sizeof(info)}; r=jfx_nle_analyze_file(argv[1],seconds,&info);
        if (r==JFX_SUCCESS) printf("%.3f LUFS peak=%.6f clap=%s time=%.6f confidence=%.3f\n",info.integrated_lufs,info.sample_peak,info.clap_detected?"yes":"no",info.clap_seconds,info.confidence);
    } else if (!strcmp(argv[0],"convert")) {
        if (argc!=3 && argc!=4) return 1;
        jfx_export_options_t options={.size=sizeof(options),.path=argv[2],.video_codec=argc==4?argv[3]:NULL,.audio=true};
        jfx_export_job_t *job=NULL; r=jfx_nle_convert_begin(argv[1],&options,&job);
        while (r==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_RUNNING) r=jfx_export_step(job,1);
        jfx_export_destroy(job);
    } else if (!strcmp(argv[0],"script") || !strcmp(argv[0],"otio-export") || !strcmp(argv[0],"otio-import")) {
        bool script=!strcmp(argv[0],"script"),import=!strcmp(argv[0],"otio-import");
        if (argc!=(script?4:3) && !(import && argc==7)) return 1;
        uint32_t w=1920,h=1080,num=30,den=1;
        if (import && argc==7) {
            uint32_t *values[]={&w,&h,&num,&den};
            for (unsigned i=0;i<4;++i) { char *end=NULL; unsigned long v=strtoul(argv[3+i],&end,10);
                if (end==argv[3+i] || *end || !v || v>1000000) return 1;
                *values[i]=(uint32_t)v;
            }
        }
        size_t length=0,written=0; char error[256]={0};
        char *text=read_text(argv[1],&length),*output=tilly_mem_alloc(JFX_PROJECT_MAX_BYTES);
        jfx_editor_t *editor=jfx_editor_create(w,h);
        if (!text || !output || !editor) r=JFX_ERROR_NOT_FOUND;
        else {
            r=import?jfx_nle_import_otio(editor,text,length,w,h,num,den):jfx_editor_load(editor,text,length,error,sizeof(error));
            if (r==JFX_SUCCESS && script) {
                size_t size=0; char *source=read_text(argv[2],&size);
                r=source?jfx_nle_run_script(editor,source,error,sizeof(error)):JFX_ERROR_NOT_FOUND;
                tilly_mem_free(source);
            }
            if (r==JFX_SUCCESS) r=!script && !import?jfx_nle_export_otio(editor,output,JFX_PROJECT_MAX_BYTES,&written):jfx_editor_save(editor,output,JFX_PROJECT_MAX_BYTES,&written);
            if (r==JFX_SUCCESS && !write_text(argv[script?3:2],output,written)) r=JFX_ERROR_BACKEND_FAILURE;
        }
        if (*error) fprintf(stderr,"%s\n",error);
        jfx_editor_destroy(editor); tilly_mem_free(text); tilly_mem_free(output);
    } else return -1;
    if (r!=JFX_SUCCESS) fprintf(stderr,"NLE: %s\n",jfx_result_to_string(r));
    return r==JFX_SUCCESS?0:1;
}
