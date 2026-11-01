#include <joltfx/sdk/ingagi.h>
#include <stdio.h>

int main(void)
{
    static const char source[] = "fn twice(n: i64) -> i64 { return n * 2; }";
    char error[4096] = {0};
    ingagi_handle vm = 0;
    ingagi_value argument = {INGAGI_INT, 0, 21, 0};
    ingagi_value result = {0};
    if (ingagi_abi_version() != INGAGI_ABI_VERSION) {
        fputs("Ingagi ABI mismatch\n", stderr);
        return 1;
    }
    ingagi_status status = ingagi_create(100000, &vm, error, sizeof(error));
    if (status == INGAGI_OK)
        status = ingagi_load(vm, source, sizeof(source) - 1, error, sizeof(error));
    if (status == INGAGI_OK)
        status = ingagi_call(vm, "twice", 5, &argument, 1, &result, error, sizeof(error));
    int failed = status != INGAGI_OK || result.kind != INGAGI_INT || result.integer != 42;
    if (failed) fprintf(stderr, "Ingagi example failed (%d): %s\n", (int)status, error);
    if (vm && ingagi_destroy(vm, error, sizeof(error)) != INGAGI_OK) {
        fprintf(stderr, "Ingagi destruction failed: %s\n", error);
        failed = 1;
    }
    if (!failed) puts("Ingagi: 42");
    return failed;
}
