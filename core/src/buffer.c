/* Engine-owned buffers.
 *
 * A jfx_buffer_t is host-visible storage owned by the engine and accounted
 * against `max_buffers`. Keeping the bytes host-visible is what lets
 * jfx_buffer_write/jfx_buffer_read be plain copies: a backend that later
 * allocates a device-side allocation keeps the same handle and only replaces
 * the copy strategy. */

#include "jfx/jfx_buffer.h"
#include "jfx/jfx_events.h"
#include "jfx/jfx_memory.h"
#include "engine_internal.h"
#include <string.h>

struct jfx_buffer {
    jfx_engine_t *engine;
    uint8_t *data;
    size_t size;
    uint32_t usage_flags;
};

jfx_result_t jfx_buffer_create(jfx_engine_t *engine, size_t size, uint32_t usage_flags,
    jfx_buffer_t **out_buffer) {
    if (!engine || !size || !usage_flags || !out_buffer) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    const uint32_t known = JFX_BUFFER_USAGE_TRANSFER_SRC | JFX_BUFFER_USAGE_TRANSFER_DST |
        JFX_BUFFER_USAGE_STORAGE;
    if (usage_flags & ~known) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!engine_acquire_buffer(engine)) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    jfx_buffer_t *buffer = jfx_heap_alloc(sizeof(*buffer), _Alignof(jfx_buffer_t));
    if (!buffer) {
        engine_release_buffer(engine);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    buffer->data = jfx_heap_alloc(size, 16u);
    if (!buffer->data) {
        jfx_heap_free(buffer);
        engine_release_buffer(engine);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(buffer->data, 0, size);
    buffer->engine = engine;
    buffer->size = size;
    buffer->usage_flags = usage_flags;
    event_publish(JFX_EVENT_RESOURCE_ALLOC, buffer);
    *out_buffer = buffer;
    return JFX_SUCCESS;
}

void jfx_buffer_destroy(jfx_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    jfx_engine_t *engine = buffer->engine;
    event_publish(JFX_EVENT_RESOURCE_FREE, buffer);
    jfx_heap_free(buffer->data);
    jfx_heap_free(buffer);
    engine_release_buffer(engine);
}

jfx_result_t jfx_buffer_write(jfx_buffer_t *buffer, size_t offset, const void *data, size_t size) {
    if (!buffer || !data || !size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memcpy(buffer->data + offset, data, size);
    return JFX_SUCCESS;
}

jfx_result_t jfx_buffer_read(jfx_buffer_t *buffer, size_t offset, void *data, size_t size) {
    if (!buffer || !data || !size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memcpy(data, buffer->data + offset, size);
    return JFX_SUCCESS;
}

size_t jfx_buffer_size(const jfx_buffer_t *buffer) { return buffer ? buffer->size : 0u; }

uint32_t jfx_buffer_usage_flags(const jfx_buffer_t *buffer) {
    return buffer ? buffer->usage_flags : 0u;
}

jfx_engine_t *jfx_buffer_engine(const jfx_buffer_t *buffer) {
    return buffer ? buffer->engine : NULL;
}
