/* Node-graph conformance.
 *
 * The graph is the substrate the node compositing panel edits, so the properties
 * that matter are structural - typing, cycle rejection, index stability - and
 * numerical: a chain of nodes has to produce the value the maths says it should,
 * because a wrong pixel here is a wrong composite in every frontend.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfx/jfx_compose.h"
#include "jfx/jfx_image.h"

#define W 8u
#define H 4u
#define N (W * H)

static uint8_t g_frame[N * 4];

static void near(float actual, float expected, float tolerance, const char *what) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL %s: got %.4f, want %.4f\n", what, (double)actual, (double)expected);
        assert(fabsf(actual - expected) <= tolerance);
    }
}

static float pixel_channel(const uint8_t *frame, size_t index, int channel) {
    return (float)frame[index * 4u + (size_t)channel] / 255.0f;
}

/* ---- The node library ----------------------------------------------------- */

static void test_kind_table(void) {
    assert(jfx_node_kind_count() > 0);
    /* Every kind must be self-consistent, because the desktop panel, the web
     * canvas and the CLI all build their widgets straight from this table. */
    for (size_t i = 0; i < jfx_node_kind_count(); ++i) {
        const jfx_node_kind_t *kind = jfx_node_kind_at(i);
        assert(kind && kind->name && kind->label && kind->category);
        assert(kind->input_count > 0 || kind->param_count > 0 || kind->string_count > 0
            || kind->output_count > 0);
        assert(kind->output_count > 0);
        assert(kind->param_count <= JFX_NODE_MAX_PARAMS);
        assert(kind->string_count <= JFX_GRAPH_MAX_STRING_PARAMS);
        for (size_t p = 0; p < kind->input_count; ++p) {
            const jfx_port_desc_t *port = &kind->inputs[p];
            assert(port->name && port->label);
            assert(port->type == JFX_PORT_IMAGE || port->type == JFX_PORT_COLOR
                || port->type == JFX_PORT_FLOAT);
        }
        for (size_t p = 0; p < kind->output_count; ++p) {
            assert(kind->outputs[p].name && kind->outputs[p].label);
        }
        for (size_t p = 0; p < kind->param_count; ++p) {
            const jfx_param_desc_t *param = &kind->params[p];
            assert(param->name && param->label);
            /* A default outside its own range would make a freshly added node
             * render something the slider cannot describe. */
            assert(param->minimum <= param->default_value);
            assert(param->default_value <= param->maximum);
            assert(param->minimum < param->maximum);
        }
        for (size_t s = 0; s < kind->string_count; ++s) {
            assert(kind->strings && kind->strings[s]);
        }
        /* Lookup by id must agree with lookup by position. */
        assert(jfx_node_kind_find(kind->name) == kind);
    }
    assert(jfx_node_kind_at(jfx_node_kind_count()) == NULL);
    assert(jfx_node_kind_find("no-such-node") == NULL);
    assert(jfx_node_kind_find(NULL) == NULL);

    /* The kinds the feature brief calls for have to actually be present. */
    assert(jfx_node_kind_find("lut") != NULL);
    assert(jfx_node_kind_find("blend") != NULL);
    assert(jfx_node_kind_find("chroma_key") != NULL);
    assert(jfx_node_kind_find("luma_key") != NULL);
    assert(jfx_node_kind_find("transform") != NULL);
    assert(jfx_node_kind_find("exposure") != NULL);
    assert(jfx_node_kind_find("lift_gamma_gain") != NULL);

    /* A blend node offers every mode, so a UI can list them from the range. */
    const jfx_node_kind_t *blend = jfx_node_kind_find("blend");
    assert(blend->param_count >= 1);
    assert(blend->params[0].name[0] == 'm');
    assert(blend->params[0].maximum == (float)(jfx_blend_mode_count() - 1));
}

static void test_blend_mode_names(void) {
    assert(jfx_blend_mode_count() == (size_t)JFX_BLEND_COUNT);
    for (int i = 0; i < JFX_BLEND_COUNT; ++i) {
        const char *name = jfx_blend_mode_name((jfx_blend_mode_t)i);
        assert(name && name[0]);
        assert(jfx_blend_mode_parse(name) == (jfx_blend_mode_t)i);
        /* A UI may hand back a label with spaces or underscores. */
        char spaced[64];
        snprintf(spaced, sizeof(spaced), "%s", name);
        for (char *p = spaced; *p; ++p) {
            if (*p == '-') {
                *p = ' ';
            }
        }
        assert(jfx_blend_mode_parse(spaced) == (jfx_blend_mode_t)i);
    }
    assert(jfx_blend_mode_parse("not-a-blend") == JFX_BLEND_COUNT);
    assert(jfx_blend_mode_parse(NULL) == JFX_BLEND_COUNT);
    /* Out-of-range values are named rather than crashing. */
    assert(jfx_blend_mode_name((jfx_blend_mode_t)999) != NULL);
    assert(strcmp(jfx_port_type_name(JFX_PORT_IMAGE), "image") == 0);
    assert(strcmp(jfx_port_type_name((jfx_port_type_t)99), "unknown") == 0);
}

