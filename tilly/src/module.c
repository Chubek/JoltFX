#include "tilly/module.h"
#include <stdlib.h>

struct tilly_module {
    char *name;
};

tilly_module_t *tilly_module_load(const char *name) {
    (void)name;
    // TODO: Implement dynamic module loading
    return NULL;
}

void tilly_module_unload(tilly_module_t *module) {
    if (module) {
        free(module);
    }
}
