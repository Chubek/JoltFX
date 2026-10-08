#include "jfx/jfx_image.h"
#include "tilly/memory.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

int main(void) {
    const unsigned char ppm[] = "P6\n1 2\n255\n\1\2\3\4\5\6";
    tilly_allocator_t *heap = (tilly_allocator_t *)tilly_default_allocator();
    size_t baseline = tilly_allocator_usage(heap);
    jfx_image_t image = {.size = sizeof(image)};
    jfx_image_load_options_t options = {.flip_vertically = true};
    assert(jfx_image_load_from_memory(ppm, sizeof(ppm)-1, NULL, &options, &image) == JFX_SUCCESS);
    assert(image.width == 1 && image.height == 2 && image.channels == 4);
    assert(image.pixels[0] == 4 && image.pixels[4] == 1);
    assert(tilly_allocator_usage(heap) >= baseline + 8);
    jfx_image_release(&image); jfx_image_release(&image);
    assert(!image.pixels && tilly_allocator_usage(heap) == baseline);
    // stb uses signed int input lengths: reject truncation before any read.
    assert(jfx_image_load_from_memory(ppm, (size_t)INT_MAX+1, NULL, NULL, &image) == JFX_ERROR_INVALID_ARGUMENT);
    assert(!image.pixels && tilly_allocator_usage(heap) == baseline);
    assert(jfx_image_load_from_memory("bad", 3, NULL, NULL, &image) == JFX_ERROR_INVALID_ARGUMENT);
    assert(!image.pixels && tilly_allocator_usage(heap) == baseline);
    // Engine render output and decoder output have the same ownership contract.
    image.pixels = tilly_mem_alloc(8); assert(image.pixels);
    jfx_image_release(&image);
    assert(tilly_allocator_usage(heap) == baseline);
    return 0;
}
