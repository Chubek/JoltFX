#include "jfx/jfx_color.h"
#include "joltscript/image_kernels.h"
#include "joltscript/image_task.h"
#include "tilly/containers.h"
#include "tilly/attributes.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const jfx_port_desc_t color_in[]={{"in","Image",JFX_PORT_IMAGE,true,{0,0,0,0}}};
static const jfx_port_desc_t color_out[]={{"out","Image",JFX_PORT_IMAGE,false,{0,0,0,0}}};
static const char *const color_path[]={"LUT path"};
#include "color_nodes.inc"
size_t jfx_color_kind_count(void) { return sizeof(color_kinds)/sizeof(*color_kinds); }
const jfx_node_kind_t *jfx_color_kind_at(size_t i) { return i<jfx_color_kind_count()?color_kinds+i:NULL; }
jfx_color_section_t jfx_color_section(const jfx_node_kind_t *k) {
    if (!k || !k->category) return JFX_COLOR_NONE;
    if (!strcmp(k->category,"Color Calibration")) return JFX_COLOR_CALIBRATION;
    if (!strcmp(k->category,"Color Grading") || !strcmp(k->category,"Color")) return JFX_COLOR_GRADING;
    return JFX_COLOR_NONE;
}
typedef struct {
    jolt_image_program_t *program;
    jolt_image_parameter_t params[JFX_NODE_MAX_PARAMS];
    size_t count;
    float *data;
    size_t data_count;
} color_task_t;

static jolt_status_t run_color(void *user,const float *src,size_t w,size_t h,size_t limit,float *out) {
    color_task_t *task=user;
    size_t bytes=w*h*4*sizeof(float);
    if (bytes>limit || w*h>SIZE_MAX/10000) return JOLT_ERR_BUDGET;
    float *premul=tilly_container_alloc(bytes);
    if (!premul) return JOLT_ERR_MEMORY;
    for (size_t i=0;i<w*h;++i) {
        float alpha=src[i*4+3];
        if (alpha<0 || alpha>1) { tilly_container_free(premul); return JOLT_ERR_ARGUMENT; }
        for (size_t c=0;c<3;++c) premul[i*4+c]=src[i*4+c]*alpha;
        premul[i*4+3]=alpha;
    }
    jolt_status_t s=jolt_image_program_run(task->program,premul,w,h,task->params,task->count,
        task->data,task->data_count,limit-bytes,w*h*10000,out);
    if (s==JOLT_OK) for (size_t i=0;i<w*h;++i) {
        float alpha=out[i*4+3];
        for (size_t c=0;c<3;++c) out[i*4+c]=alpha>0?out[i*4+c]/alpha:src[i*4+c];
    }
    tilly_container_free(premul); return s;
}

