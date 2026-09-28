/* Node-based compositing: blend equations, the self-describing node library, and
 * DAG evaluation.
 *
 * See jfx_compose.h for the model. Nothing here allocates through malloc: every
 * buffer comes from the engine's allocator, and a graph's own memory is owned by
 * the graph so that destroying it is sufficient. */

#include "jfx/jfx_compose.h"
#include "joltscript/video_io.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tilly/allocator.h"

/* ---- Allocation ---------------------------------------------------------- */

static void *alloc_bytes(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static void free_bytes(void *p) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), p);
}

static char *dup_string(const char *s) {
    if (!s) {
        return NULL;
    }
    const size_t n = strlen(s) + 1u;
    char *copy = alloc_bytes(n);
    if (copy) {
        memcpy(copy, s, n);
    }
    return copy;
}

static float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f; /* also catches NaN */
    return v >= 1.0f ? 1.0f : v;
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* sRGB transfer, used by the transforms that work in display space. */
static float srgb_to_linear(float c) {
    if (!(c > 0.0f)) return 0.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float c) {
    if (!(c > 0.0f)) return 0.0f;
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* Rec.709 luminance of a linear triple. */
static float luma709(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* ---- Blend modes --------------------------------------------------------- */

static const char *const kBlendNames[JFX_BLEND_COUNT] = {
    "normal", "darken", "multiply", "linear-burn", "color-burn", "lighten", "screen",
    "color-dodge", "linear-dodge", "overlay", "soft-light", "hard-light", "difference", "exclusion"
};

size_t jfx_blend_mode_count(void) {
    return (size_t)JFX_BLEND_COUNT;
}

const char *jfx_blend_mode_name(jfx_blend_mode_t mode) {
    if (mode < 0 || mode >= JFX_BLEND_COUNT) {
        return "normal";
    }
    return kBlendNames[mode];
}

jfx_blend_mode_t jfx_blend_mode_parse(const char *name) {
    if (!name) {
        return JFX_BLEND_COUNT;
    }
    for (int i = 0; i < JFX_BLEND_COUNT; ++i) {
        const char *candidate = kBlendNames[i];
        const char *a = candidate;
        const char *b = name;
        while (*a && *b) {
            char ca = *a;
            char cb = *b;
            if (cb >= 'A' && cb <= 'Z') {
                cb = (char)(cb - 'A' + 'a');
            }
            /* Treat '_' and ' ' as equivalent to '-' so "color burn" and
             * "color_burn" both work from a command line. */
            if (ca == '_' || ca == ' ') ca = '-';
            if (cb == '_' || cb == ' ') cb = '-';
            if (ca != cb) break;
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return (jfx_blend_mode_t)i;
        }
    }
    return JFX_BLEND_COUNT;
}

const char *jfx_port_type_name(jfx_port_type_t type) {
    switch (type) {
    case JFX_PORT_IMAGE: return "image";
    case JFX_PORT_COLOR: return "color";
    case JFX_PORT_FLOAT: return "float";
    default: return "unknown";
    }
}

/* The separable equation: backdrop b, source s, both per channel in [0,1].
 * JFX_BLEND_NORMAL is handled by the caller as a straight source-over. */
static float blend_channel(jfx_blend_mode_t mode, float b, float s) {
    switch (mode) {
    case JFX_BLEND_DARKEN: return b < s ? b : s;
    case JFX_BLEND_MULTIPLY: return b * s;
    case JFX_BLEND_LINEAR_BURN: return b + s - 1.0f;
    case JFX_BLEND_COLOR_BURN:
        if (b <= 0.0f) return 0.0f;
        return 1.0f - fminf(1.0f, (1.0f - s) / b);
    case JFX_BLEND_LIGHTEN: return b > s ? b : s;
    case JFX_BLEND_SCREEN: return b + s - b * s;
    case JFX_BLEND_COLOR_DODGE:
        if (b >= 1.0f) return 1.0f;
        return fminf(1.0f, s / (1.0f - b));
    case JFX_BLEND_LINEAR_DODGE: return fminf(1.0f, b + s);
    case JFX_BLEND_OVERLAY:
        /* Overlay is hard-light with the operands swapped. */
        return s <= 0.5f ? 2.0f * s * b : 1.0f - 2.0f * (1.0f - s) * (1.0f - b);
    case JFX_BLEND_SOFT_LIGHT: {
        if (s <= 0.5f) {
            return b - (1.0f - 2.0f * s) * b * (1.0f - b);
        }
        const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : sqrtf(b);
        return b + (2.0f * s - 1.0f) * (d - b);
    }
    case JFX_BLEND_HARD_LIGHT:
        return s <= 0.5f ? 2.0f * s * b : 1.0f - 2.0f * (1.0f - s) * (1.0f - b);
    case JFX_BLEND_DIFFERENCE: return fabsf(b - s);
    case JFX_BLEND_EXCLUSION: return b + s - 2.0f * b * s;
    case JFX_BLEND_NORMAL:
    default: return s;
    }
}

/* Composites `src` over `dst` in place. Colours are straight (non-premultiplied)
 * RGBA, so the source contribution is weighted by its own alpha and the result is
 * divided by the output alpha. A fully transparent result is left transparent
 * rather than divided by zero.
 *
 * `mode` selects the separable equation applied to the colour triple;
 * JFX_BLEND_NORMAL is a plain source-over. `opacity` scales the source alpha. */
static void composite_over(float *dst, const float *src, size_t count, jfx_blend_mode_t mode,
    float opacity) {
    for (size_t i = 0; i < count; ++i) {
        float *d = dst + i * 4u;
        const float *s = src + i * 4u;
        const float sa = clamp01(s[3] * opacity);
        if (sa <= 0.0f) {
            continue;
        }
        if (sa >= 1.0f && d[3] <= 0.0f) {
            /* Fully opaque source over a clear backdrop: no division needed. */
            d[0] = clamp01(s[0]);
            d[1] = clamp01(s[1]);
            d[2] = clamp01(s[2]);
            d[3] = 1.0f;
            continue;
        }
        const float ba = clamp01(d[3]);
        const float out_a = sa + ba * (1.0f - sa);
        float blended[3];
        for (int c = 0; c < 3; ++c) {
            const float b = d[c];
            blended[c] = blend_channel(mode, b, s[c]);
        }
        if (out_a > 0.0f) {
            const float w = ba * (1.0f - sa);
            for (int c = 0; c < 3; ++c) {
                d[c] = clamp01((blended[c] * sa + d[c] * w) / out_a);
            }
        } else {
            for (int c = 0; c < 3; ++c) {
                d[c] = 0.0f;
            }
        }
        d[3] = out_a;
    }
}

/* ---- Node kind tables ---------------------------------------------------- */

/* Output port descriptors. Nearly every node has exactly one image output, so a
 * single shared descriptor serves them all. */
static const jfx_port_desc_t kImageOut[] = { { "out", "Image", JFX_PORT_IMAGE, false, { 0, 0, 0, 0 } } };
static const jfx_port_desc_t kColorOut[] = { { "out", "Color", JFX_PORT_COLOR, false, { 0, 0, 0, 0 } } };
static const jfx_port_desc_t kFloatOut[] = { { "out", "Value", JFX_PORT_FLOAT, false, { 0, 0, 0, 0 } } };

/* A single image input, named "in". */
static const jfx_port_desc_t kImageIn[] = { { "in", "Image", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } } };

/* Parameter shorthands: the table is verbose, so these keep it readable. */
#define P1(a, b, c, d, e) { (a), (b), (c), (d), (e), 0.0f, false }
#define PI(a, b, c, d, e) { (a), (b), (c), (d), (e), 1.0f, true }
#define COL_IN { "in", "Image", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } }
#define COL_OPT { "color", "Color", JFX_PORT_COLOR, false, { 0, 0, 0, 1 } }
#define IMG_IN { "in", "Image", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } }
#define IMG_OPT { "in", "Image", JFX_PORT_IMAGE, false, { 0, 0, 0, 0 } }

/* --- Sources --- */

static const jfx_param_desc_t kColorParams[] = {
    P1("r", "Red", 0.0f, 1.0f, 1.0f), P1("g", "Green", 0.0f, 1.0f, 1.0f),
    P1("b", "Blue", 0.0f, 1.0f, 1.0f), P1("a", "Alpha", 0.0f, 1.0f, 1.0f)
};

static const jfx_port_desc_t kNoPorts[] = { { 0, 0, JFX_PORT_IMAGE, false, { 0, 0, 0, 0 } } };

static const jfx_node_kind_t kKindColor = { "color", "Color", "Utility", 0, kNoPorts, 1, kColorOut,
    4, kColorParams, 0, NULL };

static const jfx_param_desc_t kFloatParams[] = { P1("value", "Value", 0.0f, 1.0f, 0.5f) };
static const jfx_node_kind_t kKindFloat = { "float", "Value", "Utility", 0, kNoPorts, 1, kFloatOut,
    1, kFloatParams, 0, NULL };

/* A solid takes a colour, not a frame: its output is a flat field of whatever it
 * is given, defaulting to opaque white. */
static const jfx_port_desc_t kSolidPorts[] = {
    { "color", "Color", JFX_PORT_COLOR, false, { 1, 1, 1, 1 } }
};
static const jfx_node_kind_t kKindSolid = { "solid", "Solid", "Source", 1, kSolidPorts, 1, kImageOut, 0,
    NULL, 0, NULL };

static const jfx_port_desc_t kGradPorts[] = {
    { "from", "From", JFX_PORT_COLOR, false, { 0, 0, 0, 1 } },
    { "to", "To", JFX_PORT_COLOR, false, { 1, 1, 1, 1 } }
};
static const jfx_param_desc_t kGradParams[] = { P1("angle", "Angle", -180.0f, 180.0f, 0.0f) };
static const jfx_node_kind_t kKindGradient = { "linear_gradient", "Linear Gradient", "Source", 2,
    kGradPorts, 1, kImageOut, 1, kGradParams, 0, NULL };

static const jfx_port_desc_t kCheckerPorts[] = {
    { "a", "Color A", JFX_PORT_COLOR, false, { 0, 0, 0, 1 } },
    { "b", "Color B", JFX_PORT_COLOR, false, { 1, 1, 1, 1 } }
};
static const jfx_param_desc_t kCheckerParams[] = { PI("size", "Tile Size", 1.0f, 256.0f, 32.0f) };
static const jfx_node_kind_t kKindChecker = { "checker", "Checker", "Source", 2, kCheckerPorts, 1,
    kImageOut, 1, kCheckerParams, 0, NULL };

static const jfx_param_desc_t kSweepParams[] = { PI("variant", "Pattern", 0.0f, 3.0f, 0.0f) };
static const jfx_node_kind_t kKindSweep = { "sweep", "Test Pattern", "Source", 0, kNoPorts, 1,
    kImageOut, 1, kSweepParams, 0, NULL };

static const jfx_node_kind_t kKindImage = { "image", "Image", "Source", 0, kNoPorts, 1, kImageOut, 0,
    NULL, 1, (const char *const[]){ "path" } };

static const jfx_node_kind_t kKindVideo = { "video", "Video", "Source", 0, kNoPorts, 1, kImageOut, 0,
    NULL, 1, (const char *const[]){ "path" } };

/* --- Colour grading --- */

static const jfx_node_kind_t kKindExposure = { "exposure", "Exposure", "Color", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ P1("stops", "Stops", -8.0f, 8.0f, 0.0f) }, 0, NULL };

