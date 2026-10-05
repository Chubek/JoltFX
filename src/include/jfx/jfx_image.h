#ifndef JFX_IMAGE_H
#define JFX_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded images.
 *
 * The NLE needs still images as clip sources and the Hald CLUT reader needs to
 * decode an image, so both go through one loader. Decoding is through the
 * vendored stb_image, which keeps the engine free of a platform image library
 * and behaves identically on every target including WASM. */

typedef struct {
    size_t size;                 /* set to sizeof(jfx_image_t) */
    uint32_t width;
    uint32_t height;
    uint32_t channels;           /* always 4 after load */
    uint8_t *pixels;             /* tightly packed RGBA8, owned */
} jfx_image_t;

typedef struct {
    bool flip_vertically;        /* some formats store the first row last */
    /* sRGB-encoded inputs are commonly left encoded; decoding to linear is the
     * caller's choice, so this only records the intent. */
    bool assume_srgb;
} jfx_image_load_options_t;

/* Loads a still image (PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC, PNM) into RGBA8.
 * `out_image->size` must be set before the call. */
jfx_result_t jfx_image_load(const char *path, const jfx_image_load_options_t *options,
    jfx_image_t *out_image);

/* Same, from memory. `hint` may name the format to skip probing. */
jfx_result_t jfx_image_load_from_memory(const void *data, size_t size, const char *hint,
    const jfx_image_load_options_t *options, jfx_image_t *out_image);

void jfx_image_release(jfx_image_t *image);

/* True when this build has an image decoder compiled in. */
bool jfx_image_codec_available(void);

#ifdef __cplusplus
}
#endif

#endif /* JFX_IMAGE_H */
