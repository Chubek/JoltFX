#include "tillyz/tillyz.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    tillyz_context_t *ctx = tillyz_init();
    assert(ctx != NULL);

    tillyz_shutdown(ctx);

    printf("TillyZ tests passed\n");
    return 0;
}
