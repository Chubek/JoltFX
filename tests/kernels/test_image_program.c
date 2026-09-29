#include "joltscript/image_program.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static jolt_status_t compile(const char *s,jolt_image_program_t **p) {
    jolt_diagnostic_t d={.size=sizeof(d)};
    jolt_status_t status=jolt_image_compile("",s,p,&d);
    if(status!=JOLT_OK) assert(d.message[0]);
    return status;
}
static jolt_status_t run(jolt_image_program_t *p,size_t steps,float *out) {
    const float in[]={.1f,.2f,.3f,.5f};
    return jolt_image_program_run(p,in,1,1,NULL,0,NULL,0,32,steps,out);
}
int main(void) {
    const char *bad[]={
        "", "(defkernel k [x y] x)", "(defkernel k [x y c] (unknown x))",
        "(defkernel k [x y c] (if 1 0 (unknown x)))",
        "(defkernel k [x y c] (if 1 0 missing))",
        "(defkernel k [x y c] (sample x y c 0))",
        "(defkernel k [x x c] x)", "(defkernel k [x y c] (let [a] a))",
        "(defkernel k [x y c] (sum i 0 1))", "(defkernel k [x y c] (+))",
        "(param a 0 1 2 0)(defkernel k [x y c] a)",
        "(param a 0 0 1 0)(param a 0 0 1 0)(defkernel k [x y c] a)",
        "(defn sin [a b] a)(defkernel k [x y c] (sin x))",
        "(defkernel k [x y c] x)(defkernel other [x y c] y)",
        "(defkernel k [x y c] (+ x y])"
    };
    jolt_image_program_t *p=NULL;
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) { assert(compile(bad[i],&p)==JOLT_ERR_SYNTAX); assert(!p); }
    assert(compile("(defn f [a] (+ a 1))(defkernel k [x y c] (let [a 1 b (f a)] (+ a b)))",&p)==JOLT_OK);
    float out[4]; assert(run(p,1000,out)==JOLT_OK); for(int i=0;i<4;++i) assert(out[i]==3);
    jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (if (= c 3) .5 (if true .25 (/ 1 0))))",&p)==JOLT_OK);
    assert(run(p,1000,out)==JOLT_OK); assert(out[0]==.25f && out[3]==.5f); jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (let [a 1] (+ (let [a 2] a) a)))",&p)==JOLT_OK);
    assert(run(p,1000,out)==JOLT_OK); assert(out[0]==3); jolt_image_program_destroy(p);
    assert(compile("(defn forever [a] (forever a))(defkernel k [x y c] (forever x))",&p)==JOLT_OK);
    out[0]=123; assert(run(p,10000,out)==JOLT_ERR_BUDGET); assert(out[0]==123); jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (sum i 0 65536 (+ i x)))",&p)==JOLT_OK);
    assert(run(p,100,out)==JOLT_ERR_BUDGET); jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (sample x y c 0 99))",&p)==JOLT_OK);
    assert(run(p,100,out)==JOLT_ERR_ARGUMENT); jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (data -1))",&p)==JOLT_OK);
    assert(run(p,100,out)==JOLT_ERR_ARGUMENT); jolt_image_program_destroy(p);
    assert(compile("(defkernel k [x y c] (sqrt -1))",&p)==JOLT_OK);
    assert(run(p,100,out)==JOLT_ERR_NUMERIC); jolt_image_program_destroy(p);
    assert(compile("(passes 0)(defkernel k [x y c] x)",&p)==JOLT_OK);
    assert(run(p,100,out)==JOLT_ERR_ARGUMENT); jolt_image_program_destroy(p);
    return 0;
}
