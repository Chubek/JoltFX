#include <joltfx/sdk/joltscript.h>
#include <stdio.h>

int main(void)
{
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = {0};
    diagnostic.size = sizeof(diagnostic);
    jolt_status_t status = jolt_compile("(defkernel answer [] (+ 40i64 2i64))",
        &program, &diagnostic);
    if (status != JOLT_OK) {
        fprintf(stderr, "%zu:%zu: %s\n", diagnostic.line, diagnostic.column, diagnostic.message);
        jolt_program_destroy(program);
        return 1;
    }
    jolt_vm_t *vm = jolt_vm_create();
    if (!vm) {
        fputs("Cannot create Joltscript VM\n", stderr);
        jolt_program_destroy(program);
        return 1;
    }
    jolt_value_t result = {0};
    jolt_run_options_t options = {sizeof(options), 100000};
    status = jolt_program_run_values(vm, program, NULL, 0, &result, 1, &options);
    int failed = status != JOLT_OK || result.type != JOLT_TYPE_I64 || result.as.integer != 42;
    if (failed) fprintf(stderr, "Joltscript example failed (%d)\n", (int)status);
    else puts("Joltscript: 42");
    jolt_vm_destroy(vm);
    jolt_program_destroy(program);
    return failed;
}
