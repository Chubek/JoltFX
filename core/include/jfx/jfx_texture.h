#ifndef JFX_TEXTURE_H
#define JFX_TEXTURE_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_texture jfx_texture_t;

typedef enum {
    JFX_FORMAT_R8G8B8A8_UNORM,
    JFX_FORMAT_R32G32B32A32_SFLOAT,
    JFX_FORMAT_R16G16B16A16_SFLOAT
} jfx_format_t;

jfx_result_t jfx_texture_create(
    jfx_engine_t *engine,
    uint32_t width,
    uint32_t height,
    jfx_format_t format,
    jfx_texture_t **out_texture
);

void jfx_texture_destroy(jfx_texture_t *texture);

#ifdef __cplusplus
}
#endif

#endif // JFX_TEXTURE_H
