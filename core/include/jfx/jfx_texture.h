#ifndef JFX_TEXTURE_H
#define JFX_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_texture jfx_texture_t;

typedef enum {
    JFX_FORMAT_R8G8B8A8_UNORM,
    JFX_FORMAT_R32G32B32A32_SFLOAT,
    JFX_FORMAT_R16G16B16A16_SFLOAT,
    JFX_FORMAT_COUNT
} jfx_format_t;

/* Bytes occupied by one texel of `format`. Returns 0 for an out-of-range enum. */
size_t jfx_format_bytes_per_texel(jfx_format_t format);

/* Human-readable name for `format`, or "unknown". */
const char *jfx_format_name(jfx_format_t format);

/* Allocates an engine-owned 2D texture. Storage is host-visible and zeroed. */
jfx_result_t jfx_texture_create(
    jfx_engine_t *engine,
    uint32_t width,
    uint32_t height,
    jfx_format_t format,
    jfx_texture_t **out_texture
);

void jfx_texture_destroy(jfx_texture_t *texture);

/* Copies `size` bytes of texel data into the texture. `size` must be a whole
 * number of rows, and offset/stride must stay inside the texture. */
jfx_result_t jfx_texture_write(jfx_texture_t *texture, size_t offset, const void *data,
    size_t size);

jfx_result_t jfx_texture_read(jfx_texture_t *texture, size_t offset, void *data, size_t size);

/* Copies tightly packed RGBA8 pixels (width*height*4 bytes) in, converting from
 * the texture's storage format. */
jfx_result_t jfx_texture_write_rgba8(jfx_texture_t *texture, const uint8_t *rgba,
    size_t size);
jfx_result_t jfx_texture_read_rgba8(jfx_texture_t *texture, uint8_t *rgba, size_t size);

/* Direct access to the backing storage. The pointer is invalidated by destroy. */
void *jfx_texture_data(jfx_texture_t *texture);
size_t jfx_texture_data_size(const jfx_texture_t *texture);

uint32_t jfx_texture_width(const jfx_texture_t *texture);
uint32_t jfx_texture_height(const jfx_texture_t *texture);
jfx_format_t jfx_texture_format(const jfx_texture_t *texture);
jfx_engine_t *jfx_texture_engine(const jfx_texture_t *texture);

#ifdef __cplusplus
}
#endif

#endif // JFX_TEXTURE_H
