#ifndef JFX_LUT_H
#define JFX_LUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Colour lookup tables.
 *
 * A LUT is a resampling table that maps a colour to another colour. JoltFX
 * carries them for colour grading, for the look/film emulation LUTs that ship
 * with grading panels, and for any user-supplied transformation.
 *
 * Three shapes are supported, which between them cover every LUT format in
 * common use:
 *
 *   1D  `width` entries per channel. Applied as an independent curve to each
 *       channel. Adobe .cube (LUT_1D_SIZE), Autodesk .3dl, Sony .spi1d.
 *   2D  a `width` x `height` strip. Applied with bilinear interpolation.
 *       Autodesk .3dl in strip form.
 *   3D  `width^3` entries, index (r,g,b) in that order. Applied with
 *       trilinear interpolation. Adobe .cube (LUT_3D_SIZE), Sony .spi3d,
 *       Iridas .csp, Hald CLUT.
 *
 * Formats are read and written through named entry points rather than
 * guessed from a file extension, so a caller that knows what it has (or does
 * not) can be explicit. `jfx_lut_load_auto` sniffs the content for the common
 * cases. */

typedef enum {
    JFX_LUT_SHAPE_1D = 0,
    JFX_LUT_SHAPE_2D,
    JFX_LUT_SHAPE_3D
} jfx_lut_shape_t;

#define JFX_LUT_MAX_DIMENSION 256u
#define JFX_LUT_TITLE_MAX 96
#define JFX_LUT_DESCRIPTION_MAX 160

typedef struct {
    /* Resampling table. Owned by the LUT; free with jfx_lut_destroy. */
    float *entries;
    size_t entry_count;     /* width * height * depth * channels */
    size_t width;           /* 1D: entry count per channel. 2D/3D: width. */
    size_t height;          /* 1D: 1. 2D: rows. 3D: cube edge. */
    size_t depth;           /* 1D/2D: 1. 3D: cube edge. */
    uint32_t channels;      /* 1..4 */
    jfx_lut_shape_t shape;
    /* Sampling domain. Most LUTs are [0,1]; .cube files may declare otherwise
     * and a 1D curve may be declared in log or linear input space. */
    float domain_min[3];
    float domain_max[3];
    char title[JFX_LUT_TITLE_MAX];
    char description[JFX_LUT_DESCRIPTION_MAX];
} jfx_lut_t;

/* Every function that produces a `jfx_lut_t *` writes the out-parameter only on
 * success and leaves it untouched on failure, so a rejected request can never
 * orphan a LUT the caller was still holding. Callers should initialise the
 * handle to NULL and check the result. */

/* ---- Lifecycle ----------------------------------------------------------- */

/* An empty LUT with no entries. Valid to destroy. */
const jfx_lut_t *jfx_lut_empty(void);

void jfx_lut_destroy(jfx_lut_t *lut);

/* Allocates a LUT with zeroed entries. `channels` must be 1..4. A 3D LUT's
 * width, height and depth must all be equal and at most JFX_LUT_MAX_DIMENSION. */
jfx_result_t jfx_lut_create(jfx_lut_shape_t shape, size_t width, size_t height, size_t depth,
    uint32_t channels, jfx_lut_t **out_lut);

/* Deep copy. */
jfx_result_t jfx_lut_copy(const jfx_lut_t *source, jfx_lut_t **out_lut);

/* ---- Generation ---------------------------------------------------------- */

/* The identity transform: a LUT that returns its input unchanged. A 3D LUT is
 * built at `edge` (clamped to [2, JFX_LUT_MAX_DIMENSION]); a 1D LUT at
 * `entries`. Used as the default grade and as the baseline in tests. */
jfx_result_t jfx_lut_create_identity(jfx_lut_shape_t shape, size_t edge, jfx_lut_t **out_lut);

/* A 1D curve from control points. `points` holds (input, output) pairs sorted
 * by increasing input; the curve is resampled to `entries` samples. */
jfx_result_t jfx_lut_create_curve(const float *points, size_t point_count, size_t entries,
    jfx_lut_t **out_lut);

/* ---- Format reading ------------------------------------------------------ */

/* Sniffs the file's content and dispatches to the right reader. Accepts Adobe
 * .cube, Autodesk .3dl, Sony .spi1d/.spi3d, DaVinci .look and Hald CLUT images.
 * Returns JFX_ERROR_NOT_FOUND for a file it does not recognise, with a message
 * in *out_error when provided. */
jfx_result_t jfx_lut_load_auto(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);

