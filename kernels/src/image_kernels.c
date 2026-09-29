#include "joltscript/image_kernels.h"
#include "tilly/containers.h"
#include <string.h>
#include "image_sources.h"
#define IMAGE_COUNT (sizeof(image_catalog)/sizeof(*image_catalog))
struct jolt_image_kernels { jolt_image_program_t *programs[IMAGE_COUNT]; };
static size_t find_image(const char *name) {
    if(name) for(size_t i=0;i<IMAGE_COUNT;++i) if(!strcmp(name,image_catalog[i].name)) return i;
    return IMAGE_COUNT;
}
jolt_image_kernels_t *jolt_image_kernels_create(jolt_diagnostic_t *d) {
    jolt_image_kernels_t *k=tilly_container_calloc(1,sizeof(*k));
    if(!k) return NULL;
    for(size_t i=0;i<IMAGE_COUNT;++i) {
        if(jolt_image_compile(image_source_image,image_catalog[i].source,&k->programs[i],d)!=JOLT_OK) {
            jolt_image_kernels_destroy(k); return NULL;
        }
    }
    return k;
}
void jolt_image_kernels_destroy(jolt_image_kernels_t *k) {
    if(!k) return;
    for(size_t i=0;i<IMAGE_COUNT;++i) jolt_image_program_destroy(k->programs[i]);
    tilly_container_free(k);
}
size_t jolt_image_kernels_count(void) { return IMAGE_COUNT; }
const char *jolt_image_kernels_name(size_t i) { return i<IMAGE_COUNT ? image_catalog[i].name : NULL; }
size_t jolt_image_kernels_parameter_count(const jolt_image_kernels_t *k,const char *name) {
    size_t i=find_image(name);
    return k && i<IMAGE_COUNT ? jolt_image_parameter_count(k->programs[i]) : 0;
}
const jolt_image_parameter_info_t *jolt_image_kernels_parameter_info(const jolt_image_kernels_t *k,const char *name,size_t index) {
    size_t i=find_image(name);
    return k && i<IMAGE_COUNT ? jolt_image_parameter_info(k->programs[i],index) : NULL;
}
jolt_status_t jolt_image_kernels_apply(const jolt_image_kernels_t *k,const char *name,
    const float *src,size_t width,size_t height,const jolt_image_parameter_t *parameters,
    size_t parameter_count,const float *data,size_t data_count,size_t memory_limit,size_t step_limit,float *dst) {
    if(!k || !name) return JOLT_ERR_ARGUMENT;
    size_t i=find_image(name); if(i==IMAGE_COUNT) return JOLT_ERR_NOT_FOUND;
    return jolt_image_program_run(k->programs[i],src,width,height,parameters,parameter_count,data,data_count,memory_limit,step_limit,dst);
}
