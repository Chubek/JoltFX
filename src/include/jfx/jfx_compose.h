#ifndef JFX_COMPOSE_H
#define JFX_COMPOSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"
#include "jfx_image.h"
#include "jfx_lut.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node-based compositing.
 *
 * A graph is a directed acyclic set of typed nodes. Leaf nodes generate a frame
 * (a solid, a gradient, an image, a test pattern); interior nodes consume the
 * frames of other nodes and produce one, or blend two of them. The graph is
 * evaluated by pulling from the output node and caching each node's result for
 * the duration of one evaluation, so a node feeding three others is computed
 * once.
 *
 * Everything a frontend needs to build a UI comes from the node kind table
 * rather than from a hardcoded list: `jfx_node_kind` gives the display label,
 * the input ports with their names and types, and the parameters with their
 * range, step and default. The desktop panel, the web canvas, the CLI and a
 * host plugin therefore all describe the same graph, and adding a node kind
 * makes it appear in all of them.
 *
 * Colours are straight (non-premultiplied) RGBA in [0,1]. Alpha is composited
 * with the general source-over equation, so a node may output partial
 * transparency and an effect may reduce it.
 *
 * The graph has no dependency on the render backends: it produces RGBA8, which
 * is what the compositor and the timeline consume. Evaluation is the synchronous
 * CPU reference path; kernel-backed color nodes use Glue and Execution. */

#define JFX_GRAPH_MAX_NODES 256
#define JFX_GRAPH_MAX_INPUTS 4
#define JFX_NODE_MAX_PARAMS 12
#define JFX_NODE_LABEL_MAX 64
#define JFX_NODE_PATH_MAX 512
#define JFX_GRAPH_MAX_STRING_PARAMS 2
#define JFX_COMPOSE_API_MAJOR 1
#define JFX_COMPOSE_API_MINOR 1

/* ---- Blend modes --------------------------------------------------------- */

/* The separable blend equations, applied per channel to the colour triple, then
 * combined with the source-over alpha rule. JFX_BLEND_NORMAL is a plain
 * source-over with no equation. */
typedef enum {
    JFX_BLEND_NORMAL = 0,
    JFX_BLEND_DARKEN,
    JFX_BLEND_MULTIPLY,
    JFX_BLEND_LINEAR_BURN,
    JFX_BLEND_COLOR_BURN,
    JFX_BLEND_LIGHTEN,
    JFX_BLEND_SCREEN,
    JFX_BLEND_COLOR_DODGE,
    JFX_BLEND_LINEAR_DODGE,
    JFX_BLEND_OVERLAY,
    JFX_BLEND_SOFT_LIGHT,
    JFX_BLEND_HARD_LIGHT,
    JFX_BLEND_DIFFERENCE,
    JFX_BLEND_EXCLUSION,
    JFX_BLEND_COUNT
} jfx_blend_mode_t;

/* Name as it appears in a UI or on the command line. */
const char *jfx_blend_mode_name(jfx_blend_mode_t mode);
/* Parses a name case-insensitively; returns JFX_BLEND_COUNT if unknown. */
jfx_blend_mode_t jfx_blend_mode_parse(const char *name);
size_t jfx_blend_mode_count(void);

/* ---- Node kinds ---------------------------------------------------------- */

typedef enum {
    JFX_PORT_IMAGE = 0,   /* a whole frame */
    JFX_PORT_COLOR,       /* r, g, b, a */
    JFX_PORT_FLOAT        /* a single scalar */
} jfx_port_type_t;

const char *jfx_port_type_name(jfx_port_type_t type);

typedef struct {
    const char *name;     /* stable id used by the connection API */
    const char *label;    /* shown to a user */
    jfx_port_type_t type;
    bool required;        /* a required input must be connected to evaluate */
    /* For JFX_PORT_COLOR inputs, the value used when nothing is connected. */
    float default_value[4];
} jfx_port_desc_t;

typedef struct {
    const char *name;     /* stable id, e.g. "exposure" */
    const char *label;    /* shown to a user */
    float minimum;
    float maximum;
    float default_value;
    float step;           /* a hint for a slider's granularity; 0 for continuous */
    bool integral;        /* the value is a whole number, e.g. a level index */
} jfx_param_desc_t;

