/* Colour lookup tables: container, format readers and writers, and sampling.
 *
 * No dependencies, so the same code runs on the desktop, in a browser through
 * WASM, and on a phone. See jfx_lut.h for the shape and format overview. */

#include "jfx/jfx_lut.h"

#include "jfx/jfx_image.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tilly/allocator.h"

#define JFX_LUT_FILE_MAX ((size_t)64u * 1024u * 1024u)

/* The format a LUT was read from, so writers and UIs can report it rather than
 * guessing. Not part of the public struct: it is set by the readers and
 * consumed by jfx_lut_format_name. */
typedef enum {
    JFX_LUT_FORMAT_UNKNOWN = 0,
    JFX_LUT_FORMAT_CUBE,
    JFX_LUT_FORMAT_3DL,
    JFX_LUT_FORMAT_SPI1D,
    JFX_LUT_FORMAT_SPI3D,
    JFX_LUT_FORMAT_HALD,
    JFX_LUT_FORMAT_LOOK,
    JFX_LUT_FORMAT_GENERATED
} lut_format_t;

/* The private prefix. `view` is the first member, so a jfx_lut_t* and a
 * struct lut* are the same address; `owner_of` makes that explicit rather than
 * leaving pointer arithmetic at every use site. */
struct lut {
    jfx_lut_t view;
    lut_format_t format;
};

static struct lut *owner_of(const jfx_lut_t *lut) {
    return (struct lut *)(void *)(uintptr_t)lut;
}

static void lut_set_format(jfx_lut_t *lut, lut_format_t format) {
    owner_of(lut)->format = format;
}

/* ---- Small helpers ------------------------------------------------------- */

static void *lut_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) {
        return 0u; /* also catches NaN */
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (uint8_t)lrintf(value * 255.0f);
}

static void set_error(char *out_error, size_t out_error_size, const char *fmt, ...) {
    if (!out_error || !out_error_size) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(out_error, out_error_size, fmt, args);
    va_end(args);
}

const jfx_lut_t *jfx_lut_empty(void) {
    static const jfx_lut_t empty;
    return &empty;
}

void jfx_lut_destroy(jfx_lut_t *lut) {
    if (!lut) {
        return;
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), lut->entries);
    memset(lut, 0, sizeof(*lut));
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), lut);
}

