#include "joltscript/compiler.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static jolt_program_t *compile(const char *source)
{
    jolt_program_t *p = NULL;
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_status_t s = jolt_compile(source, &p, &d);
    if (s)
        fprintf(stderr, "compile %d at %zu:%zu: %s\n%s\n", s, d.line, d.column, d.message, source);
    assert(s == JOLT_OK);
    return p;
}
static jolt_value_t evaluate(jolt_vm_t *vm, const char *imports, const char *body)
{
    char source[8192];
    snprintf(source, sizeof(source), "%s (defkernel k [] %s)", imports, body);
    jolt_program_t *p = compile(source);
    jolt_value_t v = {0};
    jolt_status_t s = jolt_program_run_values(vm, p, NULL, 0, &v, 1, NULL);
    if (s)
        fprintf(stderr, "run %d: %s\n", s, source);
    assert(s == JOLT_OK);
    jolt_program_destroy(p);
    return v;
}
static void near(jolt_value_t v, double want)
{
    double actual =
        v.type == JOLT_TYPE_F64 || v.type == JOLT_TYPE_F32 ? v.as.number : (double) v.as.integer;
    if (fabs(actual - want) > 1e-9)
        fprintf(stderr, "got %.17g expected %.17g\n", actual, want);
    assert(fabs(actual - want) < 1e-9);
}
static void text(jolt_value_t v, const char *want)
{
    assert(v.type == JOLT_TYPE_STRING && v.as.string.size == strlen(want) &&
           !memcmp(v.as.string.data, want, strlen(want)));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    char file[1024], source[16384];
    const char *modules[] = {"math", "graphics", "geometry", "time", "collections", "strings"};
    for (unsigned i = 0; i < 6; ++i) {
        snprintf(file, sizeof(file), "%s/%s.jolt", argv[1], modules[i]);
        FILE *f = fopen(file, "rb");
        assert(f);
        size_t n = fread(source, 1, sizeof(source) - 1, f);
        assert(!ferror(f));
        fclose(f);
        source[n] = 0;
        jolt_program_t *p = compile(source);
        jolt_program_destroy(p);
    }
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    const struct {
        const char *module, *body;
        double expected;
    } cases[] = {
        {"math", "(sin HALF-PI)", 1},
        {"math", "(smoothstep 0 1 0.5)", .5},
        {"math", "(map-range 5 0 10 0 100)", 50},
        {"math", "(dot [1 2 3] [4 5 6])", 32},
        {"math", "(mat2-det [1 2 3 4])", -2},
        {"math", "(let random (random-seed 42u32)) (random)", 1083814273.0 / 4294967296.0},
        {"time", "(ease-out-bounce 1)", 1},
        {"time", "(ease-in-out-cubic 0.5)", .5},
        {"time", "(keyframe-value [[10 100] [0 0] [5 30]] 7.5)", 65},
        {"time", "(catmull-rom-curve 1 0 1 2 3)", 2},
        {"time", "(loop-time -1 3)", 2},
        {"graphics", "(blend-overlay 0.2 0.25)", .1},
        {"graphics", "(convolve [1 4 7] [1 2 1])", 4},
        {"graphics", "(box-filter [1 4 7])", 4},
        {"geometry", "(point3d-dist [0 0 0] [2 3 6])", 7},
        {"collections", "(reduce + 0 [1 2 3 4])", 10},
        {"collections", "(length (set 1 2 1 3))", 3},
        {"collections", "(get (frequencies [1 1 2]) 1)", 2},
        {"collections", "(length (take 5 (iterate (fn [(x i64)] (+ x 1i64)) 0i64)))", 5},
        {"strings", "(str-length \"a😀é\")", 3},
        {"strings", "(parse-int \"-9223372036854775808\")", -9223372036854775808.0},
        {"strings", "(parse-float \"1.25e2\")", 125},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        char imports[128];
        snprintf(imports, sizeof(imports), "(import jolt.%s)", cases[i].module);
        near(evaluate(vm, imports, cases[i].body), cases[i].expected);
    }
    jolt_value_t v = evaluate(vm, "(import jolt.math) (import jolt.collections (nth))",
                              "(mat2-mul [1 2 3 4] [5 6 7 8])");
    assert(v.type == JOLT_TYPE_VECTOR && v.as.collection.size == 4);
    const double matrix[] = {19, 22, 43, 50};
    for (unsigned i = 0; i < 4; ++i)
        near(v.as.collection.items[i], matrix[i]);
    v = evaluate(vm, "(import jolt.geometry)", "(scale3d [1 2 3] 2 3 4)");
    near(v.as.collection.items[2], 12);
    v = evaluate(vm, "(import jolt.geometry)",
                 "(quat-rotate (quat-from-axis-angle [0 0 1] 1.5707963267948966) [1 0 0])");
    near(v.as.collection.items[0], 0);
    near(v.as.collection.items[1], 1);
    v = evaluate(vm, "(import jolt.graphics)", "(hsv->rgb 0.3333333333333333 1 1)");
    near(v.as.collection.items[0], 0);
    near(v.as.collection.items[1], 1);
    near(v.as.collection.items[2], 0);
    v = evaluate(vm, "(import jolt.graphics)", "(alpha-composite 1 0 0 0.5 0 0 1 1)");
    near(v.as.collection.items[0], .5);
    near(v.as.collection.items[2], .5);
    near(v.as.collection.items[3], 1);
    v = evaluate(vm, "(import jolt.collections)", "(sort [3 1 2] <)");
    for (unsigned i = 0; i < 3; ++i)
        near(v.as.collection.items[i], i + 1);
    v = evaluate(vm, "(import jolt.collections)", "(map (fn [(x value)] (+ x 1)) [1 2 3])");
    near(v.as.collection.items[2], 4);
    v = evaluate(vm, "(import jolt.collections)", "(take 5 (cycle [1 2]))");
    near(v.as.collection.items[4], 1);
    v = evaluate(vm, "(import jolt.collections)", "(take 10 (range-seq 5 -1 -2))");
    assert(v.as.collection.size == 3);
    near(v.as.collection.items[2], 1);
    v = evaluate(vm, "(import jolt.collections)", "(set-intersection (set 1 2 3) (set 2 3 4))");
    assert(v.type == JOLT_TYPE_SET && v.as.collection.size == 2);
    text(evaluate(vm, "(import jolt.strings)", "(str-reverse \"a😀é\")"), "é😀a");
    text(evaluate(vm, "(import jolt.strings)", "(str-trim \"\\u{2003} héllo \\n\")"), "héllo");
    text(evaluate(vm, "(import jolt.strings)",
                  "(format \"{}:{1}:{{ok}}\" 18446744073709551615u64 \"é\")"),
         "18446744073709551615:é:{ok}");
    text(evaluate(vm, "(import jolt.strings)", "(str-replace-all \"aaa\" \"a\" \"aa\")"), "aaaaaa");
    text(evaluate(vm, "(import jolt.strings)", "(regex-find \"[a-z]+\" \"123abc456\")"), "abc");
    text(evaluate(vm, "(import jolt.strings)", "(regex-replace \"([0-9]+)\" \"a12b3\" \"<$1>\")"),
         "a<12>b<3>");
    const char *invalid[] = {
        "(import jolt.missing) (defkernel k [] 1)",
        "(import jolt.math (missing)) (defkernel k [] 1)",
        "(import jolt.math) (defkernel k [] (sin \"x\"))",
        "(import jolt.math) (import jolt.collections) (defkernel k [] (length [1 2]))"};
    for (unsigned i = 0; i < 4; ++i) {
        jolt_program_t *p = NULL;
        jolt_diagnostic_t d = {.size = sizeof(d)};
        assert(jolt_compile(invalid[i], &p, &d) != JOLT_OK && !p && d.line && d.column &&
               d.message[0]);
    }
    const char *failures[] = {
        "(import jolt.collections) (defkernel k [] (nth [1] 9))",
        "(import jolt.strings) (defkernel k [] (parse-int \"9223372036854775808\"))",
        "(import jolt.strings) (defkernel k [] (regex-find \"[\" \"x\"))",
        "(import jolt.collections) (defkernel k [] (take 4097 (range-seq 0 9999)))"};
    for (unsigned i = 0; i < 4; ++i) {
        jolt_program_t *p = compile(failures[i]);
        v.type = JOLT_TYPE_I32;
        v.as.integer = 99;
        assert(jolt_program_run_values(vm, p, NULL, 0, &v, 1, NULL) != JOLT_OK &&
               v.as.integer == 99);
        jolt_program_destroy(p);
    }
    /* A previous aggregate remains valid across failures and aliased calls. */
    v = evaluate(vm, "(import jolt.collections)", "[\"persistent\" [1 2]]");
    const jolt_value_t *saved = v.as.collection.items;
    jolt_program_t *identity = compile("(import jolt.collections) (defkernel k [(x vector)] x)");
    assert(jolt_program_run_values(vm, identity, &v, 1, &v, 1, NULL) == JOLT_OK);
    saved = v.as.collection.items;
    text(saved[0], "persistent");
    near(saved[1].as.collection.items[1], 2);
    jolt_program_destroy(identity);
    const char *edges[] = {
        "(import jolt.collections) (defkernel k [] (let f (fn [(x value)] (+ x 1))) (f \"bad\"))",
        "(import jolt.collections) (defkernel k [] (if (nth [\"bad\"] 0) 1 2))",
        "(import jolt.collections) (defkernel k [] (+ (nth [1i64] 0) (nth [1u64] 0)))",
        "(import jolt.geometry) (defkernel k [] (point-in-bbox2d [1 2 3] [0 0 0]))",
        "(import jolt.strings) (defkernel k [] (char-at \"x\" 9223372036854775807i64))",
        "(import jolt.collections) (defkernel k [] (apply bitwise-and [1.0 2.0]))",
        "(import jolt.collections) (defkernel k [] [1 (fn [] 1)])",
        "(import jolt.collections) (defkernel k [] (take 2 (lazy-seq (fn [(x value)] 7))))",
        "(import jolt.strings) (defkernel k [] (str-repeat \"xx\" 1048576))",
        "(import jolt.strings) (defkernel k [] (regex-match? \"(*NO_AUTO_POSSESS)(a+)+$\" "
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaab\"))"};
    for (size_t i = 0; i < sizeof(edges) / sizeof(*edges); ++i) {
        jolt_program_t *p = compile(edges[i]);
        jolt_value_t output = {.type = JOLT_TYPE_I32, .as.integer = 99};
        assert(jolt_program_run_values(vm, p, NULL, 0, &output, 1, NULL) != JOLT_OK);
        assert(output.as.integer == 99);
        text(saved[0], "persistent");
        jolt_program_destroy(p);
    }
    const char *budget_cases[] = {
        "(import jolt.collections) (defkernel k [] (group-by (fn [(x value)] (> x 1)) [1 2 3 4]))",
        "(import jolt.collections) (defkernel k [] (take 5 (iterate (fn [(x value)] (+ x 1)) 0)))",
        "(import jolt.strings) (defkernel k [] (regex-replace \"([0-9]+)\" \"a12b3\" \"<$1>\"))",
        "(import jolt.strings) (defkernel k [] (str-split \"a,b,c\" \",\"))",
        "(import jolt.collections) (defkernel k [] (sort [4 3 2 1] <))"};
    for (size_t i = 0; i < sizeof(budget_cases) / sizeof(*budget_cases); ++i) {
        jolt_program_t *p = compile(budget_cases[i]);
        bool succeeded = false;
        for (uint64_t steps = 1; steps <= 256; ++steps) {
            jolt_run_options_t options = {.size = sizeof(options), .steps = steps};
            jolt_value_t output = {.type = JOLT_TYPE_I32, .as.integer = 99};
            jolt_status_t status = jolt_program_run_values(vm, p, NULL, 0, &output, 1, &options);
            if (status == JOLT_OK) {
                succeeded = true;
                break;
            }
            assert(status == JOLT_ERR_BUDGET && output.as.integer == 99);
        }
        assert(succeeded);
        jolt_program_destroy(p);
    }
    near(evaluate(vm, "(import jolt.collections)", "(apply sqrt [9])"), 3);
    v = evaluate(vm, "(import jolt.collections)", "(= (set 1 2) (set 2 1))");
    assert(v.as.unsigned_integer == 1);
    v = evaluate(vm, "(import jolt.collections)", "(= (hash-map 1 2 3 4) (hash-map 3 4 1 2))");
    assert(v.as.unsigned_integer == 1);
    v = evaluate(vm, "(import jolt.collections)", "(cond [false (nth [1] 0)])");
    assert(v.type == JOLT_TYPE_VOID);
    v = evaluate(vm, "(import jolt.collections)", "(take 1 (iterate (fn [(x value)] (/ 1 0)) 7))");
    near(v.as.collection.items[0], 7);
    jolt_vm_destroy(vm);
    puts("all six standard-library modules passed");
    return 0;
}
