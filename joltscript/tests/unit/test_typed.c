#include "joltscript/compiler.h"
#include "joltscript/value.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static jolt_program_t *compile(const char *source) {
    jolt_program_t *p=NULL;jolt_diagnostic_t d={.size=sizeof(d)};
    jolt_status_t s=jolt_compile(source,&p,&d);
    if(s) fprintf(stderr,"%zu:%zu: %s (%d)\n%s\n",d.line,d.column,d.message,s,source);
    assert(s==JOLT_OK && p);return p;
}
static jolt_value_t evaluate(jolt_vm_t *vm,const char *source) {
    jolt_program_t *p=compile(source);size_t size;const uint8_t *code=jolt_program_data(p,&size);
    jolt_value_t out={0};jolt_status_t s=jolt_vm_run_values(vm,code,size,NULL,0,&out,1,NULL);
    if(s) fprintf(stderr,"runtime %d: %s\n",s,source);
    assert(s==JOLT_OK);jolt_program_destroy(p);return out;
}
int main(void) {
    jolt_vm_t *vm=jolt_vm_create();assert(vm);
    const struct { const char *source;int64_t expected; } cases[]={
        {"(defkernel k [] (+ 9007199254740993i64 2i64))",9007199254740995LL},
        {"(defkernel k [] (var n 0) (for [i 0 10] (set! n (+ n i))) n)",45},
        {"(defkernel k [] (var n 0) (for [i 10 0 -2] (set! n (+ n i))) n)",30},
        {"(defkernel k [] (var n 0) (var i 0) (while (< i 10) (set! i (+ i 1)) (when (= i 3) (continue)) (when (= i 7) (break)) (set! n (+ n i))) n)",18},
        {"(defkernel k [] (var n 0) (loop (set! n (+ n 1)) (when (= n 5) (break))) n)",5},
        {"(defn factorial [(n i64)] -> i64 (if (<= n 1i64) 1i64 (* n (factorial (- n 1i64))))) (defkernel k [] (factorial 10i64))",3628800},
        {"(defkernel k [] (let offset 7) (let f (fn [(x i32)] -> i32 (+ x offset))) (let [offset 99] (f 3)))",10},
        {"(defkernel k [] (var n 0) (let f (fn [] -> i32 (set! n (+ n 1)))) (f) (f) n)",2},
        {"(defkernel k [] ((fn [(x i32)] -> i32 (* x x)) 7))",49},
        {"(defkernel k [] (let make (fn [(x i32)] (fn [(y i32)] -> i32 (+ x y)))) (let f (make 7)) (f 5))",12},
        {"(defkernel k [] (i32 (+ 255u8 1u8)))",0},
        {"(defkernel k [] (i32 (shr -8i32 2)))",-2},
        {"(defkernel k [] (var total 0) (for [i 0 3] (for [j 0 5] (when (= j 2) (break)) (set! total (+ total 1)))) total)",6},
        {"(defkernel k [] (var calls 0) (for [i (do (set! calls (+ calls 1)) 0) (do (set! calls (+ calls 1)) 5)] ()) calls)",2}
        ,{"(defn first [] (second)) (defn second [] 7) (defkernel k [] (first))",7}
        ,{"(defn even [(n i32)] -> bool (if (= n 0) true (odd (- n 1)))) (defn odd [(n i32)] -> bool (if (= n 0) false (even (- n 1)))) (defkernel k [] (i32 (even 10)))",1}
        ,{"(defkernel k [] (var (x i32) 4) (let [f (fn [] x)] (let [x 99] (f))))",4}
        ,{"(defkernel k [] (var n 7) (loop (set! n (do (break) 9))) n)",7}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        jolt_value_t v=evaluate(vm,cases[i].source);
        assert((v.type==JOLT_TYPE_I32||v.type==JOLT_TYPE_I64)&&v.as.integer==cases[i].expected);
    }
    jolt_value_t v=evaluate(vm,"(defkernel k [] 18446744073709551615u64)");
    assert(v.type==JOLT_TYPE_U64&&v.as.unsigned_integer==UINT64_MAX);
    v=evaluate(vm,"(defkernel k [] (if true \"héllo\\n\\u{1F600}\" \"no\"))");
    assert(v.type==JOLT_TYPE_STRING && v.as.string.size==11 && !memcmp(v.as.string.data,"héllo\n😀",11));
    v=evaluate(vm,"(defkernel k [] \\u{1F600})");assert(v.type==JOLT_TYPE_CHAR&&v.as.unsigned_integer==0x1f600);
    v=evaluate(vm,"(defkernel k [] (= \"a\\0b\" \"a\\0b\"))");assert(v.type==JOLT_TYPE_BOOL&&v.as.unsigned_integer==1);
    v=evaluate(vm,"(defkernel k [] (- 16777217f64 16777216f64))");assert(v.type==JOLT_TYPE_F64&&v.as.number==1);
    v=evaluate(vm,"(defkernel k [] (- 16777217f32 16777216f32))");assert(v.type==JOLT_TYPE_F32&&v.as.number==0);
    const char *invalid[]={
        "(defkernel k [] 256u8)","(defkernel k [] 18446744073709551616u64)",
        "(defkernel k [] (+ true 1))","(defkernel k [] (if true 1 \"x\"))",
        "(defkernel k [] (let (x u8) 256) x)","(defkernel k [] (break))",
        "(defkernel k [] (for [i 0.0 4.0] ()))","(defkernel k [] \\u{D800})",
        "(defkernel k [] (var (x i32) 1) (set! x \"x\"))",
        "(defkernel k [] ((fn [(x i32)] x) \"x\"))",
        "(defkernel k [] (loop ((fn [] (break)))))",
        "(defkernel k [] (fn [] 1))","(defkernel k [] (+ 1i64 1u64))",
        "(defn f [] x) (defkernel k [] (let x 7) (f))",
        "(defn f [] (g)) (defn g [] x) (defkernel k [] (let x 7) (f))",
        "(defn f [] (f)) (defkernel k [] (f))",
        "(defkernel k [] (+ 1i32 (rgba 1 2 3 4)))",
        "(defkernel k [] (= (fn [] 1) (fn [] 1)))"
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        jolt_diagnostic_t d={.size=sizeof(d)};jolt_program_t *p=NULL;
        assert(jolt_compile(invalid[i],&p,&d)!=JOLT_OK && !p && d.line && d.column && d.message[0]);
    }
    /* Exact typed ABI, signature, immutable inputs and failure-atomic publication. */
    jolt_program_t *identity=compile("(defkernel k [(a u64) (b string)] (rgba a b \\a true))");
    size_t bytes;const uint8_t *code=jolt_program_data(identity,&bytes);
    jolt_type_t it[2],ot[4];assert(jolt_bytecode_signature(code,bytes,it,2,ot,4)==JOLT_OK);
    assert(it[0]==JOLT_TYPE_U64&&it[1]==JOLT_TYPE_STRING&&ot[2]==JOLT_TYPE_CHAR);
    jolt_value_t inputs[2]={{.type=JOLT_TYPE_U64},{.type=JOLT_TYPE_STRING}},outputs[4];
    inputs[0].as.unsigned_integer=UINT64_MAX;inputs[1].as.string.data="a\0b";inputs[1].as.string.size=3;
    assert(jolt_vm_run_values(vm,code,bytes,inputs,2,outputs,4,NULL)==JOLT_OK);
    assert(outputs[0].as.unsigned_integer==UINT64_MAX&&outputs[1].as.string.size==3&&!memcmp(outputs[1].as.string.data,"a\0b",3));
    inputs[0].type=JOLT_TYPE_BOOL;inputs[0].as.unsigned_integer=2;
    assert(jolt_vm_run_values(vm,code,bytes,inputs,2,outputs,4,NULL)==JOLT_ERR_TYPE);
    assert(outputs[0].as.unsigned_integer==UINT64_MAX&&!memcmp(outputs[1].as.string.data,"a\0b",3));
    /* Single-byte mutations/truncations exercise structural and semantic validation. */
    assert(bytes<4096);uint8_t mutated[4096];
    for(size_t i=0;i<bytes;++i) {
        assert(jolt_bytecode_validate(code,i)!=JOLT_OK);
        memcpy(mutated,code,bytes);mutated[i]^=0xff;
        (void)jolt_bytecode_validate(mutated,bytes);
    }
    jolt_program_destroy(identity);
    const char *failures[]={"(defkernel k [] (loop ()))","(defkernel k [] (/ 1i64 0i64))","(defkernel k [] (shl 1u64 64))","(defkernel k [] (u64 -1f64))"};
    for(size_t i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        jolt_program_t *p=compile(failures[i]);size_t size;const uint8_t *failure_code=jolt_program_data(p,&size);
        jolt_value_t out={.type=JOLT_TYPE_I32};out.as.integer=99;
        jolt_run_options_t options={sizeof(options),1000};
        assert(jolt_vm_run_values(vm,failure_code,size,NULL,0,&out,1,&options)==(i?JOLT_ERR_NUMERIC:JOLT_ERR_BUDGET));
        assert(out.as.integer==99);jolt_program_destroy(p);
    }
    jolt_vm_destroy(vm);puts("typed Phase 1 tests passed");return 0;
}
