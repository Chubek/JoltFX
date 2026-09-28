#include <assert.h>
#include <string.h>
#include "jfx/jfx_editor.h"

int main(void) {
    jfx_editor_t *e = jfx_editor_create(4, 2);
    assert(e);
    unsigned char before[32], after[32];
    assert(jfx_editor_render(e, 0, 4, 2, before, sizeof(before)) == JFX_SUCCESS);
    jfx_timeline_t *t = jfx_editor_timeline(e);
    uint32_t fx = jfx_timeline_add_effect(t, 0, 0, "exposure");
    assert(fx != UINT32_MAX);
    assert(jfx_timeline_set_effect_param(t, 0, 0, fx, 0, -2) == JFX_SUCCESS);
    assert(jfx_editor_render(e, 0, 4, 2, after, sizeof(after)) == JFX_SUCCESS);
    assert(memcmp(before, after, sizeof(before)) != 0);
    char doc[8192], error[128]; size_t n = 0;
    assert(jfx_editor_save(e, doc, sizeof(doc), &n) == JFX_SUCCESS);
    assert(jfx_editor_load(e, "garbage", 7, error, sizeof(error)) != JFX_SUCCESS);
    assert(jfx_editor_render(e, 0, 4, 2, before, sizeof(before)) == JFX_SUCCESS);
    assert(memcmp(before, after, sizeof(before)) == 0);
    assert(jfx_editor_load(e, doc, n, error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_editor_render(e, 0, 4, 2, before, sizeof(before)) == JFX_SUCCESS);
    assert(memcmp(before, after, sizeof(before)) == 0);
    memset(after, 0xAB, sizeof(after));
    assert(jfx_editor_render(e, 0, 4, 2, after, 1) == JFX_ERROR_INVALID_ARGUMENT);
    assert(after[0] == 0xAB);
    assert(jfx_editor_render(e, -1, 4, 2, after, sizeof(after)) == JFX_ERROR_INVALID_ARGUMENT);
    const char *graph = "size 4 2\nnode color Red\nparam 1 r 1\nparam 1 g 0\nparam 1 b 0\nnode solid Output\nlink 1 0 -> 2 0\noutput 2\n";
    assert(jfx_editor_load(e, graph, strlen(graph), error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_editor_kind(e) == JFX_PROJECT_KIND_GRAPH);
    assert(jfx_editor_render(e, 0, 4, 2, after, sizeof(after)) == JFX_SUCCESS);
    assert(after[0] == 255 && after[1] == 0 && after[2] == 0);
    assert(jfx_editor_set_output(e, 999) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_editor_destroy(e);
    jfx_editor_destroy(NULL);
    assert(!jfx_editor_create(0, 2));
    return 0;
}
