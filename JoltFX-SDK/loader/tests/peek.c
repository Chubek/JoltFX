#include <joltfx/sdk/loader.h>
#include <assert.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==5);
    jfx_renderer_info_t renderer={0}; renderer.size=sizeof(renderer);
    jfx_plugin_info_t plugin={0}; plugin.size=sizeof(plugin);
    assert(jfx_renderer_module_peek(NULL,&renderer)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_plugin_module_peek("",&plugin)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_peek(argv[1],NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_plugin_module_peek(argv[1],NULL)==JFX_ERROR_INVALID_ARGUMENT);
    renderer.size=0; plugin.size=0;
    assert(jfx_renderer_module_peek(argv[1],&renderer)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_plugin_module_peek(argv[1],&plugin)==JFX_ERROR_INVALID_ARGUMENT);
    renderer.size=sizeof(renderer); plugin.size=sizeof(plugin);
    assert(jfx_renderer_module_peek(argv[1],&renderer)==JFX_SUCCESS);
    assert(jfx_plugin_module_peek(argv[1],&plugin)==JFX_SUCCESS);
    /* Both modules are unloaded here: no borrowed descriptor pointers escape. */
    assert(!strcmp(renderer.identifier,"org.joltfx.peek.renderer") && renderer.version==7);
    assert(!strcmp(plugin.identifier,"org.joltfx.peek.plugin") && plugin.version==9);
    assert(!strcmp(renderer.name,"Peek renderer") && !strcmp(plugin.vendor,"JoltFX"));
    jfx_renderer_info_t saved_renderer=renderer;
    jfx_plugin_info_t saved_plugin=plugin;
    const jfx_result_t expected[]={JFX_ERROR_VERSION_MISMATCH,JFX_ERROR_INVALID_ARGUMENT,JFX_ERROR_NOT_FOUND};
    for (int i=2;i<5;++i) {
        assert(jfx_renderer_module_peek(argv[i],&renderer)==expected[i-2]);
        assert(jfx_plugin_module_peek(argv[i],&plugin)==expected[i-2]);
        assert(!memcmp(&renderer,&saved_renderer,sizeof(renderer)));
        assert(!memcmp(&plugin,&saved_plugin,sizeof(plugin)));
    }
    assert(jfx_renderer_module_peek("/missing/renderer",&renderer)==JFX_ERROR_NOT_FOUND);
    assert(jfx_plugin_module_peek("/missing/plugin",&plugin)==JFX_ERROR_NOT_FOUND);
    return 0;
}
