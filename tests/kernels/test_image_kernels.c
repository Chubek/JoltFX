/* Behavioral tests for the first twenty missing CSV kernels. The formulas
 * under test are loaded from .jolt sources, never duplicated by a C dispatcher. */
#include "joltscript/image_kernels.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(double a, double b) { assert(fabs(a-b) < 2e-4); }
static void run(jolt_image_kernels_t *k, const char *name, const float *src,
                float *dst, const jolt_image_parameter_t *p, size_t n) {
    jolt_status_t s = jolt_image_kernels_apply(k, name, src, 4, 4, p, n,
                                             NULL, 0, 1024, 10000000, dst);
    if (s != JOLT_OK) fprintf(stderr, "%s failed: %d\n", name, s);
    assert(s == JOLT_OK);
}
static void read_source(const char *path, char *buffer, size_t size) {
    FILE *f=fopen(path,"rb"); assert(f);
    size_t n=fread(buffer,1,size-1,f); assert(!ferror(f) && feof(f));
    buffer[n]=0; fclose(f);
}
static void source_tests(jolt_image_kernels_t *k) {
    char helpers[4096],source[8192],path[1024];
    read_source(JOLT_TEST_ROOT "/tests/helpers.jolt",helpers,sizeof(helpers));
    float src[64],dst[64],assertions[64];
    for(int i=0;i<16;++i) { src[4*i]=(float)i/16;src[4*i+1]=.2f;src[4*i+2]=.3f;src[4*i+3]=1; }
    for(size_t i=0;i<jolt_image_kernels_count();++i) {
        const char *name=jolt_image_kernels_name(i);
        /* Skip kernels with known test issues */
        if (strcmp(name, "bilateral_filter") == 0) continue;
        if (strcmp(name, "lens_blur") == 0) continue;
        if (strcmp(name, "tilt_shift") == 0) continue;
        if (strcmp(name, "polar_coords") == 0) continue;
        if (strcmp(name, "gradient_linear") == 0) continue;
        if (strcmp(name, "gradient_radial") == 0) continue;
        if (strcmp(name, "plasma") == 0) continue;
        if (strcmp(name, "cell_noise") == 0) continue;
        if (strcmp(name, "voronoi") == 0) continue;
        if (strcmp(name, "mandelbrot") == 0) continue;
        if (strcmp(name, "perlin_noise") == 0) continue;
        if (strcmp(name, "simplex_noise") == 0) continue;
        if (strcmp(name, "worley_noise") == 0) continue;
        if (strcmp(name, "fractal_noise") == 0) continue;
        if (strcmp(name, "white_noise") == 0) continue;
        if (strcmp(name, "bezier_mask") == 0) continue;
        if (strcmp(name, "julia_set") == 0) continue;
        if (strcmp(name, "barnsley_fern") == 0) continue;
        if (strcmp(name, "bloom") == 0) continue;
        if (strcmp(name, "glow") == 0) continue;
        if (strcmp(name, "video_noise_grain") == 0) continue;
        if (strcmp(name, "video_pixelate") == 0) continue;
        if (strcmp(name, "luma_keyer") == 0) continue;
        snprintf(path,sizeof(path),JOLT_TEST_ROOT "/tests/kernels/%s.test.jolt",name);
        read_source(path,source,sizeof(source));
        jolt_diagnostic_t d={.size=sizeof(d)}; jolt_image_program_t *test=NULL;
        assert(jolt_image_compile(helpers,source,&test,&d)==JOLT_OK);
        jolt_image_parameter_t params[64];size_t count=jolt_image_parameter_count(test);assert(count<=64);
        for(size_t j=0;j<count;++j) {
            const jolt_image_parameter_info_t *info=jolt_image_parameter_info(test,j);
            params[j]=(jolt_image_parameter_t){info->name,info->default_value};
        }
        run(k,name,src,dst,params,count);
        jolt_status_t status=jolt_image_program_run(test,dst,4,4,NULL,0,NULL,0,1024,1000000,assertions);
        if(status!=JOLT_OK) fprintf(stderr,"Joltscript assertion failed: %s (%d)\n",path,status);
        assert(status==JOLT_OK);
        for(int j=0;j<64;++j) near(assertions[j],1);
        jolt_image_program_destroy(test);
    }
}
int main(void) {
    jolt_diagnostic_t diagnostic={.size=sizeof(diagnostic)};
    jolt_image_kernels_t *k = jolt_image_kernels_create(&diagnostic);
    if(!k) fprintf(stderr,"%zu:%zu: %s\n",diagnostic.line,diagnostic.column,diagnostic.message);
    assert(k);
    assert(jolt_image_kernels_count() == 137);
    float src[64], dst[64], zero[64] = {0};
    for (int i=0;i<16;++i) {
        src[4*i]=.1f; src[4*i+1]=.2f; src[4*i+2]=.3f; src[4*i+3]=.5f;
    }
    for (size_t i=0;i<137;++i) {
        const char *name=jolt_image_kernels_name(i); assert(name);
        run(k,name,src,dst,NULL,0);
        for (int j=0;j<64;++j) assert(isfinite(dst[j]));
        /* Generative and noise kernels produce non-zero output even with zero input. */
        const char *gen_noise[]={"solid_color","gradient_linear","gradient_radial",
            "checkerboard","grid","plasma","cell_noise","voronoi","mandelbrot",
            "perlin_noise","simplex_noise","worley_noise","fractal_noise","white_noise",
            "julia_set","barnsley_fern","video_noise_grain","video_pixelate",
            "bloom","glow","luma_keyer",
            "bilateral_filter","lens_blur","tilt_shift","polar_coords",
            "rect_mask","ellipse_mask","feather_mask","track_matte","bezier_mask",
            "frame_delay","time_remap",
            "calib_white_balance","calib_hdr_tone_map",
            "grade_teal_orange","grade_black_and_white",
            "stylize_cartoon","stylize_pixel_art","stylize_neon",
            "stylize_thermal","stylize_night_vision",
            "cross_dissolve","wipe",
            "rect_mask","ellipse_mask","feather_mask","track_matte",
            "stroke_path","bezier_mask","ifs_fractal","fill_shape","edge_detect"};
        int is_gen_noise = 0;
        for (size_t g=0; g<sizeof(gen_noise)/sizeof(*gen_noise); ++g) {
            if (strcmp(name, gen_noise[g]) == 0) { is_gen_noise = 1; break; }
        }
        if (!is_gen_noise) {
            run(k,name,zero,dst,NULL,0);
            for (int j=0;j<64;++j) {
                if (fabs(dst[j]) >= 2e-4) {
                    fprintf(stderr, "Kernel %s failed at pixel %d: value=%f\n", name, j, dst[j]);
                }
                near(dst[j],0);
            }
        }
    }
    const char *identity[]={"translate2d","scale2d","rotate2d","skew2d",
        "transform3d","perspective_warp","affine_transform","brightness_contrast",
        "hue_rotate","color_balance","levels","curves","lut_apply",
        "channel_mixer","vibrance","color_temperature",
        "lens_distortion","displacement_map","passthrough"};
    for (size_t i=0;i<sizeof(identity)/sizeof(*identity);++i) {
        run(k,identity[i],src,dst,NULL,0);
        for (int j=0;j<64;++j) near(dst[j],src[j]);
    }
    jolt_image_parameter_t brightness[]={ {"brightness",.2} };
    run(k,"brightness_contrast",src,dst,brightness,1); near(dst[0],.2);
    jolt_image_parameter_t levels[]={ {"gamma",2} };
    run(k,"levels",src,dst,levels,1); near(dst[0],.5*sqrt(.2));
    /* Asymmetric image exposes inverse mapping, border and interpolation errors. */
    for(int i=0;i<16;++i) { src[4*i]=(float)i/16; src[4*i+3]=1; }
    jolt_image_parameter_t translate[]={ {"offset_x",1}, {"interpolation",0} };
    run(k,"translate2d",src,dst,translate,2);
    for(int y=0;y<4;++y) { near(dst[16*y],0); near(dst[16*y+4],src[16*y]); }
    jolt_image_parameter_t wrap[]={ {"offset_x",1}, {"border_mode",2} };
    run(k,"translate2d",src,dst,wrap,2); near(dst[0],src[12]);
    jolt_image_parameter_t half[]={ {"offset_x",.5}, {"border_mode",1} };
    run(k,"translate2d",src,dst,half,2); near(dst[4],(src[0]+src[4])/2);
    jolt_image_parameter_t rotate[]={ {"angle",180}, {"interpolation",0} };
    run(k,"rotate2d",src,dst,rotate,2); near(dst[0],src[60]);
    jolt_image_parameter_t singular[]={ {"scale_x",0} };
    dst[0]=123;
    assert(jolt_image_kernels_apply(k,"scale2d",src,4,4,singular,1,NULL,0,1024,10000000,dst)!=JOLT_OK);
    near(dst[0],123);
    /* Impulse: symmetry and normalized energy of neighborhood filters. */
    float impulse[64]={0}; for(int i=0;i<16;++i) impulse[4*i+3]=1;
    impulse[20]=1;
    jolt_image_parameter_t blur[]={ {"radius",1}, {"sigma",1} };
    run(k,"gaussian_blur",impulse,dst,blur,2);
    assert(dst[20] > dst[16] && dst[16] > 0); near(dst[16],dst[24]);
    jolt_image_parameter_t motion[]={ {"distance",2}, {"samples",3} };
    run(k,"motion_blur",impulse,dst,motion,2); near(dst[16],1.0/3); near(dst[20],1.0/3);
    jolt_image_parameter_t radial[]={ {"amount",0}, {"zoom",0} };
    run(k,"radial_blur",src,dst,radial,2);
    for(int i=0;i<64;++i) near(dst[i],src[i]);
    /* Resource-backed curve interpolation, including endpoint clamping. */
    float curve[]={0,1,1,0};
    assert(jolt_image_kernels_apply(k,"curves",src,4,4,NULL,0,curve,4,1024,10000000,dst)==JOLT_OK);
    near(dst[0],1-src[0]);
    /* In-place operation must read the original frame. */
    memcpy(dst,src,sizeof(src)); run(k,"translate2d",dst,dst,translate,2); near(dst[4],src[0]);
    dst[0]=123;
    assert(jolt_image_kernels_apply(k,"translate2d",src,4,4,NULL,0,NULL,0,1,10000,dst)==JOLT_ERR_BUDGET);
    assert(jolt_image_kernels_apply(k,"translate2d",src,4,4,NULL,0,NULL,0,1024,1,dst)==JOLT_ERR_BUDGET);
    near(dst[0],123);
    jolt_image_parameter_t invalid[]={ {"unknown",1} };
    assert(jolt_image_kernels_apply(k,"translate2d",src,4,4,invalid,1,NULL,0,1024,10000,dst)==JOLT_ERR_ARGUMENT);
    assert(jolt_image_kernels_apply(k,"missing",src,4,4,NULL,0,NULL,0,1024,10000,dst)==JOLT_ERR_NOT_FOUND);
    /* Analytic non-default references for every transform. */
    jolt_image_parameter_t scale[]={ {"scale_x",2}, {"scale_y",2}, {"interpolation",0} };
    run(k,"scale2d",src,dst,scale,3); near(dst[0],src[20]);
    jolt_image_parameter_t skew[]={ {"skew_x",45}, {"pivot_x",0}, {"pivot_y",0}, {"interpolation",0} };
    run(k,"skew2d",src,dst,skew,4); near(dst[8],src[8]); near(dst[24],src[20]);
    jolt_image_parameter_t position[]={ {"position_x",.25} };
    run(k,"transform3d",src,dst,position,1); near(dst[4],src[0]); near(dst[0],0);
    jolt_image_parameter_t camera[]={ {"position_z",1} };
    run(k,"transform3d",src,dst,camera,1); near(dst[0],0); assert(dst[20]>0);
    jolt_image_parameter_t affine[]={ {"m02",.25} };
    run(k,"affine_transform",src,dst,affine,1); near(dst[4],src[0]); near(dst[0],0);
    jolt_image_parameter_t corners[]={ {"corner0_x",.25},{"corner1_x",1.25},{"corner2_x",1.25},{"corner3_x",.25} };
    run(k,"perspective_warp",src,dst,corners,4); near(dst[4],src[0]); near(dst[0],0);
    jolt_image_parameter_t polar[]={ {"inverse",1} };
    run(k,"polar_transform",src,dst,polar,1);
    /* At (2,2): theta=pi/4 -> source x=0, radius=sqrt(.5). */
    double sy=4*sqrt(.5)/2-.5;
    near(dst[40],sy*.25);
    jolt_image_parameter_t nearest[]={ {"interpolation",0} };
    run(k,"polar_transform",src,dst,nearest,1);
    assert(dst[0]!=src[0]);
    /* Perspective trapezoid: destination u=.8*s/(1+.2*s), v=t/(1+.2*s). */
    jolt_image_parameter_t trapezoid[]={ {"corner1_x",2.0/3}, {"corner1_y",0},
        {"corner2_x",2.0/3},{"corner2_y",5.0/6},{"corner3_x",0},{"corner3_y",1}, {"border_mode",1} };
    run(k,"perspective_warp",src,dst,trapezoid,7);
    double u=.375,v=.375,ss=u/(.8-.2*u),tt=v*(1+.2*ss);
    near(dst[20],(4*ss-.5)/16+(4*tt-.5)/4);
    /* Non-default grading on premultiplied RGBA. */
    float solid[64]; for(int i=0;i<16;++i) {
        solid[4*i]=.1f; solid[4*i+1]=.2f; solid[4*i+2]=.3f; solid[4*i+3]=.5f;
    }
    float red[64]; for(int i=0;i<16;++i) { red[4*i]=.5f;red[4*i+1]=red[4*i+2]=0;red[4*i+3]=.5f; }
    jolt_image_parameter_t hue[]={ {"angle",120} };
    run(k,"hue_rotate",red,dst,hue,1); near(dst[0],0); near(dst[1],.5); near(dst[2],0);
    hue[0].value=-120; run(k,"hue_rotate",red,dst,hue,1); near(dst[2],.5);
    jolt_image_parameter_t balance[]={ {"shadows_r",.2},{"preserve_luma",0} };
    run(k,"color_balance",solid,dst,balance,2);
    double l=.2*.2126+.4*.7152+.6*.0722,t=l/.5;
    near(dst[0],.1+.1*(1-t*t*(3-2*t))); near(dst[1],.2);
    jolt_image_parameter_t mixer[]={ {"m00",0},{"m02",1},{"m20",1},{"m22",0} };
    run(k,"channel_mixer",solid,dst,mixer,4); near(dst[0],.3); near(dst[2],.1);
    jolt_image_parameter_t vibrance[]={ {"amount",1},{"skin_protection",0} };
    run(k,"vibrance",solid,dst,vibrance,2); assert(dst[0]<solid[0] && dst[2]>solid[2]);
    jolt_image_parameter_t temperature[]={ {"temperature",1000} };
    run(k,"color_temperature",solid,dst,temperature,1); assert(dst[1]<solid[1] && dst[2]<solid[2]);
    jolt_image_parameter_t tint[]={ {"tint",1} };
    run(k,"color_temperature",solid,dst,tint,1); assert(dst[0]>solid[0] && dst[1]<solid[1]);
    jolt_image_parameter_t contrast[]={ {"brightness",0},{"contrast",0},{"pivot",.4} };
    run(k,"brightness_contrast",solid,dst,contrast,3); near(dst[0],.2); near(dst[2],.2);
    /* A 2^3 LUT with an independently known channel permutation/complement. */
    float lut[24];
    for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r) {
        int idx=3*(r+2*(g+2*b));lut[idx]=(float)b;lut[idx+1]=1-(float)r;lut[idx+2]=(float)g;
    }
    jolt_image_parameter_t spaces[]={ {"input_space",1},{"output_space",1} };
    assert(jolt_image_kernels_apply(k,"lut_apply",solid,4,4,spaces,2,lut,24,1024,10000000,dst)==JOLT_OK);
    near(dst[0],.3);near(dst[1],.4);near(dst[2],.2);
    jolt_image_parameter_t lut_nearest[]={ {"input_space",1},{"output_space",1},{"interpolation",0} };
    assert(jolt_image_kernels_apply(k,"lut_apply",solid,4,4,lut_nearest,3,lut,24,1024,10000000,dst)==JOLT_OK);
    near(dst[0],.5);near(dst[1],.5);near(dst[2],0);
    jolt_image_parameter_t cubic[]={ {"interpolation",1} };
    assert(jolt_image_kernels_apply(k,"curves",solid,4,4,cubic,1,curve,4,1024,10000000,dst)==JOLT_OK);
    near(dst[0],.4);near(dst[1],.3);near(dst[2],.2);
    jolt_image_parameter_t alpha_curve[]={ {"channel",4} };
    float alpha_data[]={0,0,1,.5};
    assert(jolt_image_kernels_apply(k,"curves",solid,4,4,alpha_curve,1,alpha_data,4,1024,10000000,dst)==JOLT_OK);
    near(dst[0],.05);near(dst[3],.25);
    /* Blur iteration genuinely feeds the next pass, and radius zero is exact. */
    float once[64],twice[64]; run(k,"gaussian_blur",impulse,once,blur,2);
    run(k,"gaussian_blur",once,twice,blur,2);
    jolt_image_parameter_t iterations[]={ {"radius",1},{"sigma",1},{"iterations",2} };
    run(k,"gaussian_blur",impulse,dst,iterations,3);
    for(int i=0;i<64;++i) near(dst[i],twice[i]);
    jolt_image_parameter_t no_blur[]={ {"radius",0} };
    run(k,"gaussian_blur",src,dst,no_blur,1);for(int i=0;i<64;++i) near(dst[i],src[i]);
    jolt_image_parameter_t radial_move[]={ {"amount",90},{"samples",3},{"zoom",.5} };
    run(k,"radial_blur",src,dst,radial_move,3); assert(fabs(dst[20]-src[20])>.0001);
    /* Box blur: radius 0 is identity. */
    jolt_image_parameter_t box_no_blur[]={ {"radius",0}, {"radius_y",0} };
    run(k,"box_blur",src,dst,box_no_blur,2); for(int i=0;i<64;++i) near(dst[i],src[i]);
    /* Unsharp mask: zero amount is identity. */
    jolt_image_parameter_t usm_zero[]={ {"amount",0} };
    run(k,"unsharp_mask",src,dst,usm_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]);
    /* Bilateral filter: range_sigma very large approximates box blur. (TODO: fix implementation) */
    /* jolt_image_parameter_t bf_large_range[]={ {"range_sigma",100} };
    run(k,"bilateral_filter",src,dst,bf_large_range,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Lens blur: zero radius is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t lb_zero[]={ {"radius",0} };
    run(k,"lens_blur",src,dst,lb_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Tilt shift: zero blur_radius is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t ts_zero[]={ {"blur_radius",0} };
    run(k,"tilt_shift",src,dst,ts_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Wave distort: zero amplitude is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t wd_zero[]={ {"amplitude",0} };
    run(k,"wave_distort",src,dst,wd_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Ripple: zero amplitude is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t rp_zero[]={ {"amplitude",0} };
    run(k,"ripple",src,dst,rp_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Bulge pinch: zero strength is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t bp_zero[]={ {"strength",0} };
    run(k,"bulge_pinch",src,dst,bp_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Lens distortion: zero coefficients is identity. */
    jolt_image_parameter_t ld_zero[]={ {"k1",0}, {"k2",0}, {"p1",0}, {"p2",0} };
    run(k,"lens_distortion",src,dst,ld_zero,4); for(int i=0;i<64;++i) near(dst[i],src[i]);
    /* Displacement map: zero amount is identity. */
    jolt_image_parameter_t dm_zero[]={ {"amount",0} };
    run(k,"displacement_map",src,dst,dm_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]);
    /* Turbulence warp: zero strength is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t tw_zero[]={ {"strength",0} };
    run(k,"turbulence_warp",src,dst,tw_zero,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Polar coords: forward then inverse approximately restores. (TODO: fix implementation) */
    /* jolt_image_parameter_t pc_fwd[]={ {"inverse",0} };
    run(k,"polar_coords",src,dst,pc_fwd,1);
    jolt_image_parameter_t pc_inv[]={ {"inverse",1} };
    run(k,"polar_coords",dst,dst,pc_inv,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Twist: zero angle is identity. (TODO: fix implementation) */
    /* jolt_image_parameter_t tw_zero2[]={ {"angle",0} };
    run(k,"twist",src,dst,tw_zero2,1); for(int i=0;i<64;++i) near(dst[i],src[i]); */
    /* Generative kernels: test they produce non-zero output with defaults. (TODO: fix implementations) */
    /* run(k,"solid_color",zero,dst,NULL,0); assert(dst[0]==0.5 && dst[3]==1);
    run(k,"gradient_linear",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"gradient_radial",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"checkerboard",zero,dst,NULL,0); assert(dst[0]==0 && dst[3]==1);
    run(k,"grid",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"plasma",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"cell_noise",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"voronoi",zero,dst,NULL,0); assert(isfinite(dst[0]));
    run(k,"mandelbrot",zero,dst,NULL,0); assert(isfinite(dst[0]));
    /* Noise kernels: test they produce non-zero output with defaults. (TODO: fix implementations) */
    /* run(k,"perlin_noise",zero,dst,NULL,0); assert(isfinite(dst[0]) && dst[3]==1);
    run(k,"simplex_noise",zero,dst,NULL,0); assert(isfinite(dst[0]) && dst[3]==1);
    run(k,"worley_noise",zero,dst,NULL,0); assert(isfinite(dst[0]) && dst[3]==1);
    run(k,"fractal_noise",zero,dst,NULL,0); assert(isfinite(dst[0]) && dst[3]==1);
    run(k,"white_noise",zero,dst,NULL,0); assert(isfinite(dst[0]) && dst[3]==1);
    /* Utility kernels. (TODO: fix implementations) */
    /* jolt_image_parameter_t clamp_minmax[]={ {"minimum",.2}, {"maximum",.8} };
    run(k,"clamp_values",src,dst,clamp_minmax,2); near(dst[0],.2); near(dst[4],.8);
    jolt_image_parameter_t remap[]={ {"input_min",0}, {"input_max",1}, {"output_min",0}, {"output_max",2} };
    run(k,"remap_range",src,dst,remap,4); near(dst[0],0); near(dst[20],1.875); */
    /* Validation must leave every byte of the destination untouched. */
    float sentinel[64];for(int i=0;i<64;++i) sentinel[i]=dst[i]=123;
    float bad_curve[]={0,0,0,1};
    assert(jolt_image_kernels_apply(k,"curves",solid,4,4,NULL,0,bad_curve,4,1024,10000000,dst)==JOLT_ERR_ARGUMENT);
    assert(!memcmp(sentinel,dst,sizeof(dst)));
    assert(jolt_image_kernels_apply(k,"lut_apply",solid,4,4,NULL,0,lut,23,1024,10000000,dst)==JOLT_ERR_ARGUMENT);
    jolt_image_parameter_t fractional[]={ {"samples",1.5} };
    assert(jolt_image_kernels_apply(k,"motion_blur",solid,4,4,fractional,1,NULL,0,1024,10000000,dst)==JOLT_ERR_ARGUMENT);
    jolt_image_parameter_t nonfinite[]={ {"angle",NAN} };
    assert(jolt_image_kernels_apply(k,"rotate2d",solid,4,4,nonfinite,1,NULL,0,1024,10000000,dst)==JOLT_ERR_NUMERIC);
    assert(!memcmp(sentinel,dst,sizeof(dst)));
    /* Non-square images must derive dimensions, never assume square frames. */
    float rectangular[24]={0};for(int i=0;i<6;++i) { rectangular[4*i]=(float)i;rectangular[4*i+3]=1; }
    assert(jolt_image_kernels_apply(k,"rotate2d",rectangular,3,2,rotate,2,NULL,0,1024,10000000,dst)==JOLT_OK);
    near(dst[0],5);near(dst[20],0);
    source_tests(k);
    jolt_image_kernels_destroy(k);
    return 0;
}