typedef struct {
    const char *name;     /* stable id, e.g. "exposure" */
    const char *label;
    const char *category; /* grouping for a UI: "Color", "Transform", ... */
    size_t input_count;
    const jfx_port_desc_t *inputs;
    size_t output_count;
    const jfx_port_desc_t *outputs;
    size_t param_count;
    const jfx_param_desc_t *params;
    size_t string_count;  /* 0, or one/two text fields such as a file path */
    const char *const *strings;
} jfx_node_kind_t;

/* The built-in node library. `jfx_node_kind_count` and `jfx_node_kind_at`
 * enumerate it; `jfx_node_kind_find` looks one up by id. Both return NULL or
 * false rather than failing hard, so a UI can degrade on an older engine. */
size_t jfx_node_kind_count(void);
const jfx_node_kind_t *jfx_node_kind_at(size_t index);
const jfx_node_kind_t *jfx_node_kind_find(const char *name);
/* JSON descriptors for every node kind: typed inputs/outputs, parameter ranges,
 * integer hints and string fields. OUT_OF_MEMORY means the buffer is too small. */
jfx_result_t jfx_node_catalog(char *out_json, size_t capacity);

/* ---- Parameter values ---------------------------------------------------- */

/* A node's mutable state: its parameter values and its text fields. */
#define JFX_NODE_VALUE_MAX (JFX_NODE_MAX_PARAMS * 4)

typedef struct {
    float scalars[JFX_NODE_VALUE_MAX];
    char *strings[JFX_GRAPH_MAX_STRING_PARAMS];
} jfx_node_value_t;

void jfx_node_value_init(jfx_node_value_t *value, const jfx_node_kind_t *kind);
void jfx_node_value_release(jfx_node_value_t *value);
/* Deep copy. */
jfx_result_t jfx_node_value_copy(const jfx_node_value_t *source, jfx_node_value_t *dest);

/* ---- Graphs -------------------------------------------------------------- */

typedef struct jfx_graph jfx_graph_t;

jfx_graph_t *jfx_graph_create(void);
void jfx_graph_destroy(jfx_graph_t *graph);

size_t jfx_graph_node_count(const jfx_graph_t *graph);
size_t jfx_graph_node_capacity(const jfx_graph_t *graph);

/* Appends a node of the named kind. `label` may be NULL to use the kind's
 * label. Returns JFX_ERROR_INVALID_ARGUMENT for an unknown kind and
 * JFX_ERROR_OUT_OF_MEMORY when the node budget is full. `*out_node` is written
 * only on success and is left untouched on failure. */
jfx_result_t jfx_graph_add_node(jfx_graph_t *graph, const char *kind_name, const char *label,
    uint32_t *out_node);

/* Removes a node and every edge touching it. The nodes above it shift down by
 * one so the array stays in creation order, which is what a node list shows;
 * indices below the hole are unchanged. */
jfx_result_t jfx_graph_remove_node(jfx_graph_t *graph, uint32_t node);
/* Deep-copy values/strings and incoming edges; outgoing edges stay on the
 * original. Appends the copy with a 32-unit layout offset. Atomic on failure. */
jfx_result_t jfx_graph_duplicate_node(jfx_graph_t *graph, uint32_t node, uint32_t *out_node);

const jfx_node_kind_t *jfx_graph_node_kind(const jfx_graph_t *graph, uint32_t node);
const char *jfx_graph_node_label(const jfx_graph_t *graph, uint32_t node);
jfx_result_t jfx_graph_set_node_label(jfx_graph_t *graph, uint32_t node, const char *label);
const jfx_node_value_t *jfx_graph_node_value(const jfx_graph_t *graph, uint32_t node);
jfx_node_value_t *jfx_graph_node_value_mut(jfx_graph_t *graph, uint32_t node);
/* Validated parameter editing for descriptor-driven inspectors. */
jfx_result_t jfx_graph_set_node_param(jfx_graph_t *graph, uint32_t node, size_t param, float value);
/* Persistent graph-space layout, independent of a frontend's zoom/pan. Finite
 * coordinates in [-1e6,1e6]; new nodes receive a deterministic grid position. */
