/* Engine-owned textures.
 *
 * A jfx_texture_t is a host-visible 2D image accounted against `max_textures`.
 * Storage is tightly packed rows of `jfx_format_bytes_per_texel(format)` bytes.
 * The rgba8 accessors convert between that storage and the 8-bit RGBA form
 * frontends, mobile surfaces and the web bridge all speak. */

#include "jfx/jfx_texture.h"
#include "jfx/jfx_events.h"
#include "jfx/jfx_memory.h"
#include "engine_internal.h"
#include <math.h>
#include <string.h>

struct jfx_texture {
    jfx_engine_t *engine;
    uint32_t width;
    uint32_t height;
    jfx_format_t format;
    size_t stride;
    size_t size;
    void *data;
};

size_t jfx_format_bytes_per_texel(jfx_format_t format) {
    switch (format) {
    case JFX_FORMAT_R8G8B8A8_UNORM:
        return 4u;
    case JFX_FORMAT_R32G32B32A32_SFLOAT:
        return 16u;
    case JFX_FORMAT_R16G16B16A16_SFLOAT:
        return 8u;
    case JFX_FORMAT_COUNT:
    default:
        return 0u;
    }
}

const char *jfx_format_name(jfx_format_t format) {
    switch (format) {
    case JFX_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case JFX_FORMAT_R32G32B32A32_SFLOAT:
        return "R32G32B32A32_SFLOAT";
    case JFX_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    case JFX_FORMAT_COUNT:
    default:
        return "unknown";
    }
}

static uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) return 0u; /* also catches NaN */
    if (value >= 1.0f) return 255u;
    return (uint8_t)lrintf(value * 255.0f);
}

static float from_unorm8(uint8_t value) {
    return (float)value / 255.0f;
}

/* IEEE-754 binary16 conversion, used by JFX_FORMAT_R16G16B16A16_SFLOAT.
 * Values outside binary16's range saturate rather than becoming infinity,
 * which keeps a 16F texture round-tripping RGBA8 data without NaNs. */
static uint16_t float_to_half(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (((bits >> 23) & 0xFFu) == 0xFFu) {
        /* Inf or NaN. Preserve NaN-ness, saturate the exponent. */
        return (uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    }
    if (exponent >= 0x1F) {
        return (uint16_t)(sign | 0x7C00u);
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return (uint16_t)sign; /* underflows to signed zero */
        }
        mantissa |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exponent);
        return (uint16_t)(sign | (mantissa >> shift));
    }
    /* Round to nearest even on the 13 discarded mantissa bits. */
    uint32_t rounded = mantissa + 0x0FFFu + ((mantissa >> 13) & 1u);
    if (rounded & 0x800000u) {
        rounded = 0u;
        if (++exponent >= 0x1F) {
            return (uint16_t)(sign | 0x7C00u);
        }
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (rounded >> 13));
}

