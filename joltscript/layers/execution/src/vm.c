#include "joltscript/vm.h"
#include <stdlib.h>

struct jolt_vm {
    int dummy;
};

jolt_vm_t *jolt_vm_create(void) {
    return calloc(1, sizeof(jolt_vm_t));
}

void jolt_vm_destroy(jolt_vm_t *vm) {
    free(vm);
}

int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *bytecode, size_t size) {
    (void)vm;
    (void)bytecode;
    (void)size;
    // TODO: Implement bytecode execution
    return 0;
}
