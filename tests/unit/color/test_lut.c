/* Colour LUT conformance.
 *
 * Covers the container contract, the sampling maths for all three shapes, the
 * reader for every supported on-disk format, the writers, and the identity and
 * curve generators. Round-trips are the strongest check available: a LUT is
 * written and read back, and the values must match to within the precision the
 * format carries. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfx/jfx_lut.h"
#include "jfx/jfx_image.h"

static char g_tmp[512];

static const char *tmp_path(const char *name) {
    snprintf(g_tmp, sizeof(g_tmp), "%s/%s", getenv("JFX_TEST_TMP") ? getenv("JFX_TEST_TMP") : "/tmp",
        name);
    return g_tmp;
}

static void near(float actual, float expected, float tolerance, const char *what) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL %s: got %.6f, want %.6f (tolerance %.6f)\n", what, (double)actual,
            (double)expected, (double)tolerance);
        assert(fabsf(actual - expected) <= tolerance);
    }
}

/* ---- Container ----------------------------------------------------------- */

static void test_container(void) {
    assert(jfx_lut_empty() != NULL);
    assert(jfx_lut_size_of(jfx_lut_empty()) == 0u);
    assert(jfx_lut_size_of(NULL) == 0u);
    jfx_lut_destroy(NULL); /* must tolerate NULL */

    jfx_lut_t *lut = NULL;
    jfx_lut_t *rejected = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 2, 2, 2, 3, &lut) == JFX_SUCCESS && lut);
    assert(lut->width == 2 && lut->height == 2 && lut->depth == 2);
    assert(lut->entry_count == 8u * 3u);
    assert(lut->domain_max[0] == 1.0f);
    /* Entries start zeroed, so a fresh table is safe to read. */
    for (size_t i = 0; i < lut->entry_count; ++i) {
        assert(lut->entries[i] == 0.0f);
    }

    /* A 3D cube must be cubic. The rejected calls must leave the handle alone
     * rather than orphaning the LUT made above. */
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 4, 4, 8, 3, &rejected) == JFX_ERROR_INVALID_ARGUMENT);
    assert(rejected == NULL);
    jfx_lut_t *const held = lut;
#define LUT_REJECT(shape_, w_, h_, d_, c_) \
    assert(jfx_lut_create((shape_), (w_), (h_), (d_), (c_), &rejected) == JFX_ERROR_INVALID_ARGUMENT)
    LUT_REJECT(JFX_LUT_SHAPE_3D, 4, 4, 4, 0);
    LUT_REJECT(JFX_LUT_SHAPE_3D, 4, 4, 4, 5);
    LUT_REJECT(JFX_LUT_SHAPE_3D, 0, 4, 4, 3);
    LUT_REJECT(JFX_LUT_SHAPE_3D, 1024, 1024, 1024, 3);
    LUT_REJECT(JFX_LUT_SHAPE_1D, 16, 2, 1, 3);
    LUT_REJECT(JFX_LUT_SHAPE_2D, 16, 16, 2, 3);
#undef LUT_REJECT
    assert(rejected == NULL);
    assert(lut == held);
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 4, 4, 4, 0, &lut) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 4, 4, 4, 5, &lut) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 0, 4, 4, 3, &lut) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 1024, 1024, 1024, 3, &lut) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_1D, 16, 2, 1, 3, &lut) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_2D, 16, 16, 2, 3, &lut) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 4, 4, 4, 3, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_lut_destroy(lut);

    /* A deep copy is independent of its source. */
    jfx_lut_t *original = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_1D, 8, 1, 1, 3, &original) == JFX_SUCCESS);
    for (size_t i = 0; i < 8u * 3u; ++i) {
        original->entries[i] = (float)i / 8.0f;
    }
    original->domain_max[0] = 4.0f;
    snprintf(original->title, sizeof(original->title), "original");
    jfx_lut_t *copy = NULL;
    assert(jfx_lut_copy(original, &copy) == JFX_SUCCESS && copy);
    assert(copy != original);
    assert(copy->entry_count == original->entry_count);
    assert(memcmp(copy->entries, original->entries, original->entry_count * sizeof(float)) == 0);
    assert(copy->domain_max[0] == 4.0f);
    assert(strcmp(copy->title, "original") == 0);
    original->entries[0] = 99.0f;
    near(copy->entries[0], 0.0f, 1e-6f, "copy is independent");
    jfx_lut_destroy(original);
    jfx_lut_destroy(copy);
    assert(jfx_lut_copy(NULL, &copy) == JFX_ERROR_INVALID_ARGUMENT);
}

