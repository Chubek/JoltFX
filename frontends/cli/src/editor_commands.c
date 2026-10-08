#include "commands.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_color.h"
#include "jfx/jfx_export.h"
#include "jfx/jfx_plugin_sdk.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* A line-oriented terminal editor: the same command surface used by the
 * browser/mobile panels. Commands can also be piped for reproducible edits. */
int cmd_edit(int argc, char **argv) {
    if (argc<2 || (argc-2)%2) { fprintf(stderr,"usage: joltfx edit INPUT.jfx OUTPUT.jfx [--plugin MODULE ...]\n"); return 1; }
    for (int i=2;i<argc;i+=2) if (strcmp(argv[i],"--plugin")) return 1;
    FILE *input=fopen(argv[0],"rb");
    if (!input) { perror(argv[0]); return 1; }
    jfx_engine_t *engine=NULL; jfx_plugin_host_t *plugins=NULL; jfx_engine_config_t config={0};
    if (jfx_engine_init(&config,&engine)!=JFX_SUCCESS || jfx_plugin_host_create(engine,&plugins)!=JFX_SUCCESS) {
        fclose(input); jfx_engine_shutdown(engine); return 1;
    }
    char *doc=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    jfx_editor_t *e=jfx_editor_create(320,180);
    if (!doc || !e) { fclose(input); jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc); jfx_plugin_host_destroy(plugins); jfx_engine_shutdown(engine); return 1; }
    size_t n=fread(doc,1,JFX_PROJECT_MAX_BYTES+1,input); int failed=ferror(input); fclose(input);
    char error[256]={0}; int exit_code=1;
    for (int i=2;i<argc;i+=2) {
        uint32_t id;
        if (jfx_plugin_host_load(plugins,argv[i+1],&id)!=JFX_SUCCESS) { fprintf(stderr,"%s\n",jfx_plugin_host_error(plugins)); goto done; }
    }
    if (failed || jfx_editor_load(e,doc,n,error,sizeof(error))!=JFX_SUCCESS) { fprintf(stderr,"%s\n",error); goto done; }
    fprintf(stderr,"JoltFX editor: NLE | Layer Effects | Color Calibration | Color Grading | Node Compositing | 3D Modeling & Animation\n"
        "Color Grading: grade.add/param/path/enabled/reset/remove/move; 'grade' lists operators.\n"
        "Color Calibration: calibration.add/param/path/enabled/reset/remove/move; 'calibration' lists operators.\n"
        "NLE: clip.split/move/trim/duplicate/slip/ripple_delete, track.move/solo/insert_gap.\n"
        "Composition: node.add/connect/disconnect/param/path/label/position/duplicate/reset/remove/output.\n"
        "3D Modeling & Animation: 3d.add/transform/vertex/key/subdivide/align/mass/bake/import_ply/export_ply; scene3d (JSON).\n"
        "Audio: clip.audio.enabled/gain/pan/fade_in/fade_out; track.audio.gain.\n"
        "Plugins: plugin.load PATH, plugin.unload ID, plugin.action NAME TRACK CLIP NODE, plugins.\n"
        "Commands: OP A B C VALUE TEXT (zero-based indices); undo, redo, timeline, composition, nodes, show, save, quit.\n");
    char line[2048];
    while (fgets(line,sizeof(line),stdin)) {
        line[strcspn(line,"\r\n")]=0;
        if (!*line || *line=='#') continue;
        if (!strncmp(line,"plugin.load ",12)) {
            uint32_t id;
            if (jfx_plugin_host_load(plugins,line+12,&id)!=JFX_SUCCESS) { fprintf(stderr,"%s\n",jfx_plugin_host_error(plugins)); goto done; }
            printf("Loaded plugin %u\n",id); continue;
        }
        if (!strcmp(line,"plugins")) {
            for (uint32_t i=0;i<jfx_plugin_host_count(plugins);++i) {
                uint32_t id; jfx_plugin_info_t info={.size=sizeof(info)};
                if (jfx_plugin_host_info_at(plugins,i,&id,&info)==JFX_SUCCESS) printf("%u %s (%s)\n",id,info.display_name,info.identifier);
            }
            continue;
        }
        if (!strncmp(line,"plugin.unload ",14)) {
            unsigned id; char tail;
            if (sscanf(line+14,"%u %c",&id,&tail)!=1 || jfx_plugin_host_unload(plugins,id)!=JFX_SUCCESS) { fprintf(stderr,"%s\n",jfx_plugin_host_error(plugins)); goto done; }
            continue;
        }
        if (!strncmp(line,"plugin.action ",14)) {
            char action[96],tail; unsigned track,clip,node;
            if (sscanf(line+14,"%95s %u %u %u %c",action,&track,&clip,&node,&tail)!=4) goto done;
            jfx_plugin_action_context_t context={sizeof(context),e,track,clip,node};
            if (jfx_plugin_host_invoke(plugins,action,&context)!=JFX_SUCCESS) { fprintf(stderr,"%s\n",jfx_plugin_host_error(plugins)); goto done; }
            continue;
        }
        if (!strcmp(line,"undo") || !strcmp(line,"redo") || !strcmp(line,"graph") || !strcmp(line,"sequence") || !strcmp(line,"3d")) {
            if (jfx_editor_command(e,line,0,0,0,0,"")!=JFX_SUCCESS) { fprintf(stderr,"No edit to %s.\n",line); goto done; }
            continue;
        }
        if (!strcmp(line,"timeline")) {
            if (jfx_editor_sequence_state(e,doc,JFX_PROJECT_MAX_BYTES)==JFX_SUCCESS) puts(doc);
            continue;
        }
        if (!strcmp(line,"composition")) {
            if (jfx_editor_graph_state(e,doc,JFX_PROJECT_MAX_BYTES)==JFX_SUCCESS) puts(doc);
            continue;
        }
        if (!strcmp(line,"scene3d")) {
            if (jfx_editor_scene3d_state(e,doc,JFX_PROJECT_MAX_BYTES)==JFX_SUCCESS) puts(doc);
            continue;
        }
        if (!strcmp(line,"nodes")) { cmd_nodes(0,NULL); continue; }
        if (!strcmp(line,"grade") || !strcmp(line,"calibration")) {
            char *args[]={"list"}; cmd_color(1,args,!strcmp(line,"calibration")); continue;
        }
        if (strcmp(line,"quit")==0) { exit_code=0; goto done; }
        if (strcmp(line,"save")==0) break;
        if (strcmp(line,"show")==0) {
            if (jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)==JFX_SUCCESS) puts(doc);
            continue;
        }
        char op[64],text[512]={0}; unsigned a,b,c; double value;
        int count=sscanf(line,"%63s %u %u %u %lf %511[^\n]",op,&a,&b,&c,&value,text);
        if (count<5 || jfx_editor_command(e,op,a,b,c,value,text)!=JFX_SUCCESS) {
            fprintf(stderr,"Edit rejected; document unchanged by this command.\n"); goto done;
        }
    }
    if (jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)!=JFX_SUCCESS) goto done;
    FILE *output=fopen(argv[1],"wb");
    if (!output) { perror(argv[1]); goto done; }
    failed=fwrite(doc,1,n,output)!=n;
    if (fclose(output)) failed=1;
    exit_code=failed?1:0;
