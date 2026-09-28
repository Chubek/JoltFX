/* test_compiler.c — Unit tests for the Joltscript compiler
 *
 * Tests the compiler's handling of:
 * - Basic arithmetic operations
 * - Comparison operations
 * - Logical operations
 * - Bitwise operations
 * - Error handling
 * - Edge cases
 */

#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static void test_basic_arithmetic(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Addition */
    assert(jolt_compile("(defkernel add [x y] (+ x y))", &p, &d) == JOLT_OK);
    float in1[] = {3.0f, 4.0f};
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 7.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Subtraction */
    assert(jolt_compile("(defkernel sub [x y] (- x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - (-1.0f)) < 1e-6f);
    jolt_program_destroy(p);

    /* Multiplication */
    assert(jolt_compile("(defkernel mul [x y] (* x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 12.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Division */
    assert(jolt_compile("(defkernel div [x y] (/ x y))", &p, &d) == JOLT_OK);
    float in2[] = {12.0f, 4.0f};
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 3.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Min/Max */
    assert(jolt_compile("(defkernel min [x y] (min x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 3.0f) < 1e-6f);
    jolt_program_destroy(p);

    assert(jolt_compile("(defkernel max [x y] (max x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 4.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Abs */
    assert(jolt_compile("(defkernel abs [x] (abs x))", &p, &d) == JOLT_OK);
    float in3[] = {-5.0f};
    assert(jolt_program_run(vm, p, in3, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 5.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Floor */
    assert(jolt_compile("(defkernel floor [x] (floor x))", &p, &d) == JOLT_OK);
    float in4[] = {3.7f};
    assert(jolt_program_run(vm, p, in4, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 3.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Pow */
    assert(jolt_compile("(defkernel pow [x y] (pow x y))", &p, &d) == JOLT_OK);
    float in5[] = {2.0f, 3.0f};
    assert(jolt_program_run(vm, p, in5, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 8.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Sqrt */
    assert(jolt_compile("(defkernel sqrt [x] (sqrt x))", &p, &d) == JOLT_OK);
    float in6[] = {16.0f};
    assert(jolt_program_run(vm, p, in6, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 4.0f) < 1e-6f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_basic_arithmetic: PASSED\n");
}

static void test_comparison_operations(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Less than */
    assert(jolt_compile("(defkernel lt [x y] (< x y))", &p, &d) == JOLT_OK);
    float in1[] = {3.0f, 4.0f};
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    /* Greater than */
    assert(jolt_compile("(defkernel gt [x y] (> x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    /* Less than or equal */
    assert(jolt_compile("(defkernel le [x y] (<= x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    /* Greater than or equal */
    assert(jolt_compile("(defkernel ge [x y] (>= x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    /* Equal */
    assert(jolt_compile("(defkernel eq [x y] (= x y))", &p, &d) == JOLT_OK);
    float in2[] = {4.0f, 4.0f};
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    /* Not equal */
    assert(jolt_compile("(defkernel ne [x y] (!= x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_comparison_operations: PASSED\n");
}

static void test_logical_operations(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* And */
    assert(jolt_compile("(defkernel and [x y] (and x y))", &p, &d) == JOLT_OK);
    float in1[] = {1.0f, 1.0f};
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    float in2[] = {1.0f, 0.0f};
    assert(jolt_compile("(defkernel and [x y] (and x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    /* Or */
    assert(jolt_compile("(defkernel or [x y] (or x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    float in3[] = {0.0f, 0.0f};
    assert(jolt_compile("(defkernel or [x y] (or x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in3, 2, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    /* Not */
    assert(jolt_compile("(defkernel not [x] (not x))", &p, &d) == JOLT_OK);
    float in4[] = {0.0f};
    assert(jolt_program_run(vm, p, in4, 1, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    float in5[] = {1.0f};
    assert(jolt_compile("(defkernel not [x] (not x))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in5, 1, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_logical_operations: PASSED\n");
}

static void test_bitwise_operations(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Bitwise AND */
    assert(jolt_compile("(defkernel band [x y] (bitwise-and x y))", &p, &d) == JOLT_OK);
    float in1[] = {255.0f, 15.0f};
    assert(jolt_program_run(vm, p, in1, 2, out, 1) == JOLT_OK);
    assert(out[0] == 15.0f);
    jolt_program_destroy(p);

    /* Bitwise OR */
    assert(jolt_compile("(defkernel bor [x y] (bitwise-or x y))", &p, &d) == JOLT_OK);
    float in2[] = {240.0f, 15.0f};
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 255.0f);
    jolt_program_destroy(p);

    /* Bitwise XOR */
    assert(jolt_compile("(defkernel bxor [x y] (bitwise-xor x y))", &p, &d) == JOLT_OK);
    float in3[] = {255.0f, 15.0f};
    assert(jolt_program_run(vm, p, in3, 2, out, 1) == JOLT_OK);
    assert(out[0] == 240.0f);
    jolt_program_destroy(p);

    /* Shift left */
    assert(jolt_compile("(defkernel shl [x y] (shl x y))", &p, &d) == JOLT_OK);
    float in4[] = {1.0f, 4.0f};
    assert(jolt_program_run(vm, p, in4, 2, out, 1) == JOLT_OK);
    assert(out[0] == 16.0f);
    jolt_program_destroy(p);

    /* Shift right */
    assert(jolt_compile("(defkernel shr [x y] (shr x y))", &p, &d) == JOLT_OK);
    float in5[] = {16.0f, 4.0f};
    assert(jolt_program_run(vm, p, in5, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_bitwise_operations: PASSED\n");
}

static void test_select_operation(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Select: condition true */
    assert(jolt_compile("(defkernel sel [x y z] (select x y z))", &p, &d) == JOLT_OK);
    float in1[] = {1.0f, 10.0f, 20.0f};
    assert(jolt_program_run(vm, p, in1, 3, out, 1) == JOLT_OK);
    assert(out[0] == 10.0f);
    jolt_program_destroy(p);

    /* Select: condition false */
    assert(jolt_compile("(defkernel sel [x y z] (select x y z))", &p, &d) == JOLT_OK);
    float in2[] = {0.0f, 10.0f, 20.0f};
    assert(jolt_program_run(vm, p, in2, 3, out, 1) == JOLT_OK);
    assert(out[0] == 20.0f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_select_operation: PASSED\n");
}

static void test_rgba_output(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[4] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* RGBA output */
    assert(jolt_compile("(defkernel rgba-test [r g b a] (rgba (* r 2.0) (* g 2.0) (* b 2.0) a))", &p, &d) == JOLT_OK);
    float in[] = {0.1f, 0.2f, 0.3f, 0.5f};
    assert(jolt_program_run(vm, p, in, 4, out, 4) == JOLT_OK);
    assert(fabsf(out[0] - 0.2f) < 1e-6f);
    assert(fabsf(out[1] - 0.4f) < 1e-6f);
    assert(fabsf(out[2] - 0.6f) < 1e-6f);
    assert(fabsf(out[3] - 0.5f) < 1e-6f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_rgba_output: PASSED\n");
}

static void test_error_handling(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;

    /* Duplicate binding */
    assert(jolt_compile("(defkernel dup [x x] x)", &p, &d) != JOLT_OK);
    assert(!p);

    /* Undefined binding */
    assert(jolt_compile("(defkernel undef [x] (+ x y))", &p, &d) != JOLT_OK);
    assert(!p);

    /* Unknown operation */
    assert(jolt_compile("(defkernel unknown [x] (frob x))", &p, &d) != JOLT_OK);
    assert(!p);

    /* Arity error */
    assert(jolt_compile("(defkernel arity [x] (+ x))", &p, &d) != JOLT_OK);
    assert(!p);

    /* Trailing source */
    assert(jolt_compile("(defkernel trail [x] x) garbage", &p, &d) != JOLT_OK);
    assert(!p);

    /* Invalid binding name */
    assert(jolt_compile("(defkernel invalid [1x] 1x)", &p, &d) != JOLT_OK);
    assert(!p);

    /* Too many inputs */
    char src[1024];
    strcpy(src, "(defkernel many [");
    for (int i = 0; i < 33; ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "v%d ", i);
        strcat(src, buf);
    }
    strcat(src, "] v0)");
    assert(jolt_compile(src, &p, &d) != JOLT_OK);
    assert(!p);

    /* Division by zero */
    assert(jolt_compile("(defkernel div0 [x] (/ x 0.0))", &p, &d) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {1.0f};
    float out[1] = {0};
    assert(jolt_program_run(vm, p, in, 1, out, 1) == JOLT_ERR_NUMERIC);
    jolt_program_destroy(p);
    jolt_vm_destroy(vm);

    printf("test_error_handling: PASSED\n");
}

static void test_nested_expressions(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Nested arithmetic */
    assert(jolt_compile("(defkernel nested [x y z] (+ (* x y) (- z x)))", &p, &d) == JOLT_OK);
    float in[] = {2.0f, 3.0f, 10.0f};
    assert(jolt_program_run(vm, p, in, 3, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 14.0f) < 1e-6f);
    jolt_program_destroy(p);

    /* Deep nesting */
    assert(jolt_compile("(defkernel deep [x] (+ (+ (+ (+ x 1.0) 2.0) 3.0) 4.0))", &p, &d) == JOLT_OK);
    float in2[] = {0.0f};
    assert(jolt_program_run(vm, p, in2, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 10.0f) < 1e-6f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_nested_expressions: PASSED\n");
}

static void test_comments_and_whitespace(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Comments and whitespace */
    const char *src = ";; This is a comment\n(defkernel test [x] (* x 2.0)) ;; trailing comment\n";
    assert(jolt_compile(src, &p, &d) == JOLT_OK);
    float in[] = {5.0f};
    assert(jolt_program_run(vm, p, in, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 10.0f) < 1e-6f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_comments_and_whitespace: PASSED\n");
}

static void test_constants(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    float out[1] = {0};
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);

    /* Float constants */
    assert(jolt_compile("(defkernel const [x] (+ x 3.14159))", &p, &d) == JOLT_OK);
    float in[] = {0.0f};
    assert(jolt_program_run(vm, p, in, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 3.14159f) < 1e-5f);
    jolt_program_destroy(p);

    /* Negative constants */
    assert(jolt_compile("(defkernel neg [x] (+ x -2.5))", &p, &d) == JOLT_OK);
    float in2[] = {5.0f};
    assert(jolt_program_run(vm, p, in2, 1, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 2.5f) < 1e-6f);
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_constants: PASSED\n");
}

int main(void) {
    test_basic_arithmetic();
    test_comparison_operations();
    test_logical_operations();
    test_bitwise_operations();
    test_select_operation();
    test_rgba_output();
    test_error_handling();
    test_nested_expressions();
    test_comments_and_whitespace();
    test_constants();
    printf("\nAll compiler tests passed!\n");
    return 0;
}
