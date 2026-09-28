#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "joltscript/bindings.h"
#include <assert.h>
#include <math.h>
#include <string.h>
int main(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    const char *src = "; test\n(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";
    assert(jolt_compile(src, &p, &d) == JOLT_OK && p);
    float in[] = {.2f,.3f,.4f,.5f,2.f}, out[4] = {0};
    jolt_vm_t *vm = jolt_vm_create(); assert(vm);
    assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    assert(fabsf(out[0] - .4f) < 1e-6f && out[3] == .5f);
    assert(jolt_program_run(vm, p, in, 4, out, 4) == JOLT_ERR_ARGUMENT);
    size_t size = 0; const uint8_t *data = jolt_program_data(p, &size);
    assert(data && size > 16);
    assert(jolt_vm_execute(vm, data, size - 1) == JOLT_ERR_BYTECODE);
    assert(jolt_vm_execute(NULL, data, size) == JOLT_ERR_ARGUMENT);
    jolt_program_destroy(p); p = NULL;
    assert(jolt_compile("(defkernel bad [x x] x)", &p, &d) != JOLT_OK && !p);
    assert(jolt_compile("(defkernel bad [x] (+ x missing))", &p, &d) != JOLT_OK && d.line == 1);
    assert(jolt_compile("(defkernel bad [x] (+ x))", &p, &d) != JOLT_OK);
    assert(jolt_compile("(defkernel bad [x] x) garbage", &p, &d) != JOLT_OK);
    assert(jolt_compile("(defkernel div [x] (/ x 0))", &p, &d) == JOLT_OK);
    out[0] = 99;
    assert(jolt_program_run(vm, p, in, 1, out, 1) == JOLT_ERR_NUMERIC && out[0] == 99);
    jolt_program_destroy(p);
    /* Extended operations: comparison, logical, bitwise */
    assert(jolt_compile("(defkernel cmp [x] (> x 0.5))", &p, &d) == JOLT_OK);
    float xin[] = {.7f}, xout[1] = {0};
    assert(jolt_program_run(vm, p, xin, 1, xout, 1) == JOLT_OK && xout[0] == 1.f);
    xin[0] = .3f;
    assert(jolt_program_run(vm, p, xin, 1, xout, 1) == JOLT_OK && xout[0] == 0.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel cmp2 [x] (<= x 0.5))", &p, &d) == JOLT_OK);
    xin[0] = .5f;
    assert(jolt_program_run(vm, p, xin, 1, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel cmp3 [x] (= x 0.5))", &p, &d) == JOLT_OK);
    xin[0] = .5f;
    assert(jolt_program_run(vm, p, xin, 1, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel cmp4 [x] (!= x 0.5))", &p, &d) == JOLT_OK);
    xin[0] = .3f;
    assert(jolt_program_run(vm, p, xin, 1, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel log [x y] (and x y))", &p, &d) == JOLT_OK);
    float lin[] = {1.f, 0.f};
    assert(jolt_program_run(vm, p, lin, 2, xout, 1) == JOLT_OK && xout[0] == 0.f);
    lin[1] = 1.f;
    assert(jolt_program_run(vm, p, lin, 2, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel log2 [x y] (or x y))", &p, &d) == JOLT_OK);
    lin[0] = 0.f; lin[1] = 0.f;
    assert(jolt_program_run(vm, p, lin, 2, xout, 1) == JOLT_OK && xout[0] == 0.f);
    lin[0] = 1.f;
    assert(jolt_program_run(vm, p, lin, 2, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel log3 [x] (not x))", &p, &d) == JOLT_OK);
    lin[0] = 0.f;
    assert(jolt_program_run(vm, p, lin, 1, xout, 1) == JOLT_OK && xout[0] == 1.f);
    lin[0] = 1.f;
    assert(jolt_program_run(vm, p, lin, 1, xout, 1) == JOLT_OK && xout[0] == 0.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel bw [x y] (bitwise-and x y))", &p, &d) == JOLT_OK);
    float bin[] = {255.f, 15.f};
    assert(jolt_program_run(vm, p, bin, 2, xout, 1) == JOLT_OK && xout[0] == 15.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel bw2 [x y] (bitwise-or x y))", &p, &d) == JOLT_OK);
    bin[0] = 240.f; bin[1] = 15.f;
    assert(jolt_program_run(vm, p, bin, 2, xout, 1) == JOLT_OK && xout[0] == 255.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel bw3 [x y] (bitwise-xor x y))", &p, &d) == JOLT_OK);
    bin[0] = 255.f; bin[1] = 15.f;
    assert(jolt_program_run(vm, p, bin, 2, xout, 1) == JOLT_OK && xout[0] == 240.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel sh [x y] (shl x y))", &p, &d) == JOLT_OK);
    bin[0] = 1.f; bin[1] = 4.f;
    assert(jolt_program_run(vm, p, bin, 2, xout, 1) == JOLT_OK && xout[0] == 16.f);
    jolt_program_destroy(p);
    assert(jolt_compile("(defkernel sh2 [x y] (shr x y))", &p, &d) == JOLT_OK);
    bin[0] = 16.f; bin[1] = 4.f;
    assert(jolt_program_run(vm, p, bin, 2, xout, 1) == JOLT_OK && xout[0] == 1.f);
    jolt_program_destroy(p);
    jolt_vm_destroy(vm);
    return 0;
}