/* ---- Identity and curve generation --------------------------------------- */

static void test_generation(void) {
    jfx_lut_t *identity = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 17, &identity) == JFX_SUCCESS);
    assert(identity->width == 17 && identity->depth == 17);
    assert(jfx_lut_is_identity(identity, 1e-6f));
    /* An identity LUT must return its input, including between grid points. */
    const float probes[][3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f },
        { 0.5f, 0.5f, 0.5f }, { 0.25f, 0.75f, 1.0f }, { 0.13f, 0.27f, 0.91f } };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        float out[4];
        assert(jfx_lut_sample(identity, probes[i], 3, 1.0f, out) == JFX_SUCCESS);
        near(out[0], probes[i][0], 1e-5f, "identity 3D r");
        near(out[1], probes[i][1], 1e-5f, "identity 3D g");
        near(out[2], probes[i][2], 1e-5f, "identity 3D b");
        assert(out[3] == 1.0f);
    }
    jfx_lut_destroy(identity);

    /* A 1D identity, sampled off-grid. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 64, &identity) == JFX_SUCCESS);
    assert(jfx_lut_is_identity(identity, 1e-6f));
    for (float v = 0.0f; v <= 1.0f; v += 0.1f) {
        const float in[4] = { v, v, v, 1.0f };
        float out[4];
        assert(jfx_lut_sample(identity, in, 4, 1.0f, out) == JFX_SUCCESS);
        near(out[0], v, 1e-4f, "identity 1D");
    }
    jfx_lut_destroy(identity);

    /* A curve through control points: a gamma of 2.2. */
    const float points[] = { 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f };
    jfx_lut_t *curve = NULL;
    assert(jfx_lut_create_curve(points, 3, 256, &curve) == JFX_SUCCESS);
    const float mid[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    float out[4];
    assert(jfx_lut_sample(curve, mid, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 0.5f, 1e-3f, "curve hits its control point");
    /* Out-of-range input clamps rather than extrapolating. */
    const float over[4] = { 2.0f, 0.5f, 0.5f, 1.0f };
    assert(jfx_lut_sample(curve, over, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 1.0f, 1e-3f, "curve clamps above 1");
    jfx_lut_destroy(curve);

    /* Control points must ascend in input. */
    const float descending[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    assert(jfx_lut_create_curve(descending, 2, 32, &curve) == JFX_ERROR_INVALID_ARGUMENT);
    const float single[] = { 0.5f, 0.5f };
    assert(jfx_lut_create_curve(single, 1, 32, &curve) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_create_curve(NULL, 4, 32, &curve) == JFX_ERROR_INVALID_ARGUMENT);
}

/* ---- Interpolation ------------------------------------------------------- */

/* A 2x2 3D LUT maps each octant to an exact constant, so a probe in the
 * middle of an octant must return that constant. This pins the trilinear
 * indexing and the r-fastest layout. */
static void test_3d_trilinear(void) {
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 2, 2, 2, 3, &lut) == JFX_SUCCESS);
    /* index = b*4 + g*2 + r */
    for (size_t b = 0; b < 2; ++b) {
        for (size_t g = 0; g < 2; ++g) {
            for (size_t r = 0; r < 2; ++r) {
                const size_t index = b * 4u + g * 2u + r;
                lut->entries[index * 3u + 0u] = (float)r;
                lut->entries[index * 3u + 1u] = (float)g;
                lut->entries[index * 3u + 2u] = (float)b;
            }
        }
    }
    struct { float in[3]; float want[3]; } cases[] = {
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } },
        /* Corners of the cube map exactly. */
        { { 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f } },
        { { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } },
        /* Inside an octant: the midpoint of a 2-entry axis is 0.5, and both
         * neighbours are 0 and 1, so the result is exactly 0.5. */
        { { 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f } },
        { { 0.0f, 0.5f, 1.0f }, { 0.0f, 0.5f, 1.0f } },
        /* Clamped above 1. */
        { { 4.0f, 4.0f, 4.0f }, { 1.0f, 1.0f, 1.0f } },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        float out[4];
        assert(jfx_lut_sample(lut, cases[i].in, 3, 1.0f, out) == JFX_SUCCESS);
        near(out[0], cases[i].want[0], 1e-6f, "3d r");
        near(out[1], cases[i].want[1], 1e-6f, "3d g");
        near(out[2], cases[i].want[2], 1e-6f, "3d b");
    }
    jfx_lut_destroy(lut);
}

