/* Keep the vendored meter on the engine heap without modifying upstream. */
#include <stdlib.h>
#include "tilly/memory.h"
#define malloc tilly_mem_alloc
#define calloc tilly_mem_calloc
#define realloc tilly_mem_realloc
#define free tilly_mem_free
#include "ebur128.c"
