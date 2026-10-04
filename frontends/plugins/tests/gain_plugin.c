/* A minimal, self-contained OFX image-effect plugin used to test the host
 * adapter end to end: dynamic loading, the action lifecycle, the property,
 * parameter and image-effect suites, and render output.
 *
 * It is built as a real shared object rather than linked into the test binary,
 * because the behaviour under test is the loader's: symbol resolution, setHost,
 * and the action sequence driven through mainEntry.
 *
 * Behaviour: a Filter context effect that multiplies R, G and B by a `gain`
 * parameter (default 1.0, clamped to [0, 4]) and leaves alpha alone. Identical
 * formulas with no clamping are used for the RGBA `tint` parameter, so a host
 * that mishandles multi-component parameters produces visibly wrong pixels. */

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include <stdlib.h>
#include <string.h>

static OfxPropertySuiteV1 *g_prop_host = NULL;
static OfxParameterSuiteV1 *g_param_host = NULL;
static OfxImageEffectSuiteV1 *g_effect_host = NULL;
/* The OfxHost is kept because suites are fetched through it; the suite structs
 * have no fetchSuite member of their own. */
static OfxHost *g_host = NULL;

/* Per-instance state, cached at create-instance exactly as a real plugin does. */
typedef struct {
    OfxParamSetHandle param_set;
    OfxImageClipHandle source_clip;
    OfxImageClipHandle output_clip;
    OfxParamHandle gain;
    OfxParamHandle tint;
    int is_general;
} gain_instance_t;

typedef struct {
    OfxImageEffectHandle effect;
    gain_instance_t *instances[64];
    int instance_count;
} gain_plugin_t;

static gain_plugin_t g_plugin;

/* Fetches the three suites this effect needs. Called first from describe,
 * which is the earliest point the OFX specification permits. */
static OfxStatus fetch_suites(void) {
    if (!g_host || !g_host->fetchSuite) return kOfxStatFailed;
    OfxPropertySuiteV1 *prop = (OfxPropertySuiteV1 *)g_host->fetchSuite(
        g_host->host, kOfxPropertySuite, 1);
    OfxParameterSuiteV1 *param = (OfxParameterSuiteV1 *)g_host->fetchSuite(
        g_host->host, kOfxParameterSuite, 1);
    OfxImageEffectSuiteV1 *effect = (OfxImageEffectSuiteV1 *)g_host->fetchSuite(
        g_host->host, kOfxImageEffectSuite, 1);
    if (!prop || !param || !effect) return kOfxStatFailed;
    g_prop_host = prop;
    g_param_host = param;
    g_effect_host = effect;
    return kOfxStatOK;
}

static OfxStatus describe(OfxImageEffectHandle effect) {
    if (fetch_suites() != kOfxStatOK) return kOfxStatFailed;
    OfxPropertySetHandle props = NULL;
    if (g_effect_host->getPropertySet(effect, &props) != kOfxStatOK)
        return kOfxStatFailed;
    g_prop_host->propSetString(props, kOfxPropLabel, 0, "JoltFX OFX Gain");
    /* Only float RGBA is implemented, and no tiles or temporal access, so say
     * so rather than letting the host find out at render time. */
    g_prop_host->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0,
        kOfxBitDepthFloat);
    g_prop_host->propSetString(props, kOfxImageEffectPropSupportedContexts, 0,
        kOfxImageEffectContextFilter);
    g_prop_host->propSetString(props, kOfxImageEffectPropSupportedContexts, 1,
        kOfxImageEffectContextGeneral);
    return kOfxStatOK;
}