/* A 2D strip of 4x2 with a horizontal ramp: u across, v down. */
static void test_2d_bilinear(void) {
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_2D, 4, 2, 1, 3, &lut) == JFX_SUCCESS);
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 4; ++x) {
            const size_t index = y * 4u + x;
            lut->entries[index * 3u + 0u] = (float)x / 3.0f;
            lut->entries[index * 3u + 1u] = (float)y;
            lut->entries[index * 3u + 2u] = 0.0f;
        }
    }
    float out[4];
    const float in[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    assert(jfx_lut_sample(lut, in, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 1.0f, 1e-6f, "2d right edge");
    near(out[1], 1.0f, 1e-6f, "2d bottom row");
    const float mid[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
    assert(jfx_lut_sample(lut, mid, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 0.5f, 1e-5f, "2d middle of the ramp");
    jfx_lut_destroy(lut);
}

/* A 1D LUT that inverts each channel, to check the per-channel path. */
static void test_1d_curve(void) {
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_1D, 2, 1, 1, 3, &lut) == JFX_SUCCESS);
    lut->entries[0] = 1.0f;
    lut->entries[1] = 0.0f;
    lut->entries[2] = 1.0f;
    /* index 1 (x = 1) holds the other two channels */
    lut->entries[3] = 0.0f;
    lut->entries[4] = 1.0f;
    lut->entries[5] = 0.0f;
    float out[4];
    const float in[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    assert(jfx_lut_sample(lut, in, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 1.0f, 1e-6f, "1d r inverted");
    near(out[1], 0.0f, 1e-6f, "1d g inverted");
    near(out[2], 1.0f, 1e-6f, "1d b inverted");
    assert(out[3] == 1.0f);
    jfx_lut_destroy(lut);
}

/* mix = 0 must be a no-op; mix = 1 must be the full grade. */
static void test_mix(void) {
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create(JFX_LUT_SHAPE_3D, 2, 2, 2, 3, &lut) == JFX_SUCCESS);
    for (size_t i = 0; i < 8u * 3u; ++i) {
        lut->entries[i] = 1.0f; /* everything becomes white */
    }
    uint8_t pixels[8] = { 0, 128, 255, 255, 10, 20, 30, 200 };
    uint8_t out[8];
    assert(jfx_lut_apply_rgba8(lut, pixels, 2, 0.0f, out) == JFX_SUCCESS);
    assert(memcmp(pixels, out, sizeof(pixels)) == 0);
    assert(jfx_lut_apply_rgba8(lut, pixels, 2, 1.0f, out) == JFX_SUCCESS);
    assert(out[0] == 255 && out[1] == 255 && out[2] == 255);
    assert(out[3] == 255 && out[7] == 200); /* alpha passes through */
    /* Half mix: each channel lands halfway to white, and alpha is untouched. */
    assert(jfx_lut_apply_rgba8(lut, pixels, 2, 0.5f, out) == JFX_SUCCESS);
    assert(out[0] >= 127 && out[0] <= 128); /* 0    -> 255: 0.50 -> 128 */
    assert(out[1] >= 191 && out[1] <= 192); /* 128  -> 255: 0.75 -> 191 */
    assert(out[2] == 255);                  /* 255  -> 255: 1.00 -> 255 */
    assert(out[3] == 255 && out[7] == 200); /* alpha passes through */
    assert(jfx_lut_apply_rgba8(lut, pixels, 2, 1.5f, out) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_apply_rgba8(lut, pixels, 2, -0.1f, out) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_apply_rgba8(NULL, pixels, 2, 1.0f, out) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_lut_destroy(lut);
}

/* ---- Format round-trips -------------------------------------------------- */

static void test_cube_roundtrip(void) {
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 5, &lut) == JFX_SUCCESS);
    snprintf(lut->title, sizeof(lut->title), "Round Trip");
    lut->domain_min[0] = 0.0f;
    lut->domain_max[0] = 1.0f;
    char error[192] = { 0 };
    assert(jfx_lut_write_cube(lut, tmp_path("rt3d.cube"), error, sizeof(error)) == JFX_SUCCESS);

    jfx_lut_t *read = NULL;
    {
        const jfx_result_t status =
            jfx_lut_load_cube(tmp_path("rt3d.cube"), &read, error, sizeof(error));
        if (status != JFX_SUCCESS) {
            fprintf(stderr, "cube load failed: %s\n", error);
        }
        assert(status == JFX_SUCCESS);
    }
    assert(read->shape == JFX_LUT_SHAPE_3D);
    assert(read->width == 5 && read->depth == 5);
    assert(strcmp(read->title, "Round Trip") == 0);
    assert(strcmp(jfx_lut_format_name(read), "Adobe .cube") == 0);
    assert(jfx_lut_is_identity(read, 1e-4f));
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);

    /* The 1D form. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 32, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_cube(lut, tmp_path("rt1d.cube"), error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_lut_load_cube(tmp_path("rt1d.cube"), &read, error, sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_1D);
    assert(read->width == 32);
    assert(jfx_lut_is_identity(read, 1e-4f));
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);
}

static void test_cube_domain_and_errors(void) {
    char error[192] = { 0 };
    jfx_lut_t *lut = NULL;
    static const char kGood[] =
        "TITLE \"with domain\"\n"
        "DOMAIN_MIN 0.0 0.0 0.0\n"
        "DOMAIN_MAX 10.0 10.0 10.0\n"
        "LUT_1D_SIZE 3\n"
        "0.0 0.0 0.0\n"
        "5.0 5.0 5.0\n"
        "10.0 10.0 10.0\n";
    assert(jfx_lut_parse_cube(kGood, 0, &lut, error, sizeof(error)) == JFX_SUCCESS);
    near(lut->domain_max[0], 10.0f, 1e-6f, "DOMAIN_MAX parsed");
    /* 5.0 inside a [0,10] domain is the midpoint. */
    float out[4];
    const float in[4] = { 5.0f, 0.0f, 0.0f, 1.0f };
    assert(jfx_lut_sample(lut, in, 4, 1.0f, out) == JFX_SUCCESS);
    near(out[0], 5.0f, 1e-4f, "domain-respecting sample");
    jfx_lut_destroy(lut);

    /* A 1D cube is a curve on the declared domain, so 5.0 is the midpoint of a
     * 0..10 ramp, which is also 5.0. The value must not be clamped to 1. */
    struct { const char *text; const char *why; } bad[] = {
        { "LUT_3D_SIZE 2\n0 0 0\n", "too few entries" },
        { "LUT_3D_SIZE 2\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n",
            "too many entries" },
        { "LUT_3D_SIZE 2\n0 0\n", "truncated triple" },
        { "DOMAIN_MAX 1 1 1\nLUT_3D_SIZE 2\n", "inverted domain" },
        { "LUT_3D_SIZE 2\n0 0 0 0\n", "quadruple component" },
        { "LUT_3D_SIZE 0\n", "zero size" },
        { "# nothing here\n", "no header" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        error[0] = '\0';
        lut = NULL;
        jfx_result_t status = jfx_lut_parse_cube(bad[i].text, 0, &lut, error, sizeof(error));
        assert(status != JFX_SUCCESS);
        assert(lut == NULL);
        assert(error[0] != '\0');
    }
}

static void test_3dl_roundtrip(void) {
    char error[192] = { 0 };
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 6, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_3dl(lut, tmp_path("rt.3dl"), error, sizeof(error)) == JFX_SUCCESS);
    jfx_lut_t *read = NULL;
    assert(jfx_lut_load_3dl(tmp_path("rt.3dl"), &read, error, sizeof(error)) == JFX_SUCCESS);
    assert(strcmp(jfx_lut_format_name(read), "Autodesk .3dl") == 0);
    assert(read->shape == JFX_LUT_SHAPE_1D);
    assert(read->width == 6);
    assert(jfx_lut_is_identity(read, 1e-4f));
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);

    /* The 2D strip form needs the shape stated, because a headerless .3dl
     * cannot tell a strip from a curve. */
    assert(jfx_lut_create(JFX_LUT_SHAPE_2D, 4, 4, 1, 3, &lut) == JFX_SUCCESS);
    for (size_t i = 0; i < 16u; ++i) {
        lut->entries[i * 3u + 0u] = (float)(i % 4u) / 3.0f;
        lut->entries[i * 3u + 1u] = (float)(i / 4u) / 3.0f;
        lut->entries[i * 3u + 2u] = 0.0f;
    }
    assert(jfx_lut_write_3dl(lut, tmp_path("strip.3dl"), error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_lut_load_3dl_as(tmp_path("strip.3dl"), JFX_LUT_SHAPE_2D, 4, &read, error,
        sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_2D);
    assert(read->width == 4u && read->height == 4u);
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);

    /* A row length that does not divide the entry count is rejected. */
    assert(jfx_lut_load_3dl_as(tmp_path("strip.3dl"), JFX_LUT_SHAPE_2D, 5, &read, error,
        sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);
    assert(strstr(error, "rows") != NULL);
    /* Only 1D and 2D exist in this format; 3D must be refused rather than
     * silently reshaped. */
    assert(jfx_lut_load_3dl_as(tmp_path("strip.3dl"), JFX_LUT_SHAPE_3D, 0, &read, error,
        sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_load_3dl_as(tmp_path("strip.3dl"), JFX_LUT_SHAPE_2D, 0, &read, error,
        sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);

    FILE *file = fopen(tmp_path("junk.3dl"), "wb");
    assert(file);
    fprintf(file, "this is not a table\n");
    fclose(file);
    error[0] = '\0';
    assert(jfx_lut_load_3dl(tmp_path("junk.3dl"), &read, error, sizeof(error)) != JFX_SUCCESS);
    assert(error[0] != '\0');
}

static void test_spi_roundtrip(void) {
    char error[192] = { 0 };
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 4, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_spi(lut, tmp_path("rt.spi3d"), error, sizeof(error)) == JFX_SUCCESS);
    jfx_lut_t *read = NULL;
    assert(jfx_lut_load_spi(tmp_path("rt.spi3d"), &read, error, sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_3D);
    assert(read->width == 4 && read->depth == 4);
    assert(strcmp(jfx_lut_format_name(read), "Sony .spi3d") == 0);
    /* .spi is raw binary32, so the round trip is exact. */
    assert(jfx_lut_is_identity(read, 1e-7f));
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);

    /* An 8-entry 1D LUT is the trap case: 8 triples is also a 2x2x2 cube, so
     * only the .spi1d extension or an explicit shape gets it right. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 8, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_spi(lut, tmp_path("rt.spi1d"), error, sizeof(error)) == JFX_SUCCESS);
    assert(jfx_lut_load_spi(tmp_path("rt.spi1d"), &read, error, sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_1D);
    assert(read->width == 8);
    assert(strcmp(jfx_lut_format_name(read), "Sony .spi1d") == 0);
    jfx_lut_destroy(read);
    jfx_lut_destroy(lut);

    /* The same bytes, named neutrally, are genuinely ambiguous: 8 triples read
     * as a 2x2x2 cube and as an 8-entry curve, and both readings are valid.
     * That is precisely why the shape has to be stated rather than sniffed. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 8, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_spi(lut, tmp_path("ambiguous.spi"), error, sizeof(error)) == JFX_SUCCESS);
    jfx_lut_destroy(lut);
    assert(jfx_lut_load_spi_as(tmp_path("ambiguous.spi"), JFX_LUT_SHAPE_1D, &read, error,
        sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_1D && read->width == 8);
    jfx_lut_destroy(read);
    assert(jfx_lut_load_spi_as(tmp_path("ambiguous.spi"), JFX_LUT_SHAPE_3D, &read, error,
        sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_3D && read->width == 2 && read->depth == 2);
    jfx_lut_destroy(read);
    /* Without a name or an explicit shape, the cube reading wins. */
    assert(jfx_lut_load_spi(tmp_path("ambiguous.spi"), &read, error, sizeof(error)) == JFX_SUCCESS);
    assert(read->shape == JFX_LUT_SHAPE_3D);
    jfx_lut_destroy(read);

    /* 2D has no .spi representation. */
    assert(jfx_lut_load_spi_as(tmp_path("ambiguous.spi"), JFX_LUT_SHAPE_2D, &read, error,
        sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);

    /* Seven triples are neither a cube nor a usable curve, and saying "cube"
     * about them is caught rather than silently reshaped. */
    {
        FILE *seven = fopen(tmp_path("seven.spi"), "wb");
        assert(seven);
        for (int i = 0; i < 7; ++i) {
            const float triple[3] = { 0.0f, 0.0f, 0.0f };
            fwrite(triple, sizeof(float), 3, seven);
        }
        fclose(seven);
        error[0] = '\0';
        assert(jfx_lut_load_spi_as(tmp_path("seven.spi"), JFX_LUT_SHAPE_3D, &read, error,
            sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);
        assert(strstr(error, "cube") != NULL);
        assert(jfx_lut_load_spi_as(tmp_path("seven.spi"), JFX_LUT_SHAPE_1D, &read, error,
            sizeof(error)) == JFX_SUCCESS);
        assert(read->width == 7);
        jfx_lut_destroy(read);
    }

    /* A byte count that is not a whole number of triples is rejected. */
    FILE *file = fopen(tmp_path("bad.spi3d"), "wb");
    assert(file);
    for (int i = 0; i < 7; ++i) {
        fputc(i, file);
    }
    fclose(file);
    error[0] = '\0';
    assert(jfx_lut_load_spi(tmp_path("bad.spi3d"), &read, error, sizeof(error)) != JFX_SUCCESS);
    assert(error[0] != '\0');
}

static void test_look_container(void) {
    /* A minimal .look: a 3DLOOK header, an index, then the mesh and the cube. */
    char error[192] = { 0 };
    jfx_lut_t *cube = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 3, &cube) == JFX_SUCCESS);

    /* Assemble: a 3DLOOK header and index, then the mesh, then the cube. The
     * reader parses the index for the mesh and LUT sizes and hands the LUT
     * bytes to the .cube parser. */
    assert(jfx_lut_write_cube(cube, tmp_path("cube.cube"), error, sizeof(error)) == JFX_SUCCESS);
    FILE *cube_file = fopen(tmp_path("cube.cube"), "rb");
    assert(cube_file);
    fseek(cube_file, 0, SEEK_END);
    const long cube_size_signed = ftell(cube_file);
    assert(cube_size_signed > 0);
    rewind(cube_file);
    const size_t cube_size = (size_t)cube_size_signed;
    char *cube_text = malloc(cube_size + 1u);
    assert(cube_text);
    assert(fread(cube_text, 1, cube_size, cube_file) == cube_size);
    fclose(cube_file);

    static const char kMesh[] = "3DMESH\nMeshSize 0\n";
    const size_t mesh_size = sizeof(kMesh) - 1u;

    FILE *file = fopen(tmp_path("test.look"), "wb");
    assert(file);
    fputs("3DLOOK\n", file);
    fprintf(file, "LUT: 0\n");
    fprintf(file, "LUTSize: %zu\n", cube_size);
    fputs("3DMESH\n", file);
    fprintf(file, "MeshSize: %zu\n", mesh_size);
    fwrite(kMesh, 1, mesh_size, file);
    fwrite(cube_text, 1, cube_size, file);
    fclose(file);
    free(cube_text);
    jfx_lut_destroy(cube);

    jfx_lut_t *read = NULL;
    error[0] = '\0';
    {
        const jfx_result_t status =
            jfx_lut_load_look(tmp_path("test.look"), &read, error, sizeof(error));
        if (status != JFX_SUCCESS) {
            fprintf(stderr, ".look load failed: %s\n", error);
        }
        assert(status == JFX_SUCCESS && read);
    }
    assert(strcmp(jfx_lut_format_name(read), "DaVinci .look") == 0);
    assert(strstr(read->description, ".look") != NULL);
    assert(read->shape == JFX_LUT_SHAPE_3D);
    assert(read->width == 3);
    assert(jfx_lut_is_identity(read, 1e-4f));
    jfx_lut_destroy(read);

    /* A file that is not a .look at all. */
    FILE *plain = fopen(tmp_path("plain.look"), "wb");
    assert(plain);
    fputs("not a look file\n", plain);
    fclose(plain);
    error[0] = '\0';
    assert(jfx_lut_load_look(tmp_path("plain.look"), &read, error, sizeof(error)) !=
        JFX_SUCCESS);
    assert(strstr(error, "3DLOOK") != NULL);
}

static void test_hald_roundtrip(void) {
    if (!jfx_image_codec_available()) {
        return;
    }
    char error[192] = { 0 };
    /* A Hald of level 2 is an 8x8 image carrying a 4-cube, so level^2 must
     * equal the cube edge. */
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 4, &lut) == JFX_SUCCESS);
    const size_t side = 8u;
    const size_t bytes = side * side * 4u;
    uint8_t *pixels = malloc(bytes);
    assert(pixels);
    uint32_t width = 0, height = 0;
    assert(jfx_lut_write_hald(lut, 2, pixels, bytes, &width, &height) == JFX_SUCCESS);
    assert(width == 8 && height == 8);
    /* The level has to match the cube: a 4-cube is level 2, and level 3 wants a
     * 9-cube. */
    assert(jfx_lut_write_hald(lut, 3, pixels, bytes, &width, &height) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_write_hald(lut, 1, pixels, bytes, &width, &height) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_write_hald(lut, 2, pixels, bytes - 1u, &width, &height) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_lut_write_hald(NULL, 2, pixels, bytes, &width, &height) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* A PPM is enough to exercise the real decode path. */
    FILE *file = fopen(tmp_path("rt.hald.ppm"), "wb");
    assert(file);
    fprintf(file, "P6\n%zu %zu\n255\n", side, side);
    for (size_t i = 0; i < side * side; ++i) {
        const uint8_t rgb[3] = { pixels[i * 4u], pixels[i * 4u + 1u], pixels[i * 4u + 2u] };
        fwrite(rgb, 1, sizeof(rgb), file);
    }
    fclose(file);
    jfx_lut_destroy(lut);

    jfx_lut_t *read = NULL;
    error[0] = '\0';
    {
        const jfx_result_t status =
            jfx_lut_load_hald(tmp_path("rt.hald.ppm"), &read, error, sizeof(error));
        if (status != JFX_SUCCESS) {
            fprintf(stderr, "Hald load failed: %s\n", error);
        }
        assert(status == JFX_SUCCESS && read);
    }
    assert(read->shape == JFX_LUT_SHAPE_3D);
    assert(read->width == 4 && read->depth == 4);
    assert(strcmp(jfx_lut_format_name(read), "Hald CLUT") == 0);
    /* The image stores 8 bits per channel, so the values survive to within one
     * quantisation step. */
    assert(jfx_lut_is_identity(read, 1.0f / 255.0f));
    jfx_lut_destroy(read);

    /* A side that is not a perfect cube is not a Hald image. 16 is not one:
     * levels 2, 3 and 4 give sides of 8, 27 and 64. */
    file = fopen(tmp_path("sixteen.ppm"), "wb");
    assert(file);
    fprintf(file, "P6\n16 16\n255\n");
    /* Written a pixel at a time: handing fwrite a size larger than the source
     * array would read past it. */
    for (size_t i = 0; i < 16u * 16u; ++i) {
        const uint8_t grey[3] = { 128, 128, 128 };
        assert(fwrite(grey, 1, sizeof(grey), file) == sizeof(grey));
    }
    fclose(file);
    error[0] = '\0';
    assert(jfx_lut_load_hald(tmp_path("sixteen.ppm"), &read, error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(strstr(error, "perfect cube") != NULL);

    /* A non-square image is rejected too. */
    file = fopen(tmp_path("wide.ppm"), "wb");
    assert(file);
    fprintf(file, "P6\n8 4\n255\n");
    for (size_t i = 0; i < 8u * 4u; ++i) {
        const uint8_t black[3] = { 0, 0, 0 };
        assert(fwrite(black, 1, sizeof(black), file) == sizeof(black));
    }
    fclose(file);
    error[0] = '\0';
    assert(jfx_lut_load_hald(tmp_path("wide.ppm"), &read, error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(strstr(error, "square") != NULL);

    free(pixels);
}

/* ---- Dispatch ------------------------------------------------------------ */

static void test_auto_dispatch(void) {
    char error[192] = { 0 };
    jfx_lut_t *lut = NULL;
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 3, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_cube(lut, tmp_path("auto.cube"), error, sizeof(error)) == JFX_SUCCESS);
    jfx_lut_destroy(lut);
    /* Auto must find the cube regardless of the extension. */
    assert(jfx_lut_load_auto(tmp_path("auto.cube"), &lut, error, sizeof(error)) == JFX_SUCCESS);
    assert(strcmp(jfx_lut_format_name(lut), "Adobe .cube") == 0);
    jfx_lut_destroy(lut);

    /* Six entries is not a cube, so a neutrally-named .spi file can only be a
     * curve; auto must still find it. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_1D, 6, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_spi(lut, tmp_path("auto.dat"), error, sizeof(error)) == JFX_SUCCESS);
    jfx_lut_destroy(lut);
    lut = NULL;
    assert(jfx_lut_load_auto(tmp_path("auto.dat"), &lut, error, sizeof(error)) == JFX_SUCCESS);
    assert(strcmp(jfx_lut_format_name(lut), "Sony .spi1d") == 0);
    assert(lut->width == 6);
    jfx_lut_destroy(lut);

    /* An Iridas .csp is a real container this build does not parse. It must be
     * rejected with a message that says so, not misread. */
    FILE *file = fopen(tmp_path("x.csp"), "wb");
    assert(file);
    fputs("CSPLUTV100\n", file);
    fclose(file);
    error[0] = '\0';
    lut = NULL; /* a rejected load must leave the handle alone */
    assert(jfx_lut_load_auto(tmp_path("x.csp"), &lut, error, sizeof(error)) == JFX_ERROR_NOT_FOUND);
    assert(strstr(error, ".csp") != NULL);
    assert(strstr(error, "Supported") != NULL);
    assert(lut == NULL);

    assert(jfx_lut_load_auto(tmp_path("does-not-exist.cube"), &lut, error, sizeof(error)) ==
        JFX_ERROR_NOT_FOUND);
    assert(jfx_lut_load_auto(NULL, &lut, error, sizeof(error)) == JFX_ERROR_INVALID_ARGUMENT);
}

static void test_writer_rejections(void) {
    char error[192] = { 0 };
    jfx_lut_t *lut = NULL;
    /* A 3D cube has no .3dl or 2D representation; saying so beats writing
     * something a reader would misinterpret. */
    assert(jfx_lut_create_identity(JFX_LUT_SHAPE_3D, 2, &lut) == JFX_SUCCESS);
    error[0] = '\0';
    assert(jfx_lut_write_3dl(lut, tmp_path("no.3dl"), error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(strstr(error, "3D") != NULL);
    jfx_lut_destroy(lut);

    assert(jfx_lut_create(JFX_LUT_SHAPE_2D, 4, 4, 1, 3, &lut) == JFX_SUCCESS);
    error[0] = '\0';
    assert(jfx_lut_write_cube(lut, tmp_path("no.cube"), error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(strstr(error, "2D") != NULL);
    assert(jfx_lut_write_spi(lut, tmp_path("no.spi3d"), error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_lut_destroy(lut);

    /* A one-channel table cannot express RGB. */
    assert(jfx_lut_create(JFX_LUT_SHAPE_1D, 8, 1, 1, 1, &lut) == JFX_SUCCESS);
    assert(jfx_lut_write_cube(lut, tmp_path("no.cube"), error, sizeof(error)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_lut_destroy(lut);
}

int main(void) {
    test_container();
    test_generation();
    test_3d_trilinear();
    test_2d_bilinear();
    test_1d_curve();
    test_mix();
    test_cube_roundtrip();
    test_cube_domain_and_errors();
    test_3dl_roundtrip();
    test_spi_roundtrip();
    test_look_container();
    test_hald_roundtrip();
    test_auto_dispatch();
    test_writer_rejections();
    puts("LUT conformance tests passed");
    return 0;
}
