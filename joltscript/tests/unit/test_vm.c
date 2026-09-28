/* test_vm.c — Unit tests for the Joltscript VM
 *
 * Tests the VM's execution of JBC1 bytecode.
 */

#include "joltscript/vm.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static void test_vm_create_destroy(void) {
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    jolt_vm_destroy(vm);
    printf("test_vm_create_destroy: PASSED\n");
}

static void test_vm_run_basic(void) {
    /* Hand-crafted bytecode: (defkernel add [x y] (+ x y)) */
    uint8_t code[] = {
        0x4a, 0x42, 0x43, 0x31,  /* magic "JBC1" */
        0x01, 0x00, 0x00, 0x00,  /* version 1 */
        0x02, 0x00, 0x00, 0x00,  /* 2 inputs */
        0x01, 0x00, 0x00, 0x00,  /* 1 output */
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* INPUT 0 */
        0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,  /* INPUT 1 */
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* ADD */
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* OUTPUT 0 */
    };
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    float in[] = {3.0f, 4.0f};
    float out[1] = {0};
    jolt_status_t status = jolt_vm_run(vm, code, sizeof(code), in, 2, out, 1);
    assert(status == JOLT_OK);
    assert(fabsf(out[0] - 7.0f) < 1e-6f);
    jolt_vm_destroy(vm);
    printf("test_vm_run_basic: PASSED\n");
}

static void test_vm_validate(void) {
    /* Valid bytecode */
    uint8_t valid[] = {
        0x4a, 0x42, 0x43, 0x31,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,  /* CONST 1.0 */
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* OUTPUT 0 */
    };
    assert(jolt_bytecode_validate(valid, sizeof(valid)) == JOLT_OK);

    /* Invalid magic */
    uint8_t bad_magic[sizeof(valid)];
    memcpy(bad_magic, valid, sizeof(valid));
    bad_magic[0] = 0;
    assert(jolt_bytecode_validate(bad_magic, sizeof(bad_magic)) == JOLT_ERR_BYTECODE);

    /* Invalid version */
    uint8_t bad_version[sizeof(valid)];
    memcpy(bad_version, valid, sizeof(valid));
    bad_version[4] = 99;
    assert(jolt_bytecode_validate(bad_version, sizeof(bad_version)) == JOLT_ERR_BYTECODE);

    /* Too short */
    assert(jolt_bytecode_validate(valid, 8) == JOLT_ERR_BYTECODE);

    /* Invalid instruction size */
    assert(jolt_bytecode_validate(valid, sizeof(valid) - 1) == JOLT_ERR_BYTECODE);

    printf("test_vm_validate: PASSED\n");
}

static void test_vm_execute(void) {
    /* Hand-crafted bytecode: (defkernel half [x] (* x 0.5)) */
    uint8_t code[] = {
        0x4a, 0x42, 0x43, 0x31,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* INPUT 0 */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f,  /* CONST 0.5 */
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* MUL */
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* OUTPUT 0 */
    };
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    int result = jolt_vm_execute(vm, code, sizeof(code));
    assert(result == JOLT_OK);
    jolt_vm_destroy(vm);
    printf("test_vm_execute: PASSED\n");
}

static void test_vm_error_handling(void) {
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* NULL VM */
    uint8_t code[] = {
        0x4a, 0x42, 0x43, 0x31,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    float out[1] = {0};
    assert(jolt_vm_run(NULL, code, sizeof(code), NULL, 0, out, 1) == JOLT_ERR_ARGUMENT);
    assert(jolt_vm_run(vm, NULL, sizeof(code), NULL, 0, out, 1) == JOLT_ERR_ARGUMENT);
    assert(jolt_vm_run(vm, code, sizeof(code), NULL, 0, NULL, 1) == JOLT_ERR_ARGUMENT);

    /* Invalid input count */
    assert(jolt_vm_run(vm, code, sizeof(code), NULL, 2, out, 1) == JOLT_ERR_ARGUMENT);

    /* Invalid output count */
    assert(jolt_vm_run(vm, code, sizeof(code), NULL, 0, out, 2) == JOLT_ERR_ARGUMENT);

    /* Non-finite input */
    float in[] = {NAN};
    assert(jolt_vm_run(vm, code, sizeof(code), in, 1, out, 1) == JOLT_ERR_NUMERIC);

    jolt_vm_destroy(vm);
    printf("test_vm_error_handling: PASSED\n");
}

static void test_vm_stack_operations(void) {
    /* Test stack overflow protection */
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Create bytecode that pushes too many values */
    uint8_t code[16 + 8 * 200];
    memset(code, 0, sizeof(code));
    code[0] = 0x4a; code[1] = 0x42; code[2] = 0x43; code[3] = 0x31;
    code[4] = 1; code[8] = 0; code[12] = 1;
    for (int i = 0; i < 200; ++i) {
        code[16 + i * 8] = 1;  /* CONST */
        code[16 + i * 8 + 4] = 0x3f;  /* 1.0 */
    }
    code[16 + 200 * 8 - 8] = 15;  /* OUTPUT */
    assert(jolt_vm_run(vm, code, sizeof(code), NULL, 0, NULL, 0) == JOLT_ERR_ARGUMENT);

    jolt_vm_destroy(vm);
    printf("test_vm_stack_operations: PASSED\n");
}

int main(void) {
    test_vm_create_destroy();
    test_vm_run_basic();
    test_vm_validate();
    test_vm_execute();
    test_vm_error_handling();
    test_vm_stack_operations();
    printf("\nAll VM tests passed!\n");
    return 0;
}
