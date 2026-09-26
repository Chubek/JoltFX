#ifndef TILLY_ALLOCATOR_H
#define TILLY_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_allocator {
    void *(*alloc)(size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);
    void *user_data;
} tilly_allocator_t;

// Get the default system allocator
const tilly_allocator_t *tilly_default_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // TILLY_ALLOCATOR_H
