#ifndef JOLTSCRIPT_VM_H
#define JOLTSCRIPT_VM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jolt_vm jolt_vm_t;

jolt_vm_t *jolt_vm_create(void);
void jolt_vm_destroy(jolt_vm_t *vm);

int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *bytecode, size_t size);

#ifdef __cplusplus
}
#endif

#endif // JOLTSCRIPT_VM_H
