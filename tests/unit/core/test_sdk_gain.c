#include "jfx/jfx_plugin_sdk.h"
#include "jfx/jfx_color.h"
#include "jfx_test_backend.h"
#include <assert.h>
#include <string.h>

int main(int argc,char **argv) {
    assert(argc==2);
    jfx_engine_config_t config={.backend_name=jfx_test_backend()};
    jfx_engine_t *engine=NULL;
    assert(jfx_engine_init(&config,&engine)==JFX_SUCCESS);
    jfx_plugin_host_t *host=NULL;
    assert(jfx_plugin_host_create(engine,&host)==JFX_SUCCESS);
    uint32_t id=0;
    assert(jfx_plugin_host_load(host,argv[1],&id)==JFX_SUCCESS && id);
    jfx_plugin_info_t info={.size=sizeof(info)};
    assert(jfx_plugin_host_get_info(host,id,&info)==JFX_SUCCESS);
    assert(!strcmp(info.identifier,"org.example.gain"));
    const jfx_node_kind_t *kind=jfx_node_kind_find("org.example.gain");
    assert(kind && kind->param_count==1);
    jfx_node_value_t value;
    jfx_node_value_init(&value,kind); value.scalars[0]=2;
    const float input[]={.125f,.25f,.5f,.5f}; float output[4];
    assert(jfx_color_apply(kind,&value,NULL,input,1,1,output)==JFX_SUCCESS);
    assert(output[0]==.25f && output[1]==.5f && output[2]==1 && output[3]==.5f);
    jfx_node_value_release(&value);
    assert(jfx_plugin_host_unload(host,id)==JFX_SUCCESS);
    assert(!jfx_node_kind_find("org.example.gain"));
    jfx_plugin_host_destroy(host); jfx_engine_shutdown(engine);
}