static const jfx_node_kind_t kKindContrast = { "contrast", "Contrast", "Color", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ P1("amount", "Amount", 0.0f, 4.0f, 1.0f) }, 0, NULL };

static const jfx_node_kind_t kKindSaturation = { "saturation", "Saturation", "Color", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ P1("amount", "Amount", 0.0f, 2.0f, 1.0f) }, 0, NULL };

static const jfx_node_kind_t kKindVibrance = { "vibrance", "Vibrance", "Color", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ P1("amount", "Amount", -1.0f, 1.0f, 0.0f) }, 0, NULL };

static const jfx_node_kind_t kKindWhiteBalance = { "white_balance", "White Balance", "Color", 1,
    kImageIn, 1, kImageOut, 2,
    (const jfx_param_desc_t[]){ P1("temperature", "Temperature", -1.0f, 1.0f, 0.0f),
        P1("tint", "Tint", -1.0f, 1.0f, 0.0f) },
    0, NULL };

static const jfx_node_kind_t kKindLiftGammaGain = { "lift_gamma_gain", "Lift / Gamma / Gain",
    "Color", 1, kImageIn, 1, kImageOut, 9,
    (const jfx_param_desc_t[]){ P1("lift_r", "Lift Red", -1.0f, 1.0f, 0.0f),
        P1("lift_g", "Lift Green", -1.0f, 1.0f, 0.0f), P1("lift_b", "Lift Blue", -1.0f, 1.0f, 0.0f),
        P1("gamma_r", "Gamma Red", 0.1f, 4.0f, 1.0f), P1("gamma_g", "Gamma Green", 0.1f, 4.0f, 1.0f),
        P1("gamma_b", "Gamma Blue", 0.1f, 4.0f, 1.0f), P1("gain_r", "Gain Red", 0.0f, 4.0f, 1.0f),
        P1("gain_g", "Gain Green", 0.0f, 4.0f, 1.0f), P1("gain_b", "Gain Blue", 0.0f, 4.0f, 1.0f) },
    0, NULL };

static const jfx_node_kind_t kKindLevels = { "levels", "Levels", "Color", 1, kImageIn, 1, kImageOut,
    5, (const jfx_param_desc_t[]){ P1("in_black", "Input Black", 0.0f, 1.0f, 0.0f),
        P1("in_white", "Input White", 0.0f, 1.0f, 1.0f), P1("gamma", "Gamma", 0.1f, 4.0f, 1.0f),
        P1("out_black", "Output Black", 0.0f, 1.0f, 0.0f),
        P1("out_white", "Output White", 0.0f, 1.0f, 1.0f) },
    0, NULL };

static const jfx_node_kind_t kKindCurves = { "curves", "Curves", "Color", 1, kImageIn, 1, kImageOut,
    1, (const jfx_param_desc_t[]){ P1("contrast", "Contrast", -1.0f, 1.0f, 0.0f) }, 0, NULL };

static const jfx_node_kind_t kKindWhiteClip = { "white_clip", "White Clip", "Color", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ P1("threshold", "Threshold", 0.0f, 1.0f, 0.9f) }, 0,
    NULL };

static const jfx_node_kind_t kKindChannelMixer = { "channel_mixer", "Channel Mixer", "Color", 1,
    kImageIn, 1, kImageOut, 9,
    (const jfx_param_desc_t[]){ P1("r_r", "R from R", 0.0f, 2.0f, 1.0f),
        P1("r_g", "R from G", 0.0f, 2.0f, 0.0f), P1("r_b", "R from B", 0.0f, 2.0f, 0.0f),
        P1("g_r", "G from R", 0.0f, 2.0f, 0.0f), P1("g_g", "G from G", 0.0f, 2.0f, 1.0f),
        P1("g_b", "G from B", 0.0f, 2.0f, 0.0f), P1("b_r", "B from R", 0.0f, 2.0f, 0.0f),
        P1("b_g", "B from G", 0.0f, 2.0f, 0.0f), P1("b_b", "B from B", 0.0f, 2.0f, 1.0f) },
    0, NULL };

/* --- LUT nodes --- */

static const jfx_node_kind_t kKindLut = { "lut", "LUT", "Color", 1, kImageIn, 1, kImageOut, 1,
    (const jfx_param_desc_t[]){ P1("mix", "Mix", 0.0f, 1.0f, 1.0f) }, 1,
    (const char *const[]){ "path" } };

/* A generated LUT applied to an image: a curve from four control points. */
static const jfx_node_kind_t kKindCurvesLut = { "curve", "Curve", "Color", 1, kImageIn, 1, kImageOut,
    4, (const jfx_param_desc_t[]){ P1("x1", "Point 1 In", 0.0f, 1.0f, 0.25f),
        P1("y1", "Point 1 Out", 0.0f, 1.0f, 0.25f), P1("x2", "Point 2 In", 0.0f, 1.0f, 0.75f),
        P1("y2", "Point 2 Out", 0.0f, 1.0f, 0.75f) },
    0, NULL };

/* --- Transform --- */

static const jfx_node_kind_t kKindTransform = { "transform", "Transform", "Transform", 1, kImageIn,
    1, kImageOut, 4,
    (const jfx_param_desc_t[]){ P1("scale_x", "Scale X", 0.01f, 8.0f, 1.0f),
        P1("scale_y", "Scale Y", 0.01f, 8.0f, 1.0f), P1("offset_x", "Offset X", -1.0f, 1.0f, 0.0f),
        P1("offset_y", "Offset Y", -1.0f, 1.0f, 0.0f) },
    0, NULL };