jfx_result_t jfx_graph_set_node_position(jfx_graph_t *graph, uint32_t node, float x, float y);
jfx_result_t jfx_graph_node_position(const jfx_graph_t *graph, uint32_t node, float *out_x, float *out_y);

/* Marks a node and everything downstream of it stale, so the next render
 * recomputes. Values are changed through the returned pointer, so a UI must
 * call this after writing one. */
jfx_result_t jfx_graph_touch(jfx_graph_t *graph, uint32_t node);

/* Connections. Port indices are the kind's input port order. Connecting
 * overwrites whatever occupied the input. Connecting a node to itself, or
 * closing a cycle, returns JFX_ERROR_INVALID_ARGUMENT and leaves the graph
 * unchanged. */
jfx_result_t jfx_graph_connect(jfx_graph_t *graph, uint32_t from_node, size_t from_port,
    uint32_t to_node, size_t to_port);
jfx_result_t jfx_graph_disconnect(jfx_graph_t *graph, uint32_t to_node, size_t to_port);
/* The node feeding an input, or -1. */
int jfx_graph_input_source(const jfx_graph_t *graph, uint32_t to_node, size_t to_port);
/* The port on the source node, or -1 when nothing is connected. */
int jfx_graph_input_source_port(const jfx_graph_t *graph, uint32_t to_node, size_t to_port);

/* Topological order over the nodes that feed `output`. Fails with
 * JFX_ERROR_INVALID_ARGUMENT if a cycle is reachable, so a caller can detect
 * the fault before rendering rather than after. `out_order` must have room for
 * jfx_graph_node_count entries. */
jfx_result_t jfx_graph_topological_order(const jfx_graph_t *graph, uint32_t output,
    uint32_t *out_order, size_t capacity, size_t *out_count);

/* True if the graph contains a cycle anywhere, not merely on the path to
 * `output`. */
bool jfx_graph_has_cycle(const jfx_graph_t *graph);

/* Renders `output` into `out_pixels` as tightly packed RGBA8, `width` x
 * `height`. `time_seconds` drives the kinds that animate. Any node whose
 * required image input is unconnected is treated as transparent. Assigned missing
 * resources and evaluation failures return errors without overwriting output.
 * Only reachable nodes allocate frames, under a 512-MiB float-frame budget. */
jfx_result_t jfx_graph_render(const jfx_graph_t *graph, uint32_t output, uint32_t width,
    uint32_t height, float time_seconds, uint8_t *out_pixels);

/* Renders one node and returns its frame through `jfx_image_release`, for a UI
 * that wants to preview an interior node. */
jfx_result_t jfx_graph_render_node(const jfx_graph_t *graph, uint32_t node, uint32_t width,
    uint32_t height, float time_seconds, jfx_image_t *out_image);

/* Copies `text` into the node's string slot, replacing whatever was there.
 * The graph owns the copy, so the caller keeps ownership of `text`. */
jfx_result_t jfx_graph_set_node_string(jfx_graph_t *graph, uint32_t node, size_t index,
    const char *text);
/* Borrows a node's string. Valid until the node is removed or the string is
 * replaced. */
const char *jfx_graph_node_string(const jfx_graph_t *graph, uint32_t node, size_t index);

/* Blends `count` RGBA8 pixels of `src` over `dst` in place, using the same
 * separable equation and alpha rule as the `blend` node. `opacity` scales the
 * source alpha. This is what a compositor or a timeline uses to lay one frame
 * over another without building a graph. */
void jfx_blend_rgba8(uint8_t *dst, const uint8_t *src, size_t count, jfx_blend_mode_t mode,
    float opacity);

/* Human-readable names of the kinds in a graph, for diagnostics: writes a
 * summary line per node into `out_text` and reports the bytes it needed. */
jfx_result_t jfx_graph_describe(const jfx_graph_t *graph, char *out_text, size_t out_size,
    size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* JFX_COMPOSE_H */
