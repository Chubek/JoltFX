#include "jfx/jfx_color.h"
#include "jfx/jfx_editor.h"
#include "joltscript/color_io.h"
#include <assert.h>
#include <math.h>
#include <string.h>

int main(void) {
    const jfx_node_kind_t *kind = jfx_node_kind_find("grade_primary");
    assert(kind && jfx_color_section(kind) == JFX_COLOR_GRADING);
    jfx_node_value_t value;
    jfx_node_value_init(&value, kind);
    float src[] = {.25f,.125f,.0625f,.5f, 4,5,6,0}, out[8];
    assert(jfx_color_apply(kind, &value, NULL, src, 2, 1, out) == JFX_SUCCESS);
    for (int i=0;i<8;++i) assert(fabsf(src[i]-out[i])<1.e-6f);
    value.scalars[0] = 1;
    assert(jfx_color_apply(kind, &value, NULL, src, 2, 1, out) == JFX_SUCCESS);
    assert(fabsf(out[0]-.5f)<1.e-6f && out[3]==.5f && out[7]==0);
    value.scalars[0] = NAN;
    out[0] = 123;
    assert(jfx_color_apply(kind, &value, NULL, src, 2, 1, out) == JFX_ERROR_INVALID_ARGUMENT);
    assert(out[0]==123);
    assert(jfx_color_apply(NULL, &value, NULL, src, 2, 1, out) == JFX_ERROR_INVALID_ARGUMENT);

    /* A non-unit domain, channel-specific 1D curve, fractional alpha and mix. */
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_parse_cube("LUT_1D_SIZE 2\nDOMAIN_MIN -1 -1 -1\nDOMAIN_MAX 1 1 1\n0 1 0\n1 0 1\n",0,&lut,NULL,0)==JFX_SUCCESS);
    kind=jfx_node_kind_find("grade_lut"); assert(kind);
    jfx_node_value_init(&value,kind);
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
    assert(fabsf(out[0]-.625f)<1.e-5f && fabsf(out[1]-.4375f)<1.e-5f && out[3]==.5f);
    value.scalars[0]=0;
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
    assert(memcmp(src,out,sizeof(src))==0);
    value.scalars[0]=1; value.scalars[1]=0;
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
    assert(out[0]==1 && out[1]==0 && out[2]==1 && out[3]==.5f);
    value.scalars[1]=.5f; out[0]=123;
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_ERROR_INVALID_ARGUMENT && out[0]==123);
    value.scalars[1]=1;
    lut->domain_max[0]=lut->domain_min[0];
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_ERROR_INVALID_ARGUMENT && out[0]==123);
    jfx_lut_destroy(lut);

    /* Optional production library -> Glue -> calibration kernel -> executor. */
    if (jolt_color_io_available()) {
        lut=NULL;
        assert(jfx_lut_load_auto(JOLT_TEST_ROOT "/tests/fixtures/color_half.cc",&lut,NULL,0)==JFX_SUCCESS);
        kind=jfx_node_kind_find("calib_lut"); jfx_node_value_init(&value,kind);
        assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
        assert(fabsf(out[0]-src[0]*.5f)<1.e-4f && out[3]==src[3]);
        jfx_lut_destroy(lut);
    }
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D,2,&lut)==JFX_SUCCESS);
    kind=jfx_node_kind_find("grade_lut"); jfx_node_value_init(&value,kind);
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
    for (int i=0;i<8;++i) assert(fabsf(src[i]-out[i])<1.e-5f);
    /* Trilinear interpolation must respect cube axis order, not just identity. */
    for (size_t i=0;i<lut->entry_count;i+=3) {
        float red=lut->entries[i]; lut->entries[i]=lut->entries[i+2]; lut->entries[i+2]=red;
    }
    assert(jfx_color_apply(kind,&value,lut,src,2,1,out)==JFX_SUCCESS);
    assert(fabsf(out[0]-src[2])<1.e-5f && fabsf(out[2]-src[0])<1.e-5f);
    jfx_lut_destroy(lut);
    for (size_t i=0;i<jfx_color_kind_count();++i) {
        kind=jfx_color_kind_at(i); jfx_node_value_init(&value,kind);
        assert(jfx_color_apply(kind,&value,NULL,src,2,1,out)==JFX_SUCCESS);
        assert(out[3]==src[3] && out[7]==src[7]);
    }
    assert(jfx_color_kind_at(SIZE_MAX)==NULL && jfx_color_section(NULL)==JFX_COLOR_NONE);

    /* Color sections use their own indices, independent of other effects. */
    jfx_editor_t *e=jfx_editor_create(2,1); assert(e);
    assert(jfx_editor_command(e,"effect.add",0,0,0,0,"invert")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"grade.add",0,0,0,0,"grade_primary")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"calibration.add",0,0,0,0,"calib_gamma_curve")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"grade.param",0,0,0,1,"exposure")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"grade.param",0,0,0,9,"exposure")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"calibration.param",0,0,0,.5,"channel")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"grade.add",0,0,0,0,"invert")==JFX_ERROR_INVALID_ARGUMENT);
    unsigned char before[8], after[8];
    assert(jfx_editor_render(e,0,2,1,before,sizeof(before))==JFX_SUCCESS);
    char doc[8192], error[256]; size_t n;
    assert(jfx_editor_save(e,doc,sizeof(doc),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,doc,n,error,sizeof(error))==JFX_SUCCESS);
    assert(jfx_editor_render(e,0,2,1,after,sizeof(after))==JFX_SUCCESS);
    assert(memcmp(before,after,sizeof(before))==0);
    assert(jfx_editor_command(e,"grade.reset",0,0,0,0,NULL)==JFX_SUCCESS);
    assert(jfx_timeline_effect_param(jfx_editor_timeline(e),0,0,1,0)==0);
    assert(jfx_editor_command(e,"grade.path",0,0,0,0,"missing.cube")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_command(e,"grade.add",0,0,0,0,"grade_lut")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"grade.param",0,0,1,.25,"mix")==JFX_SUCCESS);
    assert(jfx_editor_save(e,doc,sizeof(doc),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,doc,n,error,sizeof(error))==JFX_SUCCESS);
    const char *empty=jfx_timeline_effect_string(jfx_editor_timeline(e),0,0,3,0);
    assert(!empty || !*empty);
    assert(jfx_timeline_effect_param(jfx_editor_timeline(e),0,0,3,0)==.25f);
    assert(jfx_timeline_set_effect_string(jfx_editor_timeline(e),0,0,3,0,"/looks/a #1 \\\"quoted\\\".cube")==JFX_SUCCESS);
    assert(jfx_editor_save(e,doc,sizeof(doc),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,doc,n,error,sizeof(error))==JFX_SUCCESS);
    assert(!strcmp(jfx_timeline_effect_string(jfx_editor_timeline(e),0,0,3,0),"/looks/a #1 \\\"quoted\\\".cube"));
    jfx_editor_destroy(e);
    char catalog[65536];
    assert(jfx_color_catalog(catalog,sizeof(catalog))==JFX_SUCCESS);
    assert(strstr(catalog,"calib_white_balance") && strstr(catalog,"grade_lut"));
    assert(jfx_color_catalog(catalog,2)==JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_color_catalog(NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    return 0;
}