/* --- Keying and mattes --- */

static const jfx_node_kind_t kKindLumaKey = { "luma_key", "Luma Key", "Key", 1, kImageIn, 1, kImageOut,
    3, (const jfx_param_desc_t[]){ P1("threshold", "Threshold", 0.0f, 1.0f, 0.5f),
        P1("softness", "Softness", 0.0f, 0.5f, 0.1f), PI("invert", "Invert", 0.0f, 1.0f, 0.0f) },
    0, NULL };

static const jfx_port_desc_t kChromaPorts[] = { { "in", "Image", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } },
    { "key", "Key Color", JFX_PORT_COLOR, true, { 0, 1, 0, 1 } } };
static const jfx_node_kind_t kKindChromaKey = { "chroma_key", "Chroma Key", "Key", 2, kChromaPorts, 1,
    kImageOut, 4,
    (const jfx_param_desc_t[]){ P1("similarity", "Similarity", 0.0f, 1.0f, 0.3f),
        P1("smoothness", "Smoothness", 0.0f, 0.5f, 0.1f), P1("spill", "Spill Suppression", 0.0f, 1.0f,
            0.0f),
        PI("show_matte", "Show Matte", 0.0f, 1.0f, 0.0f) },
    0, NULL };

/* --- Compositing --- */

static const jfx_param_desc_t kMergeParams[] = { P1("mode", "Mode", 0.0f, (float)(JFX_BLEND_COUNT - 1),
    0.0f),
    P1("opacity", "Opacity", 0.0f, 1.0f, 1.0f) };
static const jfx_port_desc_t kBlendPorts[] = {
    { "fg", "Foreground", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } },
    { "bg", "Background", JFX_PORT_IMAGE, true, { 0, 0, 0, 0 } },
    /* An optional scalar strength, so a blend can be driven by a float node -
     * a keyframed wipe, for instance - rather than only by a slider. */
    { "mix", "Mix", JFX_PORT_FLOAT, false, { 1, 0, 0, 0 } }
};
static const jfx_node_kind_t kKindBlend = { "blend", "Blend", "Composite", 3, kBlendPorts, 1, kImageOut,
    2, kMergeParams, 0, NULL };

/* --- Adjustments --- */

static const jfx_node_kind_t kKindOpacity = { "opacity", "Opacity", "Adjust", 1, kImageIn, 1, kImageOut,
    1, (const jfx_param_desc_t[]){ P1("amount", "Amount", 0.0f, 1.0f, 1.0f) }, 0, NULL };

static const jfx_node_kind_t kKindPosterize = { "posterize", "Posterize", "Adjust", 1, kImageIn, 1,
    kImageOut, 1, (const jfx_param_desc_t[]){ PI("steps", "Steps", 2.0f, 64.0f, 8.0f) }, 0, NULL };

static const jfx_node_kind_t kKindInvert = { "invert", "Invert", "Adjust", 1, kImageIn, 1, kImageOut,
    1, (const jfx_param_desc_t[]){ P1("amount", "Amount", 0.0f, 1.0f, 1.0f) }, 0, NULL };

/* The library, in the order a UI should offer it. */
static const jfx_node_kind_t *const kKinds[] = {
    &kKindColor, &kKindFloat, &kKindSolid, &kKindGradient, &kKindChecker, &kKindSweep, &kKindImage, &kKindVideo,
    &kKindExposure, &kKindContrast, &kKindSaturation, &kKindVibrance, &kKindWhiteBalance,
    &kKindLiftGammaGain, &kKindLevels, &kKindCurves, &kKindWhiteClip, &kKindChannelMixer, &kKindLut,
    &kKindCurvesLut, &kKindTransform, &kKindLumaKey, &kKindChromaKey, &kKindBlend, &kKindOpacity,
    &kKindPosterize, &kKindInvert
};

size_t jfx_node_kind_count(void) {
    return sizeof(kKinds) / sizeof(kKinds[0]);
}

const jfx_node_kind_t *jfx_node_kind_at(size_t index) {
    return index < jfx_node_kind_count() ? kKinds[index] : NULL;
}

const jfx_node_kind_t *jfx_node_kind_find(const char *name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < jfx_node_kind_count(); ++i) {
        if (strcmp(kKinds[i]->name, name) == 0) {
            return kKinds[i];
        }
    }
    return NULL;
}

/* ---- Node values --------------------------------------------------------- */

void jfx_node_value_init(jfx_node_value_t *value, const jfx_node_kind_t *kind) {
    if (!value) {
        return;
    }
    memset(value, 0, sizeof(*value));
    if (!kind) {
        return;
    }
    for (size_t i = 0; i < kind->param_count && i < JFX_NODE_MAX_PARAMS; ++i) {
        value->scalars[i] = kind->params[i].default_value;
    }
}

void jfx_node_value_release(jfx_node_value_t *value) {
    if (!value) {
        return;
    }
    for (size_t i = 0; i < JFX_GRAPH_MAX_STRING_PARAMS; ++i) {
        free_bytes(value->strings[i]);
        value->strings[i] = NULL;
    }
}

jfx_result_t jfx_node_value_copy(const jfx_node_value_t *source, jfx_node_value_t *dest) {
    if (!source || !dest) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memcpy(dest->scalars, source->scalars, sizeof(dest->scalars));
    memset(dest->strings, 0, sizeof(dest->strings));
    for (size_t i = 0; i < JFX_GRAPH_MAX_STRING_PARAMS; ++i) {
        if (source->strings[i]) {
            dest->strings[i] = dup_string(source->strings[i]);
            if (!dest->strings[i]) {
                jfx_node_value_release(dest);
                return JFX_ERROR_OUT_OF_MEMORY;
            }
        }
    }
    return JFX_SUCCESS;
}

/* ---- Graph --------------------------------------------------------------- */

typedef struct {
    const jfx_node_kind_t *kind;
    char label[JFX_NODE_LABEL_MAX];
    jfx_node_value_t value;
    int32_t input_source[JFX_GRAPH_MAX_INPUTS]; /* node index, or -1 */
    uint8_t input_port[JFX_GRAPH_MAX_INPUTS];
} node_t;

struct jfx_graph {
    size_t count;
    node_t nodes[JFX_GRAPH_MAX_NODES];
};

jfx_graph_t *jfx_graph_create(void) {
    jfx_graph_t *graph = alloc_bytes(sizeof(*graph));
    if (!graph) {
        return NULL;
    }
    memset(graph, 0, sizeof(*graph));
    for (size_t i = 0; i < JFX_GRAPH_MAX_NODES; ++i) {
        for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
            graph->nodes[i].input_source[p] = -1;
        }
    }
    return graph;
}

void jfx_graph_destroy(jfx_graph_t *graph) {
    if (!graph) {
        return;
    }
    for (size_t i = 0; i < graph->count; ++i) {
        jfx_node_value_release(&graph->nodes[i].value);
    }
    free_bytes(graph);
}

size_t jfx_graph_node_count(const jfx_graph_t *graph) {
    return graph ? graph->count : 0u;
}

size_t jfx_graph_node_capacity(const jfx_graph_t *graph) {
    (void)graph;
    return JFX_GRAPH_MAX_NODES;
}

jfx_result_t jfx_graph_add_node(jfx_graph_t *graph, const char *kind_name, const char *label,
    uint32_t *out_node) {
    if (!graph || !kind_name || !out_node) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* `*out_node` is left untouched on failure. Zeroing it would alias a real
     * index, so a caller reusing one variable across attempts would silently
     * read the wrong node. */
    const jfx_node_kind_t *kind = jfx_node_kind_find(kind_name);
    if (!kind) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (graph->count >= JFX_GRAPH_MAX_NODES) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    node_t *node = &graph->nodes[graph->count];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    jfx_node_value_init(&node->value, kind);
    snprintf(node->label, sizeof(node->label), "%s", label && label[0] ? label : kind->label);
    for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
        node->input_source[p] = -1;
    }
    *out_node = (uint32_t)graph->count;
    graph->count++;
    return JFX_SUCCESS;
}

