/* Project interchange.
 *
 * The format is the contract between the desktop editor, the CLI, the web player
 * and host plugins, so what matters is that a document survives a round trip
 * exactly, and that a malformed one is rejected with a message naming the line
 * rather than rendering as something else. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfx/jfx_project.h"

static void near(float actual, float expected, float tolerance, const char *what) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL %s: got %.4f, want %.4f\n", what, (double)actual, (double)expected);
        assert(fabsf(actual - expected) <= tolerance);
    }
}

static void test_graph_parse(void) {
    static const char kGraph[] = "# a small grade\n"
                                 "size 320 240\n"
                                 "node sweep        Bars\n"
                                 "node exposure     Hot\n"
                                 "param 2 stops 1.5\n"
                                 "link 1 0 -> 2 0\n";
    jfx_graph_t *graph = NULL;
    uint32_t output = 0, width = 0, height = 0;
    char error[192] = { 0 };
    assert(jfx_project_load_graph(kGraph, 0, &graph, &output, &width, &height, error,
        sizeof(error)) == JFX_SUCCESS);
    if (!graph) {
        fprintf(stderr, "load failed: %s\n", error);
    }
    assert(graph);
    assert(width == 320 && height == 240);
    assert(jfx_graph_node_count(graph) == 2u);
    assert(output == 1u);
    assert(strcmp(jfx_graph_node_label(graph, 0), "Bars") == 0);
    near(jfx_graph_node_value(graph, 1)->scalars[0], 1.5f, 0.0f, "exposure stops");
    assert(jfx_graph_input_source(graph, 1, 0) == 0);

    /* A graph with a string field round-trips the path. */
    static const char kWithString[] = "size 16 16\n"
                                       "node image\n"
                                       "string 1 0 /tmp/a look.cube\n";
    char *text = strdup(kWithString);
    assert(text);
    jfx_graph_t *with_string = NULL;
    assert(jfx_project_load_graph(text, 0, &with_string, &output, &width, &height, error,
        sizeof(error)) == JFX_SUCCESS);
    assert(strcmp(jfx_graph_node_string(with_string, 0, 0), "/tmp/a look.cube") == 0);
    free(text);
    jfx_graph_destroy(with_string);
    jfx_graph_destroy(graph);
}

static void test_graph_roundtrip(void) {
    jfx_graph_t *graph = jfx_graph_create();
    uint32_t source = 0, node = 0;
    assert(jfx_graph_add_node(graph, "sweep", "Bars", &source) == JFX_SUCCESS);
    assert(jfx_graph_add_node(graph, "exposure", "Hot", &node) == JFX_SUCCESS);
    jfx_graph_node_value_mut(graph, node)->scalars[0] = 1.25f;
    assert(jfx_graph_connect(graph, source, 0, node, 0) == JFX_SUCCESS);

    char text[1024];
    size_t written = 0;
    assert(jfx_project_save_graph(graph, node, 64, 32, text, sizeof(text), &written) == JFX_SUCCESS);
    assert(written > 0 && written < sizeof(text));

    jfx_graph_t *reloaded = NULL;
    uint32_t output = 0, width = 0, height = 0;
    char error[192] = { 0 };
    assert(jfx_project_load_graph(text, 0, &reloaded, &output, &width, &height, error,
        sizeof(error)) == JFX_SUCCESS);
    if (!reloaded) {
        fprintf(stderr, "round trip failed: %s\n", error);
    }
    assert(reloaded);
    assert(width == 64 && height == 32);
    assert(jfx_graph_node_count(reloaded) == 2u);
    assert(strcmp(jfx_graph_node_kind(reloaded, 0)->name, "sweep") == 0);
    assert(strcmp(jfx_graph_node_label(reloaded, 0), "Bars") == 0);
    near(jfx_graph_node_value(reloaded, 1)->scalars[0], 1.25f, 1e-4f, "round-tripped stops");
    assert(jfx_graph_input_source(reloaded, 1, 0) == 0);
    jfx_graph_destroy(reloaded);
    jfx_graph_destroy(graph);
}

