#ifndef JFX_EDITOR_H
#define JFX_EDITOR_H
#include "jfx_project.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Shared editing session. Single-owner-thread API; borrowed model handles remain
 * valid until a successful load/reset/undo/redo/cancel or destruction. Edits use the timeline and
 * graph APIs, so every interface uses the same validators and evaluator. */
#define JFX_EDITOR_API_MAJOR 1
#define JFX_EDITOR_API_MINOR 5
typedef struct jfx_editor jfx_editor_t;
jfx_editor_t *jfx_editor_create(uint32_t width, uint32_t height);
void jfx_editor_destroy(jfx_editor_t *editor);
jfx_timeline_t *jfx_editor_timeline(jfx_editor_t *editor);
jfx_graph_t *jfx_editor_graph(jfx_editor_t *editor);
jfx_project_kind_t jfx_editor_kind(const jfx_editor_t *editor);
jfx_result_t jfx_editor_set_kind(jfx_editor_t *editor, jfx_project_kind_t kind);
uint32_t jfx_editor_output(const jfx_editor_t *editor);
jfx_result_t jfx_editor_set_output(jfx_editor_t *editor, uint32_t node);
/* Loading is transactional: errors preserve both documents and current mode. */
jfx_result_t jfx_editor_load(jfx_editor_t *editor, const char *text, size_t length,
    char *out_error, size_t error_size);
jfx_result_t jfx_editor_save(const jfx_editor_t *editor, char *out_text, size_t capacity,
    size_t *out_written);
/* Render the active document, scaling the sequence raster to the requested
 * preview. Output is untouched on error. Raster dimensions are limited to 4096. */
jfx_result_t jfx_editor_render(jfx_editor_t *editor, double seconds, uint32_t width,
    uint32_t height, uint8_t *out_rgba, size_t capacity);
/* Exact sequence-frame preview/export, avoiding floating-point seek rounding.
 * write_frame produces binary PPM (RGB, alpha discarded); render errors happen
 * before the output file is opened. */
jfx_result_t jfx_editor_render_frame(jfx_editor_t *editor,uint64_t frame,uint32_t width,
    uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_editor_write_frame(jfx_editor_t *editor,uint64_t frame,uint32_t width,
    uint32_t height,const char *path);
/* FFI edits: op is documented in docs/nle.md, docs/composition.md and docs/editor.md. Indices are zero-based.
 * Unknown commands and invalid numeric values return INVALID_ARGUMENT. */
jfx_result_t jfx_editor_command(jfx_editor_t *editor, const char *op,
    uint32_t a, uint32_t b, uint32_t c, double value, const char *text);
/* Command-based sequence and graph edits have 32 steps of undo/redo, bounded by 32 MiB.
 * Load clears history. Direct borrowed-model mutations are outside this history;
 * call clear_history after changing the model directly. */
bool jfx_editor_can_undo(const jfx_editor_t *editor);
bool jfx_editor_can_redo(const jfx_editor_t *editor);
void jfx_editor_clear_history(jfx_editor_t *editor);
/* Group commands targeting one document into a single undo step (e.g. a wheel
 * drag or plugin action). Nested edits, history, loads and edits to the other
 * document return BUSY. Cancel restores the baseline and invalidates borrowed
 * handles. Failed commands leave the group active. Empty groups add no history. */
jfx_result_t jfx_editor_begin_edit(jfx_editor_t *editor, jfx_project_kind_t document);
jfx_result_t jfx_editor_commit_edit(jfx_editor_t *editor);
jfx_result_t jfx_editor_cancel_edit(jfx_editor_t *editor);
/* JSON sequence state for native/mobile/WASM/host NLE widgets. Does not change
 * the active document mode. Returns OUT_OF_MEMORY for insufficient capacity. */
jfx_result_t jfx_editor_sequence_state(const jfx_editor_t *editor, char *out_json, size_t capacity);
/* Graph state includes layout, values, strings, edges, output, raster, active
 * mode and history availability. Reading it does not switch modes. */
jfx_result_t jfx_editor_graph_state(const jfx_editor_t *editor, char *out_json, size_t capacity);
uint32_t jfx_editor_graph_width(const jfx_editor_t *editor);
uint32_t jfx_editor_graph_height(const jfx_editor_t *editor);
/* Preview/export a graph node without changing the active mode or output.
 * UINT32_MAX selects the graph's output. Graphs without an output render transparent.
 * Output is untouched on error. Export is binary PPM (RGB, alpha discarded). */
jfx_result_t jfx_editor_render_graph(jfx_editor_t *editor, uint32_t node, double seconds,
    uint32_t width, uint32_t height, uint8_t *out_rgba, size_t capacity);
jfx_result_t jfx_editor_write_graph(jfx_editor_t *editor, uint32_t node, double seconds,
    uint32_t width, uint32_t height, const char *path);
#ifdef __cplusplus
}
#endif
#endif
