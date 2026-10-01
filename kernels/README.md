# Bundled image kernels

The first CSV batch adds these **20 missing kernels** in inventory order:

| Category | Kernels |
| --- | --- |
| `transform` | `translate2d`, `scale2d`, `rotate2d`, `skew2d`, `transform3d`, `perspective_warp`, `affine_transform`, `polar_transform` |
| `color` | `brightness_contrast`, `hue_rotate`, `color_balance`, `levels`, `curves`, `lut_apply`, `channel_mixer`, `vibrance`, `color_temperature` |
| `blur_sharpen` | `gaussian_blur`, `motion_blur`, `radial_blur` |

The second CSV batch adds these **30 missing kernels** (28 with full test coverage, 2 with minimal implementations):

| Category | Kernels |
| --- | --- |
| `blur_sharpen` | `box_blur`, `unsharp_mask`, `bilateral_filter`, `lens_blur`, `tilt_shift` |
| `distortion` | `wave_distort`, `ripple`, `bulge_pinch`, `lens_distortion`, `displacement_map`, `turbulence_warp`, `polar_coords`, `twist` |
| `generative` | `solid_color`, `gradient_linear`, `gradient_radial`, `checkerboard`, `grid`, `plasma`, `cell_noise`, `voronoi`, `mandelbrot` |
| `noise` | `perlin_noise`, `simplex_noise`, `worley_noise`, `fractal_noise`, `white_noise` |
| `utility` | `passthrough`, `clamp_values` (minimal), `remap_range` (minimal) |

The third CSV batch adds these **10 missing kernels**:

| Category | Kernels |
| --- | --- |
| `fractal` | `julia_set`, `barnsley_fern` |
| `light` | `bloom`, `glow` |
| `video_effects` | `video_noise_grain`, `video_pixelate` |
| `keying` | `luma_keyer` |
| `utility` | `premultiply_alpha`, `format_convert`, `channel_split` |

The fourth CSV batch adds these **15 missing kernels**:

| Category | Kernels |
| --- | --- |
| `color_calibration` | `calib_white_balance`, `calib_hdr_tone_map` |
| `color_grading` | `grade_teal_orange`, `grade_black_and_white` |
| `stylize` | `stylize_cartoon`, `stylize_pixel_art`, `stylize_neon`, `stylize_thermal`, `stylize_night_vision` |
| `transition` | `cross_dissolve`, `wipe` |
| `mask_matte` | `rect_mask`, `ellipse_mask`, `feather_mask`, `track_matte` |
| `time` | `frame_delay`, `time_remap` |

The fifth CSV batch adds these **15 missing kernels**:

| Category | Kernels |
| --- | --- |
| `compositing` | `blend`, `alpha_composite`, `screen`, `multiply`, `overlay` |
| `mask_matte` | `bezier_mask` |
| `time` | `frame_blend`, `freeze_frame` |
| `fractal` | `ifs_fractal` |
| `transition` | `luma_wipe` |
| `light` | `directional_light` |
| `geometry` | `fill_shape` |
| `data_analysis` | `edge_detect` |
| `utility` | `channel_merge` |
| `video_effects` | `video_strobe` |
| `color_grading` | `grade_sepia` |
| `stylize` | `stylize_oil_paint` |

These sources were authored in the fifth batch and wired into the build,
verify tests, and behavioral harness in the seventh batch below.

The sixth CSV batch adds these **20 missing kernels**:

| Category | Kernels |
| --- | --- |
| `compositing` | `chroma_key`, `luma_matte`, `difference`, `add` |
| `time` | `motion_trail` |
| `light` | `point_light`, `lens_flare`, `shadow_cast` |
| `geometry` | `stroke_path`, `boolean_op`, `offset_path` |
| `data_analysis` | `motion_detect`, `histogram_compute` |
| `transition` | `zoom_transition`, `slide_transition` |
| `color_calibration` | `calib_gamma_curve`, `calib_black_level` |
| `keying` | `keying_chroma_keyer` |
| `color_grading` | `grade_split_toning` |
| `stylize` | `stylize_watercolor` |

