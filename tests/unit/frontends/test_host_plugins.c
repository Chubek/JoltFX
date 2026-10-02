/* Host-plugin bridge contract.
 *
 * The shipped bridges are host-independent: they identify and version
 * themselves but implement no import, export or effect registration. This
 * test pins that, so the day one is implemented the capability bit is added
 * here at the same time. */

#include "jfx/host_plugin.h"

#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static void check_bridge(jfx_result_t (*get_info)(jfx_host_plugin_info_t *),
    jfx_host_kind_t expected_host, const char *expected_identifier) {
    jfx_host_plugin_info_t info = { .size = sizeof(info) };
    assert(get_info(&info) == JFX_SUCCESS);
    assert(info.host == expected_host);
    assert(info.host_name && info.host_name[0]);
    assert(strcmp(info.plugin_identifier, expected_identifier) == 0);
    assert(info.requires_host_sdk == 1u);
    /* Nothing is implemented, so nothing may be advertised. */
    assert(info.features == 0u);

    /* The size guard rejects a stale or zeroed size. */
    info.size = 0;
    assert(get_info(&info) == JFX_ERROR_INVALID_ARGUMENT);
    assert(get_info(NULL) == JFX_ERROR_INVALID_ARGUMENT);
}

int main(int argc,char **argv) {
    assert(argc==2);
    for (int host=0;host<JFX_HOST_COUNT;++host) {
        assert(jfx_host_color_kind_count((jfx_host_kind_t)host,JFX_COLOR_GRADING)>0);
        assert(jfx_host_color_kind_count((jfx_host_kind_t)host,JFX_COLOR_CALIBRATION)>0);
        const jfx_node_kind_t *k=jfx_node_kind_find("grade_primary");
        jfx_node_value_t v; jfx_node_value_init(&v,k); v.scalars[0]=1;
        float in[]={.2f,.3f,.4f,.5f},out[4];
        assert(jfx_host_color_process((jfx_host_kind_t)host,k->name,&v,NULL,in,1,1,out)==JFX_SUCCESS);
        assert(fabsf(out[0]-.4f)<1.e-5f && out[3]==.5f);
        assert(jfx_host_color_process((jfx_host_kind_t)host,"bad",&v,NULL,in,1,1,out)==JFX_ERROR_INVALID_ARGUMENT);
        jfx_host_nle_t *nle=NULL;
        assert(jfx_host_nle_create((jfx_host_kind_t)host,2,1,&nle)==JFX_SUCCESS);
        assert(jfx_host_nle_edit(nle,"clip.split",0,0,0,100,"")==JFX_SUCCESS);
        assert(jfx_host_nle_edit(nle,"calibration.add",0,1,0,0,"calib_lut")==JFX_SUCCESS);
        assert(jfx_host_nle_edit(nle,"grade.add",0,1,0,0,"grade_primary")==JFX_SUCCESS);
        assert(jfx_host_nle_edit(nle,"grade.param",0,1,0,1,"exposure")==JFX_SUCCESS);
        char doc[8192],state[8192]; size_t length;
        assert(jfx_host_nle_save(nle,doc,sizeof(doc),&length)==JFX_SUCCESS);
        assert(jfx_host_nle_state(nle,state,sizeof(state))==JFX_SUCCESS);
        assert(strstr(state,"\"start\":100") && strstr(state,"\"inPoint\":100"));
        uint8_t pixels[8]; assert(jfx_host_nle_render(nle,4,2,1,pixels,sizeof(pixels))==JFX_SUCCESS);
        assert(pixels[0]==255 && pixels[3]==255);
        assert(jfx_host_nle_write_frame(nle,120,2,1,argv[1])==JFX_SUCCESS);
        FILE *file=fopen(argv[1],"rb"); assert(file);
        char header[12]={0}; assert(fread(header,1,11,file)==11 && !strcmp(header,"P6\n2 1\n255\n"));
        for (int p=0;p<2;++p) for (int c=0;c<3;++c) assert(fgetc(file)==pixels[p*4+c]);
        assert(fgetc(file)==EOF); fclose(file); assert(!remove(argv[1]));
        assert(jfx_host_nle_edit(nle,"undo",0,0,0,0,"")==JFX_SUCCESS);
        assert(jfx_host_nle_load(nle,doc,length,NULL,0)==JFX_SUCCESS);
        assert(jfx_host_nle_edit(nle,"redo",0,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
        jfx_host_nle_destroy(nle);
    }
    check_bridge(jfx_ae_plugin_get_info, JFX_HOST_AFTER_EFFECTS, "org.joltfx.after-effects");
    check_bridge(jfx_premiere_plugin_get_info, JFX_HOST_PREMIERE, "org.joltfx.premiere");
    check_bridge(jfx_davinci_plugin_get_info, JFX_HOST_DAVINCI, "org.joltfx.davinci");

    jfx_host_plugin_info_t info = { .size = sizeof(info) };
    assert(jfx_host_plugin_get_info((jfx_host_kind_t)-1, &info) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_plugin_get_info(JFX_HOST_COUNT, &info) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_plugin_get_info(JFX_HOST_DAVINCI, &info) == JFX_SUCCESS);
    assert(info.host == JFX_HOST_DAVINCI);
    assert(strcmp(info.host_name, "DaVinci Resolve") == 0);
    assert(jfx_host_nle_create(JFX_HOST_COUNT,2,1,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_nle_edit(NULL,"undo",0,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    return 0;
}