static float half_to_float(uint16_t value) {
    uint32_t sign = (uint32_t)(value & 0x8000u) << 16;
    uint32_t exponent = (uint32_t)(value >> 10) & 0x1Fu;
    uint32_t mantissa = (uint32_t)value & 0x3FFu;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            /* Subnormal: renormalize into a binary32 normal. */
            int32_t shift = 0;
            while (!(mantissa & 0x400u)) {
                mantissa <<= 1;
                shift++;
            }
            mantissa &= 0x3FFu;
            bits = sign | ((uint32_t)(127 - 15 - shift) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

jfx_result_t jfx_texture_create(jfx_engine_t *engine, uint32_t width, uint32_t height,
    jfx_format_t format, jfx_texture_t **out_texture) {
    if (!engine || !width || !height || !out_texture) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_texture = NULL;
    size_t texel = jfx_format_bytes_per_texel(format);
    if (!texel) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Reject dimensions whose row or total size would overflow size_t. */
    if ((size_t)width > SIZE_MAX / texel) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    size_t stride = (size_t)width * texel;
    if ((size_t)height > SIZE_MAX / stride) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    size_t size = stride * (size_t)height;
    if (!engine_acquire_texture(engine)) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    jfx_texture_t *texture = jfx_heap_alloc(sizeof(*texture), _Alignof(jfx_texture_t));
    if (!texture) {
        engine_release_texture(engine);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    /* 16-byte alignment keeps float and half-float rows naturally aligned. */
    texture->data = jfx_heap_alloc(size, 16u);
    if (!texture->data) {
        jfx_heap_free(texture);
        engine_release_texture(engine);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(texture->data, 0, size);
    texture->engine = engine;
    texture->width = width;
    texture->height = height;
    texture->format = format;
    texture->stride = stride;
    texture->size = size;
    event_publish(JFX_EVENT_RESOURCE_ALLOC, texture);
    *out_texture = texture;
    return JFX_SUCCESS;
}

void jfx_texture_destroy(jfx_texture_t *texture) {
    if (!texture) {
        return;
    }
    jfx_engine_t *engine = texture->engine;
    event_publish(JFX_EVENT_RESOURCE_FREE, texture);
    jfx_heap_free(texture->data);
    jfx_heap_free(texture);
    engine_release_texture(engine);
}

/* Bounds-checks a byte range against the texture's storage, rejecting ranges
 * that straddle a row boundary so callers cannot corrupt row padding. */
static jfx_result_t check_span(const jfx_texture_t *texture, size_t offset, size_t size) {
    if (!texture || !size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (offset > texture->size || size > texture->size - offset) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (size % texture->stride) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (offset % texture->stride) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_texture_write(jfx_texture_t *texture, size_t offset, const void *data,
    size_t size) {
    if (!data) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_result_t status = check_span(texture, offset, size);
    if (status != JFX_SUCCESS) {
        return status;
    }
    memcpy((uint8_t *)texture->data + offset, data, size);
    return JFX_SUCCESS;
}

jfx_result_t jfx_texture_read(jfx_texture_t *texture, size_t offset, void *data, size_t size) {
    if (!data) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_result_t status = check_span(texture, offset, size);
    if (status != JFX_SUCCESS) {
        return status;
    }
    memcpy(data, (const uint8_t *)texture->data + offset, size);
    return JFX_SUCCESS;
}

jfx_result_t jfx_texture_write_rgba8(jfx_texture_t *texture, const uint8_t *rgba, size_t size) {
    if (!texture || !rgba) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t pixels = (size_t)texture->width * (size_t)texture->height;
    if (pixels > SIZE_MAX / 4u || size != pixels * 4u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    uint8_t *dst = (uint8_t *)texture->data;
    for (size_t y = 0; y < texture->height; ++y) {
        uint8_t *row = dst + y * texture->stride;
        const uint8_t *src = rgba + y * (size_t)texture->width * 4u;
        for (uint32_t x = 0; x < texture->width; ++x) {
            const uint8_t *texel = src + (size_t)x * 4u;
            switch (texture->format) {
            case JFX_FORMAT_R8G8B8A8_UNORM:
                memcpy(row + (size_t)x * 4u, texel, 4u);
                break;
            case JFX_FORMAT_R32G32B32A32_SFLOAT: {
                float *channels = (float *)(void *)row + (size_t)x * 4u;
                for (int c = 0; c < 4; ++c) {
                    channels[c] = from_unorm8(texel[c]);
                }
                break;
            }
            case JFX_FORMAT_R16G16B16A16_SFLOAT: {
                uint16_t *channels = (uint16_t *)(void *)row + (size_t)x * 4u;
                for (int c = 0; c < 4; ++c) {
                    channels[c] = float_to_half(from_unorm8(texel[c]));
                }
                break;
            }
            case JFX_FORMAT_COUNT:
            default:
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_texture_read_rgba8(jfx_texture_t *texture, uint8_t *rgba, size_t size) {
    if (!texture || !rgba) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t pixels = (size_t)texture->width * (size_t)texture->height;
    if (pixels > SIZE_MAX / 4u || size != pixels * 4u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t *src = (const uint8_t *)texture->data;
    for (size_t y = 0; y < texture->height; ++y) {
        const uint8_t *row = src + y * texture->stride;
        uint8_t *dst = rgba + y * (size_t)texture->width * 4u;
        for (uint32_t x = 0; x < texture->width; ++x) {
            uint8_t *texel = dst + (size_t)x * 4u;
            switch (texture->format) {
            case JFX_FORMAT_R8G8B8A8_UNORM:
                memcpy(texel, row + (size_t)x * 4u, 4u);
                break;
            case JFX_FORMAT_R32G32B32A32_SFLOAT: {
                const float *channels = (const float *)(const void *)row + (size_t)x * 4u;
                for (int c = 0; c < 4; ++c) {
                    texel[c] = to_unorm8(channels[c]);
                }
                break;
            }
            case JFX_FORMAT_R16G16B16A16_SFLOAT: {
                const uint16_t *channels = (const uint16_t *)(const void *)row + (size_t)x * 4u;
                for (int c = 0; c < 4; ++c) {
                    texel[c] = to_unorm8(half_to_float(channels[c]));
                }
                break;
            }
            case JFX_FORMAT_COUNT:
            default:
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    return JFX_SUCCESS;
}

void *jfx_texture_data(jfx_texture_t *texture) { return texture ? texture->data : NULL; }

size_t jfx_texture_data_size(const jfx_texture_t *texture) { return texture ? texture->size : 0u; }

uint32_t jfx_texture_width(const jfx_texture_t *texture) { return texture ? texture->width : 0u; }

uint32_t jfx_texture_height(const jfx_texture_t *texture) { return texture ? texture->height : 0u; }

jfx_format_t jfx_texture_format(const jfx_texture_t *texture) {
    return texture ? texture->format : JFX_FORMAT_COUNT;
}

jfx_engine_t *jfx_texture_engine(const jfx_texture_t *texture) {
    return texture ? texture->engine : NULL;
}
