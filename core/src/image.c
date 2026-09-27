/* Still-image decoding, via the vendored stb_image.
 *
 * stb_image is single-header with implementation guarded by a macro, so the
 * implementation lives in this translation unit alone and every other file just
 * includes the header. */

#include "jfx/jfx_image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WRITE
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_ONLY_PSD
#define STBI_ONLY_HDR
#define STBI_ONLY_PIC
#define STBI_ONLY_PNM
#include "stb_image.h"

#include <stdlib.h>
#include <string.h>

#include "tilly/allocator.h"

bool jfx_image_codec_available(void) { return true; }

static jfx_result_t adopt(stbi_uc *pixels, int width, int height, int channels,
    const jfx_image_load_options_t *options, jfx_image_t *out_image) {
    if (width <= 0 || height <= 0 || (width > 0 && height > 0 &&
            (size_t)width > SIZE_MAX / (size_t)height / 4u)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    out_image->width = (uint32_t)width;
    out_image->height = (uint32_t)height;
    out_image->channels = (uint32_t)channels;
    out_image->pixels = (uint8_t *)pixels;
    if (options && options->flip_vertically) {
        const size_t stride = (size_t)width * (size_t)channels;
        uint8_t *scratch = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), stride,
            _Alignof(max_align_t));
        if (!scratch) {
            return JFX_ERROR_OUT_OF_MEMORY;
        }
        for (int y = 0; y < height / 2; ++y) {
            uint8_t *top = out_image->pixels + (size_t)y * stride;
            uint8_t *bottom = out_image->pixels + (size_t)(height - 1 - y) * stride;
            memcpy(scratch, top, stride);
            memcpy(top, bottom, stride);
            memcpy(bottom, scratch, stride);
        }
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), scratch);
    }
    (void)options;
    return JFX_SUCCESS;
}

jfx_result_t jfx_image_load(const char *path, const jfx_image_load_options_t *options,
    jfx_image_t *out_image) {
    if (!path || !out_image || out_image->size < sizeof(*out_image)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        return JFX_ERROR_NOT_FOUND;
    }
    jfx_result_t status = adopt(pixels, width, height, 4, options, out_image);
    if (status != JFX_SUCCESS) {
        stbi_image_free(pixels);
    }
    return status;
}

jfx_result_t jfx_image_load_from_memory(const void *data, size_t size, const char *hint,
    const jfx_image_load_options_t *options, jfx_image_t *out_image) {
    if (!data || !size || !out_image || out_image->size < sizeof(*out_image)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)data, (int)size, &width, &height,
        &channels, 4);
    if (!pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_result_t status = adopt(pixels, width, height, 4, options, out_image);
    if (status != JFX_SUCCESS) {
        stbi_image_free(pixels);
    }
    (void)hint;
    return status;
}

void jfx_image_release(jfx_image_t *image) {
    if (!image) {
        return;
    }
    stbi_image_free(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
    image->channels = 0;
}
