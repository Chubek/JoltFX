#ifndef JFX_EDITOR_H
#define JFX_EDITOR_H
#include "jfx_project.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Shared editing session. Single-owner-thread API; borrowed model handles remain
 * valid until a successful load/reset or destruction. Edits use the timeline and
 * graph APIs, so every interface uses the same validators and evaluator. */
#define JFX_EDITOR_API_MAJOR 1
#define JFX_EDITOR_API_MINOR 0
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
/* FFI edits: op is documented in docs/editor.md. Indices are zero-based.
 * Unknown commands and invalid numeric values return INVALID_ARGUMENT. */
jfx_result_t jfx_editor_command(jfx_editor_t *editor, const char *op,
    uint32_t a, uint32_t b, uint32_t c, double value, const char *text);
#ifdef __cplusplus
}
#endif
#endif