static OfxStatus describe_in_context(OfxImageEffectHandle effect,
    OfxPropertySetHandle in_args) {
    char *context = NULL;
    if (g_prop_host->propGetString(in_args, kOfxImageEffectPropContext, 0,
            &context) != kOfxStatOK || !context)
        return kOfxStatFailed;

    OfxPropertySetHandle props = NULL;
    if (g_effect_host->clipDefine(effect, kOfxImageEffectOutputClipName, &props)
        != kOfxStatOK) return kOfxStatFailed;
    g_prop_host->propSetString(props, kOfxImageEffectPropSupportedComponents, 0,
        kOfxImageComponentRGBA);

    if (g_effect_host->clipDefine(effect, kOfxImageEffectSimpleSourceClipName,
            &props) != kOfxStatOK) return kOfxStatFailed;
    g_prop_host->propSetString(props, kOfxImageEffectPropSupportedComponents, 0,
        kOfxImageComponentRGBA);

    /* In the General context an extra mask input exists but is left unwired;
     * this host supplies only the standard source, so declaring it would let a
     * plugin read a clip this host never populates. */
    if (strcmp(context, kOfxImageEffectContextGeneral) == 0) {
        if (g_effect_host->clipDefine(effect, "Mask", &props) != kOfxStatOK)
            return kOfxStatFailed;
        g_prop_host->propSetString(props, kOfxImageEffectPropSupportedComponents,
            0, kOfxImageComponentAlpha);
        g_prop_host->propSetInt(props, kOfxImageClipPropOptional, 0, 1);
    }

    OfxParamSetHandle param_set = NULL;
    if (g_effect_host->getParamSet(effect, &param_set) != kOfxStatOK)
        return kOfxStatFailed;

    if (g_param_host->paramDefine(param_set, kOfxParamTypeDouble, "gain", &props)
        != kOfxStatOK) return kOfxStatFailed;
    g_prop_host->propSetString(props, kOfxPropLabel, 0, "Gain");
    g_prop_host->propSetString(props, kOfxParamPropHint, 0, "Scales R, G and B");
    g_prop_host->propSetDouble(props, kOfxParamPropDefault, 0, 1.0);
    g_prop_host->propSetDouble(props, kOfxParamPropMin, 0, 0.0);
    g_prop_host->propSetDouble(props, kOfxParamPropMax, 0, 4.0);

    if (g_param_host->paramDefine(param_set, kOfxParamTypeRGBA, "tint", &props)
        != kOfxStatOK) return kOfxStatFailed;
    g_prop_host->propSetString(props, kOfxPropLabel, 0, "Tint");
    g_prop_host->propSetDouble(props, kOfxParamPropDefault, 0, 0.0);
    g_prop_host->propSetDouble(props, kOfxParamPropMin, 0, 0.0);
    g_prop_host->propSetDouble(props, kOfxParamPropMax, 0, 1.0);

    return kOfxStatOK;
}

static OfxStatus create_instance(OfxImageEffectHandle effect,
    OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    (void)in_args;
    (void)out_args;
    if (g_plugin.instance_count >= 64) return kOfxStatFailed;
    gain_instance_t *instance = (gain_instance_t *)calloc(1,
        sizeof(gain_instance_t));
    if (!instance) return kOfxStatFailed;
    instance->is_general = 0;
    if (g_effect_host->getParamSet(effect, &instance->param_set) != kOfxStatOK
        || g_effect_host->clipGetHandle(effect,
            kOfxImageEffectSimpleSourceClipName, &instance->source_clip, NULL)
            != kOfxStatOK
        || g_effect_host->clipGetHandle(effect, kOfxImageEffectOutputClipName,
            &instance->output_clip, NULL) != kOfxStatOK
        || g_param_host->paramGetHandle(instance->param_set, "gain",
            &instance->gain, NULL) != kOfxStatOK
        || g_param_host->paramGetHandle(instance->param_set, "tint",
            &instance->tint, NULL) != kOfxStatOK) {
        free(instance);
        return kOfxStatFailed;
    }
    g_plugin.instances[g_plugin.instance_count++] = instance;
    return kOfxStatOK;
}

static OfxStatus destroy_instance(OfxImageEffectHandle effect) {
    for (int i = 0; i < g_plugin.instance_count; ++i) {
        if (g_plugin.instances[i]) {
            free(g_plugin.instances[i]);
            g_plugin.instances[i] = NULL;
            return kOfxStatOK;
        }
    }
    (void)effect;
    return kOfxStatOK;
}

/* Reads the 8 floats of an RGBA float image: bounds, row bytes, data pointer. */
static int read_image(OfxPropertySetHandle image, int *bounds, float **pixels) {
    void *data = NULL;
    int row_bytes = 0;
    if (!image) return 0;
    if (g_prop_host->propGetIntN(image, "OfxImagePropBounds", 4, bounds)
        != kOfxStatOK) return 0;
    if (g_prop_host->propGetInt(image, "OfxImagePropRowBytes", 0, &row_bytes)
        != kOfxStatOK) return 0;
    if (g_prop_host->propGetPointer(image, "OfxImagePropData", 0, &data)
        != kOfxStatOK) return 0;
    *pixels = (float *)data;
    return row_bytes > 0 && *pixels != NULL;
}

static OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle in_args,
    OfxPropertySetHandle out_args) {
    (void)in_args;
    (void)out_args;
    gain_instance_t *instance = NULL;
    for (int i = 0; i < g_plugin.instance_count; ++i)
        if (g_plugin.instances[i]) {
            instance = g_plugin.instances[i];
            break;
        }
    if (!instance) return kOfxStatFailed;

    double gain = 1.0;
    g_param_host->paramGetValue(instance->gain, &gain);
    /* Deliberately unclamped: the host is required to have clamped `gain` to its
     * declared [0,4] range, so a value above 4 here means the host did not. */
    double tint[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (int i = 0; i < 4; ++i)
        g_param_host->paramGetValue(instance->tint, &tint[i]);

    OfxPropertySetHandle source_image = NULL;
    OfxPropertySetHandle output_image = NULL;
    if (g_effect_host->clipGetImage(instance->source_clip, 0.0, NULL,
            &source_image) != kOfxStatOK) return kOfxStatFailed;
    if (g_effect_host->clipGetImage(instance->output_clip, 0.0, NULL,
            &output_image) != kOfxStatOK) {
        g_effect_host->clipReleaseImage(source_image);
        return kOfxStatFailed;
    }

    int sb[4] = { 0, 0, 0, 0 }, ob[4] = { 0, 0, 0, 0 };
    float *src = NULL, *dst = NULL;
    int ok = read_image(source_image, sb, &src)
        && read_image(output_image, ob, &dst);
    if (ok && gain > 4.0) {
        /* Prove the clamp happened rather than silently producing absurd pixels. */
        ok = 0;
    }
    if (ok) {
        int width = ob[2] - ob[0];
        int height = ob[3] - ob[1];
        for (int y = 0; y < height && ok; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t index = 4 * ((size_t)y * (size_t)width + (size_t)x);
                dst[index + 0] = (float)((double)src[index + 0] * gain * tint[0]);
                dst[index + 1] = (float)((double)src[index + 1] * gain * tint[1]);
                dst[index + 2] = (float)((double)src[index + 2] * gain * tint[2]);
                dst[index + 3] = (float)src[index + 3];
            }
        }
    }

    g_effect_host->clipReleaseImage(output_image);
    g_effect_host->clipReleaseImage(source_image);
    (void)effect;
    return ok ? kOfxStatOK : kOfxStatFailed;
}

static OfxStatus plugin_main(const char *action, const void *handle,
    OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    OfxImageEffectHandle effect = (OfxImageEffectHandle)handle;
    if (!action) return kOfxStatFailed;
    if (!strcmp(action, kOfxActionLoad)) return kOfxStatOK;
    if (!strcmp(action, kOfxActionDescribe)) return describe(effect);
    if (!strcmp(action, kOfxImageEffectActionDescribeInContext))
        return describe_in_context(effect, in_args);
    if (!strcmp(action, kOfxActionCreateInstance))
        return create_instance(effect, in_args, out_args);
    if (!strcmp(action, kOfxActionDestroyInstance)) return destroy_instance(effect);
    if (!strcmp(action, kOfxImageEffectActionRender))
        return render(effect, in_args, out_args);
    /* Everything else is legitimately not supported by this host. */
    return kOfxStatReplyDefault;
}

/* ------------------------------------------------------------- exports */

static void set_host(OfxHost *host) {
    /* Per the specification this must only record the pointer; no OFX call may
     * be made from here. */
    g_host = host;
}

int OfxGetNumberOfPlugins(void) {
    return 1;
}

OfxPlugin *OfxGetPlugin(int nth) {
    /* The API string must match kOfxImageEffectPluginApi exactly. A host
     * compares it verbatim, and the two spellings differ only in the case of
     * the trailing "PI"/"pi", so it is referenced through the macro rather than
     * written out. */
    static OfxPlugin plugin = {
        kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion,
        "org.joltfx.ofx.gain", 1, 0,
        set_host, plugin_main
    };
    return nth == 0 ? &plugin : NULL;
}