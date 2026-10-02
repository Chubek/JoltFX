#include "joltscript/image_task.h"
#include "joltscript/pipeline.h"
#include "tilly/containers.h"
#include <math.h>
#include <string.h>

jolt_status_t jolt_image_task_run(jolt_image_task_fn task, void *user,
    const float *src, size_t w, size_t h, size_t limit, float *out) {
    if (!task || !src || !out || !w || !h || w>SIZE_MAX/h/4/sizeof(float))
        return JOLT_ERR_ARGUMENT;
    size_t count=w*h*4, bytes=count*sizeof(float);
    if (bytes>limit) return JOLT_ERR_BUDGET;
    for (size_t i=0;i<count;++i) if (!isfinite(src[i])) return JOLT_ERR_NUMERIC;
    jolt_budget_t *budget=jolt_budget_create(limit);
    if (!budget) return JOLT_ERR_MEMORY;
    jolt_status_t status=jolt_budget_acquire(budget,bytes);
    float *tmp=NULL;
    if (status==JOLT_OK) {
        tmp=tilly_container_alloc(bytes);
        if (!tmp) status=JOLT_ERR_MEMORY;
        else {
            status=task(user,src,w,h,limit-bytes,tmp);
            if (status==JOLT_OK) {
                for (size_t i=0;i<count;++i) if (!isfinite(tmp[i])) { status=JOLT_ERR_NUMERIC; break; }
                if (status==JOLT_OK) memcpy(out,tmp,bytes);
            }
        }
        jolt_budget_release(budget,bytes);
    }
    tilly_container_free(tmp); jolt_budget_destroy(budget);
    return status;
}