/* ---- Structure ------------------------------------------------------------ */

static void test_structure(void) {
    jfx_graph_t *graph = jfx_graph_create();
    assert(graph);
    assert(jfx_graph_node_count(graph) == 0u);
    assert(jfx_graph_node_capacity(graph) == JFX_GRAPH_MAX_NODES);

    uint32_t color = 0, solid = 0, exposure = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", "Backdrop", &solid) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "exposure", NULL, &exposure) == JFX_SUCCESS);
    assert(jfx_graph_node_count(graph) == 3u);
    /* A NULL label falls back to the kind's label. */
    assert(strcmp(jfx_graph_node_label(graph, solid), "Backdrop") == 0);
    assert(strcmp(jfx_graph_node_label(graph, color), "Color") == 0);
    assert(strcmp(jfx_graph_node_kind(graph, color)->name, "color") == 0);
    assert(jfx_graph_set_node_label(graph, color, "Tint") == JFX_SUCCESS);
    assert(strcmp(jfx_graph_node_label(graph, color), "Tint") == 0);

    assert(jfx_graph_add_node(graph, "no-such-kind", NULL, &exposure) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_add_node(graph, "solid", NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_add_node(NULL, "solid", NULL, &color) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_node_count(graph) == 3u);

    /* Parameters start at their declared defaults. */
    const jfx_node_value_t *value = jfx_graph_node_value(graph, exposure);
    assert(value);
    assert(value->scalars[0] == 0.0f); /* exposure defaults to 0 stops */
    jfx_node_value_t *mutable_value = jfx_graph_node_value_mut(graph, exposure);
    assert(mutable_value);
    mutable_value->scalars[0] = 1.0f;
    near(jfx_graph_node_value(graph, exposure)->scalars[0], 1.0f, 0.0f, "param write");
    assert(jfx_graph_node_value(graph, 99) == NULL);
    assert(jfx_graph_node_value_mut(graph, 99) == NULL);
    assert(jfx_graph_node_kind(graph, 99) == NULL);
    assert(jfx_graph_node_label(graph, 99) == NULL);

    /* Connections. */
    assert(jfx_graph_connect(graph, color, 0, solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_input_source(graph, solid, 0) == (int)color);
    assert(jfx_graph_input_source_port(graph, solid, 0) == 0);
    assert(jfx_graph_connect(graph, solid, 0, exposure, 0) == JFX_SUCCESS);
    assert(jfx_graph_input_source(graph, exposure, 0) == (int)solid);
    /* A colour output cannot drive an image input. */
    assert(jfx_graph_connect(graph, color, 0, exposure, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_input_source(graph, exposure, 0) == (int)solid); /* unchanged */
    /* Out-of-range ports and nodes are refused. */
    assert(jfx_graph_connect(graph, solid, 0, exposure, 9) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_connect(graph, solid, 9, exposure, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_connect(graph, 99, 0, exposure, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_disconnect(graph, exposure, 0) == JFX_SUCCESS);
    assert(jfx_graph_input_source(graph, exposure, 0) == -1);
    assert(jfx_graph_disconnect(graph, 99, 0) == JFX_ERROR_INVALID_ARGUMENT);

    jfx_graph_destroy(graph);
    jfx_graph_destroy(NULL); /* must tolerate NULL */
}

static void test_cycle_rejection(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t a = 0, b = 0, c = 0;
    assert(jfx_graph_add_node(graph, "solid", NULL, &a) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "exposure", NULL, &b) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "contrast", NULL, &c) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, a, 0, b, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, b, 0, c, 0) == JFX_SUCCESS);
    assert(!jfx_graph_has_cycle(graph));

    /* b -> c is fine; c -> b would close a loop. */
    assert(jfx_graph_connect(graph, c, 0, b, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(!jfx_graph_has_cycle(graph));
    /* A self-connection is a one-node cycle. */
    assert(jfx_graph_connect(graph, a, 0, a, 0) == JFX_ERROR_INVALID_ARGUMENT);
    /* The direct back-edge and the longer one are both refused. */
    assert(jfx_graph_connect(graph, c, 0, a, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(!jfx_graph_has_cycle(graph));

    /* A diamond is a DAG, not a cycle: two paths converge on e. */
    uint32_t d = 0;
    assert(jfx_graph_add_node(graph, "opacity", NULL, &d) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, c, 0, d, 0) == JFX_SUCCESS);
    uint32_t e = 0;
    assert(jfx_graph_add_node(graph, "invert", NULL, &e) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, c, 0, e, 0) == JFX_SUCCESS);
    /* d is not an ancestor of e, so feeding one into the other is legal. */
    assert(jfx_graph_connect(graph, d, 0, e, 0) == JFX_SUCCESS);
    assert(!jfx_graph_has_cycle(graph));
    /* But e reads c, so feeding c from e would close a loop. */
    assert(jfx_graph_connect(graph, e, 0, c, 0) == JFX_ERROR_INVALID_ARGUMENT);
    /* Likewise d. */
    assert(jfx_graph_connect(graph, e, 0, d, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(!jfx_graph_has_cycle(graph));

    /* The order puts every input before its consumer. */
    uint32_t order[JFX_GRAPH_MAX_NODES];
    size_t count = 0;
    assert(jfx_graph_topological_order(graph, e, order, JFX_GRAPH_MAX_NODES, &count) == JFX_SUCCESS);
    assert(count == 5u);
    /* The real property: every node's inputs appear before it. */
    for (size_t i = 0; i < count; ++i) {
        const uint32_t node = order[i];
        for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
            const int32_t src = jfx_graph_input_source(graph, node, p);
            if (src < 0) {
                continue;
            }
            bool found = false;
            for (size_t j = 0; j < i; ++j) {
                if (order[j] == (uint32_t)src) {
                    found = true;
                    break;
                }
            }
            assert(found);
        }
    }

    assert(jfx_graph_topological_order(graph, 99, order, JFX_GRAPH_MAX_NODES, &count) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_topological_order(graph, e, order, 2, &count) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_topological_order(NULL, e, order, JFX_GRAPH_MAX_NODES, &count) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_graph_destroy(graph);
}

static void test_removal_rewires(void) {
    /* Removing a node compacts the indices, so every reference to a node after
     * the removed one has to be rewritten or the graph silently miswires. */
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t a = 0, b = 0, c = 0, d = 0;
    assert(jfx_graph_add_node(graph, "solid", NULL, &a) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "exposure", NULL, &b) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "contrast", NULL, &c) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "opacity", NULL, &d) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, a, 0, b, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, b, 0, c, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, c, 0, d, 0) == JFX_SUCCESS);
    assert(jfx_graph_input_source(graph, d, 0) == (int)c);

    assert(jfx_graph_remove_node(graph, 1) == JFX_SUCCESS); /* drop the exposure */
    assert(jfx_graph_node_count(graph) == 3u);
    /* The survivors shift down, keeping creation order: solid, contrast, opacity. */
    assert(strcmp(jfx_graph_node_kind(graph, 0)->name, "solid") == 0);
    assert(strcmp(jfx_graph_node_kind(graph, 1)->name, "contrast") == 0);
    assert(strcmp(jfx_graph_node_kind(graph, 2)->name, "opacity") == 0);
    /* 'opacity' read 'contrast' at index 2, which is now index 1. */
    assert(jfx_graph_input_source(graph, 2, 0) == 1);
    /* 'contrast' read the node that was just removed, so it is now unconnected
     * rather than left pointing at whatever shifted into the hole. */
    assert(jfx_graph_input_source(graph, 1, 0) == -1);
    assert(jfx_graph_remove_node(graph, 99) == JFX_ERROR_INVALID_ARGUMENT);

    /* Removing the source disconnects its consumer. */
    assert(jfx_graph_remove_node(graph, 0) == JFX_SUCCESS);
    assert(jfx_graph_node_count(graph) == 2u);
    assert(jfx_graph_input_source(graph, 0, 0) == -1);
    jfx_graph_destroy(graph);
}

/* ---- Evaluation ----------------------------------------------------------- */

static void test_solid_and_color(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t color = 0, solid = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &solid) == JFX_SUCCESS);
    jfx_node_value_t *value = jfx_graph_node_value_mut(graph, color);
    value->scalars[0] = 0.25f;
    value->scalars[1] = 0.5f;
    value->scalars[2] = 0.75f;
    value->scalars[3] = 1.0f;
    assert(jfx_graph_connect(graph, color, 0, solid, 0) == JFX_SUCCESS);

    assert(jfx_graph_render(graph, solid, W, H, 0.0f, g_frame) == JFX_SUCCESS);
    for (size_t i = 0; i < N; ++i) {
        near(pixel_channel(g_frame, i, 0), 0.25f, 1.0f / 255.0f, "solid red");
        near(pixel_channel(g_frame, i, 1), 0.5f, 1.0f / 255.0f, "solid green");
        near(pixel_channel(g_frame, i, 2), 0.75f, 1.0f / 255.0f, "solid blue");
        near(pixel_channel(g_frame, i, 3), 1.0f, 0.0f, "solid alpha");
    }

    /* An unconnected solid falls back to its port default, which is white. */
    assert(jfx_graph_disconnect(graph, solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, solid, W, H, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.0f, "unconnected solid is white");

    /* Bad geometry is rejected rather than written past the buffer. */
    assert(jfx_graph_render(graph, solid, 0, H, 0.0f, g_frame) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_render(graph, solid, W, 0, 0.0f, g_frame) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_render(graph, solid, W, H, 0.0f, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_render(graph, 99, W, H, 0.0f, g_frame) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_render(NULL, solid, W, H, 0.0f, g_frame) == JFX_ERROR_INVALID_ARGUMENT);
    /* An absurd frame size is refused rather than attempted. */
    assert(jfx_graph_render(graph, solid, 100000u, 100000u, 0.0f, g_frame) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_graph_destroy(graph);
}

/* Applies one single-input adjustment to a known constant and checks the
 * result, which is how a grading panel's maths is verified. */
static void check_adjustment(const char *kind, const float *params, size_t param_count,
    const float input[3], const float expect[3]) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t color = 0, node = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &node) == JFX_SUCCESS);
    jfx_node_value_t *color_value = jfx_graph_node_value_mut(graph, color);
    color_value->scalars[0] = input[0];
    color_value->scalars[1] = input[1];
    color_value->scalars[2] = input[2];
    color_value->scalars[3] = 1.0f;
    assert(jfx_graph_connect(graph, color, 0, node, 0) == JFX_SUCCESS);

    uint32_t adjust = 0;
    assert(jfx_graph_add_node(graph, kind, NULL, &adjust) == JFX_SUCCESS);
    jfx_node_value_t *value = jfx_graph_node_value_mut(graph, adjust);
    for (size_t i = 0; i < param_count; ++i) {
        value->scalars[i] = params[i];
    }
    assert(jfx_graph_connect(graph, node, 0, adjust, 0) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, adjust, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    for (size_t i = 0; i < 4; ++i) {
        near(pixel_channel(g_frame, i, 0), expect[0], 0.02f, kind);
        near(pixel_channel(g_frame, i, 1), expect[1], 0.02f, kind);
        near(pixel_channel(g_frame, i, 2), expect[2], 0.02f, kind);
    }
    jfx_graph_destroy(graph);
}

static void test_grading_maths(void) {
    /* Exposure: one stop is a doubling. */
    const float one_stop[1] = { 1.0f };
    const float half[3] = { 0.5f, 0.5f, 0.5f };
    const float doubled[3] = { 1.0f, 1.0f, 1.0f };
    check_adjustment("exposure", one_stop, 1, half, doubled);
    const float minus_one[1] = { -1.0f };
    const float quarter[3] = { 0.25f, 0.25f, 0.25f };
    check_adjustment("exposure", minus_one, 1, half, quarter);
    /* Zero stops is a no-op. */
    const float zero[1] = { 0.0f };
    check_adjustment("exposure", zero, 1, half, half);

    /* Contrast pivots on mid-grey, so grey is unchanged at any amount. */
    const float mid_contrast[1] = { 2.0f };
    const float mid_grey[3] = { 0.5f, 0.5f, 0.5f };
    check_adjustment("contrast", mid_contrast, 1, mid_grey, mid_grey);
    const float full_contrast[1] = { 2.0f };
    const float black[3] = { 0.0f, 0.0f, 0.0f };
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    check_adjustment("contrast", full_contrast, 1, black, black);
    check_adjustment("contrast", full_contrast, 1, white, white);
    /* Doubling contrast about mid-grey sends 0.25 to 0.0. */
    check_adjustment("contrast", full_contrast, 1, (const float[]){ 0.25f, 0.25f, 0.25f },
        (const float[]){ 0.0f, 0.0f, 0.0f });

    /* Saturation of 0 is grey; 2 doubles the distance from luma. */
    const float no_sat[1] = { 0.0f };
    const float luma_of_red = 0.2126f;
    check_adjustment("saturation", no_sat, 1, (const float[]){ 1.0f, 0.0f, 0.0f },
        (const float[]){ luma_of_red, luma_of_red, luma_of_red });
    const float full_sat[1] = { 1.0f };
    check_adjustment("saturation", full_sat, 1, (const float[]){ 1.0f, 0.0f, 0.0f },
        (const float[]){ 1.0f, 0.0f, 0.0f });

    /* Levels remap the input window onto the output window. */
    const float levels[] = { 0.25f, 0.75f, 1.0f, 0.0f, 1.0f };
    check_adjustment("levels", levels, 5, (const float[]){ 0.25f, 0.25f, 0.25f },
        (const float[]){ 0.0f, 0.0f, 0.0f });
    check_adjustment("levels", levels, 5, (const float[]){ 0.75f, 0.75f, 0.75f },
        (const float[]){ 1.0f, 1.0f, 1.0f });
    check_adjustment("levels", levels, 5, (const float[]){ 0.5f, 0.5f, 0.5f },
        (const float[]){ 0.5f, 0.5f, 0.5f });

    /* The channel mixer with identity gains changes nothing; pushing red from
     * green must tint a neutral pixel. */
    const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    check_adjustment("channel_mixer", identity, 9, (const float[]){ 0.2f, 0.4f, 0.6f },
        (const float[]){ 0.2f, 0.4f, 0.6f });
    const float swap_rb[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 0 };
    check_adjustment("channel_mixer", swap_rb, 9, (const float[]){ 0.2f, 0.4f, 0.6f },
        (const float[]){ 0.2f, 0.4f, 0.0f });

    /* Lift raises the floor; gain scales; gamma bends the midpoint. */
    const float lift[9] = { 0.2f, 0.2f, 0.2f, 1, 1, 1, 1, 1, 1 };
    check_adjustment("lift_gamma_gain", lift, 9, (const float[]){ 0.0f, 0.0f, 0.0f },
        (const float[]){ 0.2f, 0.2f, 0.2f });
    const float gain[9] = { 0, 0, 0, 1, 1, 1, 0.5f, 0.5f, 0.5f };
    check_adjustment("lift_gamma_gain", gain, 9, (const float[]){ 1.0f, 1.0f, 1.0f },
        (const float[]){ 0.5f, 0.5f, 0.5f });
    /* A neutral lift/gamma/gain is a no-op. */
    const float neutral[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 1 };
    check_adjustment("lift_gamma_gain", neutral, 9, (const float[]){ 0.3f, 0.5f, 0.7f },
        (const float[]){ 0.3f, 0.5f, 0.7f });

    /* Opacity scales alpha only. */
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t color = 0, solid = 0, opacity = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &solid) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "opacity", NULL, &opacity) == JFX_SUCCESS);
    jfx_graph_node_value_mut(graph, opacity)->scalars[0] = 0.5f;
    assert(jfx_graph_connect(graph, color, 0, solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, solid, 0, opacity, 0) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, opacity, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.0f, "opacity keeps colour");
    near(pixel_channel(g_frame, 0, 3), 0.5f, 0.01f, "opacity halves alpha");
    jfx_graph_destroy(graph);
}

