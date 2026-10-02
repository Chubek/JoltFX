/* Reflection and state for every composition frontend. The widgets own only
 * selection, pan and zoom; documents, layout and history stay in the editor. */
#include "jfx/jfx_editor.h"
#include "tilly/containers.h"
#include "tilly/attributes.h"
#include <stdarg.h>
#include <stdio.h>

TILLY_PRINTF_LIKE(4, 5)
static bool append(char *out,size_t cap,size_t *used,const char *fmt,...) {
    if (*used>=cap) return false;
    va_list args; va_start(args,fmt); int n=vsnprintf(out+*used,cap-*used,fmt,args); va_end(args);
    if (n<0 || (size_t)n>=cap-*used) return false;
    *used+=(size_t)n; return true;
}
static bool string(char *out,size_t cap,size_t *used,const char *text) {
    if (!append(out,cap,used,"\"")) return false;
    for (const unsigned char *p=(const unsigned char *)(text?text:"");*p;++p) {
        if (*p=='"' || *p=='\\') { if (!append(out,cap,used,"\\%c",*p)) return false; }
        else if (*p<32) { if (!append(out,cap,used,"\\u%04x",(unsigned)*p)) return false; }
        else if (!append(out,cap,used,"%c",*p)) return false;
    }
    return append(out,cap,used,"\"");
}
#define APP(...) do { if (!append(out,cap,&used,__VA_ARGS__)) return JFX_ERROR_OUT_OF_MEMORY; } while (0)
#define STR(s) do { if (!string(out,cap,&used,s)) return JFX_ERROR_OUT_OF_MEMORY; } while (0)
jfx_result_t jfx_node_catalog(char *out,size_t cap) {
    if (!out || !cap) return JFX_ERROR_INVALID_ARGUMENT;
    size_t used=0; APP("[");
    for (size_t i=0;i<jfx_node_kind_count();++i) {
        const jfx_node_kind_t *k=jfx_node_kind_at(i);
        APP("%s{\"name\":",i?",":""); STR(k->name); APP(",\"label\":"); STR(k->label);
        APP(",\"category\":"); STR(k->category);
        for (int side=0;side<2;++side) {
            const jfx_port_desc_t *ports=side?k->outputs:k->inputs;
            size_t count=side?k->output_count:k->input_count;
            APP(",\"%s\":[",side?"outputs":"inputs");
            for (size_t p=0;p<count;++p) {
                APP("%s{\"name\":",p?",":""); STR(ports[p].name); APP(",\"label\":"); STR(ports[p].label);
                APP(",\"type\":"); STR(jfx_port_type_name(ports[p].type));
                APP(",\"required\":%s}",ports[p].required?"true":"false");
            }
            APP("]");
        }
        APP(",\"params\":[");
        for (size_t p=0;p<k->param_count;++p) {
            const jfx_param_desc_t *v=&k->params[p];
            APP("%s{\"name\":",p?",":""); STR(v->name); APP(",\"label\":"); STR(v->label);
            APP(",\"min\":%.9g,\"max\":%.9g,\"default\":%.9g,\"step\":%.9g,\"integer\":%s}",
                (double)v->minimum,(double)v->maximum,(double)v->default_value,(double)v->step,v->integral?"true":"false");
        }
        APP("],\"strings\":[");
        for (size_t s=0;s<k->string_count;++s) { APP("%s",s?",":""); STR(k->strings[s]); }
        APP("]}");
    }
    APP("]"); return JFX_SUCCESS;
}
jfx_result_t jfx_editor_graph_state(const jfx_editor_t *e,char *out,size_t cap) {
    if (!e || !out || !cap) return JFX_ERROR_INVALID_ARGUMENT;
    const jfx_graph_t *g=jfx_editor_graph((jfx_editor_t *)e); size_t used=0;
    APP("{\"active\":%s,\"width\":%u,\"height\":%u,\"output\":",jfx_editor_kind(e)==JFX_PROJECT_KIND_GRAPH?"true":"false",
        jfx_editor_graph_width(e),jfx_editor_graph_height(e));
    uint32_t output=jfx_editor_output(e);
    if (output==UINT32_MAX) APP("null"); else APP("%u",output);
    APP(",\"canUndo\":%s,\"canRedo\":%s,\"nodes\":[",jfx_editor_can_undo(e)?"true":"false",jfx_editor_can_redo(e)?"true":"false");
    for (uint32_t n=0;n<jfx_graph_node_count(g);++n) {
        const jfx_node_kind_t *k=jfx_graph_node_kind(g,n); const jfx_node_value_t *v=jfx_graph_node_value(g,n);
        float x=0,y=0; jfx_graph_node_position(g,n,&x,&y);
        APP("%s{\"kind\":",n?",":""); STR(k->name); APP(",\"label\":"); STR(jfx_graph_node_label(g,n));
        APP(",\"x\":%.9g,\"y\":%.9g,\"values\":[",(double)x,(double)y);
        for (size_t p=0;p<k->param_count;++p) APP("%s%.9g",p?",":"",(double)v->scalars[p]);
        APP("],\"strings\":[");
        for (size_t s=0;s<k->string_count;++s) { APP("%s",s?",":""); STR(jfx_graph_node_string(g,n,s)); }
        APP("],\"inputs\":[");
        for (size_t p=0;p<k->input_count;++p) {
            int source=jfx_graph_input_source(g,n,p); APP("%s",p?",":"");
            if (source<0) APP("null");
            else APP("{\"source\":%d,\"port\":%d}",source,jfx_graph_input_source_port(g,n,p));
        }
        APP("]}");
    }
    APP("]}"); return JFX_SUCCESS;
}
#undef APP
#undef STR

jfx_result_t jfx_editor_write_graph(jfx_editor_t *e,uint32_t node,double seconds,uint32_t w,uint32_t h,const char *path) {
    if (!e || !path || !*path || !w || !h || w>4096 || h>4096) return JFX_ERROR_INVALID_ARGUMENT;
    size_t count=(size_t)w*h; uint8_t *pixels=tilly_container_alloc(count*4);
    if (!pixels) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r=jfx_editor_render_graph(e,node,seconds,w,h,pixels,count*4);
    if (r==JFX_SUCCESS) {
        for (size_t p=0;p<count;++p) for (size_t c=0;c<3;++c) pixels[p*3+c]=pixels[p*4+c];
        FILE *file=fopen(path,"wb");
        if (!file) r=JFX_ERROR_NOT_FOUND;
        else {
            if (fprintf(file,"P6\n%u %u\n255\n",w,h)<0 || fwrite(pixels,3,count,file)!=count) r=JFX_ERROR_BACKEND_FAILURE;
            if (fclose(file)) r=JFX_ERROR_BACKEND_FAILURE;
        }
    }
    tilly_container_free(pixels); return r;
}
