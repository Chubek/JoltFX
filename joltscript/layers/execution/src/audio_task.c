#include "joltscript/audio_task.h"
#include "bytecode_internal.h"
#include "tilly/containers.h"
#include <math.h>
#include <limits.h>

jolt_status_t jolt_audio_accumulate(const uint8_t *code,size_t code_size,
    const float *src,size_t frames,float gain,float pan,int64_t reference_sample,
    uint64_t length,uint64_t fade_in,uint64_t fade_out,float *out) {
    if (!src || !out || !frames || frames>65536 || !isfinite(gain) || gain<0 || gain>256 ||
        !isfinite(pan) || pan<-1 || pan>1 || length>INT64_MAX || fade_in>INT64_MAX || fade_out>INT64_MAX ||
        reference_sample>INT64_MAX-(int64_t)frames) return JOLT_ERR_ARGUMENT;
    jolt_status_t status=jolt_bytecode_validate(code,code_size);
    if (status!=JOLT_OK) return status;
    if (jolt_read_u32(code+8)!=8 || jolt_read_u32(code+12)!=4) return JOLT_ERR_ARGUMENT;
    float *scratch=tilly_container_alloc(frames*2*sizeof(float));
    jolt_vm_t *vm=jolt_vm_create();
    if (!scratch || !vm) { status=JOLT_ERR_MEMORY; goto done; }
    for (size_t i=0;i<frames;++i) {
        int64_t position=reference_sample+(int64_t)i;
        /* Only ratios enter f32 bytecode: long clips keep integer sample clocks. */
        double head=fade_in?(double)position/(double)fade_in:1;
        double tail=fade_out?((double)length-(double)position)/(double)fade_out:1;
        float inputs[]={src[i*2],src[i*2+1],out[i*2],out[i*2+1],gain,pan,
            (float)fmax(0,fmin(1,head)),(float)fmax(0,fmin(1,tail))};
        float result[4];
        status=jolt_vm_run_prevalidated(vm,code,code_size,inputs,8,result,4);
        if (status!=JOLT_OK) goto done;
        scratch[i*2]=result[0]; scratch[i*2+1]=result[1];
    }
    memcpy(out,scratch,frames*2*sizeof(float));
done:
    jolt_vm_destroy(vm); tilly_container_free(scratch); return status;
}
