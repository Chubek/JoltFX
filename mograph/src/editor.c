#include "jfx/jfx_editor.h"
#include "jfx/jfx_color.h"
#include "jfx/jfx_audio.h"
#include "jfx/jfx_plugin_sdk.h"
#include "plugin_internal.h"
#include "tilly/allocator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    char *text; size_t length; jfx_project_kind_t document,active;
    const jfx_node_kind_t *plugins[JFX_PLUGIN_MAX_EFFECTS]; size_t plugin_count;
} snapshot_t;
struct jfx_editor {
    jfx_timeline_t *timeline;
    jfx_graph_t *graph;
    uint32_t output, width, height;
    jfx_project_kind_t kind;
    snapshot_t undo[32], redo[32];
    size_t undo_count, redo_count, history_bytes;
    snapshot_t edit;
    bool edit_active, edit_changed;
    jfx_timeline_t *edit_timeline;
    jfx_graph_t *edit_graph;
    uint32_t edit_output,edit_width,edit_height;
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
    jfx_editor_clear_history(e);
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
uint32_t jfx_editor_graph_width(const jfx_editor_t *e) { return e?e->width:0; }
uint32_t jfx_editor_graph_height(const jfx_editor_t *e) { return e?e->height:0; }
jfx_result_t jfx_editor_set_output(jfx_editor_t *e, uint32_t node) {
    if (!e || !jfx_graph_node_kind(e->graph, node)) return JFX_ERROR_INVALID_ARGUMENT;
    e->output = node; return JFX_SUCCESS;
}
jfx_result_t jfx_editor_load(jfx_editor_t *e, const char *text, size_t n, char *err, size_t cap) {
    if (e && e->edit_active) return JFX_ERROR_BUSY;
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
    e->kind = kind; jfx_editor_clear_history(e); return JFX_SUCCESS;
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
    if (e->kind==JFX_PROJECT_KIND_SEQUENCE)
        return jfx_editor_render_frame(e,(uint64_t)floor(seconds*jfx_timeline_fps(e->timeline)+1.e-7),w,h,out,cap);
    return jfx_editor_render_graph(e,UINT32_MAX,seconds,w,h,out,cap);
}
jfx_result_t jfx_editor_render_graph(jfx_editor_t *e,uint32_t node,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    if (!e || !out || !w || !h || w>4096 || h>4096 || cap<(size_t)w*h*4 ||
        !isfinite(seconds) || seconds<0 || seconds>1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    if (node==UINT32_MAX) node=e->output;
    if (node==UINT32_MAX) { memset(out,0,(size_t)w*h*4); return JFX_SUCCESS; }
    uint8_t *pixels = allocate((size_t)w*h*4);
    if (!pixels) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t r = jfx_graph_render(e->graph, node, w, h, (float)seconds, pixels);
    if (r == JFX_SUCCESS) memcpy(out,pixels,(size_t)w*h*4);
    release(pixels); return r;
}
jfx_result_t jfx_editor_render_frame(jfx_editor_t *e,uint64_t frame,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    if (!e || e->kind!=JFX_PROJECT_KIND_SEQUENCE || !out || !w || !h || w>4096 || h>4096 ||
        frame>INT64_MAX || cap<(size_t)w*h*4) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t sw=jfx_timeline_width(e->timeline),sh=jfx_timeline_height(e->timeline);
    uint8_t *pixels=allocate((size_t)sw*sh*4);
    if (!pixels) return JFX_ERROR_OUT_OF_MEMORY;
    double seconds=(double)frame/jfx_timeline_fps(e->timeline);
    jfx_result_t r=jfx_timeline_render(e->timeline,frame,(float)seconds,pixels);
    if (r==JFX_SUCCESS) for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x)
        memcpy(out+((size_t)y*w+x)*4,pixels+((size_t)((uint64_t)y*sh/h)*sw+(uint64_t)x*sw/w)*4,4);
    release(pixels); return r;
}

/* Compact FFI command surface for native/mobile/WASM and terminal editors.
 * Model validation remains in the timeline and graph, not in the widgets. */
static jfx_result_t command_apply(jfx_editor_t *e, const char *op, uint32_t a,
    uint32_t b, uint32_t c, double value, const char *text) {
    if (!e || !op || !isfinite(value) || value < -1.e9 || value > 1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_timeline_t *t=e->timeline; jfx_graph_t *g=e->graph;
    /* Each color section addresses its own filtered list, never an effect in
     * another panel. Storage remains the ordered, serializable clip stack. */
    if (!strncmp(op,"grade.",6) || !strncmp(op,"calibration.",12)) {
        jfx_color_section_t section=!strncmp(op,"grade.",6)?JFX_COLOR_GRADING:JFX_COLOR_CALIBRATION;
        const char *action=op+(section==JFX_COLOR_GRADING?6:12);
        if (!strcmp(action,"add")) {
            const jfx_node_kind_t *k=jfx_node_kind_find(text);
            if (!k || jfx_color_section(k)!=section) return JFX_ERROR_INVALID_ARGUMENT;
            return jfx_timeline_add_effect(t,a,b,text)==UINT32_MAX?JFX_ERROR_INVALID_ARGUMENT:JFX_SUCCESS;
        }
        uint32_t index=UINT32_MAX, ordinal=0;
        for (uint32_t i=0;i<jfx_timeline_effect_count(t,a,b);++i)
            if (jfx_color_section(jfx_timeline_effect_kind_desc(t,a,b,i))==section && ordinal++==c) { index=i; break; }
        if (index==UINT32_MAX) return JFX_ERROR_INVALID_ARGUMENT;
        const jfx_node_kind_t *k=jfx_timeline_effect_kind_desc(t,a,b,index);
        if (!strcmp(action,"param")) {
            if (!text) return JFX_ERROR_INVALID_ARGUMENT;
            for (size_t p=0;p<k->param_count;++p) if (!strcmp(text,k->params[p].name)) {
                if (value<(double)k->params[p].minimum || value>(double)k->params[p].maximum ||
                    (k->params[p].integral && floor(value)!=value)) return JFX_ERROR_INVALID_ARGUMENT;
                return jfx_timeline_set_effect_param(t,a,b,index,p,(float)value);
            }
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        if (!strcmp(action,"enabled")) return jfx_timeline_set_effect_enabled(t,a,b,index,value!=0);
        if (!strcmp(action,"remove")) return jfx_timeline_remove_effect(t,a,b,index);
        if (!strcmp(action,"reset")) {
            for (size_t i=0;i<k->param_count;++i) {
                jfx_result_t r=jfx_timeline_set_effect_param(t,a,b,index,i,k->params[i].default_value);
                if (r!=JFX_SUCCESS) return r;
            }
            return JFX_SUCCESS;
        }
        if (!strcmp(action,"path")) {
            if (!k->string_count || !text || strlen(text)>=JFX_NODE_PATH_MAX) return JFX_ERROR_INVALID_ARGUMENT;
            if (*text) {
                jfx_lut_t *lut=NULL;
                jfx_result_t r=jfx_lut_load_auto(text,&lut,NULL,0);
                if (r==JFX_SUCCESS && strcmp(k->name,"lut") && lut->shape==JFX_LUT_SHAPE_2D) r=JFX_ERROR_INVALID_ARGUMENT;
                jfx_lut_destroy(lut);
                if (r!=JFX_SUCCESS) return r;
            }
            return jfx_timeline_set_effect_string(t,a,b,index,0,text);
        }
        if (!strcmp(action,"move")) {
            if (value<0 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
            ordinal=0;
            for (uint32_t i=0;i<jfx_timeline_effect_count(t,a,b);++i)
                if (jfx_color_section(jfx_timeline_effect_kind_desc(t,a,b,i))==section && ordinal++==(uint32_t)value)
                    return jfx_timeline_move_effect(t,a,b,index,i);
        }
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (strcmp(op,"sequence")==0) return jfx_editor_set_kind(e,JFX_PROJECT_KIND_SEQUENCE);
    if (strcmp(op,"graph")==0) return jfx_editor_set_kind(e,JFX_PROJECT_KIND_GRAPH);
    if (!strcmp(op,"graph.new")) {
        if (!a || !b || a>4096 || b>4096) return JFX_ERROR_INVALID_ARGUMENT;
        jfx_graph_t *fresh=jfx_graph_create(); uint32_t output;
        if (!fresh) return JFX_ERROR_OUT_OF_MEMORY;
        jfx_result_t r=jfx_graph_add_node(fresh,"solid","Source",&output);
        if (r!=JFX_SUCCESS) { jfx_graph_destroy(fresh); return r; }
        jfx_graph_destroy(e->graph); e->graph=fresh; e->output=output; e->width=a; e->height=b;
        e->kind=JFX_PROJECT_KIND_GRAPH; return JFX_SUCCESS;
    }
    if (!strcmp(op,"graph.size")) {
        if (!a || !b || a>4096 || b>4096) return JFX_ERROR_INVALID_ARGUMENT;
        e->width=a; e->height=b; return JFX_SUCCESS;
    }
    if (strcmp(op,"track.add")==0) return jfx_timeline_add_track(t,text)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    if (strcmp(op,"track.remove")==0) return jfx_timeline_remove_track(t,a);
    if (strcmp(op,"track.mute")==0) return jfx_timeline_set_track_muted(t,a,value!=0);
    if (!strcmp(op,"sequence.new")) {
        if (!a || !b || a>4096 || b>4096 || !c || value<1 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        jfx_timeline_t *fresh=jfx_timeline_create(a,b,c,(uint32_t)value);
        if (!fresh) return JFX_ERROR_OUT_OF_MEMORY;
        jfx_timeline_add_track(fresh,"V1"); jfx_timeline_destroy(e->timeline); e->timeline=fresh;
        e->kind=JFX_PROJECT_KIND_SEQUENCE; return JFX_SUCCESS;
    }
    if (!strcmp(op,"track.solo")) return jfx_timeline_set_track_solo(t,a,value!=0);
    if (!strcmp(op,"track.name")) return jfx_timeline_set_track_name(t,a,text);
    if (!strcmp(op,"track.opacity")) return value<0 || value>1?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_set_track_opacity(t,a,(float)value);
    if (!strcmp(op,"track.blend")) return value<0 || value>=(double)JFX_BLEND_COUNT || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_set_track_blend(t,a,(jfx_blend_mode_t)value);
    if (!strcmp(op,"track.move")) return value<0 || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_move_track(t,a,(uint32_t)value);
    if (!strcmp(op,"track.insert_gap")) return value<1 || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_ripple_insert(t,a,c,(uint64_t)value);
    if (strcmp(op,"clip.add")==0) {
        if (b>=JFX_CLIP_SOURCE_COUNT || value<1 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        jfx_clip_desc_t d={0}; d.source=(jfx_clip_source_t)b; d.start_frame=c;
        d.length_frames=(uint64_t)value; d.image_path=text; d.enabled=true; d.opacity=1;
        d.source_params[0]=0.6f; d.source_params[1]=0.3f; d.source_params[2]=0.15f; d.source_params[3]=1;
        return jfx_timeline_add_clip(t,a,&d)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    }
    if (strcmp(op,"clip.remove")==0) return jfx_timeline_remove_clip(t,a,b);
    if (!strcmp(op,"clip.name")) return jfx_timeline_set_clip_name(t,a,b,text);
    if (!strcmp(op,"clip.enabled")) return jfx_timeline_set_clip_enabled(t,a,b,value!=0);
    if (!strcmp(op,"clip.blend")) return value<0 || value>=(double)JFX_BLEND_COUNT || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_set_clip_blend(t,a,b,(jfx_blend_mode_t)value);
    if (!strcmp(op,"clip.split")) return value<0 || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_split_clip(t,a,b,(uint64_t)value);
    if (!strcmp(op,"clip.move")) return value<0 || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_position_clip(t,a,b,c,(uint64_t)value);
    if (!strcmp(op,"clip.duplicate")) return value<0 || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_duplicate_clip(t,a,b,c,(uint64_t)value);
    if (!strcmp(op,"clip.slip")) return floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_slip_clip(t,a,b,(int64_t)value);
    if (!strcmp(op,"clip.ripple_delete")) return jfx_timeline_ripple_delete(t,a,b);
    if (strcmp(op,"clip.trim")==0) {
        if (value<1 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_timeline_trim_clip(t,a,b,c,(int64_t)value);
    }
    if (strcmp(op,"clip.opacity")==0) return jfx_timeline_set_clip_opacity(t,a,b,(float)value);
    if (!strcmp(op,"track.audio.gain")) return jfx_timeline_set_track_audio_gain(t,a,(float)value);
    if (!strncmp(op,"clip.audio.",11)) {
        jfx_clip_audio_t audio={.size=sizeof(audio)};
        jfx_result_t r=jfx_timeline_get_clip_audio(t,a,b,&audio);
        if (r!=JFX_SUCCESS) return r;
        const char *field=op+11;
        if (!strcmp(field,"enabled")) audio.enabled=value!=0;
        else if (!strcmp(field,"gain")) audio.gain=(float)value;
        else if (!strcmp(field,"pan")) audio.pan=(float)value;
        else if ((!strcmp(field,"fade_in") || !strcmp(field,"fade_out")) && value>=0 && floor(value)==value) {
            if (!strcmp(field,"fade_in")) audio.fade_in_frames=(uint64_t)value;
            else audio.fade_out_frames=(uint64_t)value;
        } else return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_timeline_set_clip_audio(t,a,b,&audio);
    }
    if (strcmp(op,"effect.add")==0) return jfx_timeline_add_effect(t,a,b,text)==UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS;
    if (strcmp(op,"effect.remove")==0) return jfx_timeline_remove_effect(t,a,b,c);
    if (strcmp(op,"effect.move")==0) {
        if (value<0 || floor(value)!=value) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_timeline_move_effect(t,a,b,c,(uint32_t)value);
    }
    if (strcmp(op,"effect.enabled")==0) return jfx_timeline_set_effect_enabled(t,a,b,c,value!=0);
    if (strcmp(op,"effect.opacity")==0) return jfx_timeline_set_effect_opacity(t,a,b,c,(float)value);
    if (!strcmp(op,"effect.blend")) return value<0 || value>=(double)JFX_BLEND_COUNT || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_set_effect_blend(t,a,b,c,(jfx_blend_mode_t)value);
    if (!strcmp(op,"effect.string")) return value<0 || value>=JFX_GRAPH_MAX_STRING_PARAMS || floor(value)!=value?JFX_ERROR_INVALID_ARGUMENT:jfx_timeline_set_effect_string(t,a,b,c,(size_t)value,text);
    if (!strcmp(op,"effect.key.add") || !strcmp(op,"effect.key.remove")) {
        unsigned param; unsigned long long frame; char extra;
        if (!text || sscanf(text,"%u %llu %c",&param,&frame,&extra)!=2 || frame>INT64_MAX) return JFX_ERROR_INVALID_ARGUMENT;
        uint64_t start=jfx_timeline_clip_start(t,a,b);
        int64_t offset=jfx_timeline_clip_key_offset(t,a,b);
        if (frame<start) return JFX_ERROR_INVALID_ARGUMENT;
        uint64_t local=(uint64_t)frame-start;
        if ((offset<0 && local<(uint64_t)(-offset)) || (offset>0 && local>(uint64_t)INT64_MAX-(uint64_t)offset)) return JFX_ERROR_INVALID_ARGUMENT;
        frame=offset<0?local-(uint64_t)(-offset):local+(uint64_t)offset;
        return !strcmp(op,"effect.key.add")?jfx_timeline_add_key(t,a,b,c,param,(uint64_t)frame,(float)value):jfx_timeline_remove_key(t,a,b,c,param,(uint64_t)frame);
    }
    if (strcmp(op,"effect.param")==0) {
        const jfx_node_kind_t *k=jfx_timeline_effect_kind_desc(t,a,b,c);
        if (!k || !text) return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t i=0;i<k->param_count;++i) if (strcmp(text,k->params[i].name)==0)
            return jfx_timeline_set_effect_param(t,a,b,c,i,(float)value);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (strcmp(op,"effect.path")==0) return jfx_timeline_set_effect_string(t,a,b,c,0,text);
    if (strcmp(op,"node.add")==0) {
        uint32_t n; jfx_result_t r=jfx_graph_add_node(g,text,NULL,&n);
        if (r==JFX_SUCCESS && e->output==UINT32_MAX) e->output=n;
        return r;
    }
    if (!strcmp(op,"node.duplicate")) { uint32_t n; return jfx_graph_duplicate_node(g,a,&n); }
    if (!strcmp(op,"node.label")) return jfx_graph_set_node_label(g,a,text);
    if (!strcmp(op,"node.position")) {
        char *end; if (!text || !*text) return JFX_ERROR_INVALID_ARGUMENT;
        double y=strtod(text,&end);
        if (end==text || *end || !isfinite(y) || fabs(value)>1.e6 || fabs(y)>1.e6) return JFX_ERROR_INVALID_ARGUMENT;
        return jfx_graph_set_node_position(g,a,(float)value,(float)y);
    }
    if (!strcmp(op,"node.reset")) {
        const jfx_node_kind_t *k=jfx_graph_node_kind(g,a);
        if (!k) return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t p=0;p<k->param_count;++p) jfx_graph_node_value_mut(g,a)->scalars[p]=k->params[p].default_value;
        return jfx_graph_touch(g,a);
    }
    if (strcmp(op,"node.remove")==0) {
        jfx_result_t r=jfx_graph_remove_node(g,a);
        if (r==JFX_SUCCESS && e->output!=UINT32_MAX) {
            e->output=e->output>a ? e->output-1 : (e->output==a ? (jfx_graph_node_count(g)?0:UINT32_MAX) : e->output);
        }
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
            if (value<(double)k->params[i].minimum || value>(double)k->params[i].maximum ||
                (k->params[i].integral && floor(value)!=value)) return JFX_ERROR_INVALID_ARGUMENT;
            return jfx_graph_set_node_param(g,a,i,(float)value);
        }
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return JFX_ERROR_INVALID_ARGUMENT;
}

bool jfx_editor_can_undo(const jfx_editor_t *e) { return e && e->undo_count!=0; }
bool jfx_editor_can_redo(const jfx_editor_t *e) { return e && e->redo_count!=0; }
static void snapshot_release(snapshot_t *s) {
    for (size_t i=0;i<s->plugin_count;++i) jfx_plugin_kind_release(s->plugins[i]);
    release(s->text);
}
static void snapshot_pin(snapshot_t *s,const jfx_node_kind_t *kind) {
    if (!jfx_plugin_kind_is_custom(kind)) return;
    for (size_t i=0;i<s->plugin_count;++i) if (s->plugins[i]==kind) return;
    if (s->plugin_count<JFX_PLUGIN_MAX_EFFECTS && jfx_plugin_kind_retain(kind)) s->plugins[s->plugin_count++]=kind;
}
static void clear_stack(jfx_editor_t *e,snapshot_t *stack,size_t *count) {
    while (*count) { snapshot_t s=stack[--*count]; e->history_bytes-=s.length+1; snapshot_release(&s); }
}
static void release_edit_baseline(jfx_editor_t *e) {
    jfx_timeline_destroy(e->edit_timeline); e->edit_timeline=NULL;
    jfx_graph_destroy(e->edit_graph); e->edit_graph=NULL;
}
void jfx_editor_clear_history(jfx_editor_t *e) {
    if (!e) return;
    if (e->edit_active) { snapshot_release(&e->edit); release_edit_baseline(e); e->edit_active=false; e->edit_changed=false; }
    clear_stack(e,e->undo,&e->undo_count); clear_stack(e,e->redo,&e->redo_count);
}
static jfx_result_t snapshot(jfx_editor_t *e,jfx_project_kind_t document,snapshot_t *out) {
    for (size_t cap=4096;cap<=JFX_PROJECT_MAX_BYTES;cap*=2) {
        char *text=allocate(cap); size_t n=0;
        if (!text) return JFX_ERROR_OUT_OF_MEMORY;
        jfx_result_t r=document==JFX_PROJECT_KIND_GRAPH?jfx_project_save_graph(e->graph,e->output,e->width,e->height,text,cap,&n)
            :jfx_project_save_sequence(e->timeline,text,cap,&n);
        if (r==JFX_SUCCESS) {
            char *compact=allocate(n+1);
            if (!compact) { release(text); return JFX_ERROR_OUT_OF_MEMORY; }
            memcpy(compact,text,n+1); release(text);
            memset(out,0,sizeof(*out)); out->text=compact; out->length=n; out->document=document; out->active=e->kind;
            if (document==JFX_PROJECT_KIND_GRAPH) {
                for (uint32_t node=0;node<jfx_graph_node_count(e->graph);++node) snapshot_pin(out,jfx_graph_node_kind(e->graph,node));
            } else for (uint32_t t=0;t<jfx_timeline_track_count(e->timeline);++t)
                for (uint32_t c=0;c<jfx_timeline_clip_count(e->timeline,t);++c)
                    for (uint32_t fx=0;fx<jfx_timeline_effect_count(e->timeline,t,c);++fx) snapshot_pin(out,jfx_timeline_effect_kind_desc(e->timeline,t,c,fx));
            return JFX_SUCCESS;
        }
        release(text);
        if (r!=JFX_ERROR_BACKEND_FAILURE) return r;
    }
    return JFX_ERROR_OUT_OF_MEMORY;
}
static void discard_first(jfx_editor_t *e,snapshot_t *stack,size_t *count) {
    e->history_bytes-=stack[0].length+1; snapshot_release(&stack[0]);
    memmove(stack,stack+1,(--*count)*sizeof(*stack));
}
static void push_snapshot(jfx_editor_t *e,snapshot_t *stack,size_t *count,snapshot_t s) {
    if (*count==32) discard_first(e,stack,count);
    while (e->history_bytes+s.length+1>32u*1024u*1024u) {
        if (*count) discard_first(e,stack,count);
        else if (e->undo_count) discard_first(e,e->undo,&e->undo_count);
        else if (e->redo_count) discard_first(e,e->redo,&e->redo_count);
        else break;
    }
    stack[(*count)++]=s; e->history_bytes+=s.length+1;
}
jfx_result_t jfx_editor_begin_edit(jfx_editor_t *e,jfx_project_kind_t document) {
    if (!e || (document!=JFX_PROJECT_KIND_GRAPH && document!=JFX_PROJECT_KIND_SEQUENCE)) return JFX_ERROR_INVALID_ARGUMENT;
    if (e->edit_active) return JFX_ERROR_BUSY;
    jfx_result_t r=snapshot(e,document,&e->edit);
    if (r!=JFX_SUCCESS) return r;
    /* Reserve the restoration model before any live edit. Cancel/failed plugin
     * actions can then restore atomically without allocating during rollback. */
    r=document==JFX_PROJECT_KIND_GRAPH?
        jfx_project_load_graph(e->edit.text,e->edit.length,&e->edit_graph,&e->edit_output,&e->edit_width,&e->edit_height,NULL,0):
        jfx_project_load_sequence(e->edit.text,e->edit.length,&e->edit_timeline,NULL,0);
    if (r==JFX_SUCCESS) { e->edit_active=true; e->edit_changed=false; }
    else { snapshot_release(&e->edit); release_edit_baseline(e); }
    return r;
}
jfx_result_t jfx_editor_commit_edit(jfx_editor_t *e) {
    if (!e || !e->edit_active) return JFX_ERROR_INVALID_ARGUMENT;
    if (e->edit_changed) {
        clear_stack(e,e->redo,&e->redo_count);
        push_snapshot(e,e->undo,&e->undo_count,e->edit);
    } else snapshot_release(&e->edit);
    release_edit_baseline(e);
    e->edit_active=false; e->edit_changed=false;
    return JFX_SUCCESS;
}
jfx_result_t jfx_editor_cancel_edit(jfx_editor_t *e) {
    if (!e || !e->edit_active) return JFX_ERROR_INVALID_ARGUMENT;
    if (e->edit_graph) {
        jfx_graph_destroy(e->graph); e->graph=e->edit_graph; e->edit_graph=NULL;
        e->output=e->edit_output; e->width=e->edit_width; e->height=e->edit_height;
    } else { jfx_timeline_destroy(e->timeline); e->timeline=e->edit_timeline; e->edit_timeline=NULL; }
    e->kind=e->edit.active; snapshot_release(&e->edit);
    e->edit_active=false; e->edit_changed=false;
    return JFX_SUCCESS;
}
jfx_result_t jfx_editor_command(jfx_editor_t *e,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text) {
    if (!e || !op || !isfinite(value) || value<-1.e9 || value>1.e9) return JFX_ERROR_INVALID_ARGUMENT;
    if (!strcmp(op,"undo") || !strcmp(op,"redo")) {
        if (e->edit_active) return JFX_ERROR_BUSY;
        bool undo=!strcmp(op,"undo");
        snapshot_t *from=undo?e->undo:e->redo,*to=undo?e->redo:e->undo;
        size_t *from_count=undo?&e->undo_count:&e->redo_count,*to_count=undo?&e->redo_count:&e->undo_count;
        if (!*from_count) return JFX_ERROR_INVALID_ARGUMENT;
        snapshot_t previous=from[*from_count-1], current;
        jfx_result_t r=snapshot(e,previous.document,&current);
        if (r!=JFX_SUCCESS) return r;
        jfx_timeline_t *restored=NULL; jfx_graph_t *graph=NULL; uint32_t output=0,w=0,h=0;
        r=previous.document==JFX_PROJECT_KIND_GRAPH?jfx_project_load_graph(previous.text,previous.length,&graph,&output,&w,&h,NULL,0)
            :jfx_project_load_sequence(previous.text,previous.length,&restored,NULL,0);
        if (r!=JFX_SUCCESS) { snapshot_release(&current); return r; }
        --*from_count; e->history_bytes-=previous.length+1; snapshot_release(&previous);
        if (graph) { jfx_graph_destroy(e->graph); e->graph=graph; e->output=output; e->width=w; e->height=h; }
        else { jfx_timeline_destroy(e->timeline); e->timeline=restored; }
        e->kind=previous.active;
        push_snapshot(e,to,to_count,current); return JFX_SUCCESS;
    }
    bool graph=!strncmp(op,"node.",5) || !strncmp(op,"graph.",6);
    bool record=graph || !strncmp(op,"track.",6) || !strncmp(op,"clip.",5) || !strncmp(op,"effect.",7) ||
        !strncmp(op,"grade.",6) || !strncmp(op,"calibration.",12) || !strcmp(op,"sequence.new");
    if (!record) return command_apply(e,op,a,b,c,value,text);
    if (e->edit_active) {
        if (e->edit.document!=(graph?JFX_PROJECT_KIND_GRAPH:JFX_PROJECT_KIND_SEQUENCE)) return JFX_ERROR_BUSY;
        jfx_result_t r=command_apply(e,op,a,b,c,value,text);
        if (r==JFX_SUCCESS) e->edit_changed=true;
        return r;
    }
    snapshot_t before; jfx_result_t r=snapshot(e,graph?JFX_PROJECT_KIND_GRAPH:JFX_PROJECT_KIND_SEQUENCE,&before);
    if (r!=JFX_SUCCESS) return r;
    r=command_apply(e,op,a,b,c,value,text);
    if (r==JFX_SUCCESS) {
        clear_stack(e,e->redo,&e->redo_count);
        push_snapshot(e,e->undo,&e->undo_count,before);
    } else snapshot_release(&before);
    return r;
}
