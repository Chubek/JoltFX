
/* khash's KHASH_MAP_INIT_STR declares its bucket array with khint32_t sizes
 * while the macros that follow pass size_t, so instantiating it trips this
 * project's -Wconversion. Upstream is vendored and not ours to change, and the
 * narrowing is contained inside the macro, so the diagnostic is suppressed for
 * this translation unit only rather than for the whole project. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "joltscript/bindings.h"
#include "tilly/containers.h"
#include <math.h>
#include <pthread.h>
KHASH_MAP_INIT_STR(bindings, jolt_binding_desc_t)
struct jolt_registry { khash_t(bindings) *entries; bool frozen; };
jolt_registry_t *jolt_registry_create(void) {
    jolt_registry_t *r = tilly_container_calloc(1,sizeof(*r));
    if (r && !(r->entries=kh_init(bindings))) { tilly_container_free(r); return NULL; }
    return r;
}
void jolt_registry_destroy(jolt_registry_t *r) {
    if (!r) return;
    for (khiter_t k=kh_begin(r->entries);k!=kh_end(r->entries);++k)
        if (kh_exist(r->entries,k)) tilly_container_free((void *)kh_key(r->entries,k));
    kh_destroy(bindings,r->entries); tilly_container_free(r);
}
jolt_status_t jolt_registry_register(jolt_registry_t *r, const jolt_binding_desc_t *d) {
    if (!r || !d || d->size != sizeof(*d) || !d->symbol || !d->function ||
        d->argument_count > JOLT_MAX_INPUTS || strncmp(d->symbol,"jolt.",5) || !d->symbol[5] || r->frozen)
        return JOLT_ERR_ARGUMENT;
    if (kh_get(bindings,r->entries,d->symbol) != kh_end(r->entries)) return JOLT_ERR_DUPLICATE;
    char *name = tilly_container_alloc(strlen(d->symbol)+1);
    if (!name) return JOLT_ERR_MEMORY;
    strcpy(name,d->symbol); int ret;
    khiter_t k=kh_put(bindings,r->entries,name,&ret);
    if (ret < 0) { tilly_container_free(name); return JOLT_ERR_MEMORY; }
    kh_value(r->entries,k)=*d; kh_value(r->entries,k).symbol=name;
    return JOLT_OK;
}
jolt_status_t jolt_registry_freeze(jolt_registry_t *r) {
    if (!r) return JOLT_ERR_ARGUMENT;
    r->frozen=true; return JOLT_OK;
}
jolt_status_t jolt_registry_call(const jolt_registry_t *r, const char *name,
    uint64_t caps, const float *args, size_t count, float *out) {
    if (!r || !r->frozen || !name || !out || (count && !args)) return JOLT_ERR_ARGUMENT;
    khiter_t k=kh_get(bindings,r->entries,name);
    if (k==kh_end(r->entries)) return JOLT_ERR_NOT_FOUND;
    const jolt_binding_desc_t *d=&kh_value(r->entries,k);
    if ((caps & d->required_capabilities) != d->required_capabilities) return JOLT_ERR_CAPABILITY;
    if (count!=d->argument_count) return JOLT_ERR_ARGUMENT;
    for (size_t i=0;i<count;++i) if (!isfinite(args[i])) return JOLT_ERR_NUMERIC;
    float result=0;
    jolt_status_t s=d->function(args,count,&result,d->userdata);
    if (s==JOLT_OK && !isfinite(result)) s=JOLT_ERR_NUMERIC;
    if (s==JOLT_OK) *out=result;
    return s;
}
static jolt_status_t clamp_binding(const float *args, size_t count, float *out, void *userdata) {
    (void)count; (void)userdata;
    if (args[1]>args[2]) return JOLT_ERR_ARGUMENT;
    *out=fminf(fmaxf(args[0],args[1]),args[2]); return JOLT_OK;
}
static jolt_registry_t *core_registry;
static pthread_once_t core_once=PTHREAD_ONCE_INIT;
static void init_core(void) {
    jolt_registry_t *r=jolt_registry_create();
    jolt_binding_desc_t d={sizeof(d),"jolt.core.math.clamp",3,JOLT_CAP_COMPUTE,clamp_binding,NULL};
    if (!r) return;
    if (jolt_registry_register(r,&d)!=JOLT_OK) { jolt_registry_destroy(r); return; }
    jolt_registry_freeze(r); core_registry=r;
}
void jolt_register_core_bindings(void) { (void)pthread_once(&core_once,init_core); }
const jolt_registry_t *jolt_core_bindings(void) { jolt_register_core_bindings(); return core_registry; }