The seventh CSV batch wires in the fifth batch's 17 sources and adds these
**3 brand-new kernels** (20 new registrations total):

| Category | Kernels |
| --- | --- |
| `color_calibration` | `calib_white_point` |
| `color_grading` | `grade_duotone` |
| `stylize` | `stylize_pencil_sketch` |

The eighth CSV batch adds these **20 missing kernels**:

| Category | Kernels |
| --- | --- |
| `color_calibration` | `calib_color_space_transform`, `calib_log_to_linear`, `calib_legal_to_full` |
| `color_grading` | `grade_shadows_highlights`, `grade_cross_process`, `grade_photo_filter`, `grade_color_wheels` |
| `stylize` | `stylize_ink_outline`, `stylize_line_art`, `stylize_8bit` |
| `keying` | `keying_color_keyer`, `keying_despill`, `keying_garbage_matte` |
| `video_effects` | `video_flicker`, `video_mosaic`, `video_ghosting`, `video_chroma_shift` |
| `transition` | `cube_flip` |
| `geometry` | `shape_morph` |
| `temporal` | `temporal_frame_average` |

The ninth CSV batch adds these **20 missing kernels**:

| Category | Kernels |
| --- | --- |
| `color_calibration` | `calib_rec709_to_rec2020`, `calib_srgb_to_display_p3`, `calib_matrix_transform`, `calib_full_to_legal` |
| `color_grading` | `grade_film_emulation`, `grade_kodachrome`, `grade_velvia`, `grade_portra`, `grade_bleach_bypass`, `grade_color_wash` |
| `stylize` | `stylize_comic_book`, `stylize_duotone_art`, `stylize_contour`, `stylize_xray` |
| `keying` | `keying_difference_keyer`, `keying_matte_choker` |
| `video_effects` | `video_echo`, `video_interlace` |
| `nle` | `nle_cut` |
| `temporal` | `temporal_frame_median` |

Existing sources, including `saturation`, `exposure`, and `invert`, remain in
the original 12-effect catalog. There are 165 CSV entries left after these batches.

## Execution and verification

All effect formulas, transforms, interpolation of grading curves/LUTs, and
blur accumulation are Joltscript. `common/image.jolt` contains shared functions.
The C extension supplies a bounded AST interpreter, scalar math, read-only
image/array access, parameter validation, and frame iteration. It never selects
an effect algorithm by name.

These sources use the **CPU image profile**, exposed by
`joltscript/image_program.h`. They are interpreted, not JBC1 bytecode. GPU
execution, arbitrary camera/interpolation callbacks, and frontend integration
are not provided in this batch. The CSV's `@gpu` field records algorithm
suitability; it does not claim an implemented GPU execution path. Existing
JBC1 programs and their backend dispatch remain unchanged.

```sh
cmake --preset default
cmake --build --preset default --target validate_kernels
build/joltscript/tools/joltc/joltc --check \
  --image-library kernels/common/image.jolt kernels/transform/translate2d.jolt
ctest --test-dir build -R 'kernel_' --output-on-failure
```

The catalog API is available from the existing `jolt_effects` library:

```c
#include "joltscript/image_kernels.h"
jolt_diagnostic_t diagnostic = {.size = sizeof(diagnostic)};
jolt_image_kernels_t *kernels = jolt_image_kernels_create(&diagnostic);
if (kernels) {
    const jolt_image_parameter_t parameters[] = {
        {"offset_x", 12.5}, {"border_mode", 1}
    };
    /* src/dst contain width*height*4 floats. Callers must check the status. */
    jolt_status_t status = jolt_image_kernels_apply(
        kernels, "translate2d", src, width, height, parameters, 2,
        NULL, 0, memory_limit, step_limit, dst);
    (void)status;
    jolt_image_kernels_destroy(kernels);
}
```

Enumerate with `jolt_image_kernels_count/name`. Query each named uniform's
source-defined default, range, and integer flag using
`jolt_image_kernels_parameter_count/info`. Instances and compiled programs are
immutable and can be shared across concurrent calls with independent buffers.

