#include <joltfx/sdk/loader.h>
#include <stdlib.h>
#include <string.h>
#ifndef FIXTURE_MODE
#define FIXTURE_MODE 0
#endif
static jfx_result_t initialize(const jfx_plugin_api_t *api,void **data) {
    (void)api; (void)data; abort(); /* inspection must never initialize */
}
static jfx_result_t render(const jfx_renderer_host_t *host,const jfx_renderer_frame_t *frame,
                           uint8_t *out,size_t capacity) {
    (void)host; (void)frame; (void)out; (void)capacity; abort();
}
JFX_RENDERER_EXPORT jfx_result_t jfx_renderer_entry(uint32_t major,uint32_t minor,jfx_renderer_definition_t *out) {
    (void)minor;
    if (major!=JFX_RENDERER_ABI_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->abi_major=major;
    out->minimum_abi_minor=FIXTURE_MODE==1?JFX_RENDERER_ABI_MINOR+1:0;
    out->identifier="org.joltfx.peek.renderer"; out->name="Peek renderer";
    out->render=FIXTURE_MODE==2?NULL:render; out->version=7;
    return JFX_SUCCESS;
}
JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(uint32_t major,uint32_t minor,jfx_plugin_definition_t *out) {
    (void)minor;
    if (major!=JFX_PLUGIN_SDK_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->sdk_major=major;
    out->minimum_sdk_minor=FIXTURE_MODE==1?JFX_PLUGIN_SDK_MINOR+1:0;
    out->info.size=sizeof(out->info);
    out->info.identifier=FIXTURE_MODE==2?
        "org.joltfx.identifier.that.is.longer.than.the.public.sixty.three.byte.limit":
        "org.joltfx.peek.plugin";
    out->info.display_name="Peek plugin"; out->info.vendor="JoltFX";
    out->info.version=9; out->initialize=initialize;
    return JFX_SUCCESS;
}