static void test_compositing(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t red = 0, blue = 0, fg_solid = 0, bg_solid = 0, blend = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &red) == JFX_SUCCESS);
    jfx_node_value_t *rv = jfx_graph_node_value_mut(graph, red);
    rv->scalars[0] = 1.0f;
    rv->scalars[1] = 0.0f;
    rv->scalars[2] = 0.0f;
    rv->scalars[3] = 1.0f;
    assert(jfx_graph_add_node(graph, "color", NULL, &blue) == JFX_SUCCESS);
    jfx_node_value_t *bv = jfx_graph_node_value_mut(graph, blue);
    bv->scalars[0] = 0.0f;
    bv->scalars[1] = 0.0f;
    bv->scalars[2] = 1.0f;
    bv->scalars[3] = 1.0f;
    /* A colour drives a solid, and the solid is what a blend consumes. */
    assert(jfx_graph_add_node(graph, "solid", NULL, &fg_solid) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &bg_solid) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, red, 0, fg_solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, blue, 0, bg_solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "blend", NULL, &blend) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, fg_solid, 0, blend, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, bg_solid, 0, blend, 1) == JFX_SUCCESS);

    /* Normal at full opacity: the foreground wins. */
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.0f, "fg red over bg");
    near(pixel_channel(g_frame, 0, 1), 0.0f, 0.0f, "fg red over bg");
    near(pixel_channel(g_frame, 0, 2), 0.0f, 0.0f, "fg red over bg");

    /* Multiply. */
    jfx_graph_node_value_mut(graph, blend)->scalars[0] = (float)JFX_BLEND_MULTIPLY;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 0.0f, 0.0f, "multiply red*blue");
    near(pixel_channel(g_frame, 0, 2), 0.0f, 0.0f, "multiply red*blue");

    /* Screen. */
    jfx_graph_node_value_mut(graph, blend)->scalars[0] = (float)JFX_BLEND_SCREEN;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.0f, "screen red");
    near(pixel_channel(g_frame, 0, 2), 1.0f, 0.0f, "screen blue");

    /* Difference. */
    jfx_graph_node_value_mut(graph, blend)->scalars[0] = (float)JFX_BLEND_DIFFERENCE;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.0f, "difference red");
    near(pixel_channel(g_frame, 0, 2), 1.0f, 0.0f, "difference blue");

    /* The scalar mix input scales the blend strength. */
    jfx_graph_node_value_mut(graph, blend)->scalars[0] = (float)JFX_BLEND_NORMAL;
    uint32_t half = 0;
    assert(jfx_graph_add_node(graph, "float", NULL, &half) == JFX_SUCCESS);
    jfx_graph_node_value_mut(graph, half)->scalars[0] = 0.5f;
    assert(jfx_graph_connect(graph, half, 0, blend, 2) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    /* Half-way between opaque red and opaque blue. */
    near(pixel_channel(g_frame, 0, 0), 0.5f, 0.01f, "driven half mix red");
    near(pixel_channel(g_frame, 0, 2), 0.5f, 0.01f, "driven half mix blue");

    /* Zero strength leaves the background. */
    jfx_graph_node_value_mut(graph, half)->scalars[0] = 0.0f;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 0.0f, 0.0f, "zero mix leaves bg");
    near(pixel_channel(g_frame, 0, 2), 1.0f, 0.0f, "zero mix leaves bg");
    jfx_graph_destroy(graph);
}

