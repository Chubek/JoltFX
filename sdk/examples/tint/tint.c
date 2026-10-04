#include "jfx/jfx_plugin_sdk.h"
#include <string.h>

typedef struct { const jfx_plugin_api_t *api; uint32_t frames; } tint_state_t;
static jfx_result_t tint(void *userdata,const jfx_plugin_image_t *image) {
    (void)userdata;
    float amount=image->parameters[0];
    for (size_t p=0;p<(size_t)image->width*image->height;++p) {
        const float *in=image->input+p*4; float *out=image->output+p*4;
        out[0]=in[0]+(1-in[0])*amount;
        out[1]=in[1]*(1-amount*.25f); out[2]=in[2]*(1-amount*.5f); out[3]=in[3];
    }
    return JFX_SUCCESS;
}
static void frame_event(jfx_event_type_t type,void *data,void *userdata) {
    (void)type; (void)data; ++((tint_state_t *)userdata)->frames;
}
static jfx_result_t apply(void *userdata,const jfx_plugin_action_context_t *selection) {
    tint_state_t *state=userdata;
    return state->api->editor_command(selection->editor,"effect.add",selection->track,selection->clip,0,0,"org.joltfx.example.tint");
}
static jfx_result_t initialize(const jfx_plugin_api_t *api,void **out_state) {
    tint_state_t *state=api->allocate(api->context,sizeof(*state),_Alignof(tint_state_t));
    if (!state) return JFX_ERROR_OUT_OF_MEMORY;
    *state=(tint_state_t){api,0}; *out_state=state;
    const jfx_param_desc_t params[]={ {"amount","Warmth",0,1,.25f,.01f,false} };
    const jfx_plugin_effect_desc_t effect={sizeof(effect),"org.joltfx.example.tint","Warm Tint",
        "Color Grading",1,params,tint,state};
    jfx_result_t r=api->register_effect(api->context,&effect);
    if (r!=JFX_SUCCESS) return r;
    const jfx_plugin_kernel_desc_t kernel={sizeof(kernel),"org.joltfx.example.invert","Kernel Invert","Effects","",
        "(param amount 1 0 1 0)\n(defkernel invert [x y c] (if (= c 3) (sample x y 3 0 0) "
        "(+ (* (sample x y c 0 0) (- 1 amount)) (* (- (sample x y 3 0 0) (sample x y c 0 0)) amount))))"};
    r=api->register_kernel(api->context,&kernel);
    if (r!=JFX_SUCCESS) return r;
    const jfx_plugin_action_desc_t action={sizeof(action),"org.joltfx.example.apply","Apply Warm Tint",
        JFX_PROJECT_KIND_SEQUENCE,apply,state};
    r=api->register_action(api->context,&action);
    if (r!=JFX_SUCCESS) return r;
    r=api->subscribe(api->context,JFX_EVENT_FRAME_END,frame_event,state);
    if (r==JFX_SUCCESS) api->log(api->context,"Warm Tint plugin ready (native effect + Joltscript kernel).");
    return r;
}
static void shutdown(void *userdata) {
    tint_state_t *state=userdata;
    if (state) state->api->deallocate(state->api->context,state);
}
JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(uint32_t major,uint32_t minor,jfx_plugin_definition_t *out) {
    (void)minor;
    if (major!=JFX_PLUGIN_SDK_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    if (!out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    *out=(jfx_plugin_definition_t){sizeof(*out),JFX_PLUGIN_SDK_MAJOR,0,
        {sizeof(jfx_plugin_desc_t),"org.joltfx.example","Warm Tint Example","JoltFX",JFX_PLUGIN_VERSION(1,0,0),
            JFX_PLUGIN_API_VERSION,JFX_PLUGIN_CAP_KERNELS|JFX_PLUGIN_CAP_EVENTS|JFX_PLUGIN_CAP_EDITOR},initialize,shutdown};
    return JFX_SUCCESS;
}
