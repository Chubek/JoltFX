#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "joltscript/bindings.h"
#include <assert.h>
#include <math.h>
static int invoked;
static jolt_status_t call(const float *a,size_t n,float *r,void *u) {
    (void)n;(void)u; ++invoked; *r=a[0]*2; return JOLT_OK;
}
int main(void) {
    jolt_registry_t *r=jolt_registry_create(); assert(r);
    char name[]="jolt.test.double";
    jolt_binding_desc_t d={sizeof(d),name,1,JOLT_CAP_COMPUTE,call,NULL};
    assert(jolt_registry_register(r,&d)==JOLT_OK);
    assert(jolt_registry_register(r,&d)==JOLT_ERR_DUPLICATE);
    name[5]='X'; assert(jolt_registry_freeze(r)==JOLT_OK);
    float x=3,y=99;
    assert(jolt_registry_call(r,"jolt.test.double",0,&x,1,&y)==JOLT_ERR_CAPABILITY && !invoked && y==99);
    assert(jolt_registry_call(r,"jolt.test.double",1,&x,1,&y)==JOLT_OK && y==6);
    assert(jolt_registry_call(r,"missing",1,&x,1,&y)==JOLT_ERR_NOT_FOUND);
    assert(jolt_registry_call(r,"jolt.test.double",1,&x,0,&y)==JOLT_ERR_ARGUMENT);
    assert(jolt_registry_register(r,&d)==JOLT_ERR_ARGUMENT);
    jolt_registry_destroy(r);
    jolt_budget_t *b=jolt_budget_create(10); assert(b);
    assert(jolt_budget_acquire(b,8)==JOLT_OK);
    assert(jolt_budget_acquire(b,3)==JOLT_ERR_BUDGET && jolt_budget_used(b)==8);
    jolt_budget_release(b,8); assert(jolt_budget_used(b)==0); jolt_budget_destroy(b);
    jolt_program_t *p=NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))",&p,NULL)==JOLT_OK);
    size_t bytes,id; const uint8_t *code=jolt_program_data(p,&bytes);
    jolt_pipeline_t *pipe=jolt_pipeline_create(4096); assert(pipe);
    float gain=2, input[]={.1f,.2f,.3f,1}, out[4]={0};
    assert(jolt_pipeline_add(pipe,code,bytes,-1,&gain,1,&id)==JOLT_OK && id==0);
    assert(jolt_pipeline_add(pipe,code,bytes,0,&gain,1,&id)==JOLT_OK && id==1);
    assert(jolt_pipeline_run(pipe,input,1,1,out)==JOLT_OK && fabsf(out[0]-.4f)<1e-6f);
    assert(jolt_pipeline_run(pipe,input,SIZE_MAX,1,out)==JOLT_ERR_ARGUMENT);
    jolt_pipeline_destroy(pipe);
    pipe=jolt_pipeline_create(1);
    assert(jolt_pipeline_add(pipe,code,bytes,-1,&gain,1,&id)==JOLT_OK);
    assert(jolt_pipeline_run(pipe,input,1,0,out)==JOLT_ERR_BUDGET);
    jolt_pipeline_destroy(pipe);
    pipe=jolt_pipeline_create(4096);
    assert(jolt_pipeline_add(pipe,code,bytes,1,&gain,1,&id)==JOLT_OK);
    assert(jolt_pipeline_add(pipe,code,bytes,0,&gain,1,&id)==JOLT_OK);
    assert(jolt_pipeline_run(pipe,input,1,0,out)==JOLT_ERR_CYCLE);
    jolt_pipeline_destroy(pipe); jolt_program_destroy(p);
    return 0;
}
