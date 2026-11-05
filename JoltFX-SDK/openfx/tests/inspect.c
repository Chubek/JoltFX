#include <joltfx/sdk/openfx.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Test-only allocator; production hosts provide their engine allocator. */
static size_t live, calls, fail_at;
static jfx_ofx_host_t *active;
static unsigned logs;
static void log_message(void *context,const char *message) {
    (void)context;
    assert(!strcmp(message,"fixture 42")); ++logs;
    if (active) {
        jfx_ofx_plugin_info_t info={0}; info.size=sizeof(info);
        assert(jfx_ofx_host_close(active)==JFX_ERROR_BUSY);
        assert(jfx_ofx_host_plugin_info(active,0,&info)==JFX_ERROR_BUSY);
    }
}
static void *allocate(void *context,size_t bytes,size_t alignment) {
    (void)context; (void)alignment;
    if (++calls==fail_at) return NULL;
    void *p=malloc(bytes);
    if (p) ++live;
    return p;
}
static void release(void *context,void *p) { (void)context; if (p) { --live; free(p); } }
int main(int argc,char **argv) {
    assert(argc==2);
    jfx_ofx_services_t services={sizeof(services),NULL,allocate,release,log_message};
    jfx_ofx_host_t *host=NULL;
    assert(jfx_ofx_host_open(NULL,&services,&host)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_host_open("/missing/joltfx.ofx",&services,&host)==JFX_ERROR_NOT_FOUND && !host);
    assert(jfx_ofx_host_close(NULL)==JFX_SUCCESS);
    assert(jfx_ofx_host_open(argv[1],&services,&host)==JFX_SUCCESS);
    assert(jfx_ofx_host_plugin_count(host)==2);
    jfx_ofx_host_t *duplicate=NULL;
    assert(jfx_ofx_host_open(argv[1],&services,&duplicate)==JFX_ERROR_BUSY && !duplicate);
    active=host;
    jfx_ofx_plugin_info_t info={0};
    assert(jfx_ofx_host_plugin_info(host,0,&info)==JFX_ERROR_INVALID_ARGUMENT);
    info.size=sizeof(info);
    assert(jfx_ofx_host_plugin_info(host,2,&info)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_host_plugin_info(host,0,&info)==JFX_SUCCESS);
    assert(!strcmp(info.identifier,"org.joltfx.inspection"));
    assert(!strcmp(info.label,"SDK inspection fixture"));
    assert(!strcmp(info.grouping,"JoltFX/Tests"));
    assert(info.version_major==2 && info.version_minor==3 && info.supports_filter);
    assert(jfx_ofx_host_plugin_info(host,0,&info)==JFX_SUCCESS);
    assert(logs==1); /* cached inspection does not rerun Describe */
    jfx_ofx_plugin_info_t saved=info;
    assert(jfx_ofx_host_plugin_info(host,1,&info)==JFX_ERROR_NOT_IMPLEMENTED);
    assert(!memcmp(&saved,&info,sizeof(info)));
    assert(strstr(jfx_ofx_host_error(host),"OfxActionDescribe"));
    assert(jfx_ofx_host_plugin_info(host,1,&info)==JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_ofx_host_close(host)==JFX_SUCCESS && !live);
    active=NULL;
    /* Fail every allocation in a complete open/describe/close cycle. */
    size_t total=calls;
    for (size_t n=1;n<=total+1;++n) {
        calls=0; fail_at=n; host=NULL;
        jfx_result_t r=jfx_ofx_host_open(argv[1],&services,&host);
        if (r==JFX_SUCCESS) {
            info.size=sizeof(info);
            r=jfx_ofx_host_plugin_info(host,0,&info);
            assert(r==JFX_SUCCESS || r==JFX_ERROR_OUT_OF_MEMORY);
            if (r!=JFX_SUCCESS) {
                fail_at=0;
                assert(jfx_ofx_host_plugin_info(host,0,&info)==JFX_SUCCESS);
            }
            assert(jfx_ofx_host_close(host)==JFX_SUCCESS);
        } else assert(r==JFX_ERROR_OUT_OF_MEMORY && !host);
        assert(!live);
    }
    return 0;
}