done:
    jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc);
    jfx_plugin_host_destroy(plugins); jfx_engine_shutdown(engine); return exit_code;
}

static int positive(const char *text,uint32_t *out) {
    char *end; unsigned long n=strtoul(text,&end,10);
    if (!*text || *end || !n || n>1000000000u) return 0;
    *out=(uint32_t)n; return 1;
}
int cmd_scene3d(int argc,char **argv) {
    if (argc>=1 && !strcmp(argv[0],"edit")) return cmd_edit(argc-1,argv+1);
    if (argc<2) { fprintf(stderr,"usage: joltfx 3d new FILE | info FILE | edit IN OUT | render FILE OUT.png [SECONDS]\n"); return 1; }
    bool create=!strcmp(argv[0],"new"),info=!strcmp(argv[0],"info"),render=!strcmp(argv[0],"render");
    if ((!create && !info && !render) || (render?(argc<3 || argc>4):argc!=2)) return 1;
    char *buffer=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    jfx_editor_t *e=jfx_editor_create(960,540); int code=1; size_t n=0;
    if (!buffer || !e) goto cleanup;
    if (create) {
        if (jfx_editor_command(e,"3d.new",0,0,0,0,"")!=JFX_SUCCESS ||
            jfx_editor_command(e,"3d.add",0,0,0,0,"cube")!=JFX_SUCCESS ||
            jfx_editor_save(e,buffer,JFX_PROJECT_MAX_BYTES,&n)!=JFX_SUCCESS) goto cleanup;
        FILE *file=fopen(argv[1],"wb"); if (!file) goto cleanup;
        bool failed=fwrite(buffer,1,n,file)!=n; if (fclose(file)) failed=true; code=failed?1:0;
    } else {
        FILE *file=fopen(argv[1],"rb"); if (!file) goto cleanup;
        n=fread(buffer,1,JFX_PROJECT_MAX_BYTES+1,file); bool failed=ferror(file)!=0; fclose(file);
        char error[256]={0};
        if (failed || jfx_editor_load(e,buffer,n,error,sizeof(error))!=JFX_SUCCESS || jfx_editor_kind(e)!=JFX_PROJECT_KIND_SCENE3D) { fprintf(stderr,"3D scene rejected: %s\n",error); goto cleanup; }
        if (info) { if (jfx_editor_scene3d_state(e,buffer,JFX_PROJECT_MAX_BYTES)==JFX_SUCCESS) { puts(buffer); code=0; } }
        else {
            double seconds=0;
            if (argc==4) { char *end=NULL; seconds=strtod(argv[3],&end); if (end==argv[3] || *end || !isfinite(seconds) || seconds<0) goto cleanup; }
            code=jfx_scene3d_write_png(jfx_editor_scene3d(e),seconds,960,540,argv[2])==JFX_SUCCESS?0:1;
        }
    }
cleanup:
    tilly_free((tilly_allocator_t *)tilly_default_allocator(),buffer); jfx_editor_destroy(e); return code;
}
int cmd_nle(int argc,char **argv) {
    if (argc<2) { print_command_help("nle"); return 1; }
    if (!strcmp(argv[0],"edit")) return cmd_edit(argc-1,argv+1);
    if (!strcmp(argv[0],"render")) return cmd_render_sequence(argc-1,argv+1);
    if (!strcmp(argv[0],"export")) return cmd_media_export(argc-1,argv+1);
    int is_new=!strcmp(argv[0],"new");
    if (!is_new && (strcmp(argv[0],"info") || argc!=2)) { print_command_help("nle"); return 1; }
    uint32_t width=320,height=180,num=30,den=1;
    if (is_new) for (int i=2;i<argc;) {
        if (i+2>=argc) { print_command_help("nle"); return 1; }
        if (!strcmp(argv[i],"--size")) {
            if (!positive(argv[i+1],&width) || !positive(argv[i+2],&height)) return 1;
        } else if (!strcmp(argv[i],"--fps")) {
            if (!positive(argv[i+1],&num) || !positive(argv[i+2],&den)) return 1;
        } else { print_command_help("nle"); return 1; }
        i+=3;
    }
    char *doc=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    jfx_editor_t *e=jfx_editor_create(width,height); int result=1; size_t n=0;
    if (!doc || !e) goto done;
    if (is_new) {
        if (jfx_editor_command(e,"sequence.new",width,height,num,den,"")!=JFX_SUCCESS ||
            jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)!=JFX_SUCCESS) goto done;
        FILE *file=fopen(argv[1],"wb"); if (!file) { perror(argv[1]); goto done; }
        int failed=fwrite(doc,1,n,file)!=n; if (fclose(file)) failed=1;
        result=failed?1:0;
    } else {
        FILE *file=fopen(argv[1],"rb"); if (!file) { perror(argv[1]); goto done; }
        n=fread(doc,1,JFX_PROJECT_MAX_BYTES+1,file); int failed=ferror(file); fclose(file);
        char error[256]={0};
        if (failed || jfx_editor_load(e,doc,n,error,sizeof(error))!=JFX_SUCCESS || jfx_editor_kind(e)!=JFX_PROJECT_KIND_SEQUENCE) {
            fprintf(stderr,"Unable to open NLE sequence: %s\n",error); goto done;
        }
        if (jfx_editor_sequence_state(e,doc,JFX_PROJECT_MAX_BYTES)!=JFX_SUCCESS) goto done;
        puts(doc); result=0;
    }
