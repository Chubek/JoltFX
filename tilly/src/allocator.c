#include "tilly/allocator.h"
#include <stdlib.h>

static void *default_alloc(size_t size, void *user_data) {
    (void)user_data;
    return malloc(size);
}

static void default_free(void *ptr, void *user_data) {
    (void)user_data;
    free(ptr);
}

static tilly_allocator_t default_allocator = {
    .alloc = default_alloc,
    .free = default_free,
    .user_data = NULL
};

const tilly_allocator_t *tilly_default_allocator(void) {
    return &default_allocator;
}
