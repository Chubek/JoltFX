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
    /* A continuous gesture is one history step; cancellation restores the model. */
    jfx_editor_clear_history(e);
    assert(jfx_editor_begin_edit(NULL, JFX_PROJECT_KIND_GRAPH) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_begin_edit(e, (jfx_project_kind_t)99) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_commit_edit(e) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_cancel_edit(e) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_begin_edit(e, JFX_PROJECT_KIND_GRAPH) == JFX_SUCCESS);
    assert(jfx_editor_begin_edit(e, JFX_PROJECT_KIND_GRAPH) == JFX_ERROR_BUSY);
    for (int i=1;i<=10;++i)
        assert(jfx_editor_command(e,"node.param",0,0,0,i/10.0,"g") == JFX_SUCCESS);
    assert(jfx_editor_command(e,"clip.opacity",0,0,0,.5,"") == JFX_ERROR_BUSY);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"") == JFX_ERROR_BUSY);
    assert(jfx_editor_commit_edit(e) == JFX_SUCCESS);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"") == JFX_SUCCESS);
    assert(jfx_graph_node_value(jfx_editor_graph(e),0)->scalars[1] == 0);
    assert(!jfx_editor_can_undo(e));
    assert(jfx_editor_command(e,"redo",0,0,0,0,"") == JFX_SUCCESS);
    assert(jfx_editor_begin_edit(e, JFX_PROJECT_KIND_GRAPH) == JFX_SUCCESS);
    assert(jfx_editor_command(e,"node.remove",0,0,0,0,"") == JFX_SUCCESS);
    assert(jfx_editor_cancel_edit(e) == JFX_SUCCESS);
    assert(jfx_graph_node_count(jfx_editor_graph(e)) == 2);
    assert(jfx_graph_node_value(jfx_editor_graph(e),0)->scalars[1] == 1);
    jfx_editor_destroy(e);
    jfx_editor_destroy(NULL);
    assert(!jfx_editor_create(0, 2));
    return 0;
}
