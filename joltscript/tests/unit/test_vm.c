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

static void test_local_validation(void) {
    uint8_t code[] = {
        0x4a,0x42,0x43,0x31, 1,0,0,0, 0,0,0,0, 1,0,0,0,
        1,0,0,0, 0,0,0x80,0x3f,
        30,0,0,0, 0,0,0,0,
        31,0,0,0, 0,0,0,0,
        29,0,0,0, 0,0,0,0,
        15,0,0,0, 0,0,0,0
    };
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_OK);
    code[44] = 1; /* Uninitialized local read. */
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_ERR_BYTECODE);
    code[44] = 0;
    code[28] = JOLT_MAX_LOCALS; /* Invalid store slot. */
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_ERR_BYTECODE);
    code[28] = 0;
    code[36] = 1; /* DROP must have zero operand. */
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_ERR_BYTECODE);
    code[36] = 0;
    code[16] = JOLT_OP_STORE_LOCAL; /* Store from empty stack. */
    memset(code + 20, 0, 4);
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_ERR_BYTECODE);
    code[16] = JOLT_OP_DROP; /* Drop from empty stack. */
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_ERR_BYTECODE);
}

static void write_word(uint8_t *code, size_t pos, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) code[pos+i] = (uint8_t)(word >> (8*i));
}

static void test_branch_validation(void) {
    const uint32_t words[] = {
        JOLT_BYTECODE_MAGIC, 1, 1, 1,
        JOLT_OP_INPUT, 0,                  /* 16 */
        JOLT_OP_BRANCH_IF_ZERO, 64,        /* 24 */
        JOLT_OP_CONST, 0x3f800000,         /* 32 */
        JOLT_OP_STORE_LOCAL, 0,            /* 40 */
        JOLT_OP_DROP, 0,                   /* 48 */
        JOLT_OP_JUMP, 88,                  /* 56 */
        JOLT_OP_CONST, 0x40000000,         /* 64 */
        JOLT_OP_STORE_LOCAL, 0,            /* 72 */
        JOLT_OP_DROP, 0,                   /* 80 */
        JOLT_OP_LOAD_LOCAL, 0,             /* 88 */
        JOLT_OP_OUTPUT, 0                  /* 96 */
    };
    uint8_t code[sizeof(words)], bad[sizeof(words)];
    for (size_t i = 0; i < sizeof(words)/sizeof(*words); ++i) write_word(code, i*4, words[i]);
    assert(jolt_bytecode_validate(code, sizeof(code)) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    for (unsigned i = 0; i < 2; ++i) {
        float in = (float)i, out = 99;
        assert(jolt_vm_run(vm, code, sizeof(code), &in, 1, &out, 1) == JOLT_OK);
        assert(out == (i ? 1.f : 2.f));
    }
    const uint32_t targets[] = {0, 16, 24, 25, 103, 112, UINT32_MAX};
    for (size_t i = 0; i < sizeof(targets)/sizeof(*targets); ++i) {
        memcpy(bad, code, sizeof(code)); write_word(bad, 28, targets[i]);
        assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);
    }
    memcpy(bad, code, sizeof(code));
    write_word(bad, 72, JOLT_OP_NOT); /* local initialized only on true path */
    assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);
    float in = 1, out = 99;
    assert(jolt_vm_run(vm, bad, sizeof(bad), &in, 1, &out, 1) == JOLT_ERR_BYTECODE);
    assert(out == 99); /* even when the actual run would take the initialized path */
    const uint32_t slots[] = {31, 32, 63, 64, 127};
    for (size_t i = 0; i < sizeof(slots)/sizeof(*slots); ++i) {
        memcpy(bad, code, sizeof(code));
        write_word(bad, 44, slots[i]); write_word(bad, 76, slots[i]); write_word(bad, 92, slots[i]);
        assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_OK);
        write_word(bad, 72, JOLT_OP_NOT); write_word(bad, 76, 0);
        assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);
    }
    memcpy(bad, code, sizeof(code));
    write_word(bad, 80, JOLT_OP_NOT); /* unequal stack depths at join */
    assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);
    memcpy(bad, code, sizeof(code));
    write_word(bad, 28, sizeof(code)); /* path exits without output */
    assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);
    memcpy(bad, code, sizeof(code));
    write_word(bad, 16, JOLT_OP_BRANCH_IF_ZERO); write_word(bad, 20, 24);
    assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE); /* underflow */
    memcpy(bad, code, sizeof(code));
    write_word(bad, 24, JOLT_OP_JUMP); /* unreachable then instructions */
    assert(jolt_bytecode_validate(bad, sizeof(bad)) == JOLT_ERR_BYTECODE);

    /* A terminal jump to size is allowed after complete output. */
    uint8_t terminal[40] = {0};
    memcpy(terminal, code, 16);
    write_word(terminal, 16, JOLT_OP_INPUT);
    write_word(terminal, 24, JOLT_OP_OUTPUT);
    write_word(terminal, 32, JOLT_OP_JUMP); write_word(terminal, 36, sizeof(terminal));
    assert(jolt_bytecode_validate(terminal, sizeof(terminal)) == JOLT_OK);
    assert(jolt_vm_run(vm, terminal, sizeof(terminal), &in, 1, &out, 1) == JOLT_OK && out == in);

    const uint32_t output_words[] = {
        JOLT_BYTECODE_MAGIC, 1, 1, 1, JOLT_OP_INPUT, 0,
        JOLT_OP_BRANCH_IF_ZERO, 56, JOLT_OP_CONST, 0x3f800000,
        JOLT_OP_OUTPUT, 0, JOLT_OP_JUMP, 72,
        JOLT_OP_CONST, 0x40000000, JOLT_OP_DROP, 0,
        JOLT_OP_CONST, 0x3f800000, JOLT_OP_OUTPUT, 0
    };
    for (size_t i = 0; i < sizeof(output_words)/sizeof(*output_words); ++i)
        write_word(bad, i*4, output_words[i]);
    assert(jolt_bytecode_validate(bad, sizeof(output_words)) == JOLT_ERR_BYTECODE);
    jolt_vm_destroy(vm);
}