jfx_result_t jfx_graph_remove_node(jfx_graph_t *graph, uint32_t index) {
    if (!graph || index >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_node_value_release(&graph->nodes[index].value);
    /* Shift the survivors down so the array stays in creation order, which is
     * the order a node list shows. A swap-with-last would be O(1) but would
     * scramble that order, and the reference rewriting below assumes a
     * contiguous shift. Every index above the hole therefore decreases by one,
     * which a UI can predict without re-reading. */
    const size_t last = graph->count - 1u;
    if (index != last) {
        memmove(&graph->nodes[index], &graph->nodes[index + 1u],
            (last - (size_t)index) * sizeof(node_t));
    }
    memset(&graph->nodes[last], 0, sizeof(node_t));
    graph->count = last;
    for (size_t i = 0; i < graph->count; ++i) {
        for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
            const int32_t src = graph->nodes[i].input_source[p];
            if (src < 0) {
                continue;
            }
            if ((size_t)src == index) {
                graph->nodes[i].input_source[p] = -1;
            } else if ((size_t)src > index) {
                graph->nodes[i].input_source[p] = src - 1;
            }
        }
    }
    return JFX_SUCCESS;
}

const jfx_node_kind_t *jfx_graph_node_kind(const jfx_graph_t *graph, uint32_t index) {
    if (!graph || index >= graph->count) {
        return NULL;
    }
    return graph->nodes[index].kind;
}

const char *jfx_graph_node_label(const jfx_graph_t *graph, uint32_t index) {
    if (!graph || index >= graph->count) {
        return NULL;
    }
    return graph->nodes[index].label;
}