/* An opaque source over a transparent one, and a half-transparent one, exercise
 * the alpha rule in composite_over. */
static void test_alpha_compositing(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t fg_color = 0, bg_color = 0, fg = 0, bg = 0, blend = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &fg_color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "color", NULL, &bg_color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &fg) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &bg) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "blend", NULL, &blend) == JFX_SUCCESS);
    jfx_node_value_t *f = jfx_graph_node_value_mut(graph, fg_color);
    f->scalars[0] = 1.0f;
    f->scalars[1] = 0.0f;
    f->scalars[2] = 0.0f;
    f->scalars[3] = 1.0f; /* opaque red */
    jfx_node_value_t *b = jfx_graph_node_value_mut(graph, bg_color);
    b->scalars[0] = 0.0f;
    b->scalars[1] = 0.0f;
    b->scalars[2] = 1.0f;
    b->scalars[3] = 1.0f; /* opaque blue */
    assert(jfx_graph_connect(graph, fg_color, 0, fg, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, bg_color, 0, bg, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, fg, 0, blend, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, bg, 0, blend, 1) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 3), 1.0f, 0.0f, "opaque over opaque stays opaque");

    /* A half-transparent red over blue gives the alpha-weighted mix. */
    jfx_graph_node_value_mut(graph, fg_color)->scalars[3] = 0.5f;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 0.5f, 0.01f, "half red");
    near(pixel_channel(g_frame, 0, 2), 0.5f, 0.01f, "half blue");
    near(pixel_channel(g_frame, 0, 3), 1.0f, 0.0f, "still opaque");

    /* A fully transparent foreground changes nothing. */
    jfx_graph_node_value_mut(graph, fg_color)->scalars[3] = 0.0f;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 0.0f, 0.0f, "clear fg leaves bg red");
    near(pixel_channel(g_frame, 0, 2), 1.0f, 0.0f, "clear fg leaves bg blue");

    /* Over nothing at all: the result is the foreground's own colour, not a
     * division by zero. */
    assert(jfx_graph_disconnect(graph, blend, 1) == JFX_SUCCESS);
    jfx_graph_node_value_mut(graph, fg_color)->scalars[3] = 0.5f;
    jfx_graph_node_value_mut(graph, fg_color)->scalars[0] = 1.0f;
    assert(jfx_graph_render(graph, blend, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 0), 1.0f, 0.01f, "fg over nothing keeps its colour");
    near(pixel_channel(g_frame, 0, 3), 0.5f, 0.01f, "fg over nothing keeps its alpha");
    jfx_graph_destroy(graph);
}

