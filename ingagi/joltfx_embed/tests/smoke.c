#include "Ingagi-Module.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ingagi_handle active;
static ingagi_status twice(void *data, const ingagi_value *args, uint32_t argc,
    ingagi_value *out, char *error, size_t capacity) {
    (void)error; (void)capacity;
    assert(argc==1 && args[0].kind==INGAGI_INT);
    /* Reentry must report Busy, not deadlock or destroy this activation. */
    assert(ingagi_destroy(active, NULL, 0)==INGAGI_BUSY);
    *out=(ingagi_value){INGAGI_INT,0,args[0].integer * *(int*)data,0};
    return INGAGI_OK;
}
int main(void) {
    assert(ingagi_abi_version()==INGAGI_ABI_VERSION);
    char error[4096];
    assert(ingagi_create(1000,&active,error,sizeof(error))==INGAGI_OK);
    int factor=2;
    ingagi_function fn={"twice",5,1,0,twice,&factor};
    ingagi_module module={INGAGI_ABI_VERSION,1,&fn};
    assert(ingagi_register_module(active,&module,error,sizeof(error))==INGAGI_OK);
    assert(ingagi_register_module(active,&module,error,sizeof(error))==INGAGI_INVALID_ARGUMENT);
    const char *source="fn main(x: i64) { return twice(x); }";
    assert(ingagi_load(active,source,strlen(source),error,sizeof(error))==INGAGI_OK);
    ingagi_value arg={INGAGI_INT,0,21,0}, result={0};
    assert(ingagi_call(active,"main",4,&arg,1,&result,error,sizeof(error))==INGAGI_OK);
    assert(result.kind==INGAGI_INT && result.integer==42);
    assert(error[0]==0);
    arg.reserved=1;
    assert(ingagi_call(active,"main",4,&arg,1,&result,error,sizeof(error))==INGAGI_INVALID_ARGUMENT);
    char small[2]={'X','X'};
    assert(ingagi_load(active,NULL,1,small,sizeof(small))==INGAGI_INVALID_ARGUMENT);
    assert(small[1]==0);
    assert(ingagi_call(active,"main",4,NULL,257,&result,error,sizeof(error))==INGAGI_INVALID_ARGUMENT);
    assert(ingagi_load(active,"fn {",4,error,sizeof(error))==INGAGI_COMPILE_ERROR);
    arg.reserved=0;
    assert(ingagi_call(active,"main",4,&arg,1,&result,error,sizeof(error))==INGAGI_OK && result.integer==42);
    assert(ingagi_destroy(active,error,sizeof(error))==INGAGI_OK);
    assert(ingagi_destroy(active,error,sizeof(error))==INGAGI_INVALID_HANDLE);
    puts("C ABI callback/reentry/reload/error checks passed");
    return 0;
}