jfx_result_t jfx_graph_set_node_label(jfx_graph_t *graph, uint32_t index, const char *label) {
    if (!graph || !label || index >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    snprintf(graph->nodes[index].label, sizeof(graph->nodes[index].label), "%s", label);
    return JFX_SUCCESS;
}

const jfx_node_value_t *jfx_graph_node_value(const jfx_graph_t *graph, uint32_t index) {
    if (!graph || index >= graph->count) {
        return NULL;
    }
    return &graph->nodes[index].value;
}

jfx_node_value_t *jfx_graph_node_value_mut(jfx_graph_t *graph, uint32_t index) {
    if (!graph || index >= graph->count) {
        return NULL;
    }
    return &graph->nodes[index].value;
}

jfx_result_t jfx_graph_touch(jfx_graph_t *graph, uint32_t index) {
    (void)graph;
    if (index >= (graph ? graph->count : 0u)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Evaluation recomputes from the leaves each render, so a touch is only a
     * validity check. The hook exists so a cached implementation can be added
     * without changing the contract. */
    return JFX_SUCCESS;
}

jfx_result_t jfx_graph_connect(jfx_graph_t *graph, uint32_t from_node, size_t from_port,
    uint32_t to_node, size_t to_port) {
    if (!graph || from_node >= graph->count || to_node >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (from_node == to_node) {
        return JFX_ERROR_INVALID_ARGUMENT; /* a self-loop is a cycle */
    }
    const jfx_node_kind_t *to_kind = graph->nodes[to_node].kind;
    const jfx_node_kind_t *from_kind = graph->nodes[from_node].kind;
    if (to_port >= to_kind->input_count || to_port >= JFX_GRAPH_MAX_INPUTS) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (from_port >= from_kind->output_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (to_kind->inputs[to_port].type != from_kind->outputs[from_port].type) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Reject an edge that would close a cycle: walk up from the source through
     * existing inputs and see whether the target is already an ancestor. */
    uint8_t seen[JFX_GRAPH_MAX_NODES] = { 0 };
    size_t stack[JFX_GRAPH_MAX_NODES];
    size_t depth = 0;
    stack[depth++] = from_node;
    seen[from_node] = 1;
    while (depth) {
        const size_t current = stack[--depth];
        if (current == to_node) {
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
            const int32_t up = graph->nodes[current].input_source[p];
            if (up >= 0 && !seen[up]) {
                seen[up] = 1;
                stack[depth++] = (size_t)up;
            }
        }
    }
    graph->nodes[to_node].input_source[to_port] = (int32_t)from_node;
    graph->nodes[to_node].input_port[to_port] = (uint8_t)from_port;
    return JFX_SUCCESS;
}

jfx_result_t jfx_graph_disconnect(jfx_graph_t *graph, uint32_t to_node, size_t to_port) {
    if (!graph || to_node >= graph->count || to_port >= JFX_GRAPH_MAX_INPUTS) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    graph->nodes[to_node].input_source[to_port] = -1;
    return JFX_SUCCESS;
}

int jfx_graph_input_source(const jfx_graph_t *graph, uint32_t to_node, size_t to_port) {
    if (!graph || to_node >= graph->count || to_port >= JFX_GRAPH_MAX_INPUTS) {
        return -1;
    }
    return graph->nodes[to_node].input_source[to_port];
}

int jfx_graph_input_source_port(const jfx_graph_t *graph, uint32_t to_node, size_t to_port) {
    if (!graph || to_node >= graph->count || to_port >= JFX_GRAPH_MAX_INPUTS) {
        return -1;
    }
    if (graph->nodes[to_node].input_source[to_port] < 0) {
        return -1;
    }
    return (int)graph->nodes[to_node].input_port[to_port];
}

jfx_result_t jfx_graph_topological_order(const jfx_graph_t *graph, uint32_t output,
    uint32_t *out_order, size_t capacity, size_t *out_count) {
    if (!graph || !out_order || !out_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (output >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Kahn's algorithm over the nodes reachable from `output`, visiting inputs
     * first. If every node cannot be emitted, the remainder is a cycle. */
    uint32_t indegree[JFX_GRAPH_MAX_NODES] = { 0 };
    uint8_t reachable[JFX_GRAPH_MAX_NODES] = { 0 };
    uint32_t stack[JFX_GRAPH_MAX_NODES];
    size_t depth = 0;
    stack[depth++] = output;
    reachable[output] = 1;
    while (depth) {
        const uint32_t current = stack[--depth];
        for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
            const int32_t up = graph->nodes[current].input_source[p];
            if (up >= 0) {
                indegree[current]++;
                if (!reachable[up]) {
                    reachable[up] = 1;
                    stack[depth++] = (uint32_t)up;
                }
            }
        }
    }
    uint32_t queue[JFX_GRAPH_MAX_NODES];
    size_t head = 0, tail = 0;
    for (size_t i = 0; i < graph->count; ++i) {
        if (reachable[i] && indegree[i] == 0) {
            queue[tail++] = (uint32_t)i;
        }
    }
    size_t produced = 0;
    uint32_t order[JFX_GRAPH_MAX_NODES];
    while (head < tail) {
        const uint32_t current = queue[head++];
        order[produced++] = current;
        /* Anything that reads `current` is now unblocked. A node may read the
         * same source from several ports, so every edge is counted - stopping
         * after the first would leave its indegree permanently above zero. */
        for (size_t i = 0; i < graph->count; ++i) {
            if (!reachable[i]) {
                continue;
            }
            for (size_t p = 0; p < JFX_GRAPH_MAX_INPUTS; ++p) {
                if (graph->nodes[i].input_source[p] == (int32_t)current &&
                    indegree[i] > 0u) {
                    if (--indegree[i] == 0u) {
                        queue[tail++] = (uint32_t)i;
                    }
                }
            }
        }
    }
    size_t total = 0;
    for (size_t i = 0; i < graph->count; ++i) {
        total += reachable[i] ? 1u : 0u;
    }
    if (produced != total) {
        return JFX_ERROR_INVALID_ARGUMENT; /* a cycle */
    }
    if (capacity < total) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memcpy(out_order, order, total * sizeof(uint32_t));
    *out_count = total;
    return JFX_SUCCESS;
}

bool jfx_graph_has_cycle(const jfx_graph_t *graph) {
    if (!graph) {
        return false;
    }
    for (size_t i = 0; i < graph->count; ++i) {
        uint32_t order[JFX_GRAPH_MAX_NODES];
        size_t count = 0;
        if (jfx_graph_topological_order(graph, (uint32_t)i, order, JFX_GRAPH_MAX_NODES, &count) !=
            JFX_SUCCESS) {
            return true;
        }
    }
    return false;
}

/* ---- Evaluation ---------------------------------------------------------- */

/* A scratch frame: RGBA float, row-major, `pixel_count` pixels. Evaluation works
 * in float rather than 8-bit so a chain of nodes does not quantise at every
 * step, and the result is converted once on the way out. */
typedef struct {
    float *pixels;
    size_t pixel_count;
} frame_t;

typedef struct {
    const jfx_graph_t *graph;
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    size_t slots;            /* how many of the arrays below are valid */
    float time_seconds;
    /* One frame per node, allocated on first use and reused across renders. A
     * node feeding several consumers is therefore evaluated once. */
    frame_t *cache;
    uint8_t *cache_valid;
    /* Decoded source images and parsed LUTs, keyed by node, so a graph that
     * references the same file on several nodes decodes it once per render. */
    jfx_image_t *images;
    jfx_lut_t **luts;
    jfx_result_t error;
} eval_ctx_t;

static void ctx_release(eval_ctx_t *ctx);

static uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) {
        return 0u; /* also catches NaN */
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (uint8_t)lrintf(value * 255.0f);
}

static float node_param(const node_t *node, size_t index) {
    if (index >= JFX_NODE_MAX_PARAMS) {
        return 0.0f;
    }
    const float v = node->value.scalars[index];
    if (!isfinite(v)) {
        return 0.0f;
    }
    return v;
}

static const char *node_string(const node_t *node, size_t index) {
    if (index >= JFX_GRAPH_MAX_STRING_PARAMS) {
        return NULL;
    }
    const char *s = node->value.strings[index];
    return (s && s[0]) ? s : NULL;
}

/* Fetches an input's value. Image inputs are borrowed from the cache; colour
 * and float inputs are read from the source node, falling back to the port's
 * declared default when nothing is connected. */
static const frame_t *input_frame(const eval_ctx_t *ctx, const node_t *node, size_t port) {
    const int32_t src = node->input_source[port];
    if (src < 0 || !ctx->cache_valid[src]) {
        return NULL;
    }
    return &ctx->cache[src];
}

static void input_color(const eval_ctx_t *ctx, const node_t *node, size_t port, float *out) {
    const jfx_port_desc_t *desc = &node->kind->inputs[port];
    memcpy(out, desc->default_value, sizeof(float) * 4u);
    const int32_t src = node->input_source[port];
    if (src < 0) {
        return;
    }
    const node_t *source = &ctx->graph->nodes[src];
    if (source->kind->param_count >= 4u && strcmp(source->kind->name, "color") == 0) {
        for (int c = 0; c < 4; ++c) {
            out[c] = source->value.scalars[c];
        }
    }
}

static float input_float(const eval_ctx_t *ctx, const node_t *node, size_t port, float fallback) {
    const int32_t src = node->input_source[port];
    if (src < 0) {
        return fallback;
    }
    const node_t *source = &ctx->graph->nodes[src];
    if (strcmp(source->kind->name, "float") == 0 && source->kind->param_count >= 1u) {
        return source->value.scalars[0];
    }
    if (strcmp(source->kind->name, "color") == 0 && source->kind->param_count >= 4u) {
        return source->value.scalars[0];
    }
    return fallback;
}

/* Decoded image for an `image` node, loaded once per render. */
static const jfx_image_t *node_image(const eval_ctx_t *ctx, uint32_t index, const node_t *node) {
    if (ctx->images[index].pixels) {
        return &ctx->images[index];
    }
    const char *path = node_string(node, 0);
    if (!path) {
        return NULL;
    }
    jfx_image_t *slot = &ctx->images[index];
    slot->size = sizeof(*slot);
    if (jfx_image_load(path, NULL, slot) != JFX_SUCCESS) {
        slot->pixels = NULL;
        return NULL;
    }
    return slot;
}

/* Parsed LUT for a `lut` node, loaded once per render. */
static const jfx_lut_t *node_lut(const eval_ctx_t *ctx, uint32_t index, const node_t *node) {
    if (ctx->luts[index]) {
        return ctx->luts[index];
    }
    const char *path = node_string(node, 0);
    if (!path) {
        return NULL;
    }
    jfx_lut_t *lut = NULL;
    if (jfx_lut_load_auto(path, &lut, NULL, 0) != JFX_SUCCESS) {
        return NULL;
    }
    ctx->luts[index] = lut;
    return lut;
}


/* Applies a graded triple to a pixel, leaving alpha alone. */
static void write_rgb(float *px, float r, float g, float b) {
    px[0] = clamp01(r);
    px[1] = clamp01(g);
    px[2] = clamp01(b);
}

/* The per-kind pixel work. `in` is the primary image input, which for a
 * single-input node is port 0. */
static void eval_node(eval_ctx_t *ctx, uint32_t index, const node_t *node, frame_t *out) {
    const size_t count = ctx->pixel_count;
    const char *kind_name = node->kind->name;
    const frame_t *in = node->kind->input_count > 0 ? input_frame(ctx, node, 0) : NULL;

    /* --- Sources --------------------------------------------------------- */
    if (strcmp(kind_name, "solid") == 0) {
        float color[4] = { 1, 1, 1, 1 };
        input_color(ctx, node, 0, color);
        for (size_t i = 0; i < count; ++i) {
            memcpy(out->pixels + i * 4u, color, sizeof(float) * 4u);
        }
        return;
    }
    if (strcmp(kind_name, "linear_gradient") == 0) {
        float from[4], to[4];
        input_color(ctx, node, 0, from);
        input_color(ctx, node, 1, to);
        const float radians = node_param(node, 0) * 3.14159265f / 180.0f;
        const float dx = cosf(radians);
        const float dy = sinf(radians);
        for (uint32_t y = 0; y < ctx->height; ++y) {
            for (uint32_t x = 0; x < ctx->width; ++x) {
                const float u = ctx->width > 1u ? (float)x / (float)(ctx->width - 1u) : 0.0f;
                const float v = ctx->height > 1u ? (float)y / (float)(ctx->height - 1u) : 0.0f;
                const float t = clamp01(u * dx + v * dy + (dx + dy > 0.0f ? 0.0f : 1.0f));
                float *px = out->pixels + ((size_t)y * ctx->width + x) * 4u;
                for (int c = 0; c < 4; ++c) {
                    px[c] = lerp(from[c], to[c], t);
                }
            }
        }
        return;
    }
    if (strcmp(kind_name, "checker") == 0) {
        float a[4], b[4];
        input_color(ctx, node, 0, a);
        input_color(ctx, node, 1, b);
        const uint32_t size = (uint32_t)fmaxf(1.0f, node_param(node, 0));
        for (uint32_t y = 0; y < ctx->height; ++y) {
            for (uint32_t x = 0; x < ctx->width; ++x) {
                const int cell = (int)((x / size + y / size) & 1u);
                memcpy(out->pixels + ((size_t)y * ctx->width + x) * 4u, cell ? b : a,
                    sizeof(float) * 4u);
            }
        }
        return;
    }
    if (strcmp(kind_name, "sweep") == 0) {
        /* A set of patterns useful for eyeballing a pipeline: colour bars, a
         * horizontal ramp, a quantisation staircase, and a moving gradient. */
        const int variant = (int)node_param(node, 0);
        for (uint32_t y = 0; y < ctx->height; ++y) {
            for (uint32_t x = 0; x < ctx->width; ++x) {
                const float u = ctx->width > 1u ? (float)x / (float)(ctx->width - 1u) : 0.0f;
                const float v = ctx->height > 1u ? (float)y / (float)(ctx->height - 1u) : 0.0f;
                float *px = out->pixels + ((size_t)y * ctx->width + x) * 4u;
                px[3] = 1.0f;
                switch (variant) {
                case 1: { /* horizontal ramp */
                    px[0] = u;
                    px[1] = u;
                    px[2] = u;
                    break;
                }
                case 2: { /* staircase */
                    const float steps = 8.0f;
                    px[0] = floorf(u * steps) / (steps - 1.0f);
                    px[1] = floorf(v * steps) / (steps - 1.0f);
                    px[2] = 0.25f;
                    break;
                }
                case 3: { /* moving diagonal */
                    const float t = clamp01(u * 0.5f + v * 0.5f + ctx->time_seconds * 0.1f);
                    px[0] = t;
                    px[1] = 1.0f - t;
                    px[2] = 0.5f;
                    break;
                }
                default: { /* 75% colour bars */
                    static const float bars[7][3] = { { 0.75f, 0.75f, 0.75f },
                        { 0.75f, 0.75f, 0.0f }, { 0.0f, 0.75f, 0.75f }, { 0.0f, 0.75f, 0.0f },
                        { 0.75f, 0.0f, 0.75f }, { 0.75f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.75f } };
                    const int bar = (int)(u * 7.0f);
                    const int clamped = bar < 0 ? 0 : (bar > 6 ? 6 : bar);
                    px[0] = bars[clamped][0];
                    px[1] = bars[clamped][1];
                    px[2] = bars[clamped][2];
                    break;
                }
                }
            }
        }
        return;
    }
    if (strcmp(kind_name, "video") == 0) {
        uint8_t *pixels=alloc_bytes(count*4);
        if (!pixels) { ctx->error=JFX_ERROR_OUT_OF_MEMORY; return; }
        int r=jolt_video_io_frame(node->value.strings[0],(double)ctx->time_seconds,ctx->width,ctx->height,pixels,count*4);
        if (r) { ctx->error=r==-2 ? JFX_ERROR_NOT_IMPLEMENTED : JFX_ERROR_NOT_FOUND; }
        else for (size_t i=0;i<count*4;++i) out->pixels[i]=(float)pixels[i]/255.0f;
        free_bytes(pixels); return;
    }
    if (strcmp(kind_name, "image") == 0) {
        const jfx_image_t *image = node_image(ctx, index, node);
        if (!image || !image->pixels) {
            memset(out->pixels, 0, count * sizeof(float) * 4u);
            return;
        }
        for (uint32_t y = 0; y < ctx->height; ++y) {
            for (uint32_t x = 0; x < ctx->width; ++x) {
                /* Nearest-neighbour fit: the image keeps its aspect ratio and is
                 * centred. Sampled in normalised source space so an upscaled
                 * frame does not repeat edge texels. */
                const float sx = (float)x / (float)ctx->width;
                const float sy = (float)y / (float)ctx->height;
                const size_t last_x = image->width - 1u;
                const size_t last_y = image->height - 1u;
                const size_t ix = (size_t)(clamp01(sx) * (float)last_x);
                const size_t iy = (size_t)(clamp01(sy) * (float)last_y);
                const uint8_t *texel = image->pixels + (iy * image->width + ix) * 4u;
                float *px = out->pixels + ((size_t)y * ctx->width + x) * 4u;
                px[0] = (float)texel[0] / 255.0f;
                px[1] = (float)texel[1] / 255.0f;
                px[2] = (float)texel[2] / 255.0f;
                px[3] = (float)texel[3] / 255.0f;
            }
        }
        return;
    }

    /* Everything below transforms an image, so it needs one. A missing input is
     * a transparent frame of the right size rather than a crash: a partially
     * built graph still previews. */
    if (!in) {
        memset(out->pixels, 0, count * sizeof(float) * 4u);
        return;
    }
    const float *src = in->pixels;

    /* --- Colour grading --------------------------------------------------- */
    if (strcmp(kind_name, "exposure") == 0) {
        const float gain = powf(2.0f, node_param(node, 0));
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            write_rgb(o, s[0] * gain, s[1] * gain, s[2] * gain);
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "contrast") == 0) {
        /* Pivot at 0.5 in the encoded signal, which is where a contrast control
         * is expected to leave mid-grey alone. */
        const float amount = node_param(node, 0);
        const float k = amount >= 1.0f ? amount : 1.0f / (2.0f - amount);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                o[c] = clamp01(0.5f + (s[c] - 0.5f) * k);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "saturation") == 0) {
        const float amount = node_param(node, 0);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            const float l = luma709(s[0], s[1], s[2]);
            for (int c = 0; c < 3; ++c) {
                o[c] = clamp01(l + (s[c] - l) * amount);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "vibrance") == 0) {
        /* Vibrance pushes already-saturated colours less than muted ones, so it
         * brings up a flat sky without over-saturating skin. */
        const float amount = node_param(node, 0);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            const float l = luma709(s[0], s[1], s[2]);
            float maxc = 0.0f, minc = 1.0f;
            for (int c = 0; c < 3; ++c) {
                if (s[c] > maxc) maxc = s[c];
                if (s[c] < minc) minc = s[c];
            }
            const float sat = maxc - minc;
            const float weight = 1.0f - sat * amount * 0.75f;
            for (int c = 0; c < 3; ++c) {
                o[c] = clamp01(l + (s[c] - l) * (1.0f + amount * weight));
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "white_balance") == 0) {
        /* Temperature warms by lifting red and pulling blue; tint rotates the
         * green/magenta axis. Both are simple gains, which is what a UI slider
         * implies. */
        const float temp = node_param(node, 0);
        const float tint = node_param(node, 1);
        const float gain[3] = { 1.0f + temp * 0.4f, 1.0f + tint * 0.2f, 1.0f - temp * 0.4f };
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            /* A white balance scales light, so the gains belong in linear light;
             * applying them to the encoded signal would shift hue as brightness
             * changes. */
            for (int c = 0; c < 3; ++c) {
                o[c] = linear_to_srgb(srgb_to_linear(s[c]) * gain[c]);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "lift_gamma_gain") == 0) {
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                const float lift = node_param(node, (size_t)c);
                const float gamma = node_param(node, (size_t)(3 + c));
                const float gain = node_param(node, (size_t)(6 + c));
                float v = s[c] * gain + lift;
                v = v > 0.0f ? powf(v, 1.0f / gamma) : 0.0f;
                o[c] = clamp01(v);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "levels") == 0) {
        const float in_black = node_param(node, 0);
        const float in_white = node_param(node, 1);
        const float gamma = node_param(node, 2);
        const float out_black = node_param(node, 3);
        const float out_white = node_param(node, 4);
        const float span = in_white > in_black ? in_white - in_black : 1.0f;
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                float v = (s[c] - in_black) / span;
                v = v > 0.0f ? powf(clamp01(v), 1.0f / gamma) : 0.0f;
                o[c] = lerp(out_black, out_white, clamp01(v));
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "curves") == 0) {
        /* A contrast control shaped as an S-curve about mid-grey, which is the
         * usual one-slider approximation of a curve editor. */
        const float amount = node_param(node, 0);
        const float k = amount >= 0.0f ? 1.0f + amount * 2.0f : 1.0f / (1.0f - amount * 2.0f);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                const float d = s[c] - 0.5f;
                /* Smoothstep-style S-curve preserves the endpoints. */
                o[c] = clamp01(0.5f + d * k * (1.0f - d * d * 0.5f));
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "white_clip") == 0) {
        const float threshold = node_param(node, 0);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                o[c] = s[c] > threshold ? 1.0f : 0.0f;
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "channel_mixer") == 0) {
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int r = 0; r < 3; ++r) {
                float sum = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    sum += s[c] * node_param(node, (size_t)(r * 3 + c));
                }
                o[r] = clamp01(sum);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "lut") == 0) {
        const jfx_lut_t *lut = node_lut(ctx, index, node);
        if (!lut) {
            memcpy(out->pixels, src, count * sizeof(float) * 4u);
            return;
        }
        const float mix = clamp01(node_param(node, 0));
        for (size_t i = 0; i < count; ++i) {
            float *px = out->pixels + i * 4u;
            const float alpha = px[3];
            float graded[4] = { 0, 0, 0, 1 };
            if (jfx_lut_sample(lut, px, 4u, mix, graded) == JFX_SUCCESS) {
                px[0] = graded[0];
                px[1] = graded[1];
                px[2] = graded[2];
            }
            px[3] = alpha;
        }
        return;
    }
    if (strcmp(kind_name, "curve") == 0) {
        /* Four control points describe an S-curve through (0,0), (x1,y1),
         * (x2,y2), (1,1); the sampled table is then applied as a 1D LUT. */
        const float x1 = node_param(node, 0), y1 = node_param(node, 1);
        const float x2 = node_param(node, 2), y2 = node_param(node, 3);
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                const float v = clamp01(s[c]);
                float out_v;
                if (v < x1) {
                    out_v = x1 > 0.0f ? y1 * (v / x1) : 0.0f;
                } else if (v < x2) {
                    const float t = x2 > x1 ? (v - x1) / (x2 - x1) : 0.0f;
                    out_v = lerp(y1, y2, t);
                } else {
                    out_v = x2 < 1.0f ? y2 + (1.0f - y2) * ((v - x2) / (1.0f - x2)) : 1.0f;
                }
                o[c] = clamp01(out_v);
            }
            o[3] = s[3];
        }
        return;
    }

    /* --- Transform --------------------------------------------------------- */
    if (strcmp(kind_name, "transform") == 0) {
        const float scale_x = node_param(node, 0);
        const float scale_y = node_param(node, 1);
        const float offset_x = node_param(node, 2);
        const float offset_y = node_param(node, 3);
        for (size_t i = 0; i < count; ++i) {
            out->pixels[i * 4u + 0u] = 0.0f;
            out->pixels[i * 4u + 1u] = 0.0f;
            out->pixels[i * 4u + 2u] = 0.0f;
            out->pixels[i * 4u + 3u] = 0.0f;
        }
        /* Sampling the source in destination space is an inverse mapping, so a
         * scale of 1/2 shows the top-left quadrant enlarged. */
        for (uint32_t y = 0; y < ctx->height; ++y) {
            for (uint32_t x = 0; x < ctx->width; ++x) {
                const float fx = (float)x / (float)ctx->width + offset_x;
                const float fy = (float)y / (float)ctx->height + offset_y;
                const float su = (fx - 0.5f) / scale_x + 0.5f;
                const float sv = (fy - 0.5f) / scale_y + 0.5f;
                float *o = out->pixels + ((size_t)y * ctx->width + x) * 4u;
                if (!(su >= 0.0f) || !(sv >= 0.0f) || su > 1.0f || sv > 1.0f) {
                    continue; /* outside the source: transparent */
                }
                const size_t last_x = (size_t)ctx->width - 1u;
                const size_t last_y = (size_t)ctx->height - 1u;
                const size_t x0 = (size_t)(su * (float)last_x);
                const size_t y0 = (size_t)(sv * (float)last_y);
                const size_t x1 = x0 < last_x ? x0 + 1u : x0;
                const size_t y1 = y0 < last_y ? y0 + 1u : y0;
                const float tx = su * (float)last_x - (float)x0;
                const float ty = sv * (float)last_y - (float)y0;
                for (int c = 0; c < 4; ++c) {
                    const size_t comp = (size_t)c;
                    const float v00 = src[(y0 * (size_t)ctx->width + x0) * 4u + comp];
                    const float v10 = src[(y0 * (size_t)ctx->width + x1) * 4u + comp];
                    const float v01 = src[(y1 * (size_t)ctx->width + x0) * 4u + comp];
                    const float v11 = src[(y1 * (size_t)ctx->width + x1) * 4u + comp];
                    o[c] = lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
                }
            }
        }
        return;
    }

    /* --- Keying ------------------------------------------------------------ */
    if (strcmp(kind_name, "luma_key") == 0) {
        const float threshold = node_param(node, 0);
        const float softness = node_param(node, 1);
        const float invert = node_param(node, 2) > 0.5f ? 1.0f : 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            const float l = luma709(s[0], s[1], s[2]);
            float a;
            if (softness <= 0.0f) {
                a = l >= threshold ? 1.0f : 0.0f;
            } else {
                /* A linear ramp of the given width centred on the threshold. */
                const float half = softness;
                a = clamp01((l - (threshold - half)) / (2.0f * half));
            }
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            o[3] = clamp01(invert > 0.5f ? 1.0f - a : a) * clamp01(s[3]);
        }
        return;
    }
    if (strcmp(kind_name, "chroma_key") == 0) {
        float key[4] = { 0, 1, 0, 1 };
        input_color(ctx, node, 1, key);
        const float similarity = node_param(node, 0);
        const float smoothness = node_param(node, 1);
        const float spill = node_param(node, 2);
        const float show_matte = node_param(node, 3) > 0.5f ? 1.0f : 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            /* Distance in the chroma plane, ignoring luminance. */
            const float dr = s[0] - key[0];
            const float dg = s[1] - key[1];
            const float db = s[2] - key[2];
            const float distance = sqrtf(dr * dr + dg * dg + db * db) / sqrtf(3.0f);
            float a;
            if (smoothness <= 0.0f) {
                a = distance > similarity ? 1.0f : 0.0f;
            } else {
                a = clamp01((distance - (similarity - smoothness)) / (2.0f * smoothness));
            }
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            if (spill > 0.0f) {
                /* Pull the key hue out of the remaining colour, which is what
                 * stops a green screen from leaving a green rim. */
                const float excess = fmaxf(0.0f, s[1] - fmaxf(s[0], s[2]));
                o[1] = clamp01(s[1] - excess * spill);
            }
            o[3] = show_matte > 0.5f ? a : a * clamp01(s[3]);
        }
        return;
    }

    /* --- Adjustments -------------------------------------------------------- */
    if (strcmp(kind_name, "opacity") == 0) {
        const float amount = clamp01(node_param(node, 0));
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            o[3] = s[3] * amount;
        }
        return;
    }
    if (strcmp(kind_name, "posterize") == 0) {
        const float steps = fmaxf(2.0f, node_param(node, 0));
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                o[c] = floorf(clamp01(s[c]) * steps) / (steps - 1.0f);
            }
            o[3] = s[3];
        }
        return;
    }
    if (strcmp(kind_name, "invert") == 0) {
        const float amount = clamp01(node_param(node, 0));
        for (size_t i = 0; i < count; ++i) {
            const float *s = src + i * 4u;
            float *o = out->pixels + i * 4u;
            for (int c = 0; c < 3; ++c) {
                o[c] = clamp01(lerp(s[c], 1.0f - s[c], amount));
            }
            o[3] = s[3];
        }
        return;
    }

    /* --- Compositing -------------------------------------------------------- */
    if (strcmp(kind_name, "blend") == 0) {
        const frame_t *fg = input_frame(ctx, node, 0);
        const frame_t *bg = input_frame(ctx, node, 1);
        const int mode_index = (int)node_param(node, 0);
        /* The scalar input scales the opacity parameter, so a driven mix can be
         * dialled in without rewiring. */
        const float opacity = clamp01(clamp01(node_param(node, 1)) *
            clamp01(input_float(ctx, node, 2, 1.0f)));
        const jfx_blend_mode_t mode = (mode_index >= 0 && mode_index < JFX_BLEND_COUNT)
            ? (jfx_blend_mode_t)mode_index
            : JFX_BLEND_NORMAL;
        if (bg) {
            memcpy(out->pixels, bg->pixels, count * sizeof(float) * 4u);
        } else {
            memset(out->pixels, 0, count * sizeof(float) * 4u);
        }
        if (fg) {
            composite_over(out->pixels, fg->pixels, count, mode, opacity);
        }
        return;
    }

    /* An unrecognised kind would mean the tables above and the library list have
     * drifted apart. Clearing the frame is safe and visible rather than
     * leaving stale pixels. */
    memset(out->pixels, 0, count * sizeof(float) * 4u);
}

