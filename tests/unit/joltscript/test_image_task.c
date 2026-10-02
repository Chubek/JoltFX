#include "joltscript/image_task.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct { jolt_status_t status; float value; size_t budget; unsigned calls; } task_t;
static jolt_status_t process(void *user,const float *src,size_t w,size_t h,size_t limit,float *out) {
    task_t *task=user; ++task->calls; task->budget=limit;
    memcpy(out,src,w*h*4*sizeof(float)); out[0]=task->value;
    return task->status;
}
int main(void) {
    float src[4]={.1f,.2f,.3f,1}, out[4]={9,9,9,9};
    task_t task={JOLT_OK,.5f,0,0};
    assert(jolt_image_task_run(process,&task,src,1,1,100,out)==JOLT_OK);
    assert(out[0]==.5f && out[3]==1 && task.budget==100-sizeof(src));
    task.status=JOLT_ERR_BUDGET;
    assert(jolt_image_task_run(process,&task,src,1,1,100,src)==JOLT_ERR_BUDGET);
    assert(src[0]==.1f && src[3]==1);
    task.status=JOLT_OK; task.value=NAN;
    assert(jolt_image_task_run(process,&task,src,1,1,100,out)==JOLT_ERR_NUMERIC);
    assert(out[0]==.5f);
    unsigned calls=task.calls;
    assert(jolt_image_task_run(process,&task,src,1,1,1,out)==JOLT_ERR_BUDGET);
    src[0]=INFINITY;
    assert(jolt_image_task_run(process,&task,src,1,1,100,out)==JOLT_ERR_NUMERIC);
    assert(jolt_image_task_run(process,&task,src,SIZE_MAX,2,SIZE_MAX,out)==JOLT_ERR_ARGUMENT);
    assert(jolt_image_task_run(NULL,&task,src,1,1,100,out)==JOLT_ERR_ARGUMENT);
    assert(jolt_image_task_run(process,&task,NULL,1,1,100,out)==JOLT_ERR_ARGUMENT);
    assert(task.calls==calls && out[0]==.5f);
    return 0;
}
