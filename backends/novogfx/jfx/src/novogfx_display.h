#ifndef JFX_NOVOGFX_DISPLAY_H
#define JFX_NOVOGFX_DISPLAY_H

/* Private desktop scanout bridge. All calls on the owning UI thread.
 * This is display output, not GPU shader execution. No native types escape. */
#include <stddef.h>
#include <stdint.h>
#include "jfx/jfx_result.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jfx_novogfx_display jfx_novogfx_display_t;
jfx_result_t jfx_novogfx_display_create(const char *path,
    jfx_novogfx_display_t **out_display, char *error, size_t error_size);
void jfx_novogfx_display_destroy(jfx_novogfx_display_t *display);
uint32_t jfx_novogfx_display_width(const jfx_novogfx_display_t *display);
uint32_t jfx_novogfx_display_height(const jfx_novogfx_display_t *display);
/* Nonblocking: BUSY means a prior flip is still in flight, never reuse it. */
jfx_result_t jfx_novogfx_display_present(jfx_novogfx_display_t *display,
    const uint8_t *rgba, size_t pitch);
#ifdef __cplusplus
}
#endif
#endif
