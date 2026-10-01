#include "jfx/jfx_editor.h"
#include "tilly/allocator.h"
#include <math.h>
#include <string.h>
struct jfx_editor {
    jfx_timeline_t *timeline;
    jfx_graph_t *graph;
    uint32_t output, width, height;
    jfx_project_kind_t kind;
};
static void *allocate(size_t n) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), n, _Alignof(max_align_t));
}
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(), p); }
jfx_editor_t *jfx_editor_create(uint32_t w, uint32_t h) {
    if (!w || !h || w > 4096 || h > 4096) return NULL;
    jfx_editor_t *e = allocate(sizeof(*e));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e));
    e->width = w; e->height = h;
    e->timeline = jfx_timeline_create(w, h, 30, 1);
    e->graph = jfx_graph_create();
    if (!e->timeline || !e->graph) { jfx_editor_destroy(e); return NULL; }
    uint32_t track = jfx_timeline_add_track(e->timeline, "V1");
    jfx_clip_desc_t clip = {0};
    clip.name = "First clip"; clip.source = JFX_CLIP_SOLID;
    clip.source_params[0] = 0.6f; clip.source_params[1] = 0.3f;
    clip.source_params[2] = 0.15f; clip.source_params[3] = 1;
    clip.length_frames = 300; clip.opacity = 1; clip.enabled = true;
    if (track == UINT32_MAX || jfx_timeline_add_clip(e->timeline, track, &clip) == UINT32_MAX ||
        jfx_graph_add_node(e->graph, "solid", "Source", &e->output) != JFX_SUCCESS) {
        jfx_editor_destroy(e); return NULL;
    }
    return e;
}
void jfx_editor_destroy(jfx_editor_t *e) {
    if (!e) return;
    jfx_timeline_destroy(e->timeline); jfx_graph_destroy(e->graph); release(e);
}
jfx_timeline_t *jfx_editor_timeline(jfx_editor_t *e) { return e ? e->timeline : NULL; }
jfx_graph_t *jfx_editor_graph(jfx_editor_t *e) { return e ? e->graph : NULL; }
jfx_project_kind_t jfx_editor_kind(const jfx_editor_t *e) { return e ? e->kind : JFX_PROJECT_KIND_SEQUENCE; }
jfx_result_t jfx_editor_set_kind(jfx_editor_t *e, jfx_project_kind_t kind) {
    if (!e || (kind != JFX_PROJECT_KIND_SEQUENCE && kind != JFX_PROJECT_KIND_GRAPH)) return JFX_ERROR_INVALID_ARGUMENT;
    e->kind = kind; return JFX_SUCCESS;
}
uint32_t jfx_editor_output(const jfx_editor_t *e) { return e ? e->output : UINT32_MAX; }
jfx_result_t jfx_editor_set_output(jfx_editor_t *e, uint32_t node) {
    if (!e || !jfx_graph_node_kind(e->graph, node)) return JFX_ERROR_INVALID_ARGUMENT;
    e->output = node; return JFX_SUCCESS;
}
jfx_result_t jfx_editor_load(jfx_editor_t *e, const char *text, size_t n, char *err, size_t cap) {
    if (!e || !text || !n || n > JFX_PROJECT_MAX_BYTES) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_project_kind_t kind;
    jfx_result_t r = jfx_project_kind_of(text, n, &kind, err, cap);
    if (r != JFX_SUCCESS) return r;
    if (kind == JFX_PROJECT_KIND_SEQUENCE) {
        jfx_timeline_t *t = NULL;
        r = jfx_project_load_sequence(text, n, &t, err, cap);
        if (r != JFX_SUCCESS) return r;
        if (jfx_timeline_width(t) > 4096 || jfx_timeline_height(t) > 4096) {
            jfx_timeline_destroy(t); return JFX_ERROR_INVALID_ARGUMENT;
        }
        jfx_timeline_destroy(e->timeline); e->timeline = t;
    } else {
        jfx_graph_t *g = NULL; uint32_t o, w, h;
        r = jfx_project_load_graph(text, n, &g, &o, &w, &h, err, cap);
        if (r != JFX_SUCCESS) return r;
        if (w > 4096 || h > 4096) { jfx_graph_destroy(g); return JFX_ERROR_INVALID_ARGUMENT; }
        jfx_graph_destroy(e->graph); e->graph = g; e->output = o; e->width = w; e->height = h;
    }
    e->kind = kind; return JFX_SUCCESS;
}
jfx_result_t jfx_editor_save(const jfx_editor_t *e, char *text, size_t cap, size_t *written) {
    if (!e || !text || !written) return JFX_ERROR_INVALID_ARGUMENT;
    return e->kind == JFX_PROJECT_KIND_SEQUENCE ? jfx_project_save_sequence(e->timeline, text, cap, written)
        : jfx_project_save_graph(e->graph, e->output, e->width, e->height, text, cap, written);
}
jfx_result_t jfx_editor_render(jfx_editor_t *e, double seconds, uint32_t w, uint32_t h,
    uint8_t *out, size_t cap) {
    if (!e || !out || !w || !h || w > 4096 || h > 4096 || cap < (size_t)w*h*4 ||
        !isfinite(seconds) || seconds < 0 || seconds > 1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t sw = w, sh = h;
    if (e->kind == JFX_PROJECT_KIND_SEQUENCE) {
        sw = jfx_timeline_width(e->timeline); sh = jfx_timeline_height(e->timeline);
    }
    uint8_t *pixels = allocate((size_t)sw*sh*4);
    if (!pixels) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r = e->kind == JFX_PROJECT_KIND_SEQUENCE
        ? jfx_timeline_render(e->timeline, (uint64_t)(seconds*jfx_timeline_fps(e->timeline)), (float)seconds, pixels)
        : jfx_graph_render(e->graph, e->output, sw, sh, (float)seconds, pixels);
    if (r == JFX_SUCCESS) {
        for (uint32_t y=0; y<h; ++y) for (uint32_t x=0; x<w; ++x)
            memcpy(out+((size_t)y*w+x)*4, pixels+((size_t)((uint64_t)y*sh/h)*sw+(uint64_t)x*sw/w)*4, 4);
    }
    release(pixels); return r;
}

/* Compact FFI command surface for native/mobile/WASM and terminal editors.
 * Model validation remains in the timeline and graph, not in the widgets. */
jfx_result_t jfx_editor_command(jfx_editor_t *e, const char *op, uint32_t a,
    uint32_t b, uint32_t c, double value, const char *text) {
    if (!e || !op || !isfinite(value) || value < -1.e9 || value > 1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_timeline_t *t=e->timeline; jfx_graph_t *g=e->graph;
    if (strcmp(op,"sequence")==0) return jfx_editor_set_kind(e,JFX_PROJECT_KIND_SEQUENCE);
    if (strcmp(op,"graph")==0) return jfx_editor_set_kind(e,JFX_PROJECT_KIND_GRAPH);
    if (strcmp(op,"track.add")==0) return jfx_timeline_add_track(t,text)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    if (strcmp(op,"track.remove")==0) return jfx_timeline_remove_track(t,a);
    if (strcmp(op,"track.mute")==0) return jfx_timeline_set_track_muted(t,a,value!=0);
    if (strcmp(op,"clip.add")==0) {
        if (b>=JFX_CLIP_SOURCE_COUNT || value<1 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        jfx_clip_desc_t d={0}; d.source=(jfx_clip_source_t)b; d.start_frame=c;
        d.length_frames=(uint64_t)value; d.image_path=text; d.enabled=true; d.opacity=1;
        d.source_params[0]=0.6f; d.source_params[1]=0.3f; d.source_params[2]=0.15f; d.source_params[3]=1;
        return jfx_timeline_add_clip(t,a,&d)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    }
    if (strcmp(op,"clip.remove")==0) return jfx_timeline_remove_clip(t,a,b);
    if (strcmp(op,"clip.trim")==0) {
        if (value<1 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_timeline_trim_clip(t,a,b,c,(int64_t)value);
    }
    if (strcmp(op,"clip.opacity")==0) return jfx_timeline_set_clip_opacity(t,a,b,(float)value);
    if (strcmp(op,"effect.add")==0) return jfx_timeline_add_effect(t,a,b,text)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    if (strcmp(op,"effect.remove")==0) return jfx_timeline_remove_effect(t,a,b,c);
    if (strcmp(op,"effect.move")==0) {
        if (value<0 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_timeline_move_effect(t,a,b,c,(uint32_t)value);
    }
    if (strcmp(op,"effect.enabled")==0) return jfx_timeline_set_effect_enabled(t,a,b,c,value!=0);
    if (strcmp(op,"effect.opacity")==0) return jfx_timeline_set_effect_opacity(t,a,b,c,(float)value);
    if (strcmp(op,"effect.param")==0) {
        const jfx_node_kind_t *k=jfx_timeline_effect_kind_desc(t,a,b,c);
        if (!k || !text) return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t i=0;i<k->param_count;++i) if (strcmp(text,k->params[i].name)==0)
            return jfx_timeline_set_effect_param(t,a,b,c,i,(float)value);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (strcmp(op,"effect.path")==0) return jfx_timeline_set_effect_string(t,a,b,c,0,text);
    if (strcmp(op,"node.add")==0) { uint32_t n; return jfx_graph_add_node(g,text,NULL,&n); }
    if (strcmp(op,"node.remove")==0) {
        jfx_result_t r=jfx_graph_remove_node(g,a);
        if (r==JFX_SUCCESS) e->output=e->output>a ? e->output-1 : (e->output==a ? 0 : e->output);
        return r;
    }
    if (strcmp(op,"node.output")==0) return jfx_editor_set_output(e,a);
    if (strcmp(op,"node.connect")==0) {
        if (value<0 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_graph_connect(g,a,(size_t)value,b,c);
    }
    if (strcmp(op,"node.disconnect")==0) return jfx_graph_disconnect(g,a,b);
    if (strcmp(op,"node.path")==0) return jfx_graph_set_node_string(g,a,b,text);
    if (strcmp(op,"node.param")==0) {
        const jfx_node_kind_t *k=jfx_graph_node_kind(g,a);
        if (!k || !text) return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t i=0;i<k->param_count;++i) if (strcmp(text,k->params[i].name)==0) {
            if (value<(double)k->params[i].minimum || value>(double)k->params[i].maximum) return JFX_ERROR_INVALID_ARGUMENT;
            jfx_graph_node_value_mut(g,a)->scalars[i]=(float)value; return jfx_graph_touch(g,a);
        }
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return JFX_ERROR_INVALID_ARGUMENT;
}
