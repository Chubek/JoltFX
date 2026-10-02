#include "jfx/host_plugin.h"
#include "tilly/containers.h"

struct jfx_host_nle { jfx_editor_t *editor; };
jfx_result_t jfx_host_nle_export_begin(jfx_host_nle_t *s,const jfx_export_options_t *o,jfx_export_job_t **out) {
    return jfx_export_begin(s?s->editor:NULL,o,out);
}
jfx_result_t jfx_host_nle_audio_mixer(jfx_host_nle_t *s,uint32_t rate,jfx_audio_mixer_t **out) {
    return jfx_audio_mixer_create(s?jfx_editor_timeline(s->editor):NULL,rate,out);
}
jfx_result_t jfx_host_nle_create(jfx_host_kind_t host,uint32_t w,uint32_t h,jfx_host_nle_t **out) {
    if (!out || host<0 || host>=JFX_HOST_COUNT || !w || !h || w>4096 || h>4096) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_host_nle_t *s=tilly_container_alloc(sizeof(*s));
    if (!s) return JFX_ERROR_OUT_OF_MEMORY;
    s->editor=jfx_editor_create(w,h);
    if (!s->editor) { tilly_container_free(s); return JFX_ERROR_OUT_OF_MEMORY; }
    *out=s; return JFX_SUCCESS;
}
void jfx_host_nle_destroy(jfx_host_nle_t *s) {
    if (s) { jfx_editor_destroy(s->editor); tilly_container_free(s); }
}
jfx_result_t jfx_host_nle_load(jfx_host_nle_t *s,const char *text,size_t n,char *error,size_t cap) {
    return s?jfx_editor_load(s->editor,text,n,error,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_nle_save(jfx_host_nle_t *s,char *out,size_t cap,size_t *n) {
    return s?jfx_editor_save(s->editor,out,cap,n):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_nle_state(jfx_host_nle_t *s,char *out,size_t cap) {
    return s?jfx_editor_sequence_state(s->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_nle_edit(jfx_host_nle_t *s,const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) {
    return s?jfx_editor_command(s->editor,op,a,b,c,v,text):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_nle_render(jfx_host_nle_t *s,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    return s?jfx_editor_render(s->editor,seconds,w,h,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_nle_write_frame(jfx_host_nle_t *s,uint64_t frame,uint32_t w,uint32_t h,const char *path) {
    return s?jfx_editor_write_frame(s->editor,frame,w,h,path):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_composition_create(jfx_host_kind_t host,uint32_t w,uint32_t h,jfx_host_composition_t **out) {
    if (!out) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_host_composition_t *s=NULL; jfx_result_t r=jfx_host_nle_create(host,w,h,&s);
    if (r!=JFX_SUCCESS) return r;
    r=jfx_editor_command(s->editor,"graph.new",w,h,0,0,"");
    if (r!=JFX_SUCCESS) { jfx_host_nle_destroy(s); return r; }
    jfx_editor_clear_history(s->editor); *out=s; return JFX_SUCCESS;
}
void jfx_host_composition_destroy(jfx_host_composition_t *s) { jfx_host_nle_destroy(s); }
jfx_result_t jfx_host_composition_load(jfx_host_composition_t *s,const char *text,size_t n,char *error,size_t cap) {
    jfx_project_kind_t kind;
    if (!s || !text || n>JFX_PROJECT_MAX_BYTES || jfx_project_kind_of(text,n,&kind,error,cap)!=JFX_SUCCESS ||
        kind!=JFX_PROJECT_KIND_GRAPH) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_editor_load(s->editor,text,n,error,cap);
}
jfx_result_t jfx_host_composition_save(jfx_host_composition_t *s,char *out,size_t cap,size_t *n) {
    return s?jfx_project_save_graph(jfx_editor_graph(s->editor),jfx_editor_output(s->editor),
        jfx_editor_graph_width(s->editor),jfx_editor_graph_height(s->editor),out,cap,n):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_composition_state(jfx_host_composition_t *s,char *out,size_t cap) {
    return s?jfx_editor_graph_state(s->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_composition_edit(jfx_host_composition_t *s,const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) {
    return jfx_host_nle_edit(s,op,a,b,c,v,text);
}
jfx_result_t jfx_host_composition_render(jfx_host_composition_t *s,uint32_t node,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    return s?jfx_editor_render_graph(s->editor,node,seconds,w,h,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_host_composition_write_frame(jfx_host_composition_t *s,uint32_t node,double seconds,uint32_t w,uint32_t h,const char *path) {
    return s?jfx_editor_write_graph(s->editor,node,seconds,w,h,path):JFX_ERROR_INVALID_ARGUMENT;
}

typedef struct {
    const char *name;
    const char *identifier;
} host_definition_t;

static const host_definition_t definitions[JFX_HOST_COUNT] = {
    [JFX_HOST_AFTER_EFFECTS] = { "After Effects", "org.joltfx.after-effects" },
    [JFX_HOST_PREMIERE] = { "Premiere Pro", "org.joltfx.premiere" },
    [JFX_HOST_DAVINCI] = { "DaVinci Resolve", "org.joltfx.davinci" },
};

size_t jfx_host_color_kind_count(jfx_host_kind_t host,jfx_color_section_t section) {
    if (host<0 || host>=JFX_HOST_COUNT || section==JFX_COLOR_NONE) return 0;
    size_t count=0;
    for (size_t i=0;i<jfx_color_kind_count();++i) if (jfx_color_section(jfx_color_kind_at(i))==section) ++count;
    return count;
}
const jfx_node_kind_t *jfx_host_color_kind_at(jfx_host_kind_t host,jfx_color_section_t section,size_t index) {
    if (host<0 || host>=JFX_HOST_COUNT || section==JFX_COLOR_NONE) return NULL;
    for (size_t i=0;i<jfx_color_kind_count();++i) {
        const jfx_node_kind_t *k=jfx_color_kind_at(i);
        if (jfx_color_section(k)==section && index--==0) return k;
    }
    return NULL;
}
jfx_result_t jfx_host_color_process(jfx_host_kind_t host,const char *kind,
    const jfx_node_value_t *values,const jfx_lut_t *lut,const float *src,size_t w,size_t h,float *out) {
    if (host<0 || host>=JFX_HOST_COUNT) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_color_apply(jfx_node_kind_find(kind),values,lut,src,w,h,out);
}

jfx_result_t jfx_host_plugin_get_info(jfx_host_kind_t host,
    jfx_host_plugin_info_t *out_info) {
    if (!out_info || out_info->size < sizeof(*out_info) || host < 0 || host >= JFX_HOST_COUNT) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    out_info->size = sizeof(*out_info);
    out_info->host = host;
    out_info->host_name = definitions[host].name;
    out_info->plugin_identifier = definitions[host].identifier;
    /* Advertise nothing. This translation unit ships the host-independent
     * bridge only: no host SDK entry point, no AEGP/Fusion/OpenFX registration
     * and no import or export is implemented, so claiming those features would
     * make a host accept a plugin that cannot do the job. Features are added
     * here by the same file that implements them. */
    out_info->features = 0u;
    /* The proprietary host SDK (AE SDK, Premiere SDK, Fusion/OpenFX) is required
     * before any of these bridges can be loaded into a host. */
    out_info->requires_host_sdk = 1u;
    return JFX_SUCCESS;
}
