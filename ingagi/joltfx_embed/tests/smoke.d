import ingagi_module;
import core.stdc.string : strlen;
int main() {
    ingagi_handle handle;
    char[4096] error;
    assert(ingagi_abi_version()==INGAGI_ABI_VERSION);
    assert(ingagi_create(1000,&handle,error.ptr,error.length)==INGAGI_OK);
    scope(exit) ingagi_destroy(handle,null,0);
    immutable source="fn main(x: i64) { return x + 1; }";
    assert(ingagi_load(handle,source.ptr,source.length,error.ptr,error.length)==INGAGI_OK);
    ingagi_value arg=ingagi_value(INGAGI_INT,0,41,0), result;
    assert(ingagi_call(handle,"main".ptr,4,&arg,1,&result,error.ptr,error.length)==INGAGI_OK);
    assert(result.integer==42);
    return 0;
}