static void test_graph_rejects(void) {
    struct { const char *text; const char *why; } bad[] = {
        { "size 10 10\nnonsense 1\n", "unknown directive" },
        { "size 10 10\nnode not_a_kind\n", "unknown kind" },
        { "size 10 10\nnode sweep\nparam 9 value 1\n", "no such parameter" },
        { "size 10 10\nnode sweep\nnode exposure\nlink 1 0 -> 3 0\n", "no such node" },
        { "size 10 10\nnode sweep\nnode exposure\nlink 2 0 -> 1 0\n", "would close a cycle" },
        { "size 0 10\nnode sweep\n", "zero raster" },
        { "# only a comment\n", "no nodes" },
        { "size 10 10\nparam 1 stops 1\n", "param before a node" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        jfx_graph_t *graph = NULL;
        uint32_t output = 0, width = 0, height = 0;
        char error[192] = { 0 };
        const jfx_result_t status = jfx_project_load_graph(bad[i].text, 0, &graph, &output,
            &width, &height, error, sizeof(error));
        if (status == JFX_SUCCESS) {
            fprintf(stderr, "accepted a bad graph: %s\n", bad[i].why);
        }
        assert(status != JFX_SUCCESS);
        assert(graph == NULL); /* the out-parameter is untouched on failure */
        assert(error[0] != '\0');
        jfx_graph_destroy(graph);
    }
    /* A buffer too small is an error, not a truncation. */
    jfx_graph_t *graph = jfx_graph_create();
    char tiny[8];
    assert(jfx_project_save_graph(graph, 0, 1, 1, tiny, sizeof(tiny), NULL) ==
        JFX_ERROR_BACKEND_FAILURE);
    jfx_graph_destroy(graph);
}

static void test_sequence_roundtrip(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(320, 180, 30000, 1001);
    assert(timeline);
    const uint32_t lower = jfx_timeline_add_track(timeline, "V1");
    const uint32_t upper = jfx_timeline_add_track(timeline, "V2");

    jfx_clip_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.name = "Opener";
    desc.source = JFX_CLIP_SOLID;
    desc.source_params[0] = 0.2f;
    desc.source_params[1] = 0.4f;
    desc.source_params[2] = 0.6f;
    desc.source_params[3] = 1.0f;
    desc.start_frame = 5;
    desc.length_frames = 40;
    desc.opacity = 1.0f;
    desc.blend_mode = JFX_BLEND_NORMAL;
    desc.enabled = true;
    const uint32_t clip = jfx_timeline_add_clip(timeline, lower, &desc);
    assert(clip == 0u);

    const uint32_t effect = jfx_timeline_add_effect(timeline, lower, clip, "exposure");
    assert(jfx_timeline_set_effect_param(timeline, lower, clip, effect, 0, 0.75f) ==
        JFX_SUCCESS);
    const uint32_t lut = jfx_timeline_add_effect(timeline, lower, clip, "lut");
    assert(jfx_timeline_set_effect_string(timeline, lower, clip, lut, 0, "/looks/teal.cube") ==
        JFX_SUCCESS);
    assert(jfx_timeline_set_effect_param(timeline, lower, clip, lut, 0, 0.5f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(timeline, lower, clip, effect, 0, 5, 0.0f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(timeline, lower, clip, effect, 0, 45, 2.0f) == JFX_SUCCESS);

    jfx_clip_desc_t second = desc;
    second.name = "Grade";
    second.source = JFX_CLIP_IMAGE;
    second.image_path = "/frames/plate.png";
    second.start_frame = 0;
    second.length_frames = 20;
    second.source_params[0] = 0.0f;
    second.source_params[1] = 0.0f;
    second.source_params[2] = 0.0f;
    second.source_params[3] = 1.0f;
    assert(jfx_timeline_add_clip(timeline, upper, &second) == 0u);

    char text[2048];
    size_t written = 0;
    assert(jfx_project_save_sequence(timeline, text, sizeof(text), &written) == JFX_SUCCESS);
    assert(written > 0 && written < sizeof(text));

    jfx_timeline_t *reloaded = NULL;
    char error[192] = { 0 };
    assert(jfx_project_load_sequence(text, 0, &reloaded, error, sizeof(error)) == JFX_SUCCESS);
    if (!reloaded) {
        fprintf(stderr, "sequence round trip failed: %s\n", error);
        printf("---\n%s---\n", text);
    }
    assert(reloaded);
    assert(jfx_timeline_width(reloaded) == 320);
    assert(jfx_timeline_height(reloaded) == 180);
    /* The exact rate survives, not a rounded 29.97. */
    assert(jfx_timeline_fps_num(reloaded) == 30000);
    assert(jfx_timeline_fps_den(reloaded) == 1001);
    assert(jfx_timeline_track_count(reloaded) == 2u);
    assert(strcmp(jfx_timeline_track_name(reloaded, 0), "V1") == 0);
    assert(strcmp(jfx_timeline_track_name(reloaded, 1), "V2") == 0);

    assert(jfx_timeline_clip_count(reloaded, 0) == 1u);
    assert(strcmp(jfx_timeline_clip_name(reloaded, 0, 0), "Opener") == 0);
    assert(jfx_timeline_clip_source(reloaded, 0, 0) == JFX_CLIP_SOLID);
    assert(jfx_timeline_clip_start(reloaded, 0, 0) == 5u);
    assert(jfx_timeline_clip_length(reloaded, 0, 0) == 40u);
    near(jfx_timeline_clip_params(reloaded, 0, 0)[0], 0.2f, 1e-4f, "clip red");
    near(jfx_timeline_clip_params(reloaded, 0, 0)[3], 1.0f, 1e-4f, "clip alpha");

    assert(jfx_timeline_effect_count(reloaded, 0, 0) == 2u);
    assert(strcmp(jfx_timeline_effect_kind(reloaded, 0, 0, 0), "exposure") == 0);
    near(jfx_timeline_effect_param(reloaded, 0, 0, 0, 0), 0.75f, 1e-4f, "round-tripped stops");
    assert(strcmp(jfx_timeline_effect_kind(reloaded, 0, 0, 1), "lut") == 0);
    assert(strcmp(jfx_timeline_effect_string(reloaded, 0, 0, 1, 0), "/looks/teal.cube") == 0);
    near(jfx_timeline_effect_param(reloaded, 0, 0, 1, 0), 0.5f, 1e-4f, "round-tripped mix");
    /* Keyframes survive, so a reloaded document animates the same way. */
    assert(jfx_timeline_key_count(reloaded, 0, 0, 0, 0) == 2u);
    near(jfx_timeline_effect_param_at(reloaded, 0, 0, 0, 0, 25), 1.0f, 0.01f,
        "round-tripped keyframe");

    assert(jfx_timeline_clip_source(reloaded, 1, 0) == JFX_CLIP_IMAGE);
    assert(strcmp(jfx_timeline_clip_path(reloaded, 1, 0), "/frames/plate.png") == 0);

    /* Missing assigned LUTs and images report errors. Clear the LUT and disable
     * the placeholder image clip before comparing rendered pixel parity. */
    uint8_t *a = malloc(320u * 180u * 4u);
    uint8_t *b = malloc(320u * 180u * 4u);
    assert(a && b);
    assert(jfx_timeline_render(timeline, 10, 0.0f, a) != JFX_SUCCESS);
    assert(jfx_timeline_render(reloaded, 10, 0.0f, b) != JFX_SUCCESS);
    assert(jfx_timeline_set_effect_string(timeline, lower, clip, lut, 0, "") == JFX_SUCCESS);
    assert(jfx_timeline_set_effect_string(reloaded, lower, clip, lut, 0, "") == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 10, 0.0f, a) != JFX_SUCCESS);
    assert(jfx_timeline_render(reloaded, 10, 0.0f, b) != JFX_SUCCESS);
    assert(jfx_timeline_set_clip_enabled(timeline,1,0,false)==JFX_SUCCESS);
    assert(jfx_timeline_set_clip_enabled(reloaded,1,0,false)==JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 10, 0.0f, a) == JFX_SUCCESS);
    assert(jfx_timeline_render(reloaded, 10, 0.0f, b) == JFX_SUCCESS);
    assert(memcmp(a, b, 320u * 180u * 4u) == 0);
    free(a);
    free(b);
    jfx_timeline_destroy(reloaded);
    jfx_timeline_destroy(timeline);
}

static void test_sequence_rejects(void) {
    struct { const char *text; const char *why; } bad[] = {
        { "size 10 10\nclip solid 0 1 1 1 1 1 0 0 0 0\n", "clip before a track" },
        { "size 10 10\ntrack V1\nclip nonsense 0 1 1 1 1 1\n", "unknown source" },
        { "size 10 10\ntrack V1\nclip solid 0 1\n", "too few arguments" },
        { "size 10 10\ntrack V1\nclip solid 0 0 1 1 1 1 0 0 0 0\n", "zero length" },
        { "size 10 10\ntrack V1\nclip solid 0 1 1 1 1 1 0 0 0 0\neffect not_a_kind\n",
            "unknown effect" },
        { "size 10 10\ntrack V1\nclip solid 0 1 1 1 1 1 0 0 0 0\neffect blend\n",
            "a blend is not a stack stage" },
        { "size 10 10\nnonsense\n", "unknown directive" },
        { "# nothing\n", "no tracks" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        jfx_timeline_t *timeline = NULL;
        char error[192] = { 0 };
        const jfx_result_t status =
            jfx_project_load_sequence(bad[i].text, 0, &timeline, error, sizeof(error));
        if (status == JFX_SUCCESS) {
            fprintf(stderr, "accepted a bad sequence: %s\n", bad[i].why);
        }
        assert(status != JFX_SUCCESS);
        assert(timeline == NULL);
        assert(error[0] != '\0');
        jfx_timeline_destroy(timeline);
    }
}

/* A document with no `size` line gets the default raster rather than an error. */
static void test_sequence_defaults(void) {
    jfx_timeline_t *timeline = NULL;
    char error[192] = { 0 };
    assert(jfx_project_load_sequence("track V1\nclip solid 0 1 1 1 1 1 0 0 0 0 Opener\n", 0, &timeline,
        error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_timeline_width(timeline) == 1920u);
    assert(jfx_timeline_height(timeline) == 1080u);
    assert(jfx_timeline_fps_num(timeline) == 30u);
    assert(jfx_timeline_fps_den(timeline) == 1u);
    jfx_timeline_destroy(timeline);
}

static void test_kind_detection(void) {
    jfx_project_kind_t kind = JFX_PROJECT_KIND_SEQUENCE;
    char error[192] = { 0 };
    assert(jfx_project_kind_of("size 10 10\ntrack V1\n", 0, &kind, error, sizeof(error)) ==
        JFX_SUCCESS);
    assert(kind == JFX_PROJECT_KIND_SEQUENCE);
    assert(jfx_project_kind_of("size 10 10\nnode sweep\n", 0, &kind, error, sizeof(error)) ==
        JFX_SUCCESS);
    assert(kind == JFX_PROJECT_KIND_GRAPH);
    assert(jfx_project_kind_of("", 0, &kind, error, sizeof(error)) != JFX_SUCCESS);
    assert(jfx_project_kind_of(NULL, 0, &kind, error, sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);
}

int main(void) {
    test_graph_parse();
    test_graph_roundtrip();
    test_graph_rejects();
    test_sequence_roundtrip();
    test_sequence_rejects();
    test_sequence_defaults();
    test_kind_detection();
    puts("Project interchange tests passed");
    return 0;
}
