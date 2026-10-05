#ifndef JFX_BUFFER_H
#define JFX_BUFFER_H

#include <stddef.h>
#include <stdint.h>

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

/* Allocates an engine-owned buffer of `size` bytes. The buffer holds
 * host-visible storage, so write/read are plain copies and no staging is
 * involved. usage_flags must name at least one usage. */
jfx_result_t jfx_buffer_create(
    jfx_engine_t *engine,
    size_t size,
    uint32_t usage_flags,
    jfx_buffer_t **out_buffer
);

void jfx_buffer_destroy(jfx_buffer_t *buffer);

jfx_result_t jfx_buffer_write(jfx_buffer_t *buffer, size_t offset, const void *data, size_t size);

jfx_result_t jfx_buffer_read(jfx_buffer_t *buffer, size_t offset, void *data, size_t size);

size_t jfx_buffer_size(const jfx_buffer_t *buffer);
uint32_t jfx_buffer_usage_flags(const jfx_buffer_t *buffer);
/* The engine that owns this buffer, or NULL for a foreign handle. */
jfx_engine_t *jfx_buffer_engine(const jfx_buffer_t *buffer);

#ifdef __cplusplus
}
#endif

#endif // JFX_BUFFER_H
