#include <joltfx/sdk/plugins.h>
#include <string.h>
static jfx_result_t process(void *userdata,const jfx_plugin_image_t *image) {
    (void)userdata;
    if (!image || image->size<sizeof(*image) || !image->input || !image->output ||
        image->parameter_count!=1 || !image->parameters) return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t i=0;i<(size_t)image->width*image->height*4;++i)
        image->output[i]=image->input[i]*(i%4==3?1.f:image->parameters[0]);
    return JFX_SUCCESS;
}
static jfx_result_t initialize(const jfx_plugin_api_t *api,void **out) {
    if (!api || api->size<sizeof(*api) || api->sdk_major!=JFX_PLUGIN_SDK_MAJOR || !out)
        return JFX_ERROR_VERSION_MISMATCH;
    *out=NULL;
    jfx_param_desc_t param={"gain","Gain",0,4,1,.01f,false};
    jfx_plugin_effect_desc_t effect={sizeof(effect),"org.example.gain","SDK Gain","Color Grading",1,&param,process,NULL};
    return api->register_effect(api->context,&effect);
}
JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(uint32_t major,uint32_t minor,jfx_plugin_definition_t *out) {
    (void)minor;
    if (!out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    if (major!=JFX_PLUGIN_SDK_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->sdk_major=major;
    out->info=(jfx_plugin_desc_t){sizeof(out->info),"org.example.gain","SDK Gain","JoltFX",JFX_PLUGIN_VERSION(1,0,0),JFX_PLUGIN_API_VERSION,JFX_PLUGIN_CAP_KERNELS};
    out->initialize=initialize; return JFX_SUCCESS;
}
