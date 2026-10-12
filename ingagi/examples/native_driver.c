#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

extern int64_t ingagi_entry(void);

int main(void) {
    printf("%" PRId64 "\n", ingagi_entry());
    return 0;
}