static void test_keying(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t color = 0, solid = 0, key_color = 0, chroma = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &solid) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "chroma_key", NULL, &chroma) == JFX_SUCCESS);
    jfx_node_value_t *c = jfx_graph_node_value_mut(graph, color);
    c->scalars[0] = 0.0f;
    c->scalars[1] = 1.0f;
    c->scalars[2] = 0.0f;
    c->scalars[3] = 1.0f; /* pure green */
    assert(jfx_graph_connect(graph, color, 0, solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, solid, 0, chroma, 0) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "color", NULL, &key_color) == JFX_SUCCESS);
    jfx_node_value_t *k = jfx_graph_node_value_mut(graph, key_color);
    k->scalars[0] = 0.0f;
    k->scalars[1] = 1.0f;
    k->scalars[2] = 0.0f;
    k->scalars[3] = 1.0f;
    assert(jfx_graph_connect(graph, key_color, 0, chroma, 1) == JFX_SUCCESS);

    /* A hard key with no smoothness: the keyed colour drops out entirely. */
    jfx_node_value_t *v = jfx_graph_node_value_mut(graph, chroma);
    v->scalars[0] = 0.3f;
    v->scalars[1] = 0.0f;
    assert(jfx_graph_render(graph, chroma, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 3), 0.0f, 0.0f, "keyed green is transparent");

    /* A colour far from the key survives untouched. */
    c->scalars[0] = 0.5f;
    c->scalars[1] = 0.0f;
    c->scalars[2] = 0.5f;
    assert(jfx_graph_render(graph, chroma, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 3), 1.0f, 0.0f, "magenta survives the green key");
    near(pixel_channel(g_frame, 0, 0), 0.5f, 0.01f, "magenta colour is unchanged");

    /* The luma key on a mid-grey and a black frame. */
    uint32_t luma = 0;
    assert(jfx_graph_add_node(graph, "luma_key", NULL, &luma) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, solid, 0, luma, 0) == JFX_SUCCESS);
    jfx_node_value_t *lv = jfx_graph_node_value_mut(graph, luma);
    lv->scalars[0] = 0.5f;
    lv->scalars[1] = 0.0f; /* hard edge */
    c->scalars[0] = 0.0f;
    c->scalars[1] = 0.0f;
    c->scalars[2] = 0.0f; /* black */
    assert(jfx_graph_render(graph, luma, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 3), 0.0f, 0.0f, "black is keyed out");
    c->scalars[0] = 1.0f;
    c->scalars[1] = 1.0f;
    c->scalars[2] = 1.0f; /* white */
    assert(jfx_graph_render(graph, luma, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(pixel_channel(g_frame, 0, 3), 1.0f, 0.0f, "white survives");
    jfx_graph_destroy(graph);
}

/* Every node kind must render without crashing, including with its inputs
 * unconnected - a panel lets a user build a partial graph and must preview it. */
static void test_every_kind_renders(void) {
    for (size_t i = 0; i < jfx_node_kind_count(); ++i) {
        const jfx_node_kind_t *kind = jfx_node_kind_at(i);
        jfx_graph_t *graph = jfx_graph_create();
        uint32_t node = 0;
        assert(jfx_graph_add_node(graph, kind->name, NULL, &node) == JFX_SUCCESS);
        /* An unassigned source previews transparent; an assigned missing video
         * reports its path/decoder error instead of hiding it. */
        if (!strcmp(kind->name, "video")) {
            assert(jfx_graph_render(graph,node,4,3,0.5f,g_frame)==JFX_SUCCESS && g_frame[3]==0);
            assert(jfx_graph_set_node_string(graph,node,0,"/missing/composition-video.mp4")==JFX_SUCCESS);
            jfx_result_t missing=jfx_graph_render(graph,node,4,3,0.5f,g_frame);
            assert(missing==JFX_ERROR_NOT_FOUND || missing==JFX_ERROR_NOT_IMPLEMENTED);
            jfx_graph_destroy(graph);
            continue;
        }
        /* Wire a solid into any required image input so the node has something
         * real to work on. */
        uint32_t solid = 0;
        if (jfx_graph_add_node(graph, "solid", NULL, &solid) == JFX_SUCCESS) {
            for (size_t p = 0; p < kind->input_count; ++p) {
                if (kind->inputs[p].type == JFX_PORT_IMAGE && kind->inputs[p].required) {
                    jfx_graph_connect(graph, solid, 0, node, p);
                }
            }
        }
        const jfx_result_t status = jfx_graph_render(graph, node, 4, 3, 0.5f, g_frame);
        if (status != JFX_SUCCESS) {
            fprintf(stderr, "kind '%s' did not render: %d\n", kind->name, (int)status);
        }
        assert(status == JFX_SUCCESS);
        jfx_graph_destroy(graph);

        /* And on its own, with nothing connected at all. */
        graph = jfx_graph_create();
        assert(jfx_graph_add_node(graph, kind->name, NULL, &node) == JFX_SUCCESS);
        const jfx_result_t bare = jfx_graph_render(graph, node, 4, 3, 0.5f, g_frame);
        if (bare != JFX_SUCCESS) {
            fprintf(stderr, "bare kind '%s' did not render: %d\n", kind->name, (int)bare);
        }
        assert(bare == JFX_SUCCESS);
        jfx_graph_destroy(graph);
    }
}

/* A diamond must evaluate its shared node once. The observable check is that a
 * chain still produces the right value; the structural check is the order. */
static void test_diamond_evaluation(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t color = 0, solid = 0, left = 0, right = 0, merge = 0;
    assert(jfx_graph_add_node(graph, "color", NULL, &color) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "solid", NULL, &solid) == JFX_SUCCESS);
    jfx_node_value_t *c = jfx_graph_node_value_mut(graph, color);
    c->scalars[0] = 0.25f;
    c->scalars[1] = 0.5f;
    c->scalars[2] = 0.75f;
    c->scalars[3] = 1.0f;
    assert(jfx_graph_connect(graph, color, 0, solid, 0) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "exposure", NULL, &left) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "contrast", NULL, &right) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "blend", NULL, &merge) == JFX_SUCCESS);
    jfx_graph_node_value_mut(graph, left)->scalars[0] = 1.0f;
    jfx_graph_node_value_mut(graph, right)->scalars[0] = 1.0f; /* no-op contrast */
    assert(jfx_graph_connect(graph, solid, 0, left, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, solid, 0, right, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, left, 0, merge, 0) == JFX_SUCCESS);
    assert(jfx_graph_connect(graph, right, 0, merge, 1) == JFX_SUCCESS);
    assert(jfx_graph_render(graph, merge, 2, 2, 0.0f, g_frame) == JFX_SUCCESS);
    /* The foreground is one stop hotter: 0.25 -> 0.5, 0.5 -> 1.0, 0.75 clamps. */
    near(pixel_channel(g_frame, 0, 0), 0.5f, 0.01f, "diamond red");
    near(pixel_channel(g_frame, 0, 1), 1.0f, 0.0f, "diamond green");
    near(pixel_channel(g_frame, 0, 2), 1.0f, 0.0f, "diamond blue");
    jfx_graph_destroy(graph);
}

