#include "joltscript/effects.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static void near(float a,float b) { assert(fabsf(a-b)<1e-5f); }
int main(void) {
    jolt_effects_t *e=jolt_effects_create(); assert(e && jolt_effects_count(e)==12);
    float input[64],out[64];
    for (int i=0;i<16;++i) { input[i*4]=.1f; input[i*4+1]=.2f; input[i*4+2]=.3f; input[i*4+3]=.5f; }
    float luminance=.1f*.2126f+.2f*.7152f+.3f*.0722f;
    float expected[]={.1f,.1f,.4f,luminance,.1f,.2498f,.1f,0.f,1.f/6.f,.1f,.3f,.1f};
    for (size_t n=0;n<12;++n) {
        const char *name=jolt_effects_name(n); assert(name);
        assert(jolt_effects_apply(e,name,input,16,NULL,0,sizeof(out),out)==JOLT_OK);
        for (int i=0;i<16;++i) { near(out[i*4],expected[n]); near(out[i*4+3],.5f); }
        float zero[4]={0},result[4];
        assert(jolt_effects_apply(e,name,zero,1,NULL,0,16,result)==JOLT_OK);
        for (int c=0;c<4;++c) near(result[c],0);
    }
    float amount=100;
    assert(jolt_effects_apply(e,"brightness",input,16,&amount,1,sizeof(out),out)==JOLT_OK); near(out[0],.4f);
    amount=0;
    assert(jolt_effects_apply(e,"opacity",input,16,&amount,1,sizeof(out),out)==JOLT_OK);
    for (int i=0;i<64;++i) near(out[i],0);
    assert(jolt_effects_apply(e,"invert",input,16,NULL,0,sizeof(input),input)==JOLT_OK); near(input[0],.4f);
    out[0]=99;
    assert(jolt_effects_apply(e,"invert",input,16,NULL,0,1,out)==JOLT_ERR_BUDGET && out[0]==99);
    assert(jolt_effects_apply(e,"missing",input,16,NULL,0,256,out)==JOLT_ERR_NOT_FOUND);
    jolt_effects_destroy(e); return 0;
}
