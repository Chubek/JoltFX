#include "commands.h"
#include "jfx/jfx_plugin_sdk.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_plugins(int argc,char **argv) {
    int inspect=argc==2 && !strcmp(argv[0],"inspect");
    int render=(argc==4 || argc==5) && !strcmp(argv[0],"render");
    if (!inspect && !render) { print_command_help("plugins"); return 1; }
    uint64_t frame=0;
    if (render && argc==5) {
        char *end=NULL; frame=strtoull(argv[4],&end,10);
        if (!*argv[4] || argv[4][0]=='-' || *end || frame>1000000000u) return 1;
    }
    jfx_engine_t *engine=NULL; jfx_plugin_host_t *host=NULL; jfx_editor_t *editor=NULL;
    jfx_engine_config_t config={0}; uint32_t id=0; int result=1; char *text=NULL;
    if (jfx_engine_init(&config,&engine)!=JFX_SUCCESS || jfx_plugin_host_create(engine,&host)!=JFX_SUCCESS) goto done;
    if (jfx_plugin_host_load(host,argv[1],&id)!=JFX_SUCCESS) {
        fprintf(stderr,"Plugin: %s\n",jfx_plugin_host_error(host)); goto done;
    }
    if (inspect) {
        jfx_plugin_info_t info={.size=sizeof(info)};
        if (jfx_plugin_host_get_info(host,id,&info)!=JFX_SUCCESS) goto done;
        printf("%s (%s) by %s, %u.%u.%u, capabilities 0x%x\n",info.display_name,info.identifier,info.vendor,
            info.version>>24,(info.version>>12)&4095u,info.version&4095u,info.capabilities);
        for (uint32_t i=0;i<jfx_plugin_host_action_count(host);++i) {
            jfx_plugin_action_info_t action={.size=sizeof(action)};
            if (jfx_plugin_host_action_info(host,i,&action)==JFX_SUCCESS) printf("action %s: %s\n",action.name,action.label);
        }
        result=0; goto done;
    }
    editor=jfx_editor_create(320,180);
    text=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    if (!editor || !text) goto done;
    FILE *file=fopen(argv[2],"rb"); if (!file) { perror(argv[2]); goto done; }
    size_t length=fread(text,1,JFX_PROJECT_MAX_BYTES+1,file); int failed=ferror(file); fclose(file);
    char error[256]={0};
    jfx_result_t r=failed?JFX_ERROR_INVALID_ARGUMENT:jfx_editor_load(editor,text,length,error,sizeof(error));
    if (r==JFX_SUCCESS) {
        if (jfx_editor_kind(editor)==JFX_PROJECT_KIND_GRAPH)
            r=jfx_editor_write_graph(editor,UINT32_MAX,(double)frame/30,jfx_editor_graph_width(editor),jfx_editor_graph_height(editor),argv[3]);
        else {
            jfx_timeline_t *t=jfx_editor_timeline(editor);
            r=jfx_editor_write_frame(editor,frame,jfx_timeline_width(t),jfx_timeline_height(t),argv[3]);
        }
    }
    if (r!=JFX_SUCCESS) fprintf(stderr,"Plugin render: %s %s\n",jfx_result_to_string(r),error);
    else result=0;
done:
    tilly_free((tilly_allocator_t *)tilly_default_allocator(),text);
    jfx_editor_destroy(editor); jfx_plugin_host_destroy(host); jfx_engine_shutdown(engine); return result;
}