jfx_result_t jfx_lut_load_cube(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);
/* A .3dl file carries no header, so it does not say whether it holds a
 * per-channel curve or a 2D strip. This loader reads one triple per entry as a
 * 1D LUT, which is what Resolve, Drone and the Lustre 3DL tools emit. Use
 * `jfx_lut_load_3dl_as` for the strip form. */
jfx_result_t jfx_lut_load_3dl(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);

/* Reads a .3dl with the shape stated rather than inferred: `shape` is 1D or 2D,
 * and `strip_width` is the row length, required for 2D and ignored for 1D. */
jfx_result_t jfx_lut_load_3dl_as(const char *path, jfx_lut_shape_t shape, size_t strip_width,
    jfx_lut_t **out_lut, char *out_error, size_t out_error_size);
/* Sony .spi1d and .spi3d are headerless float triples, so their content cannot
 * say which shape it is: this loader takes the shape from the file name, and
 * otherwise from the entry count. Use `jfx_lut_load_spi_as` when the caller
 * knows. */
jfx_result_t jfx_lut_load_spi(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);

/* Reads a Sony .spi table with the shape stated rather than inferred. `shape`
 * is 1D or 3D; the entry count must agree with it. */
jfx_result_t jfx_lut_load_spi_as(const char *path, jfx_lut_shape_t shape, jfx_lut_t **out_lut,
    char *out_error, size_t out_error_size);
/* DaVinci .look is a container; this extracts and parses the embedded cube. */
jfx_result_t jfx_lut_load_look(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);
/* A Hald CLUT image: an N^2 x N^2 image holding every slice of an N-cube. */
jfx_result_t jfx_lut_load_hald(const char *path, jfx_lut_t **out_lut, char *out_error,
    size_t out_error_size);

/* Parses an in-memory cube document. `size` may be 0 for NUL-terminated text. */
jfx_result_t jfx_lut_parse_cube(const char *text, size_t size, jfx_lut_t **out_lut,
    char *out_error, size_t out_error_size);

/* ---- Format writing ------------------------------------------------------ */

jfx_result_t jfx_lut_write_cube(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size);
jfx_result_t jfx_lut_write_3dl(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size);
jfx_result_t jfx_lut_write_spi(const jfx_lut_t *lut, const char *path, char *out_error,
    size_t out_error_size);

/* Renders a 3D LUT as a Hald CLUT image.
 *
 * A Hald image of level L is L^3 square and carries a cube of edge L^2: level 2
 * is an 8x8 image of a 4-cube, level 4 is 64x64 of a 16-cube, level 8 is 512x512
 * of a 64-cube, and level 16 is 4096x4096 of a 256-cube. The cube is written as
 * one continuous buffer with red varying fastest, so entry i lands at pixel
 * (i % side, i / side). `level` must therefore satisfy level^2 == lut->width.
 *
 * Callers supply the image buffer; `out_pixels` receives tightly packed RGBA8 of
 * (L^3)^2 * 4 bytes. */
jfx_result_t jfx_lut_write_hald(const jfx_lut_t *lut, size_t level, uint8_t *out_pixels,
    size_t out_size, uint32_t *out_width, uint32_t *out_height);

/* ---- Application --------------------------------------------------------- */

/* Applies `lut` to `pixels` (interleaved RGBA8) in place.
 *
 * A 1D or 2D LUT transforms colour channels; a 3D LUT transforms the colour
 * triple and leaves alpha untouched. `mix` in [0,1] interpolates between the
 * input and the result, which is how a grade is dialled back. */
jfx_result_t jfx_lut_apply_rgba8(const jfx_lut_t *lut, const uint8_t *pixels, size_t count,
    float mix, uint8_t *out_pixels);

/* Applies `lut` to `pixels` (interleaved RGBA float, 0..1) in place. */
jfx_result_t jfx_lut_apply_rgba32f(const jfx_lut_t *lut, float *pixels, size_t count,
    float mix, float *out_pixels);

/* Samples a single colour without touching an image. `rgba` is 3 or 4
 * components. Writes 4 components (alpha is 1.0 unless it was supplied). */
jfx_result_t jfx_lut_sample(const jfx_lut_t *lut, const float *rgba, size_t components,
    float mix, float *out_rgba);

/* ---- Introspection ------------------------------------------------------- */

const char *jfx_lut_shape_name(jfx_lut_shape_t shape);
/* Human-readable name of a format this library reads, or "unknown". */
const char *jfx_lut_format_name(const jfx_lut_t *lut);
size_t jfx_lut_size_of(const jfx_lut_t *lut);
bool jfx_lut_is_identity(const jfx_lut_t *lut, float tolerance);

#ifdef __cplusplus
}
#endif

#endif /* JFX_LUT_H */
