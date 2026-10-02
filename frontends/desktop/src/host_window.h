#ifndef JFX_DESKTOP_HOST_WINDOW_H
#define JFX_DESKTOP_HOST_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host window for the desktop frontend.
 *
 * Owns everything platform-specific: the OS window, the OpenGL context, the
 * ImGui platform and renderer backends, the input queue and the preview
 * texture. The frontend never includes an SDL or GL header, so it builds and
 * runs (headless) on hosts with no SDL2 and no display server. */

typedef struct jfx_desktop_window jfx_desktop_window_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    const char *title;
    bool resizable;
} jfx_desktop_window_config_t;

/* True when this build has a windowing backend compiled in. */
bool jfx_desktop_window_available(void);

/* Name of the windowing backend ("sdl2", or "none" when compiled without it). */
const char *jfx_desktop_window_backend_name(void);

/* Creates the window and initializes SDL, OpenGL and the ImGui backends.
 * Returns NULL and writes a human-readable reason to out_error (static
 * storage, valid until the next call) when no display or context is available. */
jfx_desktop_window_t *jfx_desktop_window_create(const jfx_desktop_window_config_t *config,
    char *out_error, size_t out_error_size);

void jfx_desktop_window_destroy(jfx_desktop_window_t *window);
bool jfx_desktop_window_queue_audio(jfx_desktop_window_t *window,const float *stereo,uint32_t frames);
uint32_t jfx_desktop_window_queued_audio(jfx_desktop_window_t *window);
void jfx_desktop_window_clear_audio(jfx_desktop_window_t *window);

/* Pumps OS events, feeds them to ImGui and opens a new ImGui frame. Returns
 * false once the user has asked to close the window. */
bool jfx_desktop_window_begin_frame(jfx_desktop_window_t *window);

/* Rasterizes the current ImGui draw data and presents. */
void jfx_desktop_window_end_frame(jfx_desktop_window_t *window);

/* Drawable size in pixels, which differs from the window size on HiDPI. */
uint32_t jfx_desktop_window_drawable_width(const jfx_desktop_window_t *window);
uint32_t jfx_desktop_window_drawable_height(const jfx_desktop_window_t *window);

/* Uploads (or re-uploads) tightly packed RGBA8 pixels and returns an opaque
 * texture handle for ImGui::Image, or NULL when there is no window. The
 * handle stays valid until the next upload or until the window is destroyed. */
void *jfx_desktop_window_upload_rgba8(jfx_desktop_window_t *window, uint32_t width,
    uint32_t height, const uint8_t *rgba);

#endif /* JFX_DESKTOP_HOST_WINDOW_H */
