# Bundled image kernels

The first CSV batch adds these **20 missing kernels** in inventory order:

| Category | Kernels |
| --- | --- |
| `transform` | `translate2d`, `scale2d`, `rotate2d`, `skew2d`, `transform3d`, `perspective_warp`, `affine_transform`, `polar_transform` |
| `color` | `brightness_contrast`, `hue_rotate`, `color_balance`, `levels`, `curves`, `lut_apply`, `channel_mixer`, `vibrance`, `color_temperature` |
| `blur_sharpen` | `gaussian_blur`, `motion_blur`, `radial_blur` |

Existing sources, including `saturation`, `exposure`, and `invert`, remain in
the original 12-effect catalog. There are 299 CSV entries left after this batch.

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
all twenty sources as separate CTest cases.

CPU/GPU parity cannot be claimed for this profile until a GPU implementation
exists. The legacy JBC1 conformance tests continue to cover their own backend
path. This batch does not replace unsupported operations with no-op stubs.