static void run_words(jolt_vm_t *vm, const uint32_t *words, size_t word_count,
                      const float *inputs, size_t input_count,
                      float *outputs, size_t output_count) {
    uint8_t code[512];
    assert(word_count * sizeof(uint32_t) <= sizeof(code));
    for (size_t i = 0; i < word_count; ++i) write_word(code, i * sizeof(uint32_t), words[i]);
    assert(jolt_bytecode_validate(code, word_count * sizeof(uint32_t)) == JOLT_OK);
    assert(jolt_vm_run(vm, code, word_count * sizeof(uint32_t), inputs, input_count,
                       outputs, output_count) == JOLT_OK);
}

static void test_vm_backend_selection(void) {
    assert(jolt_vm_backend_available(JOLT_VM_BACKEND_AUTO));
    assert(jolt_vm_backend_available(JOLT_VM_BACKEND_INTERPRETER));
    assert(strcmp(jolt_vm_backend_name(JOLT_VM_BACKEND_SLJIT), "sljit") == 0);

    jolt_vm_t *interp = jolt_vm_create();
    jolt_vm_t *jit = jolt_vm_create();
    assert(interp && jit);
    assert(jolt_vm_set_backend(interp, JOLT_VM_BACKEND_INTERPRETER) == JOLT_OK);
    assert(jolt_vm_get_backend(interp) == JOLT_VM_BACKEND_INTERPRETER);
    if (!jolt_vm_backend_available(JOLT_VM_BACKEND_SLJIT)) {
        assert(jolt_vm_set_backend(jit, JOLT_VM_BACKEND_SLJIT) == JOLT_ERR_CAPABILITY);
        jolt_vm_destroy(interp);
        jolt_vm_destroy(jit);
        return;
    }
    assert(jolt_vm_set_backend(jit, JOLT_VM_BACKEND_SLJIT) == JOLT_OK);
    assert(jolt_vm_get_backend(jit) == JOLT_VM_BACKEND_SLJIT);

    const uint32_t arithmetic_words[] = {
        JOLT_BYTECODE_MAGIC, 1, 2, 4,
        JOLT_OP_INPUT, 0, JOLT_OP_INPUT, 1, JOLT_OP_ADD, 0, JOLT_OP_OUTPUT, 0,
        JOLT_OP_INPUT, 0, JOLT_OP_ABS, 0, JOLT_OP_CONST, 0x40000000,
        JOLT_OP_POW, 0, JOLT_OP_FLOOR, 0, JOLT_OP_OUTPUT, 1,
        JOLT_OP_INPUT, 0, JOLT_OP_INPUT, 1, JOLT_OP_LT, 0,
        JOLT_OP_CONST, 0x41200000, JOLT_OP_CONST, 0x41a00000,
        JOLT_OP_SELECT, 0, JOLT_OP_OUTPUT, 2,
        JOLT_OP_CONST, 0x40a00000, JOLT_OP_STORE_LOCAL, 0, JOLT_OP_DROP, 0,
        JOLT_OP_LOAD_LOCAL, 0, JOLT_OP_CONST, 0x40400000,
        JOLT_OP_BITWISE_AND, 0, JOLT_OP_OUTPUT, 3
    };
    const float inputs[] = {-3.0f, 5.0f};
    float interp_out[4] = {0}, jit_out[4] = {0};
    run_words(interp, arithmetic_words, sizeof(arithmetic_words)/sizeof(*arithmetic_words),
              inputs, 2, interp_out, 4);
    run_words(jit, arithmetic_words, sizeof(arithmetic_words)/sizeof(*arithmetic_words),
              inputs, 2, jit_out, 4);
    for (size_t i = 0; i < 4; ++i) assert(fabsf(interp_out[i] - jit_out[i]) < 1e-6f);
    assert(jit_out[0] == 2.0f && jit_out[1] == 9.0f && jit_out[2] == 10.0f && jit_out[3] == 1.0f);

    const uint32_t branch_words[] = {
        JOLT_BYTECODE_MAGIC, 1, 1, 1,
        JOLT_OP_INPUT, 0,
        JOLT_OP_BRANCH_IF_ZERO, 64,
        JOLT_OP_CONST, 0x3f800000,
        JOLT_OP_STORE_LOCAL, 0,
        JOLT_OP_DROP, 0,
        JOLT_OP_JUMP, 88,
        JOLT_OP_CONST, 0x40000000,
        JOLT_OP_STORE_LOCAL, 0,
        JOLT_OP_DROP, 0,
        JOLT_OP_LOAD_LOCAL, 0,
        JOLT_OP_OUTPUT, 0
    };
    for (unsigned i = 0; i < 2; ++i) {
        float in = (float)i;
        float a = 0.0f, b = 0.0f;
        run_words(interp, branch_words, sizeof(branch_words)/sizeof(*branch_words), &in, 1, &a, 1);
        run_words(jit, branch_words, sizeof(branch_words)/sizeof(*branch_words), &in, 1, &b, 1);
        assert(a == b && b == (i ? 1.0f : 2.0f));
    }

    const uint32_t div_zero_words[] = {
        JOLT_BYTECODE_MAGIC, 1, 1, 1,
        JOLT_OP_INPUT, 0, JOLT_OP_CONST, 0,
        JOLT_OP_DIV, 0, JOLT_OP_OUTPUT, 0
    };
    uint8_t div_zero_code[sizeof(div_zero_words)];
    for (size_t i = 0; i < sizeof(div_zero_words)/sizeof(*div_zero_words); ++i)
        write_word(div_zero_code, i * sizeof(uint32_t), div_zero_words[i]);
    float one = 1.0f, unchanged = 42.0f;
    assert(jolt_vm_run(jit, div_zero_code, sizeof(div_zero_code), &one, 1, &unchanged, 1) == JOLT_ERR_NUMERIC);
    assert(unchanged == 42.0f);

    jolt_vm_destroy(interp);
    jolt_vm_destroy(jit);
    printf("test_vm_backend_selection: PASSED\n");
}

int main(void) {
    test_branch_validation();
    test_local_validation();
    test_vm_backend_selection();
    test_vm_create_destroy();
    test_vm_run_basic();
    test_vm_validate();
    test_vm_execute();
    test_vm_error_handling();
    test_vm_stack_operations();
    printf("\nAll VM tests passed!\n");
    return 0;
}
