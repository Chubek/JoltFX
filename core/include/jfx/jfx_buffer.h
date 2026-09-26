#ifndef JFX_BUFFER_H
#define JFX_BUFFER_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_buffer jfx_buffer_t;

typedef enum {
    JFX_BUFFER_USAGE_TRANSFER_SRC = 1 << 0,
    JFX_BUFFER_USAGE_TRANSFER_DST = 1 << 1,
    JFX_BUFFER_USAGE_STORAGE = 1 << 2
} jfx_buffer_usage_flags_t;

jfx_result_t jfx_buffer_create(
    jfx_engine_t *engine,
    size_t size,
    uint32_t usage_flags,
    jfx_buffer_t **out_buffer
);

void jfx_buffer_destroy(jfx_buffer_t *buffer);

jfx_result_t jfx_buffer_write(jfx_buffer_t *buffer, size_t offset, const void *data, size_t size);

jfx_result_t jfx_buffer_read(jfx_buffer_t *buffer, size_t offset, void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // JFX_BUFFER_H