The runner allocates two temporary frames through the engine allocator and
requires `memory_limit >= 2 * width * height * 4 * sizeof(float)` (with checked
size arithmetic). This limit covers frame scratch, not the immutable parsed
program or fixed interpreter stack. `step_limit` bounds evaluated AST nodes
across all pixels, channels and passes. Exhaustion returns `JOLT_ERR_BUDGET`.
Maximum call/evaluation depth is 128; source nesting is limited to 64, source
nodes to 8191, functions to 128, uniforms to 64, and local bindings to 512.
Each source/library is limited to 1 MiB. Sum ranges are bounded to 65536 terms
and full-frame passes to 16. Large-radius Gaussian blurs are deliberately
interpreted and can be slow or exceed a small step budget.

Input and resource arrays must have the declared lengths and contain finite
floats. Missing uniforms use defaults; supplied finite values clamp to source
ranges. Fractional integer/enumeration values, duplicate/unknown names,
malformed resources, and singular transforms return errors. Destination pixels
are published only after success, so in-place use and overlapping buffers are
safe. Transparent pixels are processed as premultiplied values; color grading
unpremultiplies and repremultiplies explicitly. RGB can exceed alpha where a
kernel exposes unclamped/HDR output.

## Source conventions

The image profile uses S-expressions exclusively:

```lisp
;; A logical vector parameter is flattened into named scalar uniforms.
;; translation in pixels, range [-16384,16384]
(param offset_x 0 -16384 16384 0)
(param offset_y 0 -16384 16384 0)
;; The final flag marks an integer uniform.
(param interpolation 1 0 1 1)
(param border_mode 0 0 3 1)
(defkernel translate2d [x y c]
  (sample (- x offset_x) (- y offset_y) c interpolation border_mode))
```

`defn` declares reusable scalar functions. `let [name expression ...] body`
has sequential lexical bindings. `if` evaluates only the chosen branch.
`sum index start end expression` accumulates in double precision over integer
indices in the half-open range. `(passes iterations)` repeats the entire
kernel over the previous result. Parameters, `width`, `height`, zero-based `pass`, `pi`, `true`,
and `false` are visible inside functions. A kernel returns one channel and
is invoked for each `(x,y,c)`, with `c=0,1,2,3` for RGBA.

`sample x y c interpolation border` reads the current source frame.
`data index` and `data-count` read the optional resource array. `require`
returns 1 when its argument is nonzero, otherwise fails the call. Scalar
arithmetic, comparisons, boolean operations, `min/max`, `abs`, `floor/ceil`,
`round`, `sqrt`, `pow`, `exp/log`, `sin/cos/tan`, `atan2`, and `mod` are supported.
User functions cannot shadow language intrinsics. There is no filesystem,
foreign-call, heap, or thread access from Joltscript.

Vector and matrix parameters are logical groups flattened at the C boundary:
`offset_x/y`, `pivot_x/y`, `scale_x/y/z`, `shadows_r/g/b`, and row-major
`m00 ... m22`. Defaults and tight ranges appear next to every declaration.
The input image is passed separately. Curve and LUT objects use the resource
array, never a float encoding of a pointer.

### Arity is not uniform — check before writing a kernel

Most builtins are fixed-arity and fail the whole program with
`wrong function arity` otherwise. The exceptions:

| Form | Arity |
|------|-------|
| `+ - *` | variadic, one or more arguments |
| `/` `min` `max` `and` `or` | exactly two |
| `sample` | exactly five: `x y c interpolation border` |
| `data` / `data-count` / `require` | one / zero / one |
| `let` / `if` / `sum` | two / three / four |

So a 3-way maximum needs `max3` — `(max a (max b c))` passes only because the
outer `max` receives exactly two arguments. `all2..all4` and `any2..any4` in
`common/image.jolt` exist for the same reason: `and`/`or` cannot take three
operands. `def` does not exist; constants are written inline or bound with
`let`. User `defn` recursion is supported, bounded by the profile's
expression-depth limit.

### There is no fold, only `sum`

`sum` accumulates, so reductions needing a minimum, maximum or nearest match
have no direct expression. Three workarounds are used in
`common/image.jolt`; new kernels should reuse them rather than reinvent:

- `alpha-min` / `alpha-max` — neighbourhood extrema, with the maximum
  expressed as `1 - mean(1 - a)`.
- `worley-soft` / `worley-soft-x` / `worley-soft-y` — a nearest-cell pick as an
  exponentially weighted average over the 3×3 neighbourhood, which converges to
  the true nearest cell as the sharpness parameter rises.
- `soft-max-luma` in `calib_luminance_match` — a frame maximum as log-sum-exp.

Any frame-wide reduction that needs a true maximum should use one of these.
Reaching for `sum` there silently returns a sum.

## Resource array layouts

The resource array is one flat float block shared by every kernel, so each
kernel documents its own encoding in a comment above its params. The layouts in
use are: a bare `(x,y)` pair list for `curves`; length-prefixed curves
(`data[off] = N`, then `N` pairs) for `grade_rgb_curves`, read with
`curve-value-at`; a cube of RGB triples with red index fastest for
`grade_color_lookup` and `calib_3d_lut_calib`; a leading optional point count
then `N` RGB triples for `calib_1d_lut_calib`; `data[0] = N` then `N` RGB
triples for `grade_gradient_map`, read with `colormap-value`; a flattened
closed polygon (`data[0] = N`, then `N` normalized pairs) at a float offset for
the geometry and `bezier_mask` kernels; and a second RGBA frame
(`width*height*4` floats) for the multi-frame and match kernels, read with
`to-frame`.

Every multi-frame or path kernel must tolerate an **empty** resource array:
`(data-count)` is 0 in the common case, and an unguarded `(data ...)` read fails
the call. `to-frame` and the path helpers do this internally; new kernels
should follow suit.

## Transform conventions

- Integer `(x,y)` is a texel center. UV edge coordinates are
  `((x+.5)/width,(y+.5)/height)`. Normalized pivots/anchors may lie outside
  the image. Positive Y points down; positive image-plane rotation is clockwise.
- Translation and blur distances are pixels; rotation/skew angles are degrees.
  Scale zero and singular shear/matrix transforms are rejected.
- Interpolation: `0=nearest`, `1=bilinear`. Borders:
  `0=transparent`, `1=clamp`, `2=wrap`, `3=mirror` (edge texels repeat).
- Affine matrices and perspective corners map source UVs to destination UVs;
  the renderer inverse-maps samples. Corner order is top-left, top-right,
  bottom-right, bottom-left. Antialiasing enables four subpixel samples at
  offsets ±0.25. It is supersampling, not an adaptive reconstruction filter.
- `transform3d` projects the image plane at local Z=0 with XYZ Euler rotations
  (matrix order Rz Ry Rx), normalized plane-unit position/scale, and a pinhole
  camera whose `camera` uniform is focal distance. The neutral camera is at
  distance 1. Local `scale_z` has no effect on the Z=0 plane. The center must
  remain in front of the camera; an edge-on/singular projection is rejected.
- `polar_transform`: forward output X spans `[0,2π)` and output Y spans radial
  distance. `radius` is a fraction of the shorter source dimension. Inverse
  mode samples that polar layout into a Cartesian output; clamp borders are
  used at the polar texture bounds.

## Color and blur conventions

- `brightness_contrast` uses a contrast multiplier around the selected pivot,
  followed by additive brightness. `levels` uses exponent `1/gamma`.
- Hue-only rotations in HSV and HSL both preserve chroma and minimum, so the
  two color-space choices produce the same result. Optional luma preservation
  adds the Rec.709 luminance difference before gamut clamping.
- Color balance uses smooth shadow/highlight weights around middle gray and
  their complementary midtone weight. Vibrance attenuates saturation gain by
  existing saturation; optional skin protection attenuates warm hues.
- Curves accept strictly increasing `(x,y)` pairs, at least two. Empty data is
  identity. `channel=0` selects RGB, `1..3` a color channel, `4` alpha. Alpha
  curves rescale premultiplied RGB. Interpolation is linear (`0`) or cubic
  Hermite with finite-difference tangents (`1`); outside the domain, endpoint
  values are used. Alpha is always clamped to `[0,1]`.
