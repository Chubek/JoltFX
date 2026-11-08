#include "jfx/jfx_nle.h"
#include "joltscript/image_kernels.h"
#include "tilly/memory.h"
#include <math.h>
#include <string.h>
jfx_result_t jfx_nle_transition(const char *kernel,const uint8_t *from,const uint8_t *to,
    uint32_t w,uint32_t h,double progress,uint8_t *out,size_t cap) {
    if (!kernel || (strcmp(kernel,"morph_cut") && strcmp(kernel,"cross_dissolve")) ||
        !from || !to || !out || !w || !h || w>4096 || h>4096 || cap<(size_t)w*h*4 ||
        !isfinite(progress) || progress<0 || progress>1) return JFX_ERROR_INVALID_ARGUMENT;
    size_t count=(size_t)w*h*4;
    if (progress==0 || progress==1) { memmove(out,progress==0?from:to,count); return JFX_SUCCESS; }
    /* 3 owned frames plus the interpreter's 2 scratch frames, bounded to 512 MiB. */
    if (count>512u*1024u*1024u/(5*sizeof(float))) return JFX_ERROR_OUT_OF_MEMORY;
    float *frames=tilly_mem_alloc(count*3*sizeof(float));
    if (!frames) return JFX_ERROR_OUT_OF_MEMORY;
    for (size_t i=0;i<count;++i) {
        frames[i]=(float)from[i]/255*(i%4==3?1:(float)from[i-i%4+3]/255);
        frames[count+i]=(float)to[i]/255*(i%4==3?1:(float)to[i-i%4+3]/255);
    }
    jolt_image_program_t *program=NULL;
    jolt_status_t s=jolt_image_kernel_compile(kernel,&program,NULL);
    jolt_image_parameter_t parameter={"progress",progress};
    if (s==JOLT_OK) s=jolt_image_program_run(program,frames,w,h,&parameter,1,frames+count,count,
        512u*1024u*1024u-count*3*sizeof(float),1000000000u,frames+count*2);
    if (s==JOLT_OK) for (size_t i=0;i<count;++i) {
        float a=frames[count*2+i-i%4+3],v=frames[count*2+i];
        if (i%4!=3) v=a>0?v/a:0;
        out[i]=(uint8_t)lrintf(fmaxf(0,fminf(1,v))*255);
    }
    jolt_image_program_destroy(program); tilly_mem_free(frames);
    return s==JOLT_OK?JFX_SUCCESS:s==JOLT_ERR_MEMORY || s==JOLT_ERR_BUDGET?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_BACKEND_FAILURE;
}
