#include "joltscript/pipeline.h"
#include "bytecode_internal.h"
#include "tilly/containers.h"
#include <math.h>
struct jolt_budget { size_t limit, used; };
jolt_budget_t *jolt_budget_create(size_t limit) {
    jolt_budget_t *b=tilly_container_calloc(1,sizeof(*b));
    if (b) b->limit=limit;
    return b;
}
void jolt_budget_destroy(jolt_budget_t *b) { tilly_container_free(b); }
jolt_status_t jolt_budget_acquire(jolt_budget_t *b,size_t n) {
    if (!b) return JOLT_ERR_ARGUMENT;
    if (n>b->limit-b->used) return JOLT_ERR_BUDGET;
    b->used+=n; return JOLT_OK;
}
void jolt_budget_release(jolt_budget_t *b,size_t n) { if (b && n<=b->used) b->used-=n; }
size_t jolt_budget_used(const jolt_budget_t *b) { return b ? b->used : 0; }
typedef struct {
    uint8_t *code; size_t size, count; int predecessor;
    float parameters[JOLT_MAX_INPUTS-4];
} stage_t;
struct jolt_pipeline { kvec_t(stage_t) stages; jolt_budget_t *budget; };
jolt_pipeline_t *jolt_pipeline_create(size_t limit) {
    jolt_pipeline_t *p=tilly_container_calloc(1,sizeof(*p));
    if (p && !(p->budget=jolt_budget_create(limit))) { tilly_container_free(p); return NULL; }
    return p;
}
void jolt_pipeline_destroy(jolt_pipeline_t *p) {
    if (!p) return;
    for (size_t i=0;i<p->stages.n;++i) tilly_container_free(p->stages.a[i].code);
    tilly_vec_destroy(p->stages); jolt_budget_destroy(p->budget); tilly_container_free(p);
}
jolt_status_t jolt_pipeline_add(jolt_pipeline_t *p,const uint8_t *code,size_t size,
    int pred,const float *parameters,size_t count,size_t *out) {
    if (!p || !out || pred < -1 || count>JOLT_MAX_INPUTS-4 || (count && !parameters)) return JOLT_ERR_ARGUMENT;
    jolt_status_t s=jolt_bytecode_validate(code,size); if (s!=JOLT_OK) return s;
    if (jolt_read_u32(code+8)!=4+count || jolt_read_u32(code+12)!=4) return JOLT_ERR_ARGUMENT;
    for (size_t i=0;i<count;++i) if (!isfinite(parameters[i])) return JOLT_ERR_NUMERIC;
    if (p->stages.n>=1024) return JOLT_ERR_BUDGET;
    if (!tilly_vec_reserve(&p->stages,p->stages.n+1)) return JOLT_ERR_MEMORY;
    uint8_t *copy=tilly_container_alloc(size); if (!copy) return JOLT_ERR_MEMORY;
    memcpy(copy,code,size);
    stage_t stage={.code=copy,.size=size,.count=count,.predecessor=pred};
    if (count) memcpy(stage.parameters,parameters,count*sizeof(float));
    *out=p->stages.n; p->stages.a[p->stages.n++]=stage; return JOLT_OK;
}
jolt_status_t jolt_pipeline_run(jolt_pipeline_t *p,const float *rgba,size_t pixels,
    size_t output,float *out) {
    if (!p || !rgba || !out || !pixels || pixels>SIZE_MAX/(4*sizeof(float)) || output>=p->stages.n)
        return JOLT_ERR_ARGUMENT;
    /* One-input DAG: follow each chain, emitting dependencies before users. */
    unsigned char state[1024]={0}; size_t order[1024], chain[1024], n=0;
    for (size_t i=0;i<p->stages.n;++i) {
        size_t node=i, length=0;
        for (;;) {
            if (state[node]==2) break;
            if (state[node]==1) return JOLT_ERR_CYCLE;
            state[node]=1; chain[length++]=node;
            int pred=p->stages.a[node].predecessor;
            if (pred==-1) break;
            if ((size_t)pred>=p->stages.n) return JOLT_ERR_ARGUMENT;
            node=(size_t)pred;
        }
        while (length) { node=chain[--length]; state[node]=2; order[n++]=node; }
    }
    size_t frame=pixels*4*sizeof(float);
    if (p->stages.n>SIZE_MAX/frame) return JOLT_ERR_BUDGET;
    size_t bytes=frame*p->stages.n;
    jolt_status_t s=jolt_budget_acquire(p->budget,bytes); if (s!=JOLT_OK) return s;
    float *storage=tilly_container_alloc(bytes);
    jolt_vm_t *vm=jolt_vm_create();
    if (!storage || !vm) { s=JOLT_ERR_MEMORY; goto done; }
    for (size_t j=0;j<n;++j) {
        size_t id=order[j]; stage_t *stage=&p->stages.a[id];
        const float *src=stage->predecessor<0 ? rgba : storage+(size_t)stage->predecessor*pixels*4;
        float args[JOLT_MAX_INPUTS];
        memcpy(args+4,stage->parameters,stage->count*sizeof(float));
        for (size_t i=0;i<pixels;++i) {
            memcpy(args,src+i*4,4*sizeof(float));
            s=jolt_vm_run_prevalidated(vm,stage->code,stage->size,args,4+stage->count,
                storage+(id*pixels+i)*4,4);
            if (s!=JOLT_OK) goto done;
        }
    }
    memcpy(out,storage+output*pixels*4,frame);
done:
    jolt_vm_destroy(vm); tilly_container_free(storage); jolt_budget_release(p->budget,bytes); return s;
}
