#include "joltscript/effects.h"
#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "tilly/containers.h"
#include <math.h>
#include "effect_sources.h"
static const struct {
    const char *name; size_t count; float default_value, min, max; const char *source;
} catalog[] = {
#include "effect_catalog.inc"
};
KHASH_MAP_INIT_STR(effects, jolt_program_t *)
struct jolt_effects { khash_t(effects) *programs; };
jolt_effects_t *jolt_effects_create(void) {
    jolt_effects_t *e=tilly_container_calloc(1,sizeof(*e));
    if (!e) return NULL;
    e->programs=kh_init(effects);
    if (!e->programs) { tilly_container_free(e); return NULL; }
    for (size_t i=0;i<sizeof(catalog)/sizeof(*catalog);++i) {
        jolt_program_t *p=NULL; int ret;
        if (jolt_compile(catalog[i].source,&p,NULL)!=JOLT_OK) { jolt_effects_destroy(e); return NULL; }
        khiter_t k=kh_put(effects,e->programs,catalog[i].name,&ret);
        if (ret<0) { jolt_program_destroy(p); jolt_effects_destroy(e); return NULL; }
        kh_value(e->programs,k)=p;
    }
    return e;
}
void jolt_effects_destroy(jolt_effects_t *e) {
    if (!e) return;
    for (khiter_t k=kh_begin(e->programs);k!=kh_end(e->programs);++k)
        if (kh_exist(e->programs,k)) jolt_program_destroy(kh_value(e->programs,k));
    kh_destroy(effects,e->programs); tilly_container_free(e);
}
size_t jolt_effects_count(const jolt_effects_t *e) { return e ? kh_size(e->programs) : 0; }
const char *jolt_effects_name(size_t i) { return i<sizeof(catalog)/sizeof(*catalog) ? catalog[i].name : NULL; }
jolt_status_t jolt_effects_apply(jolt_effects_t *e,const char *name,const float *in,size_t pixels,
    const float *params,size_t count,size_t limit,float *out) {
    if (!e || !name || !in || !out || !pixels || (count && !params)) return JOLT_ERR_ARGUMENT;
    khiter_t k=kh_get(effects,e->programs,name);
    if (k==kh_end(e->programs)) return JOLT_ERR_NOT_FOUND;
    size_t index=0;
    while (strcmp(catalog[index].name,name)) ++index;
    if (count && count!=catalog[index].count) return JOLT_ERR_ARGUMENT;
    float value=count ? params[0] : catalog[index].default_value;
    if (!isfinite(value)) return JOLT_ERR_NUMERIC;
    value=fminf(catalog[index].max,fmaxf(catalog[index].min,value));
    jolt_pipeline_t *pipeline=jolt_pipeline_create(limit);
    if (!pipeline) return JOLT_ERR_MEMORY;
    size_t size,id;
    const uint8_t *code=jolt_program_data(kh_value(e->programs,k),&size);
    jolt_status_t s=jolt_pipeline_add(pipeline,code,size,-1,&value,catalog[index].count,&id);
    if (s==JOLT_OK) s=jolt_pipeline_run(pipeline,in,pixels,id,out);
    jolt_pipeline_destroy(pipeline); return s;
}