- LUT data contains `3*N*N*N` floats, `N>=2`, red index fastest, then green,
  then blue. Its domain is linear RGB `[0,1]^3`; interpolation is nearest (`0`)
  or trilinear (`1`). `input_space/output_space` are `0=sRGB`, `1=linear`.
  The source is converted to the LUT domain, graded/mixed, then converted to
  output space. Empty data is an identity LUT (space conversion still applies).
- Channel mixing uses a 3×3 straight-RGB matrix. `output_space=1` decodes the
  mixed sRGB values to linear. With `preserve_alpha=0`, output is opaque.
- Temperature uses a relative blackbody RGB approximation, normalized to
  6500K in linear light, plus a green/magenta tint multiplier. It is a creative
  white-balance approximation, not spectral chromatic adaptation.
- Gaussian blur uses two separable passes with normalized Gaussian weights. Radius rounds up; zero is identity. Iterations feed the
  prior full-frame result back into the kernel. Alpha is filtered with RGB.
- Directional motion blur normalizes its direction and samples uniformly across
  `distance * shutter`, centered on the current pixel. Radial blur combines an
  angular span (`amount`, degrees) with radial scaling (`zoom`). Both use
  bilinear sampling with clamp borders; one sample is identity.

## Tests and current limits

`tests/kernels/<name>.test.jolt` contains executable assertions, run by
`test_image_kernels` against rendered frames. Shared assertion functions are
in `tests/helpers.jolt`. The C harness also checks all defaults on 4×4 solids,
transparent inputs, analytic non-default outputs, non-square frames, resource
ordering, multiple blur passes, in-place calls, and error atomicity.
`test_image_program` tests syntax, scopes, lazy branches, recursion/step limits,
invalid sampling/data access, and numeric failures. The compiler driver checks
all two hundred ninety-four sources as separate CTest cases.

CPU/GPU parity cannot be claimed for this profile until a GPU implementation
exists. The legacy JBC1 conformance tests continue to cover their own backend
path. This batch does not replace unsupported operations with no-op stubs.

## Coverage and what the profile cannot express

`scripts/audit-kernels.py` reports the current state: **294 registered image-profile
kernels**, covering **297 of the 322 CSV rows**, with **0 passthrough stubs**. The
legacy 12-effect JBC1 catalogue in `kernels/color/` is separate and is exempt.

The 25 remaining rows are not omissions; their output type is not an image, and this
profile's only output is an image. They split into two groups:

- **17 that cannot be represented at all**, because they produce something other than
  a frame: `AudioBuffer` (`nle_audio_crossfade`, `nle_audio_ducking`), `Mesh`
  (`extrude_path`), `ParticleSystem` (5 `particle_*` except `particle_trail`),
  `Path` (`track_planar_tracker`), `Point[]` (5), `Vec2[]` (3).
- **8 that are a scalar or a vector** (`Float`, `Vec2`, `Vec3`): 6 `track_*` and
  2 `audio_*`. A global estimate is a whole-frame reduction, and the profile has no
  way to publish one except as a uniform image, which changes the output contract.
  That is a decision for the API owner, not something to smuggle in here.

`optical_flow` and `track_motion_tracker` are the exceptions that do return a
FlowField: they publish it as a 2-channel image (c=0 is u, c=1 is v), which is
documented in each file.

## Cost model: reductions are expensive here

A kernel is invoked per `(x,y,c)` and cannot share work between output pixels, so a
whole-frame reduction costs `O(frame area)` *per pixel*. `histogram_compute`,
`optical_flow` and `track_motion_tracker` are all in that class. They are correct,
they are `@gpu No`, and they are not usable at delivery resolution. This is a
property of the profile, not of the individual kernels, and it is the main reason
some CSV operations do not belong in an image kernel at all.

Two substitutions recur because the profile has no fold and no tuple type:

- **argmin/argmax** become a log-sum-exp or an exponentially weighted average over
  the candidate set (`worley-soft`, `alpha-max`, `soft-max-luma`, and the
  nearest-hit selection in `obj_render` and `track_motion_tracker`).
- **3-vectors** become three scalar functions, recomputing the shared part
  (`tri-n` in `obj_render`). There is no way to return a tuple.