jfx_result_t jfx_lut_create(jfx_lut_shape_t shape, size_t width, size_t height, size_t depth,
    uint32_t channels, jfx_lut_t **out_lut) {
    if (!out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* `*out_lut` is published only on success. Clearing it on a rejected
     * request would orphan whatever the caller already had there. */
    if (channels < 1u || channels > 4u || !width || !height || !depth) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (width > JFX_LUT_MAX_DIMENSION || height > JFX_LUT_MAX_DIMENSION ||
        depth > JFX_LUT_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape == JFX_LUT_SHAPE_3D && (width != height || height != depth)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape == JFX_LUT_SHAPE_1D && (width > JFX_LUT_MAX_DIMENSION || height != 1u ||
            depth != 1u)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape == JFX_LUT_SHAPE_2D && depth != 1u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (height && width > SIZE_MAX / height) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    size_t cells = width * height;
    if (depth && cells > SIZE_MAX / depth) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    cells *= depth;
    if (channels && cells > SIZE_MAX / channels) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    const size_t count = cells * channels;
    if (count > SIZE_MAX / sizeof(float)) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    struct lut *self = lut_alloc(sizeof(*self));
    if (!self) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(self, 0, sizeof(*self));
    self->view.entries = lut_alloc(count * sizeof(float));
    if (!self->view.entries) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), self);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(self->view.entries, 0, count * sizeof(float));
    self->view.entry_count = count;
    self->view.width = width;
    self->view.height = height;
    self->view.depth = depth;
    self->view.channels = channels;
    self->view.shape = shape;
    self->view.domain_min[0] = 0.0f;
    self->view.domain_min[1] = 0.0f;
    self->view.domain_min[2] = 0.0f;
    self->view.domain_max[0] = 1.0f;
    self->view.domain_max[1] = 1.0f;
    self->view.domain_max[2] = 1.0f;
    self->format = JFX_LUT_FORMAT_GENERATED;
    *out_lut = &self->view;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_copy(const jfx_lut_t *source, jfx_lut_t **out_lut) {
    if (!source || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!source->entries || !source->entry_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_lut_t *result = NULL;
    jfx_result_t status = jfx_lut_create(source->shape, source->width, source->height,
        source->depth, source->channels, &result);
    if (status != JFX_SUCCESS) {
        return status;
    }
    memcpy(result->entries, source->entries, source->entry_count * sizeof(float));
    result->domain_min[0] = source->domain_min[0];
    result->domain_min[1] = source->domain_min[1];
    result->domain_min[2] = source->domain_min[2];
    result->domain_max[0] = source->domain_max[0];
    result->domain_max[1] = source->domain_max[1];
    result->domain_max[2] = source->domain_max[2];
    memcpy(result->title, source->title, sizeof(result->title));
    memcpy(result->description, source->description, sizeof(result->description));
    lut_set_format(result, owner_of(source)->format);
    *out_lut = result;
    return JFX_SUCCESS;
}

/* ---- File I/O ------------------------------------------------------------ */

static char *read_file(const char *path, size_t *out_size) {
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    const long length = ftell(file);
    if (length < 0 || (size_t)length > JFX_LUT_FILE_MAX) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *bytes = lut_alloc((size_t)length + 1u);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    const size_t got = fread(bytes, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return NULL;
    }
    bytes[got] = '\0';
    *out_size = got;
    return bytes;
}

/* ---- Sampling ------------------------------------------------------------ */

static float clamp01(float value) {
    if (!(value > 0.0f)) return 0.0f; /* also catches NaN */
    return value >= 1.0f ? 1.0f : value;
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Maps one channel through the LUT's input domain into [0,1] table space. */
static float normalize_channel(const jfx_lut_t *lut, float value, int channel) {
    const float lo = lut->domain_min[channel];
    const float hi = lut->domain_max[channel];
    if (!(hi > lo)) {
        return clamp01(value);
    }
    return clamp01((value - lo) / (hi - lo));
}

static const float *entry_at(const jfx_lut_t *lut, size_t index) {
    return lut->entries + index * lut->channels;
}

/* 1D: an independent curve per channel. A 1-entry-per-channel table cannot be
 * interpolated, so it is treated as a constant. */
static void sample_1d(const jfx_lut_t *lut, const float *rgba, float *out) {
    const size_t last = lut->width > 0 ? lut->width - 1u : 0u;
    for (uint32_t channel = 0; channel < 3u; ++channel) {
        const float normalized = normalize_channel(lut, rgba[channel], (int)channel);
        const float position = normalized * (float)last;
        size_t low = (size_t)position;
        if (low > last) {
            low = last;
        }
        const size_t high = low < last ? low + 1u : last;
        const float t = position - (float)low;
        const float a = entry_at(lut, low)[channel];
        const float b = entry_at(lut, high)[channel];
        out[channel] = lerp(a, b, t);
    }
    out[3] = rgba[3];
}

/* 2D: a `width` x `height` strip, u across the rows, v down them. */
static void sample_2d(const jfx_lut_t *lut, const float *rgba, float *out) {
    const float u = normalize_channel(lut, rgba[0], 0);
    const float v = normalize_channel(lut, rgba[1], 1);
    const float x = u * (float)(lut->width - 1u);
    const float y = v * (float)(lut->height - 1u);
    size_t x0 = (size_t)x;
    size_t y0 = (size_t)y;
    if (x0 > lut->width - 1u) x0 = lut->width - 1u;
    if (y0 > lut->height - 1u) y0 = lut->height - 1u;
    const size_t x1 = x0 < lut->width - 1u ? x0 + 1u : x0;
    const size_t y1 = y0 < lut->height - 1u ? y0 + 1u : y0;
    const float fx = x - (float)x0;
    const float fy = y - (float)y0;
    for (uint32_t channel = 0; channel < 3u; ++channel) {
        const float p00 = entry_at(lut, y0 * lut->width + x0)[channel];
        const float p10 = entry_at(lut, y0 * lut->width + x1)[channel];
        const float p01 = entry_at(lut, y1 * lut->width + x0)[channel];
        const float p11 = entry_at(lut, y1 * lut->width + x1)[channel];
        out[channel] = lerp(lerp(p00, p10, fx), lerp(p01, p11, fx), fy);
    }
    out[3] = rgba[3];
}

/* 3D: trilinear over a cube indexed [z * width^2 + y * width + x], which is the
 * order every common format uses for r, g, b slowest-to-fastest. */
static void sample_3d(const jfx_lut_t *lut, const float *rgba, float *out) {
    const float r = normalize_channel(lut, rgba[0], 0);
    const float g = normalize_channel(lut, rgba[1], 1);
    const float b = normalize_channel(lut, rgba[2], 2);
    const size_t edge = lut->width;
    const size_t last = edge - 1u;
    const float fr = r * (float)last;
    const float fg = g * (float)last;
    const float fb = b * (float)last;
    size_t r0 = (size_t)fr, g0 = (size_t)fg, b0 = (size_t)fb;
    if (r0 > last) r0 = last;
    if (g0 > last) g0 = last;
    if (b0 > last) b0 = last;
    const size_t r1 = r0 < last ? r0 + 1u : r0;
    const size_t g1 = g0 < last ? g0 + 1u : g0;
    const size_t b1 = b0 < last ? b0 + 1u : b0;
    const float dr = fr - (float)r0;
    const float dg = fg - (float)g0;
    const float db = fb - (float)b0;

    const size_t layer = edge * edge;
    for (uint32_t channel = 0; channel < 3u; ++channel) {
        const float c000 = entry_at(lut, b0 * layer + g0 * edge + r0)[channel];
        const float c100 = entry_at(lut, b0 * layer + g0 * edge + r1)[channel];
        const float c010 = entry_at(lut, b0 * layer + g1 * edge + r0)[channel];
        const float c110 = entry_at(lut, b0 * layer + g1 * edge + r1)[channel];
        const float c001 = entry_at(lut, b1 * layer + g0 * edge + r0)[channel];
        const float c101 = entry_at(lut, b1 * layer + g0 * edge + r1)[channel];
        const float c011 = entry_at(lut, b1 * layer + g1 * edge + r0)[channel];
        const float c111 = entry_at(lut, b1 * layer + g1 * edge + r1)[channel];
        const float c00 = lerp(c000, c100, dr);
        const float c10 = lerp(c010, c110, dr);
        const float c01 = lerp(c001, c101, dr);
        const float c11 = lerp(c011, c111, dr);
        out[channel] = lerp(lerp(c00, c10, dg), lerp(c01, c11, dg), db);
    }
    out[3] = rgba[3];
}

jfx_result_t jfx_lut_sample(const jfx_lut_t *lut, const float *rgba, size_t components,
    float mix, float *out_rgba) {
    if (!lut || !lut->entries || !rgba || !out_rgba || (components != 3u && components != 4u)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(mix) || mix < 0.0f || mix > 1.0f) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    float in[4] = { rgba[0], rgba[1], rgba[2], components == 4u ? rgba[3] : 1.0f };
    if (!isfinite(in[0]) || !isfinite(in[1]) || !isfinite(in[2])) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    float out[4];
    switch (lut->shape) {
    case JFX_LUT_SHAPE_1D: sample_1d(lut, in, out); break;
    case JFX_LUT_SHAPE_2D: sample_2d(lut, in, out); break;
    case JFX_LUT_SHAPE_3D: sample_3d(lut, in, out); break;
    default: return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (mix >= 1.0f) {
        out_rgba[0] = out[0];
        out_rgba[1] = out[1];
        out_rgba[2] = out[2];
        out_rgba[3] = components == 4u ? out[3] : in[3];
    } else {
        out_rgba[0] = lerp(in[0], out[0], mix);
        out_rgba[1] = lerp(in[1], out[1], mix);
        out_rgba[2] = lerp(in[2], out[2], mix);
        out_rgba[3] = components == 4u ? lerp(in[3], out[3], mix) : in[3];
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_apply_rgba8(const jfx_lut_t *lut, const uint8_t *pixels, size_t count,
    float mix, uint8_t *out_pixels) {
    if (!lut || !pixels || !out_pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(mix) || mix < 0.0f || mix > 1.0f) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *in = pixels + i * 4u;
        uint8_t *out = out_pixels + i * 4u;
        const float rgba[4] = { (float)in[0] / 255.0f, (float)in[1] / 255.0f,
            (float)in[2] / 255.0f, (float)in[3] / 255.0f };
        float graded[4];
        if (jfx_lut_sample(lut, rgba, 4, mix, graded) != JFX_SUCCESS) {
            return JFX_ERROR_BACKEND_FAILURE;
        }
        /* A 1D or 2D LUT can carry an alpha row; a 3D LUT cannot, so alpha
         * passes through untouched. Either way it must not drift. */
        out[0] = to_unorm8(graded[0]);
        out[1] = to_unorm8(graded[1]);
        out[2] = to_unorm8(graded[2]);
        out[3] = in[3];
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_apply_rgba32f(const jfx_lut_t *lut, float *pixels, size_t count,
    float mix, float *out_pixels) {
    if (!lut || !pixels || !out_pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(mix) || mix < 0.0f || mix > 1.0f) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        const float *in = pixels + i * 4u;
        float *out = out_pixels + i * 4u;
        float graded[4];
        if (jfx_lut_sample(lut, in, 4, mix, graded) != JFX_SUCCESS) {
            return JFX_ERROR_BACKEND_FAILURE;
        }
        out[0] = graded[0];
        out[1] = graded[1];
        out[2] = graded[2];
        out[3] = in[3];
    }
    return JFX_SUCCESS;
}

/* ---- Cube (.cube), 1D and 3D --------------------------------------------- */

/* Text scanning over an in-memory document, NUL-terminated, with line tracking
 * so a diagnostic can name the line. */
typedef struct {
    const char *text;
    const char *cursor;
    size_t line;
} text_scanner_t;

static bool skip_space_and_comments(text_scanner_t *s) {
    for (;;) {
        while (*s->cursor == '\n') {
            s->line++;
            s->cursor++;
        }
        while (*s->cursor == ' ' || *s->cursor == '\t' || *s->cursor == '\r') {
            s->cursor++;
        }
        if (*s->cursor != '#') {
            return true;
        }
        while (*s->cursor && *s->cursor != '\n') {
            s->cursor++;
        }
    }
}

/* Reads the next whitespace-delimited word. Returns false at end of input. */
static bool next_token(text_scanner_t *s, char *out, size_t out_size) {
    if (!skip_space_and_comments(s)) {
        return false;
    }
    size_t n = 0;
    while (*s->cursor && !isspace((unsigned char)*s->cursor)) {
        if (n + 1u >= out_size) {
            return false;
        }
        out[n++] = *s->cursor++;
    }
    out[n] = '\0';
    return n > 0;
}

static bool next_double(text_scanner_t *s, double *out) {
    char word[64];
    if (!next_token(s, word, sizeof(word))) {
        return false;
    }
    char *end = NULL;
    const double value = strtod(word, &end);
    if (end == word || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

jfx_result_t jfx_lut_parse_cube(const char *text, size_t size, jfx_lut_t **out_lut,
    char *out_error, size_t out_error_size) {
    if (!text || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    (void)size;

    size_t one_d = 0, three_d = 0;
    double domain_min[3] = { 0.0, 0.0, 0.0 };
    double domain_max[3] = { 1.0, 1.0, 1.0 };
    char title[JFX_LUT_TITLE_MAX] = { 0 };

    /* First pass: find the declared size and domain. */
    {
        text_scanner_t s = { text, text, 1 };
        for (;;) {
            char word[64];
            if (!next_token(&s, word, sizeof(word))) {
                break;
            }
            if (strcmp(word, "LUT_1D_SIZE") == 0) {
                double value = 0.0;
                if (!next_double(&s, &value) || value < 2.0 ||
                    value > (double)JFX_LUT_MAX_DIMENSION) {
                    set_error(out_error, out_error_size, "line %zu: bad LUT_1D_SIZE", s.line);
                    return JFX_ERROR_INVALID_ARGUMENT;
                }
                one_d = (size_t)value;
            } else if (strcmp(word, "LUT_3D_SIZE") == 0) {
                double value = 0.0;
                if (!next_double(&s, &value) || value < 2.0 ||
                    value > (double)JFX_LUT_MAX_DIMENSION) {
                    set_error(out_error, out_error_size, "line %zu: bad LUT_3D_SIZE", s.line);
                    return JFX_ERROR_INVALID_ARGUMENT;
                }
                three_d = (size_t)value;
            } else if (strcmp(word, "DOMAIN_MIN") == 0) {
                for (int i = 0; i < 3; ++i) {
                    if (!next_double(&s, &domain_min[i])) {
                        set_error(out_error, out_error_size, "line %zu: bad DOMAIN_MIN", s.line);
                        return JFX_ERROR_INVALID_ARGUMENT;
                    }
                }
            } else if (strcmp(word, "DOMAIN_MAX") == 0) {
                for (int i = 0; i < 3; ++i) {
                    if (!next_double(&s, &domain_max[i])) {
                        set_error(out_error, out_error_size, "line %zu: bad DOMAIN_MAX", s.line);
                        return JFX_ERROR_INVALID_ARGUMENT;
                    }
                }
            } else if (strcmp(word, "TITLE") == 0) {
                const char *quote = strchr(s.cursor, '"');
                if (quote) {
                    const char *end = strchr(quote + 1, '"');
                    if (end) {
                        size_t n = (size_t)(end - quote - 1);
                        if (n >= sizeof(title)) {
                            n = sizeof(title) - 1u;
                        }
                        memcpy(title, quote + 1, n);
                        title[n] = '\0';
                        s.cursor = end + 1;
                    }
                }
            }
        }
        if (domain_max[0] <= domain_min[0] || domain_max[1] <= domain_min[1] ||
            domain_max[2] <= domain_min[2]) {
            set_error(out_error, out_error_size, "DOMAIN_MAX must exceed DOMAIN_MIN");
            return JFX_ERROR_INVALID_ARGUMENT;
        }
    }
    if (!one_d && !three_d) {
        set_error(out_error, out_error_size,
            "no LUT_1D_SIZE or LUT_3D_SIZE header (not an Adobe .cube file?)");
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_lut_t *lut = NULL;
    jfx_result_t status = jfx_lut_create(
        three_d ? JFX_LUT_SHAPE_3D : JFX_LUT_SHAPE_1D,
        three_d ? three_d : one_d, three_d ? three_d : 1u, three_d ? three_d : 1u, 3u, &lut);
    if (status != JFX_SUCCESS) {
        set_error(out_error, out_error_size, "cannot allocate a %zux LUT",
            three_d ? three_d : one_d);
        return status;
    }
    for (int i = 0; i < 3; ++i) {
        lut->domain_min[i] = (float)domain_min[i];
        lut->domain_max[i] = (float)domain_max[i];
    }
    memcpy(lut->title, title, sizeof(title));

    /* Second pass: read the table, in file order. `expected` counts triples,
     * which is what one .cube line holds, not the component count. */
    const size_t expected = lut->entry_count / 3u;
    size_t filled = 0;
    text_scanner_t s = { text, text, 1 };
    for (;;) {
        char word[64];
        if (!next_token(&s, word, sizeof(word))) {
            break;
        }
        /* The header is interleaved with the data, so a keyword is skipped
         * along with its arguments rather than being mistaken for a value. */
        if (strcmp(word, "TITLE") == 0) {
            const char *quote = strchr(s.cursor, '"');
            if (!quote) {
                set_error(out_error, out_error_size, "line %zu: TITLE has no value", s.line);
                jfx_lut_destroy(lut);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            const char *end = strchr(quote + 1, '"');
            if (!end) {
                set_error(out_error, out_error_size, "line %zu: unterminated TITLE", s.line);
                jfx_lut_destroy(lut);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            s.cursor = end + 1;
            continue;
        }
        if (strcmp(word, "DOMAIN_MIN") == 0 || strcmp(word, "DOMAIN_MAX") == 0 ||
            strcmp(word, "LUT_1D_SIZE") == 0 || strcmp(word, "LUT_3D_SIZE") == 0) {
            int arity = (strcmp(word, "DOMAIN_MIN") == 0 || strcmp(word, "DOMAIN_MAX") == 0) ? 3 : 1;
            for (int i = 0; i < arity; ++i) {
                double ignored = 0.0;
                if (!next_double(&s, &ignored)) {
                    set_error(out_error, out_error_size, "line %zu: %s is missing arguments",
                        s.line, word);
                    jfx_lut_destroy(lut);
                    return JFX_ERROR_INVALID_ARGUMENT;
                }
            }
            continue;
        }
        double component[3];
        char *tail = NULL;
        component[0] = strtod(word, &tail);
        if (tail == word) {
            set_error(out_error, out_error_size, "line %zu: unexpected token '%s'", s.line, word);
            jfx_lut_destroy(lut);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        for (int i = 1; i < 3; ++i) {
            if (!next_double(&s, &component[i])) {
                set_error(out_error, out_error_size, "line %zu: expected three components",
                    s.line);
                jfx_lut_destroy(lut);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        }
        if (filled >= expected) {
            set_error(out_error, out_error_size,
                "line %zu: more data than the declared size allows", s.line);
            jfx_lut_destroy(lut);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        float *out = lut->entries + filled * 3u;
        out[0] = (float)component[0];
        out[1] = (float)component[1];
        out[2] = (float)component[2];
        filled++;
    }
    if (filled != expected) {
        set_error(out_error, out_error_size, "expected %zu entries, found %zu", expected, filled);
        jfx_lut_destroy(lut);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    lut->description[0] = '\0';
    snprintf(lut->description, sizeof(lut->description), "Adobe .cube, %zux%s, %zu entries",
        three_d ? three_d : one_d, three_d ? "3D" : "1D", filled);
    lut_set_format(lut, JFX_LUT_FORMAT_CUBE);
    *out_lut = lut;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_load_cube(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    char *text = read_file(path, &size);
    if (!text) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    jfx_result_t status = jfx_lut_parse_cube(text, size, out_lut, out_error, out_error_size);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
    return status;
}

jfx_result_t jfx_lut_write_cube(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size) {
    if (!lut || !lut->entries || !path) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->channels < 3u) {
        set_error(out_error, out_error_size, "a .cube table needs at least three channels");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->shape == JFX_LUT_SHAPE_2D) {
        set_error(out_error, out_error_size,
            "a 2D strip has no .cube representation; write it as .3dl or fold it into a 3D cube");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(out_error, out_error_size, "cannot write '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    fprintf(file, "# Created by JoltFX\n");
    if (lut->title[0]) {
        fprintf(file, "TITLE \"%s\"\n", lut->title);
    }
    fprintf(file, "DOMAIN_MIN %.6f %.6f %.6f\n", (double)lut->domain_min[0],
        (double)lut->domain_min[1], (double)lut->domain_min[2]);
    fprintf(file, "DOMAIN_MAX %.6f %.6f %.6f\n", (double)lut->domain_max[0],
        (double)lut->domain_max[1], (double)lut->domain_max[2]);
    if (lut->shape == JFX_LUT_SHAPE_1D) {
        fprintf(file, "LUT_1D_SIZE %zu\n", lut->width);
    } else {
        fprintf(file, "LUT_3D_SIZE %zu\n", lut->width);
    }
    for (size_t i = 0; i < lut->entry_count / lut->channels; ++i) {
        const float *entry = entry_at(lut, i);
        fprintf(file, "%.6f %.6f %.6f\n", (double)entry[0], (double)entry[1],
            (double)entry[2]);
    }
    const bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok) {
        set_error(out_error, out_error_size, "short write to '%s'", path);
        return JFX_ERROR_BACKEND_FAILURE;
    }
    return JFX_SUCCESS;
}

/* ---- Autodesk .3dl -------------------------------------------------------- */

/* A .3dl file is a bare list of float triples with no header, so the file
 * alone does not say whether it is a per-channel curve or a 2D strip. Rather
 * than guess from the entry count - which silently misreads a 6-entry 1D LUT as
 * a 2-entry one - the shape is a parameter and the single-argument loader
 * states its choice. */
jfx_result_t jfx_lut_load_3dl_as(const char *path, jfx_lut_shape_t shape, size_t strip_width,
    jfx_lut_t **out_lut, char *out_error, size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape != JFX_LUT_SHAPE_1D && shape != JFX_LUT_SHAPE_2D) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape == JFX_LUT_SHAPE_2D && (!strip_width || strip_width > JFX_LUT_MAX_DIMENSION)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    char *text = read_file(path, &size);
    if (!text) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }

    /* Count the triples first so the table can be sized exactly. */
    size_t triples = 0;
    {
        text_scanner_t s = { text, text, 1 };
        double a = 0.0, b = 0.0, c = 0.0;
        while (next_double(&s, &a)) {
            if (!next_double(&s, &b) || !next_double(&s, &c)) {
                break;
            }
            triples++;
        }
    }
    if (triples == 0u || triples > (1u << 20)) {
        set_error(out_error, out_error_size,
            "no float triples found (not a .3dl file?), or more than %u", 1u << 20);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_lut_shape_t shape_value = shape;
    size_t width, height;
    if (shape == JFX_LUT_SHAPE_1D) {
        if (triples > JFX_LUT_MAX_DIMENSION) {
            set_error(out_error, out_error_size, "%zu entries exceeds the 1D limit of %u",
                triples, JFX_LUT_MAX_DIMENSION);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        width = triples;
        height = 1u;
    } else {
        if (triples % strip_width) {
            set_error(out_error, out_error_size,
                "%zu entries do not divide into rows of %zu", triples, strip_width);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        width = strip_width;
        height = triples / strip_width;
        if (height > JFX_LUT_MAX_DIMENSION) {
            set_error(out_error, out_error_size, "%zu rows exceeds the limit of %u", height,
                JFX_LUT_MAX_DIMENSION);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
    }
    shape_value = shape;

    jfx_lut_t *lut = NULL;
    jfx_result_t status = jfx_lut_create(shape_value, width, height, 1u, 3u, &lut);
    if (status != JFX_SUCCESS) {
        set_error(out_error, out_error_size, "cannot allocate a %zux%zu table", width, height);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);
        return status;
    }

    size_t filled = 0;
    text_scanner_t s = { text, text, 1 };
    double a = 0.0, b = 0.0, c = 0.0;
    while (next_double(&s, &a) && next_double(&s, &b) && next_double(&s, &c)) {
        if (filled >= lut->entry_count / 3u) {
            break;
        }
        float *out = lut->entries + filled * 3u;
        out[0] = (float)a;
        out[1] = (float)b;
        out[2] = (float)c;
        filled++;
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), text);

    if (filled != lut->entry_count / 3u) {
        set_error(out_error, out_error_size, "expected %zu entries, found %zu",
            lut->entry_count / 3u, filled);
        jfx_lut_destroy(lut);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    snprintf(lut->description, sizeof(lut->description), "Autodesk .3dl, %zux%s, %zu entries",
        width, shape == JFX_LUT_SHAPE_1D ? "1D" : "2D", filled);
    lut_set_format(lut, JFX_LUT_FORMAT_3DL);
    *out_lut = lut;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_load_3dl(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    /* One triple per entry, i.e. a per-channel curve. This is what Resolve,
     * Drone and the Lustre 3DL tools produce; a strip needs the explicit form. */
    return jfx_lut_load_3dl_as(path, JFX_LUT_SHAPE_1D, 0, out_lut, out_error, out_error_size);
}

jfx_result_t jfx_lut_write_3dl(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size) {
    if (!lut || !lut->entries || !path) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->channels < 3u) {
        set_error(out_error, out_error_size, "a .3dl table needs three channels");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->shape == JFX_LUT_SHAPE_3D) {
        set_error(out_error, out_error_size,
            "a 3D cube has no .3dl representation; write it as .cube or .spi3d");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(out_error, out_error_size, "cannot write '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    fprintf(file, "# Created by JoltFX\n");
    for (size_t i = 0; i < lut->entry_count / lut->channels; ++i) {
        const float *entry = entry_at(lut, i);
        fprintf(file, "%.6f %.6f %.6f\n", (double)entry[0], (double)entry[1],
            (double)entry[2]);
    }
    const bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok) {
        set_error(out_error, out_error_size, "short write to '%s'", path);
        return JFX_ERROR_BACKEND_FAILURE;
    }
    return JFX_SUCCESS;
}

/* ---- Sony .spi1d / .spi3d ------------------------------------------------ */

/* Both are raw little-endian binary32 triples with no header, so the content
 * alone cannot say whether the file is a curve or a cube: a 1D LUT with eight
 * entries has exactly the same eight triples as a 2x2x2 cube. The format is
 * therefore taken from the shape the caller states, or from the file name, and
 * the entry count is only used to check that the stated shape fits. */
jfx_result_t jfx_lut_load_spi_as(const char *path, jfx_lut_shape_t shape, jfx_lut_t **out_lut,
    char *out_error, size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (shape != JFX_LUT_SHAPE_1D && shape != JFX_LUT_SHAPE_3D) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    char *bytes = read_file(path, &size);
    if (!bytes) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    if (size < 12u || (size % 12u) != 0u) {
        set_error(out_error, out_error_size,
            "'%s' is %zu bytes, not a whole number of 12-byte float triples", path, size);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t triples = size / 12u;

    size_t width;
    if (shape == JFX_LUT_SHAPE_3D) {
        size_t edge = (size_t)llround(cbrt((double)triples));
        while (edge > 1u && edge * edge * edge < triples) {
            edge++;
        }
        while (edge > 1u && (edge - 1u) * (edge - 1u) * (edge - 1u) > triples) {
            edge--;
        }
        if (edge * edge * edge != triples) {
            set_error(out_error, out_error_size,
                "%zu triples do not form a cube, so this is not a .spi3d", triples);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        if (edge < 2u || edge > JFX_LUT_MAX_DIMENSION) {
            set_error(out_error, out_error_size, "cube edge %zu is outside [2, %u]", edge,
                JFX_LUT_MAX_DIMENSION);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        width = edge;
    } else {
        if (triples < 2u || triples > JFX_LUT_MAX_DIMENSION) {
            set_error(out_error, out_error_size,
                "%zu entries is outside the 1D range [2, %u]", triples, JFX_LUT_MAX_DIMENSION);
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        width = triples;
    }

    jfx_lut_t *lut = NULL;
    jfx_result_t status = jfx_lut_create(shape, width, shape == JFX_LUT_SHAPE_3D ? width : 1u,
        shape == JFX_LUT_SHAPE_3D ? width : 1u, 3u, &lut);
    if (status != JFX_SUCCESS) {
        set_error(out_error, out_error_size, "cannot allocate a %zu-entry table", width);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return status;
    }
    for (size_t i = 0; i < lut->entry_count / 3u; ++i) {
        float rgb[3];
        memcpy(rgb, bytes + i * 12u, sizeof(rgb));
        lut->entries[i * 3u + 0u] = rgb[0];
        lut->entries[i * 3u + 1u] = rgb[1];
        lut->entries[i * 3u + 2u] = rgb[2];
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
    if (shape == JFX_LUT_SHAPE_3D) {
        snprintf(lut->description, sizeof(lut->description),
            "Sony .spi3d, %zux%zux%zu cube, %zu entries", width, width, width,
            lut->entry_count / 3u);
    } else {
        snprintf(lut->description, sizeof(lut->description), "Sony .spi1d, %zu entries, %zu samples",
            width, width);
    }
    lut_set_format(lut, shape == JFX_LUT_SHAPE_3D ? JFX_LUT_FORMAT_SPI3D : JFX_LUT_FORMAT_SPI1D);
    *out_lut = lut;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_load_spi(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* The .spi1d and .spi3d extensions are the only thing that distinguishes the
     * two shapes, so prefer them. A file named something else is read as a
     * cube when its triples form one and as a curve otherwise. */
    const char *dot = strrchr(path, '.');
    if (dot) {
        if (strcasecmp(dot, ".spi1d") == 0) {
            return jfx_lut_load_spi_as(path, JFX_LUT_SHAPE_1D, out_lut, out_error,
                out_error_size);
        }
        if (strcasecmp(dot, ".spi3d") == 0) {
            return jfx_lut_load_spi_as(path, JFX_LUT_SHAPE_3D, out_lut, out_error,
                out_error_size);
        }
    }
    size_t size = 0;
    char *bytes = read_file(path, &size);
    if (!bytes) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    const size_t triples = (size - (size % 12u)) / 12u;
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
    size_t edge = (size_t)llround(cbrt((double)triples));
    while (edge > 1u && edge * edge * edge < triples) {
        edge++;
    }
    while (edge > 1u && (edge - 1u) * (edge - 1u) * (edge - 1u) > triples) {
        edge--;
    }
    const jfx_lut_shape_t shape = (edge >= 2u && edge * edge * edge == triples)
        ? JFX_LUT_SHAPE_3D
        : JFX_LUT_SHAPE_1D;
    return jfx_lut_load_spi_as(path, shape, out_lut, out_error, out_error_size);
}

jfx_result_t jfx_lut_write_spi(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size) {
    if (!lut || !lut->entries || !path) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->channels < 3u) {
        set_error(out_error, out_error_size, "a .spi table needs three channels");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (lut->shape == JFX_LUT_SHAPE_2D) {
        set_error(out_error, out_error_size, "a 2D strip has no .spi representation");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(out_error, out_error_size, "cannot write '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    for (size_t i = 0; i < lut->entry_count / lut->channels; ++i) {
        const float *entry = entry_at(lut, i);
        const float rgb[3] = { entry[0], entry[1], entry[2] };
        fwrite(rgb, sizeof(float), 3, file);
    }
    const bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok) {
        set_error(out_error, out_error_size, "short write to '%s'", path);
        return JFX_ERROR_BACKEND_FAILURE;
    }
    return JFX_SUCCESS;
}

/* ---- DaVinci .look ------------------------------------------------------- */

/* A .look is a text index followed by a mesh block and then an embedded Adobe
 * .cube document:
 *
 *     3DLOOK
 *     LUT: 0            <- offset of the cube from the end of the index
 *     LUTSize: 4096
 *     3DMESH
 *     MeshSize: 12
 *     Shaders: 0
 *     Divisor: 1
 *     <MeshSize bytes of mesh>
 *     <LUTSize bytes of cube text>
 *
 * The index keys are bare words, not `key: value` on one line, so the walk below
 * matches on the key at the start of a line and reads the value that follows. */
static bool look_key(const char *line, const char *key, const char *next, long *out_value) {
    const size_t length = strlen(key);
    if (strncmp(line, key, length) != 0) {
        return false;
    }
    if (line[length] != '\0' && line[length] != ' ' && line[length] != '\t' &&
        line[length] != ':') {
        return false;
    }
    const char *value = line + length;
    while (*value == ' ' || *value == '\t' || *value == ':') {
        value++;
    }
    if (next && *value == '\0') {
        value = next; /* the value sits alone on the following line */
    }
    if (*value == '\0') {
        return false;
    }
    char *tail = NULL;
    const long parsed = strtol(value, &tail, 10);
    if (tail == value) {
        return false;
    }
    *out_value = parsed;
    return true;
}

jfx_result_t jfx_lut_load_look(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    char *bytes = read_file(path, &size);
    if (!bytes) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }
    if (size < 16u || memcmp(bytes, "3DLOOK", 6u) != 0) {
        set_error(out_error, out_error_size, "'%s' does not start with a 3DLOOK header", path);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    long mesh_size = -1;
    long lut_size = -1;
    long lut_offset = 0;
    size_t header_end = 0; /* first byte of the data section, i.e. after the index */
    {
        const char *cursor = bytes;
        const char *const limit = bytes + size;
        char pending[256] = { 0 }; /* a key whose value sits on the next line */
        /* The index is the contiguous run of key lines at the top of the file.
         * The first line that is not a key ends it, which is what tells us where
         * the mesh and cube payloads begin. */
        while (cursor < limit) {
            const char *eol = strchr(cursor, '\n');
            const size_t length = eol ? (size_t)(eol - cursor) : (size_t)(limit - cursor);
            char line[256];
            const size_t n = length < sizeof(line) ? length : sizeof(line) - 1u;
            memcpy(line, cursor, n);
            line[n] = '\0';
            if (n == 0u) {
                break; /* a blank line ends the index */
            }
            long value = 0;
            const bool matched = pending[0]
                ? (sscanf(line, "%ld", &value) == 1)
                : (look_key(line, "LUTSize", NULL, &value) || look_key(line, "MeshSize", NULL,
                       &value)
                    || look_key(line, "LUT", NULL, &value)
                    || look_key(line, "Shaders", NULL, &value)
                    || look_key(line, "Divisor", NULL, &value) || strcmp(line, "3DLOOK") == 0
                    || strcmp(line, "3DMESH") == 0);
            if (!matched) {
                if (pending[0]) {
                    /* A key with no value: the index is malformed. */
                    set_error(out_error, out_error_size, "'%s' has a key with no value", path);
                    tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
                    return JFX_ERROR_INVALID_ARGUMENT;
                }
                break; /* the payload starts here */
            }
            if (pending[0]) {
                if (strcmp(pending, "LUTSize") == 0) {
                    lut_size = value;
                } else if (strcmp(pending, "MeshSize") == 0) {
                    mesh_size = value;
                }
                pending[0] = '\0';
            } else if (look_key(line, "LUTSize", NULL, &value)) {
                lut_size = value;
            } else if (look_key(line, "MeshSize", NULL, &value)) {
                mesh_size = value;
            } else if (look_key(line, "LUT", NULL, &value)) {
                lut_offset = value;
            } else if (strncmp(line, "LUTSize", 7u) == 0 || strncmp(line, "MeshSize", 8u) == 0) {
                /* The key matched but carried no inline value, so the number is
                 * on the following line. */
                snprintf(pending, sizeof(pending), "%s", line);
            }
            if (!eol) {
                break;
            }
            cursor = eol + 1;
            header_end = (size_t)(cursor - bytes);
        }
    }
    if (lut_size <= 0) {
        set_error(out_error, out_error_size, "'%s' has no readable LUTSize index entry", path);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (mesh_size < 0) {
        mesh_size = 0;
    }
    /* The cube sits after the mesh block. `LUT:` is the offset of the cube from
     * the end of the index; writers leave it at zero, in which case the cube
     * simply follows the mesh. */
    const size_t mesh_end = header_end + (size_t)mesh_size;
    size_t cube_start = lut_offset > 0 ? header_end + (size_t)lut_offset : mesh_end;
    if (cube_start < mesh_end) {
        cube_start = mesh_end; /* the mesh must not overlap the cube */
    }
    if (cube_start > size || (size_t)lut_size > size - cube_start) {
        set_error(out_error, out_error_size,
            "'%s' declares a %ld-byte cube at offset %zu but the file is %zu bytes", path,
            lut_size, cube_start, size);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_result_t status = jfx_lut_parse_cube(bytes + cube_start, (size_t)lut_size, out_lut,
        out_error, out_error_size);
    if (status == JFX_SUCCESS && *out_lut) {
        snprintf((*out_lut)->description, sizeof((*out_lut)->description),
            "DaVinci .look (embedded .cube), %zu entries", (*out_lut)->entry_count / 3u);
        lut_set_format(*out_lut, JFX_LUT_FORMAT_LOOK);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
    return status;
}

/* ---- Hald CLUT ------------------------------------------------------------ */

/* A Hald CLUT of level L is an image of side L^3 that carries a cube of edge
 * L^2, so the cube edge is the square of the cube root of the image side. Level
 * 2 is an 8x8 image of a 4-cube; level 8 is 512x512 of a 64-cube; level 16 is
 * 4096x4096 of a 256-cube, which is the largest this library accepts. */
static jfx_result_t hald_geometry(int width, int height, size_t *out_edge, char *out_error,
    size_t out_error_size, const char *path) {
    const int side = width;
    if (side != height) {
        set_error(out_error, out_error_size, "'%s' is %dx%d; a Hald image must be square", path,
            width, height);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t level = (size_t)llround(cbrt((double)side));
    while (level > 1u && (level - 1u) * (level - 1u) * (level - 1u) > (size_t)side) {
        level--;
    }
    while (level * level * level < (size_t)side) {
        level++;
    }
    if (level * level * level != (size_t)side) {
        set_error(out_error, out_error_size,
            "'%s' is %d px on a side; a Hald image side must be a perfect cube (levels 2, 3, 4, "
            "8, 16, ... give 8, 27, 64, 512, 4096)",
            path, side);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t edge = level * level;
    if (edge < 2u || edge > JFX_LUT_MAX_DIMENSION) {
        set_error(out_error, out_error_size,
            "'%s' is level %zu, which needs a %zux%zux%zu cube; this library reads up to %u",
            path, level, edge, edge, edge, JFX_LUT_MAX_DIMENSION);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_edge = edge;
    return JFX_SUCCESS;
}

/* The cube is written as one continuous buffer with red varying fastest, then
 * green, then blue, starting at the upper-left near corner. Filling a square
 * image in scanline order with that buffer is the whole format. */
jfx_result_t jfx_lut_load_hald(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    if (!jfx_image_codec_available()) {
        set_error(out_error, out_error_size,
            "this build cannot read Hald CLUT images (no image decoder is compiled in)");
        return JFX_ERROR_NOT_FOUND;
    }
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_image_t image = { .size = sizeof(image) };
    if (jfx_image_load(path, NULL, &image) != JFX_SUCCESS || !image.pixels) {
        set_error(out_error, out_error_size, "cannot decode '%s' as an image", path);
        return JFX_ERROR_NOT_FOUND;
    }
    size_t edge = 0;
    jfx_result_t status = hald_geometry((int)image.width, (int)image.height, &edge, out_error,
        out_error_size, path);
    if (status != JFX_SUCCESS) {
        jfx_image_release(&image);
        return status;
    }
    jfx_lut_t *lut = NULL;
    status = jfx_lut_create(JFX_LUT_SHAPE_3D, edge, edge, edge, 3u, &lut);
    if (status != JFX_SUCCESS) {
        set_error(out_error, out_error_size, "cannot allocate a %zux%zux%zu cube", edge, edge, edge);
        jfx_image_release(&image);
        return status;
    }
    const size_t side = image.width;
    /* Entry i of the cube is at pixel (i % side, i / side). */
    for (size_t i = 0; i < lut->entry_count / 3u; ++i) {
        const uint8_t *texel = image.pixels + (i % side + (i / side) * side) * 4u;
        lut->entries[i * 3u + 0u] = (float)texel[0] / 255.0f;
        lut->entries[i * 3u + 1u] = (float)texel[1] / 255.0f;
        lut->entries[i * 3u + 2u] = (float)texel[2] / 255.0f;
    }
    jfx_image_release(&image);
    snprintf(lut->description, sizeof(lut->description),
        "Hald CLUT, %zux%zux%zu cube in a %zux%zu image", edge, edge, edge, side, side);
    lut_set_format(lut, JFX_LUT_FORMAT_HALD);
    *out_lut = lut;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_write_hald(const jfx_lut_t *lut, size_t level, uint8_t *out_pixels,
    size_t out_size, uint32_t *out_width, uint32_t *out_height) {
    if (!lut || !lut->entries || !out_pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (level < 2u || level * level > JFX_LUT_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t edge = level * level;
    if (lut->shape != JFX_LUT_SHAPE_3D || lut->width != edge) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t side = level * level * level;
    if (side > SIZE_MAX / side || side * side > SIZE_MAX / 4u) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    const size_t needed = side * side * 4u;
    if (out_size < needed) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memset(out_pixels, 0, needed);
    for (size_t i = 0; i < lut->entry_count / 3u; ++i) {
        const float *entry = entry_at(lut, i);
        uint8_t *texel = out_pixels + (i % side + (i / side) * side) * 4u;
        texel[0] = to_unorm8(entry[0]);
        texel[1] = to_unorm8(entry[1]);
        texel[2] = to_unorm8(entry[2]);
        texel[3] = 255u;
    }
    if (out_width) {
        *out_width = (uint32_t)side;
    }
    if (out_height) {
        *out_height = (uint32_t)side;
    }
    return JFX_SUCCESS;
}

/* ---- Generation ----------------------------------------------------------- */

jfx_result_t jfx_lut_create_identity(jfx_lut_shape_t shape, size_t edge, jfx_lut_t **out_lut) {
    if (!out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (edge < 2u) {
        edge = 2u;
    }
    if (edge > JFX_LUT_MAX_DIMENSION) {
        edge = JFX_LUT_MAX_DIMENSION;
    }
    jfx_lut_t *lut = NULL;
    jfx_result_t status = jfx_lut_create(shape, edge, shape == JFX_LUT_SHAPE_3D ? edge : 1u,
        shape == JFX_LUT_SHAPE_3D ? edge : 1u, 3u, &lut);
    if (status != JFX_SUCCESS) {
        return status;
    }
    for (size_t i = 0; i < lut->entry_count / 3u; ++i) {
        float *entry = lut->entries + i * 3u;
        if (shape == JFX_LUT_SHAPE_3D) {
            const size_t index = i;
            const size_t last = edge - 1u;
            entry[0] = (float)(index % edge) / (float)last;
            entry[1] = (float)((index / edge) % edge) / (float)last;
            entry[2] = (float)(index / (edge * edge)) / (float)last;
        } else {
            const float position = (float)i / (float)(edge - 1u);
            entry[0] = position;
            entry[1] = position;
            entry[2] = position;
        }
    }
    snprintf(lut->description, sizeof(lut->description), "identity %zux%s", edge,
        shape == JFX_LUT_SHAPE_3D ? "3D" : "1D");
    lut_set_format(lut, JFX_LUT_FORMAT_GENERATED);
    *out_lut = lut;
    return JFX_SUCCESS;
}

jfx_result_t jfx_lut_create_curve(const float *points, size_t point_count, size_t entries,
    jfx_lut_t **out_lut) {
    if (!points || point_count < 2u || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (entries < 2u) {
        entries = 2u;
    }
    if (entries > JFX_LUT_MAX_DIMENSION) {
        entries = JFX_LUT_MAX_DIMENSION;
    }
    /* The control points must ascend in input so the walk below is monotone. */
    for (size_t i = 1; i < point_count; ++i) {
        if (!(points[i * 2u] > points[(i - 1u) * 2u])) {
            return JFX_ERROR_INVALID_ARGUMENT;
        }
    }
    jfx_lut_t *lut = NULL;
    jfx_result_t status = jfx_lut_create(JFX_LUT_SHAPE_1D, entries, 1, 1, 3u, &lut);
    if (status != JFX_SUCCESS) {
        return status;
    }
    for (size_t i = 0; i < entries; ++i) {
        const float x = (float)i / (float)(entries - 1u);
        size_t segment = 0;
        while (segment + 2u < point_count && points[(segment + 1u) * 2u] < x) {
            segment++;
        }
        const float x0 = points[segment * 2u];
        const float x1 = points[(segment + 1u) * 2u];
        const float y0 = points[segment * 2u + 1u];
        const float y1 = points[(segment + 1u) * 2u + 1u];
        const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
        const float y = lerp(y0, y1, t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
        lut->entries[i * 3u + 0u] = y;
        lut->entries[i * 3u + 1u] = y;
        lut->entries[i * 3u + 2u] = y;
    }
    snprintf(lut->description, sizeof(lut->description), "1D curve, %zu points -> %zu samples",
        point_count, entries);
    lut_set_format(lut, JFX_LUT_FORMAT_GENERATED);
    *out_lut = lut;
    return JFX_SUCCESS;
}

/* ---- Auto detection ------------------------------------------------------- */

jfx_result_t jfx_lut_load_auto(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size) {
    if (!path || !out_lut) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    char *bytes = read_file(path, &size);
    if (!bytes) {
        set_error(out_error, out_error_size, "cannot read '%s'", path);
        return JFX_ERROR_NOT_FOUND;
    }

    /* Try each reader in order of how unambiguous it is, and report the first
     * success. A format that is merely *possible* does not win over one that
     * is definitive. */
    struct { const char *name; jfx_result_t (*load)(const char *, jfx_lut_t **, char *, size_t); }
    readers[] = {
        { "DaVinci .look", jfx_lut_load_look },
        { "Adobe .cube", jfx_lut_load_cube },
        { "Hald CLUT", jfx_lut_load_hald },
        { "Autodesk .3dl", jfx_lut_load_3dl },
        { "Sony .spi1d/.spi3d", jfx_lut_load_spi },
    };
    char reason[192];
    for (size_t i = 0; i < sizeof(readers) / sizeof(readers[0]); ++i) {
        reason[0] = '\0';
        /* Each reader is handed a fresh handle: a failed attempt must not leave
         * a partial LUT behind for the next one to trip over. */
        jfx_lut_t *candidate = NULL;
        if (readers[i].load(path, &candidate, reason, sizeof(reason)) == JFX_SUCCESS && candidate) {
            *out_lut = candidate;
            tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
            return JFX_SUCCESS;
        }
        jfx_lut_destroy(candidate);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), bytes);
    set_error(out_error, out_error_size,
        "'%s' is not a LUT this build reads. Supported: Adobe .cube (1D and 3D), Autodesk .3dl "
        "(1D and 2D), Sony .spi1d and .spi3d, DaVinci .look, and Hald CLUT images. "
        "Iridas .csp is detected as a container but not yet parsed.",
        path);
    return JFX_ERROR_NOT_FOUND;
}

/* ---- Introspection -------------------------------------------------------- */

const char *jfx_lut_shape_name(jfx_lut_shape_t shape) {
    switch (shape) {
    case JFX_LUT_SHAPE_1D: return "1D";
    case JFX_LUT_SHAPE_2D: return "2D";
    case JFX_LUT_SHAPE_3D: return "3D";
    default: return "unknown";
    }
}

const char *jfx_lut_format_name(const jfx_lut_t *lut) {
    if (!lut) {
        return "unknown";
    }
    switch (owner_of(lut)->format) {
    case JFX_LUT_FORMAT_CUBE: return "Adobe .cube";
    case JFX_LUT_FORMAT_3DL: return "Autodesk .3dl";
    case JFX_LUT_FORMAT_SPI1D: return "Sony .spi1d";
    case JFX_LUT_FORMAT_SPI3D: return "Sony .spi3d";
    case JFX_LUT_FORMAT_HALD: return "Hald CLUT";
    case JFX_LUT_FORMAT_LOOK: return "DaVinci .look";
    case JFX_LUT_FORMAT_GENERATED: return "generated";
    default: return "unknown";
    }
}

size_t jfx_lut_size_of(const jfx_lut_t *lut) {
    return lut ? lut->entry_count : 0u;
}

bool jfx_lut_is_identity(const jfx_lut_t *lut, float tolerance) {
    if (!lut || !lut->entries || !isfinite(tolerance) || tolerance < 0.0f) {
        return false;
    }
    for (size_t i = 0; i < lut->entry_count / lut->channels; ++i) {
        const float *entry = entry_at(lut, i);
        for (uint32_t c = 0; c < 3u; ++c) {
            /* Compare against the table's own grid position, so a 3D cube is
             * judged on the samples it holds rather than on continuous input. */
            float expected;
            if (lut->shape == JFX_LUT_SHAPE_3D) {
                const size_t last = lut->width - 1u;
                const size_t index = i;
                const size_t axis = c == 0u ? index % lut->width
                    : (c == 1u ? (index / lut->width) % lut->width : index / (lut->width * lut->width));
                expected = (float)axis / (float)last;
            } else {
                expected = (float)i / (float)(lut->width - 1u);
            }
            if (fabsf(entry[c] - expected) > tolerance) {
                return false;
            }
        }
    }
    return true;
}