done:
    jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc); return result;
}

int cmd_compose(int argc,char **argv) {
    if (argc<2) { print_command_help("compose"); return 1; }
    if (!strcmp(argv[0],"edit")) return cmd_edit(argc-1,argv+1);
    if (!strcmp(argv[0],"export")) return cmd_media_export(argc-1,argv+1);
    bool is_new=!strcmp(argv[0],"new"),render=!strcmp(argv[0],"render");
    if (!is_new && !render && (strcmp(argv[0],"info") || argc!=2)) { print_command_help("compose"); return 1; }
    uint32_t width=0,height=0,node=UINT32_MAX; double seconds=0; const char *output=NULL;
    for (int i=2;i<argc;++i) {
        if (!strcmp(argv[i],"--size") && i+2<argc) {
            if (!positive(argv[i+1],&width) || !positive(argv[i+2],&height) || width>4096 || height>4096) return 1;
            i+=2;
        } else if (render && !strcmp(argv[i],"-o") && i+1<argc) output=argv[++i];
        else if (render && !strcmp(argv[i],"--time") && i+1<argc) {
            char *end; seconds=strtod(argv[++i],&end);
            if (end==argv[i] || *end || !isfinite(seconds) || seconds<0 || seconds>1.e9) return 1;
        } else if (render && !strcmp(argv[i],"--node") && i+1<argc) {
            char *end; const char *s=argv[++i]; unsigned long value=strtoul(s,&end,10);
            if (!*s || *s=='-' || *end || value>=JFX_GRAPH_MAX_NODES) return 1;
            node=(uint32_t)value;
        } else { print_command_help("compose"); return 1; }
    }
    if (render && !output) { print_command_help("compose"); return 1; }
    char *doc=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    jfx_editor_t *e=jfx_editor_create(width?width:320,height?height:180); int result=1; size_t n=0;
    if (!doc || !e) goto done;
    if (is_new) {
        if (jfx_editor_command(e,"graph.new",width?width:320,height?height:180,0,0,"")!=JFX_SUCCESS ||
            jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)!=JFX_SUCCESS) goto done;
        FILE *file=fopen(argv[1],"wb"); if (!file) { perror(argv[1]); goto done; }
        int failed=fwrite(doc,1,n,file)!=n; if (fclose(file)) failed=1; result=failed?1:0;
    } else {
        FILE *file=fopen(argv[1],"rb"); if (!file) { perror(argv[1]); goto done; }
        n=fread(doc,1,JFX_PROJECT_MAX_BYTES+1,file); int failed=ferror(file); fclose(file); char error[256]={0};
        if (failed || jfx_editor_load(e,doc,n,error,sizeof(error))!=JFX_SUCCESS || jfx_editor_kind(e)!=JFX_PROJECT_KIND_GRAPH) {
            fprintf(stderr,"Unable to open composition: %s\n",error); goto done;
        }
        if (render) {
            jfx_result_t r=jfx_editor_write_graph(e,node,seconds,width?width:jfx_editor_graph_width(e),height?height:jfx_editor_graph_height(e),output);
            if (r!=JFX_SUCCESS) fprintf(stderr,"Composition export failed: %s\n",jfx_result_to_string(r));
            result=r==JFX_SUCCESS?0:1;
        } else if (jfx_editor_graph_state(e,doc,JFX_PROJECT_MAX_BYTES)==JFX_SUCCESS) { puts(doc); result=0; }
    }