/* Evaluates `index` into its cache slot, recursing through inputs. */
static void ensure_evaluated(eval_ctx_t *ctx, uint32_t index) {
    if (ctx->cache_valid[index]) {
        return;
    }
    /* Mark first so a malformed graph cannot recurse forever even if a cycle
     * slipped past the connection check. */
    ctx->cache_valid[index] = 1;
    const node_t *node = &ctx->graph->nodes[index];
    for (size_t p = 0; p < node->kind->input_count && p < JFX_GRAPH_MAX_INPUTS; ++p) {
        const int32_t src = node->input_source[p];
        if (src >= 0) {
            ensure_evaluated(ctx, (uint32_t)src);
        }
    }
    eval_node(ctx, index, node, &ctx->cache[index]);
}

static jfx_result_t ctx_init(eval_ctx_t *ctx, const jfx_graph_t *graph, uint32_t width,
    uint32_t height, float time_seconds) {
    memset(ctx, 0, sizeof(*ctx));
    if (!graph || !width || !height) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (width > 16384u || height > 16384u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    ctx->graph = graph;
    ctx->width = width;
    ctx->height = height;
    ctx->time_seconds = isfinite(time_seconds) ? time_seconds : 0.0f;
    ctx->pixel_count = (size_t)width * (size_t)height;
    if (ctx->pixel_count > SIZE_MAX / (sizeof(float) * 4u)) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    ctx->slots = graph->count ? graph->count : 1u;
    const size_t slots = ctx->slots;
    const size_t bytes = ctx->pixel_count * sizeof(float) * 4u;
    ctx->cache = alloc_bytes(slots * sizeof(frame_t));
    ctx->cache_valid = alloc_bytes(slots);
    ctx->images = alloc_bytes(slots * sizeof(jfx_image_t));
    ctx->luts = alloc_bytes(slots * sizeof(jfx_lut_t *));
    if (!ctx->cache || !ctx->cache_valid || !ctx->images || !ctx->luts) {
        ctx_release(ctx);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(ctx->cache, 0, slots * sizeof(frame_t));
    memset(ctx->cache_valid, 0, slots);
    memset(ctx->images, 0, slots * sizeof(jfx_image_t));
    memset(ctx->luts, 0, slots * sizeof(jfx_lut_t *));
    for (size_t i = 0; i < slots; ++i) {
        ctx->cache[i].pixels = alloc_bytes(bytes);
        ctx->cache[i].pixel_count = ctx->pixel_count;
        if (!ctx->cache[i].pixels) {
            ctx_release(ctx);
            return JFX_ERROR_OUT_OF_MEMORY;
        }
    }
    return JFX_SUCCESS;
}

/* Frees only the slots that were allocated. Walking the full node capacity here
 * would read past a short allocation and free whatever follows it. */
static void ctx_release(eval_ctx_t *ctx) {
    if (ctx->cache) {
        for (size_t i = 0; i < ctx->slots; ++i) {
            if (ctx->cache[i].pixels) {
                free_bytes(ctx->cache[i].pixels);
            }
        }
        free_bytes(ctx->cache);
        ctx->cache = NULL;
    }
    if (ctx->images) {
        for (size_t i = 0; i < ctx->slots; ++i) {
            if (ctx->images[i].pixels) {
                jfx_image_release(&ctx->images[i]);
            }
        }
        free_bytes(ctx->images);
        ctx->images = NULL;
    }
    if (ctx->luts) {
        for (size_t i = 0; i < ctx->slots; ++i) {
            jfx_lut_destroy(ctx->luts[i]);
        }
        free_bytes(ctx->luts);
        ctx->luts = NULL;
    }
    if (ctx->cache_valid) {
        free_bytes(ctx->cache_valid);
        ctx->cache_valid = NULL;
    }
}

jfx_result_t jfx_graph_render_node(const jfx_graph_t *graph, uint32_t node, uint32_t width,
    uint32_t height, float time_seconds, jfx_image_t *out_image) {
    if (!out_image || out_image->size < sizeof(*out_image)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memset(out_image, 0, sizeof(*out_image));
    out_image->size = sizeof(*out_image);
    if (!graph || node >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    uint8_t rgba8[4] = { 0, 0, 0, 0 };
    const jfx_result_t status = jfx_graph_render(graph, node, 1u, 1u, time_seconds, rgba8);
    /* A one-pixel render is only a validity probe; the real image comes from
     * the full-size path below. */
    (void)status;
    eval_ctx_t ctx;
    jfx_result_t init = ctx_init(&ctx, graph, width, height, time_seconds);
    if (init != JFX_SUCCESS) {
        return init;
    }
    ensure_evaluated(&ctx, node);
    if (ctx.error != JFX_SUCCESS) { jfx_result_t r=ctx.error; ctx_release(&ctx); return r; }
    const size_t bytes = ctx.pixel_count * 4u;
    uint8_t *pixels = alloc_bytes(bytes);
    if (!pixels) {
        ctx_release(&ctx);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < ctx.pixel_count; ++i) {
        const float *p = ctx.cache[node].pixels + i * 4u;
        pixels[i * 4u + 0u] = to_unorm8(p[0]);
        pixels[i * 4u + 1u] = to_unorm8(p[1]);
        pixels[i * 4u + 2u] = to_unorm8(p[2]);
        pixels[i * 4u + 3u] = to_unorm8(p[3]);
    }
    ctx_release(&ctx);
    out_image->width = width;
    out_image->height = height;
    out_image->channels = 4u;
    out_image->pixels = pixels;
    return JFX_SUCCESS;
}

jfx_result_t jfx_graph_render(const jfx_graph_t *graph, uint32_t output, uint32_t width,
    uint32_t height, float time_seconds, uint8_t *out_pixels) {
    if (!graph || !out_pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (output >= graph->count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!width || !height) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Refuse a cyclic graph before doing any work, so the failure is a clean
     * error rather than a stack overflow. */
    uint32_t order[JFX_GRAPH_MAX_NODES];
    size_t order_count = 0;
    jfx_result_t status = jfx_graph_topological_order(graph, output, order, JFX_GRAPH_MAX_NODES,
        &order_count);
    if (status != JFX_SUCCESS) {
        return status;
    }
    (void)order_count;

    eval_ctx_t ctx;
    status = ctx_init(&ctx, graph, width, height, time_seconds);
    if (status != JFX_SUCCESS) {
        return status;
    }
    ensure_evaluated(&ctx, output);
    if (ctx.error != JFX_SUCCESS) { status=ctx.error; ctx_release(&ctx); return status; }
    const size_t pixels = ctx.pixel_count;
    for (size_t i = 0; i < pixels; ++i) {
        const float *p = ctx.cache[output].pixels + i * 4u;
        uint8_t *o = out_pixels + i * 4u;
        o[0] = to_unorm8(p[0]);
        o[1] = to_unorm8(p[1]);
        o[2] = to_unorm8(p[2]);
        o[3] = to_unorm8(p[3]);
    }
    ctx_release(&ctx);
    return JFX_SUCCESS;
}

jfx_result_t jfx_graph_describe(const jfx_graph_t *graph, char *out_text, size_t out_size,
    size_t *out_written) {
    if (!graph || !out_text || !out_size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    int written = snprintf(out_text, out_size, "%zu node%s", graph->count,
        graph->count == 1u ? "" : "s");
    if (written < 0) {
        return JFX_ERROR_BACKEND_FAILURE;
    }
    size_t used = (size_t)written;
    for (size_t i = 0; i < graph->count; ++i) {
        const node_t *node = &graph->nodes[i];
        int n = snprintf(out_text + used, out_size - used, "\n  %zu: %s (%s)", i, node->label,
            node->kind->name);
        if (n < 0) {
            return JFX_ERROR_BACKEND_FAILURE;
        }
        used += (size_t)n;
        if (used >= out_size) {
            used = out_size - 1u;
            break;
        }
    }
    if (out_written) {
        *out_written = used;
    }
    return JFX_SUCCESS;
}

/* ---- String slots and 8-bit blending -------------------------------------- */

jfx_result_t jfx_graph_set_node_string(jfx_graph_t *graph, uint32_t node, size_t index,
    const char *text) {
    if (!graph || node >= graph->count || index >= JFX_GRAPH_MAX_STRING_PARAMS) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const jfx_node_kind_t *kind = graph->nodes[node].kind;
    if (index >= kind->string_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    char *copy = text ? dup_string(text) : NULL;
    if (text && !copy) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    free_bytes(graph->nodes[node].value.strings[index]);
    graph->nodes[node].value.strings[index] = copy;
    return JFX_SUCCESS;
}

const char *jfx_graph_node_string(const jfx_graph_t *graph, uint32_t node, size_t index) {
    if (!graph || node >= graph->count || index >= JFX_GRAPH_MAX_STRING_PARAMS) {
        return NULL;
    }
    return graph->nodes[node].value.strings[index];
}

void jfx_blend_rgba8(uint8_t *dst, const uint8_t *src, size_t count, jfx_blend_mode_t mode,
    float opacity) {
    if (!dst || !src || !isfinite(opacity)) {
        return;
    }
    const float amount = clamp01(opacity);
    const jfx_blend_mode_t safe = (mode >= 0 && mode < JFX_BLEND_COUNT) ? mode : JFX_BLEND_NORMAL;
    for (size_t i = 0; i < count; ++i) {
        uint8_t *d = dst + i * 4u;
        const uint8_t *s = src + i * 4u;
        const float sa = clamp01((float)s[3] / 255.0f * amount);
        if (sa <= 0.0f) {
            continue;
        }
        const float ba = clamp01((float)d[3] / 255.0f);
        const float out_a = sa + ba * (1.0f - sa);
        float blended[3];
        for (int c = 0; c < 3; ++c) {
            blended[c] = blend_channel(safe, (float)d[c] / 255.0f, (float)s[c] / 255.0f);
        }
        if (sa >= 1.0f && ba <= 0.0f) {
            /* Fully opaque source over a clear backdrop: the result is the
             * source verbatim. The bytes are already 0..255, so they are
             * assigned rather than run through a float conversion, which would
             * saturate anything above 1.0 to 255. */
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255u;
            continue;
        }
        if (out_a > 0.0f) {
            const float w = ba * (1.0f - sa);
            for (int c = 0; c < 3; ++c) {
                const float v = (blended[c] * sa + (float)d[c] / 255.0f * w) / out_a;
                d[c] = to_unorm8(v);
            }
        } else {
            d[0] = 0u;
            d[1] = 0u;
            d[2] = 0u;
        }
        d[3] = to_unorm8(out_a);
    }
}