static jfx_result_t pack_lut(const jfx_lut_t *lut,color_task_t *task) {
    if (!lut) return JFX_SUCCESS;
    if (!lut->entries || lut->width<2 || lut->width>JFX_LUT_MAX_DIMENSION ||
        !lut->channels || lut->channels>4 ||
        (lut->shape!=JFX_LUT_SHAPE_1D && lut->shape!=JFX_LUT_SHAPE_3D)) return JFX_ERROR_INVALID_ARGUMENT;
    size_t n=lut->width;
    if ((lut->shape==JFX_LUT_SHAPE_1D && (lut->height!=1 || lut->depth!=1)) ||
        (lut->shape==JFX_LUT_SHAPE_3D && (lut->height!=n || lut->depth!=n))) return JFX_ERROR_INVALID_ARGUMENT;
    size_t points=lut->shape==JFX_LUT_SHAPE_1D?n:n*n*n;
    if (lut->entry_count!=points*lut->channels) return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t c=0;c<3;++c) if (!isfinite(lut->domain_min[c]) || !isfinite(lut->domain_max[c]) ||
        lut->domain_max[c]<=lut->domain_min[c]) return JFX_ERROR_INVALID_ARGUMENT;
    task->data_count=8+points*3;
    task->data=tilly_container_alloc(task->data_count*sizeof(float));
    if (!task->data) return JFX_ERROR_OUT_OF_MEMORY;
    task->data[0]=lut->shape==JFX_LUT_SHAPE_1D?1:3; task->data[1]=(float)n;
    memcpy(task->data+2,lut->domain_min,3*sizeof(float));
    memcpy(task->data+5,lut->domain_max,3*sizeof(float));
    for (size_t i=0;i<points;++i) for (size_t c=0;c<3;++c) {
        float v=lut->entries[i*lut->channels+(c<lut->channels?c:0)];
        if (!isfinite(v)) return JFX_ERROR_INVALID_ARGUMENT;
        task->data[8+i*3+c]=v;
    }
    return JFX_SUCCESS;
}
jfx_result_t jfx_color_apply(const jfx_node_kind_t *kind,const jfx_node_value_t *v,
    const jfx_lut_t *lut,const float *src,size_t w,size_t h,float *out) {
    if (!kind || !v || !src || !out || !w || !h || w>4096 || h>4096) return JFX_ERROR_INVALID_ARGUMENT;
    const jfx_node_kind_t *k=NULL;
    for (size_t i=0;i<jfx_color_kind_count();++i) if (kind==color_kinds+i) k=kind;
    if (!k) return JFX_ERROR_INVALID_ARGUMENT;
    color_task_t task={0}; task.count=k->param_count;
    for (size_t i=0;i<task.count;++i) {
        const jfx_param_desc_t *p=k->params+i; float val=v->scalars[i];
        if (!isfinite(val) || val<p->minimum || val>p->maximum || (p->integral && floorf(val)!=val))
            return JFX_ERROR_INVALID_ARGUMENT;
        task.params[i]=(jolt_image_parameter_t){p->name,(double)val};
    }
    jfx_result_t result=pack_lut(lut,&task);
    if (result==JFX_SUCCESS) {
        jolt_status_t s=jolt_image_kernel_compile(k->name,&task.program,NULL);
        size_t budget=512u*1024u*1024u;
        size_t resource=task.data_count*sizeof(float);
        if (s==JOLT_OK) s=resource>budget?JOLT_ERR_BUDGET:
            jolt_image_task_run(run_color,&task,src,w,h,budget-resource,out);
        result=s==JOLT_OK?JFX_SUCCESS:(s==JOLT_ERR_MEMORY || s==JOLT_ERR_BUDGET)?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_INVALID_ARGUMENT;
    }
    jolt_image_program_destroy(task.program); tilly_container_free(task.data); return result;
}

TILLY_PRINTF_LIKE(4, 5)
static int append(char *out,size_t cap,size_t *pos,const char *fmt,...) {
    if (*pos>=cap) return 0;
    va_list args; va_start(args,fmt);
    int n=vsnprintf(out+*pos,cap-*pos,fmt,args); va_end(args);
    if (n<0 || (size_t)n>=cap-*pos) return 0;
    *pos+=(size_t)n; return 1;
}
jfx_result_t jfx_color_catalog(char *out,size_t cap) {
    if (!out || !cap) return JFX_ERROR_INVALID_ARGUMENT;
    size_t pos=0;
#define APP(...) do { if (!append(out,cap,&pos,__VA_ARGS__)) return JFX_ERROR_OUT_OF_MEMORY; } while (0)
    APP("[");
    size_t emitted=0;
    for (size_t i=0;i<jfx_node_kind_count();++i) {
        const jfx_node_kind_t *k=jfx_node_kind_at(i);
        if (jfx_color_section(k)==JFX_COLOR_NONE) continue;
        APP("%s{\"name\":\"%s\",\"label\":\"%s\",\"section\":\"%s\",\"path\":%s,\"params\":[",emitted++?",":"",k->name,k->label,
            jfx_color_section(k)==JFX_COLOR_CALIBRATION?"calibration":"grade",k->string_count?"true":"false");
        for (size_t p=0;p<k->param_count;++p) {
            const jfx_param_desc_t *d=k->params+p;
            APP("%s{\"name\":\"%s\",\"label\":\"%s\",\"min\":%.9g,\"max\":%.9g,\"default\":%.9g,\"integer\":%s}",
                p?",":"",d->name,d->label,(double)d->minimum,(double)d->maximum,(double)d->default_value,d->integral?"true":"false");
        }
        APP("]}");
    }
    APP("]");
#undef APP
    return JFX_SUCCESS;
}