done:
    jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc); return result;
}

static bool export_progress(void *user,uint64_t completed,uint64_t total) {
    (void)user; fprintf(stderr,"\rExport: %llu/%llu frames",(unsigned long long)completed,(unsigned long long)total); return true;
}
int cmd_media_export(int argc,char **argv) {
    if (argc<3) { print_command_help("export-video"); return 1; }
    jfx_export_options_t o={.size=sizeof(o),.audio=true};
    const char *modules[32]; size_t module_count=0;
    for (int i=1;i<argc;++i) {
        const char *key=argv[i];
        if (!strcmp(key,"--no-audio")) { o.audio=false; continue; }
        if (i+1>=argc) { print_command_help("export-video"); return 1; }
        const char *v=argv[++i];
        if (!strcmp(key,"-o")) o.path=v;
        else if (!strcmp(key,"--codec")) o.video_codec=v;
        else if (!strcmp(key,"--audio-codec")) o.audio_codec=v;
        else if (!strcmp(key,"--container")) o.container=v;
        else if (!strcmp(key,"--plugin") && module_count<32) modules[module_count++]=v;
        else {
            char *end; unsigned long long n=strtoull(v,&end,10);
            if (!*v || *v=='-' || *end || n>10000000) return 1;
            if (!strcmp(key,"--start")) o.start_frame=(uint64_t)n;
            else if (!strcmp(key,"--frames")) o.frame_count=(uint64_t)n;
            else if (!strcmp(key,"--sample-rate")) o.sample_rate=(uint32_t)n;
            else if (!strcmp(key,"--width")) o.width=(uint32_t)n;
            else if (!strcmp(key,"--height")) o.height=(uint32_t)n;
            else { print_command_help("export-video"); return 1; }
        }
    }
    if (!o.path) { print_command_help("export-video"); return 1; }
    FILE *file=fopen(argv[0],"rb"); if (!file) { perror(argv[0]); return 1; }
    char *text=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    if (!text) { fclose(file); return 1; }
    size_t n=fread(text,1,JFX_PROJECT_MAX_BYTES+1,file); int failed=ferror(file); fclose(file);
    jfx_editor_t *e=jfx_editor_create(16,16); char error[256]={0};
    jfx_engine_t *engine=NULL; jfx_plugin_host_t *plugins=NULL; jfx_engine_config_t config={0};
    jfx_result_t r=failed?JFX_ERROR_BACKEND_FAILURE:!e?JFX_ERROR_OUT_OF_MEMORY:JFX_SUCCESS;
    if (r==JFX_SUCCESS && module_count) {
        r=jfx_engine_init(&config,&engine);
        if (r==JFX_SUCCESS) r=jfx_plugin_host_create(engine,&plugins);
        for (size_t i=0;r==JFX_SUCCESS && i<module_count;++i) {
            uint32_t id; r=jfx_plugin_host_load(plugins,modules[i],&id);
            if (r!=JFX_SUCCESS) snprintf(error,sizeof(error),"%s",jfx_plugin_host_error(plugins));
        }
    }
    if (r==JFX_SUCCESS) r=jfx_editor_load(e,text,n,error,sizeof(error));
    if (r==JFX_SUCCESS) r=jfx_editor_export_video(e,&o,export_progress,NULL);
    if (r!=JFX_SUCCESS) fprintf(stderr,"\nExport failed: %s %s\n",jfx_result_to_string(r),error);
    else fprintf(stderr,"\nWritten %s\n",o.path);
    jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),text);
    jfx_plugin_host_destroy(plugins); jfx_engine_shutdown(engine); return r==JFX_SUCCESS?0:1;
}