static void test_render_node_and_describe(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t sweep = 0;
    assert(jfx_graph_add_node(graph, "sweep", NULL, &sweep) == JFX_SUCCESS);
    jfx_image_t image = { .size = sizeof(image) };
    assert(jfx_graph_render_node(graph, sweep, 4, 3, 0.0f, &image) == JFX_SUCCESS);
    assert(image.width == 4 && image.height == 3 && image.channels == 4);
    assert(image.pixels);
    jfx_image_release(&image);
    /* Releasing twice is safe, and an out-of-range size is refused. */
    jfx_image_release(&image);
    jfx_image_t bad = { .size = 1 };
    assert(jfx_graph_render_node(graph, sweep, 4, 3, 0.0f, &bad) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_render_node(graph, 99, 4, 3, 0.0f, &image) == JFX_ERROR_INVALID_ARGUMENT);

    char text[256];
    size_t written = 0;
    assert(jfx_graph_describe(graph, text, sizeof(text), &written) == JFX_SUCCESS);
    assert(written > 0 && strstr(text, "sweep") != NULL);
    assert(jfx_graph_describe(graph, text, 0, &written) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_describe(NULL, text, sizeof(text), &written) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_graph_destroy(graph);
}

/* A node budget overrun is a clean error, not a heap overflow. */
static void test_node_budget(void) {
    jfx_graph_t *graph = jfx_graph_create();
    for (size_t i = 0; i < JFX_GRAPH_MAX_NODES; ++i) {
        uint32_t node = 0;
        assert(jfx_graph_add_node(graph, "solid", NULL, &node) == JFX_SUCCESS);
        assert(node == (uint32_t)i);
    }
    assert(jfx_graph_node_count(graph) == JFX_GRAPH_MAX_NODES);
    uint32_t overflow = 0;
    assert(jfx_graph_add_node(graph, "solid", NULL, &overflow) == JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_graph_node_count(graph) == JFX_GRAPH_MAX_NODES);
    jfx_graph_destroy(graph);
}

int main(void) {
    test_kind_table();
    test_blend_mode_names();
    test_structure();
    test_cycle_rejection();
    test_removal_rewires();
    test_solid_and_color();
    test_grading_maths();
    test_compositing();
    test_alpha_compositing();
    test_keying();
    test_every_kind_renders();
    test_diamond_evaluation();
    test_render_node_and_describe();
    test_node_budget();
    puts("Compositing conformance tests passed");
    return 0;
}
