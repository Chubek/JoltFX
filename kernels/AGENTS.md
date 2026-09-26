# AGENTS.md — JoltFX Kernels

## Overview

This file guides agents implementing Joltscript kernels for JoltFX. Each kernel is a self-contained compute unit executed by the Execution Layer. Kernels are authored in Joltscript and compiled to GPU-executable code via the Glue Layer.

---

## Repository Layout

```
kernels/
  transform/
  color/
  blur_sharpen/
  distortion/
  generative/
  compositing/
  particle/
  text/
  3d/
  audio_reactive/
  mask_matte/
  time/
  noise/
  fractal/
  transition/
  light/
  geometry/
  data_analysis/
  utility/
tests/
  kernels/        # one test file per kernel (named <kernel_name>.test.jolt)
  conformance/    # cross-backend conformance tests
  perf/           # kernel performance benchmarks
docs/
  kernels/        # auto-generated from kernel metadata (by joltdoc)
```

---

## Kernel File Structure

Every kernel lives in its category folder as `<kernel_name>.jolt`. The file must contain three sections in order: metadata block, input/output declarations, and the kernel body.

```lisp
;; kernels/blur_sharpen/gaussian_blur.jolt
// @kernel      gaussian_blur
// @category    blur_sharpen
// @description Applies a separable Gaussian blur to an image layer.
// @complexity  Medium
// @gpu         Yes
// @since       1.0.0
// @author      joltfx-team

(module jolt.fx.kernels.blur_sharpen)

(import jolt.core (ImageBuffer for-each-pixel image-width image-height))
(import jolt.math (exp sqrt pi))

input  image   src
output image   dst

param float radius      = 2.0   // blur radius in pixels, range [0.5, 64.0]
param int   quality     = 3     // number of passes, range [1, 8]
param bool  alpha_aware = true  // preserve alpha channel during blur

kernel {
  (let width (image-width src))
  (let height (image-height src))
  (let sigma (/ radius 2.0))
  (let kernel-size (* 2 (ceil (* 3 sigma)) 1))

  ;; Separable blur: horizontal then vertical
  (with-arena
    (let temp (arena-alloc ImageBuffer width height))
    
    ;; Horizontal pass
    (for-each-pixel [x y] [width height]
      (let sum (rgba 0 0 0 0))
      (let weight-sum 0.0)
      (for [i (- kernel-size) kernel-size]
        (let wx (clamp (+ x i) 0 (- width 1)))
        (let px (sample src wx y))
        (let w (exp (- (/ (* i i) (* 2 sigma sigma)))))
        (set! sum (rgba-add sum (rgba-scale px w)))
        (set! weight-sum (+ weight-sum w)))
      (set-pixel! temp x y (rgba-div sum weight-sum)))
    
    ;; Vertical pass
    (for-each-pixel [x y] [width height]
      (let sum (rgba 0 0 0 0))
      (let weight-sum 0.0)
      (for [i (- kernel-size) kernel-size]
        (let hy (clamp (+ y i) 0 (- height 1)))
        (let px (sample temp x hy))
        (let w (exp (- (/ (* i i) (* 2 sigma sigma)))))
        (set! sum (rgba-add sum (rgba-scale px w)))
        (set! weight-sum (+ weight-sum w)))
      (set-pixel! dst x y (rgba-div sum weight-sum)))))
}
```

### Required Metadata Fields

| Field | Description | Required |
|-------|-------------|----------|
| `@kernel` | Kernel name (snake_case, matches filename) | Yes |
| `@category` | Category folder name | Yes |
| `@description` | One-line description (max 120 chars) | Yes |
| `@complexity` | `Low` \| `Medium` \| `High` | Yes |
| `@gpu` | `Yes` \| `No` \| `Partial` | Yes |
| `@since` | Version introduced (semver) | Yes |
| `@author` | Author/team identifier | No |

**No field may be left blank.** The `jolt verify` command validates all metadata.

---

## Naming Conventions

- **Kernel names**: `snake_case`, all lowercase, no abbreviations unless listed in the approved shortlist below.
- **Parameter names**: `snake_case`. Boolean params use a positive assertion (`alpha_aware`, not `no_alpha`).
- **Input/output names**: `src` for the primary image input, `dst` for the primary image output. Secondary inputs are named by role (`mask`, `displacement`, `lut`, `overlay`).
- **Module path**: `jolt.fx.kernels.<category>`

**Approved abbreviations:** `lut`, `rgb`, `hsl`, `hsv`, `uv`, `ao`, `ifs`, `rng`, `fft`, `dct`, `sdf`.

---

## Input and Output Types

| Type Token | Description | Typical Use |
|------------|-------------|-------------|
| `image` | RGBA float32 framebuffer (premultiplied alpha) | Primary image I/O |
| `audio` | Mono or stereo float32 audio frame | Audio-reactive kernels |
| `mask` | Single-channel float32 mask (0.0–1.0) | Matting, compositing |
| `path` | Vector path data (cubic Béziers) | Text, shape kernels |
| `geometry` | 3D mesh data (vertices, indices, attributes) | 3D kernels |
| `scalar` | Animated scalar value (float32) | Parameter drives |
| `lut` | 3D color lookup table (17×17×17 default) | Color grading |

### Rules

- A kernel may declare **multiple inputs** but **exactly one `output image dst`** unless it is a Data/Analysis kernel, which may output `scalar` or `mask` instead.
- All image buffers are **premultiplied alpha** by convention. Kernels must handle correctly (see Common Mistakes).
- Buffer dimensions are inferred from `src` at runtime; do not hardcode.

---

## Parameter Rules

- **Every parameter needs a default value** and a comment with its unit and range.
- **Range `[min, max]` is enforced at runtime**; pick tight ranges rather than open-ended ones.
- **Keep parameters between 3 and 6 per kernel**. If more are genuinely needed, group them into a sub-struct rather than expanding the flat list.
- **Do not add a parameter** for something that can be derived from an existing one.
- **Parameter types** must match the primitive type table in the Joltscript spec.

### Parameter Annotation Format

```lisp
param <type> <name> = <default>  // <description>, range [<min>, <max>]
```

Examples:
```lisp
param float radius      = 2.0   // blur radius in pixels, range [0.5, 64.0]
param int   quality     = 3     // number of passes, range [1, 8]
param bool  alpha_aware = true  // preserve alpha channel during blur
param float2 center     = (0.5, 0.5)  // effect center in UV space, range [0,1] each
param float4 color      = (1,1,1,1)   // tint color RGBA, range [0,1] each
```

---

## GPU vs CPU Kernels

| Designation | When to Use | Constraints |
|-------------|-------------|-------------|
| `@gpu Yes` | Maps naturally to parallel pixel/vertex work | No heap allocation in kernel body; use only stack variables and tile-shared buffer API |
| `@gpu No` | Serial algorithms or heavy branching on scalar data (e.g., `histogram_compute`, `corner_detect`) | Runs on CPU thread pool; can use heap allocation |
| `@gpu Partial` | Hot path is parallel but setup/readback is serial | Hybrid: CPU setup → GPU dispatch → CPU readback |

**GPU kernels must be written without heap allocation inside the kernel body.** Use fixed-size stack arrays or the tile-shared buffer API (`js_shared_alloc`, `js_shared_free`).

---

## Complexity Guidelines

| Level | Meaning | Examples |
|-------|---------|----------|
| Low | Single-pass, O(1) per pixel, no texture fetches beyond src | `brightness`, `invert`, `channel_swap` |
| Medium | Multi-pass or small neighbourhood fetch (≤ 64 samples) | `gaussian_blur`, `bilateral_filter`, `sobel_edge` |
| High | Iterative, large neighbourhood, FFT, or geometry pass | `optical_flow`, `fluid_sim`, `path_tracer` |

Complexity affects scheduling priority and resource budget allocation.

---

## Writing a New Kernel — Step-by-Step

1. **Pick the correct category folder.** If no folder fits, raise it in review before creating a new one.
2. **Create** `kernels/<category>/<kernel_name>.jolt`.
3. **Fill in all metadata fields first**, before writing any logic.
4. **Declare inputs, outputs, and parameters.** Verify types against the table above.
5. **Implement the kernel body.** Prefer built-in Joltscript intrinsics over manual math where available (e.g., `js_sample_bilinear`, `js_luminance`, `js_blend_mode`, `js_gaussian_weight`).
6. **Add a test file** at `tests/kernels/<kernel_name>.test.jolt` (see Testing).
7. **Run** `jolt verify kernels/<category>/<kernel_name>.jolt` before opening a PR.
8. **Run conformance tests** for the category: `ctest -R "kernel_<category>"`.

---

## Testing

Each test file must cover:

1. **Default parameters** on a 4×4 solid-color input — assert output is not all zeros and not identical to input (unless the kernel is a passthrough).
2. **At least one edge-case parameter** (minimum, maximum, or boolean toggled).
3. **For compositing and mask kernels**: a fully transparent input layer.
4. **For kernels with `@gpu Yes`**: verify CPU and GPU paths produce bit-identical results (within float tolerance).

Test helper functions are in `tests/helpers.jolt`. Use `assert_pixel`, `assert_channel_range`, `assert_equals_ref`, and `assert_gpu_cpu_match` rather than rolling manual comparisons.

```lisp
;; tests/kernels/gaussian_blur.test.jolt
(import "../helpers.jolt")

(test "default blur on solid red"
  (let src (solid-color 1.0 0.0 0.0 1.0 4 4))
  (let dst (run-kernel "gaussian_blur" src radius: 2.0))
  (assert-channel-range dst :channel RGB :min 0.9 :max 1.0))

(test "zero radius is identity"
  (let src (checkerboard 4 4))
  (let dst (run-kernel "gaussian_blur" src radius: 0.5))
  (assert-equals-ref dst src :tolerance 0.01))

(test "gpu-cpu match"
  (let src (noise 32 32))
  (assert-gpu-cpu-match "gaussian_blur" src radius: 3.0 :tolerance 1e-5))
```

### Running Tests

```bash
# All kernel tests
ctest --test-dir build -R kernel_

# Specific kernel
ctest --test-dir build -R "gaussian_blur"

# Conformance (all backends)
ctest --test-dir build -R conformance

# Performance
ctest --test-dir build -R perf
```

---

## Categories and Assigned Kernels

Refer to the canonical kernel spreadsheet for the full list. A brief summary:

| Category | Example Kernels |
|----------|-----------------|
| Transform | `translate2d`, `rotate2d`, `scale2d`, `perspective_warp`, `corner_pin` |
| Color | `curves`, `lut_apply`, `color_temperature`, `hue_saturation`, `levels` |
| Blur/Sharpen | `gaussian_blur`, `bilateral_filter`, `tilt_shift`, `unsharp_mask`, `median_blur` |
| Distortion | `displacement_map`, `turbulence_warp`, `twist`, `bulge`, `ripple` |
| Generative | `voronoi`, `plasma`, `mandelbrot`, `cellular`, `perlin_2d` |
| Compositing | `alpha_composite`, `chroma_key`, `luma_matte`, `blend_modes`, `premultiply` |
| Particle | `particle_emit`, `particle_trail`, `particle_forces`, `particle_collision` |
| Text | `text_render`, `text_kinetic`, `text_on_path`, `text_shatter` |
| 3D | `depth_map_shade`, `ambient_occlusion`, `normal_map`, `environment_reflection` |
| Audio-Reactive | `audio_spectrum_bars`, `audio_beat_flash`, `audio_waveform`, `audio_onset_trigger` |
| Mask/Matte | `bezier_mask`, `track_matte`, `roto_brush`, `difference_matte` |
| Time | `time_remap`, `frame_blend`, `motion_blur`, `echo`, `time_offset` |
| Noise | `perlin_noise`, `fractal_noise`, `voronoi_noise`, `cell_noise`, `simplex_noise` |
| Fractal | `julia_set`, `ifs_fractal`, `mandelbrot_zoom`, `lyapunov` |
| Transition | `luma_wipe`, `cube_flip`, `slide`, `dissolve`, `morph` |
| Light | `bloom`, `lens_flare`, `shadow_cast`, `god_rays`, `volumetric_light` |
| Geometry | `stroke_path`, `shape_morph`, `polygon_triangulate`, `voronoi_diagram` |
| Data/Analysis | `optical_flow`, `motion_detect`, `histogram`, `dominant_color`, `face_detect` |
| Utility | `channel_split`, `channel_merge`, `remap_range`, `format_convert`, `resize` |

---

## Kernel Composition Guidelines

### Reuse Over Duplication

If kernel A is a special case of kernel B, implement A as a thin wrapper that calls B with fixed parameters. **Do not duplicate the math.**

```lisp
;; Good: box_blur as wrapper around gaussian_blur
(defkernel box_blur
  :inputs  [(src ImageBuffer)]
  :outputs [(dst ImageBuffer)]
  :params  [(radius f32 :default 5.0 :range [0.5, 64.0])]
  
  (gaussian_blur src dst :radius radius :quality 1 :alpha_aware true))
```

### Composition Patterns

```lisp
;; Sequential
(compose kernel_a kernel_b kernel_c)

;; Parallel (independent)
(parallel
  (kernel_a :param 1.0)
  (kernel_b :param 2.0))

;; Conditional
(if-condition (predicate)
  (kernel_a)
  (kernel_b))
```

---

## Intrinsics Reference (Commonly Used)

| Intrinsic | Purpose |
|-----------|---------|
| `js_sample_bilinear` | Bilinear texture sampling with clamp/wrap modes |
| `js_sample_nearest` | Nearest-neighbor sampling |
| `js_luminance` | Rec. 709 luminance from RGB |
| `js_chrominance` | Chroma channels (Cb, Cr) |
| `js_hsv_to_rgb` / `js_rgb_to_hsv` | Color space conversion |
| `js_blend_mode` | Porter-Duff and Photoshop blend modes |
| `js_gaussian_weight` | Precomputed Gaussian weight for given offset/sigma |
| `js_scharr_x` / `js_scharr_y` | Scharr gradient operators |
| `js_sobel_x` / `js_sobel_y` | Sobel gradient operators |
| `js_premultiply` / `js_unpremultiply` | Alpha premultiplication |
| `js_srgb_to_linear` / `js_linear_to_srgb` | sRGB ↔ Linear conversion |
| `js_rotate_uv` | Rotate UV coordinates |
| `js_polar_uv` | Cartesian → Polar UV transform |

See `joltscript/stdlib/graphics.jolt` and `geometry.jolt` for full list.

---

## PR Checklist

Before requesting review, confirm:

- [ ] Metadata block complete, no blank fields
- [ ] Parameter ranges tight and documented with units
- [ ] `jolt verify` passes with no warnings
- [ ] Test file present and all tests green (CPU + GPU match)
- [ ] No new external dependencies introduced
- [ ] GPU/CPU designation matches the algorithm's actual parallelism
- [ ] Kernel name matches the canonical spreadsheet exactly
- [ ] Uses intrinsics where available instead of manual math
- [ ] Alpha handling correct for premultiplied inputs
- [ ] No heap allocation in GPU kernel body

---

## Common Mistakes

### Overly Wide Parameter Ranges
A `radius` of `[0, 99999]` is not useful and slows the bounds-check optimizer. Anchor ranges to the largest value that produces visually meaningful output.

### Missing Alpha Handling
Most image kernels must treat alpha-premultiplied inputs correctly. Check whether `src` is premultiplied before sampling; use `js_unpremultiply` / `js_premultiply` at the boundary.

### Heap Allocation in GPU Kernels
Allocating a dynamic array inside a GPU kernel body is a compile error. Use fixed-size stack arrays or the tile-shared buffer API.

### Copying Logic From a Similar Kernel
If kernel A is a special case of kernel B, implement A as a thin wrapper that calls B with fixed parameters. Do not duplicate the math.

### Ignoring Tile Boundaries
GPU kernels execute in tiles. Neighborhood fetches near tile edges need `js_shared_load`/`js_shared_store` or explicit halo handling. Use `js_sample_bilinear` which handles this automatically.

### Hardcoding Buffer Dimensions
Always use `(image-width src)` and `(image-height src)` at runtime. Kernels are resolution-independent.

### Float Precision Assumptions
Don't assume `f32` precision is sufficient for accumulation. Use `f64` locals for reduction sums, then cast back.

---

## Contacts

- **Kernel spec questions**: `#joltfx-kernels`
- **Glue Layer integration**: `#joltfx-compiler`
- **Execution Layer integration**: `#joltfx-engine`
- **Backend-specific issues**: `#joltfx-backend-vulkan`, `#joltfx-backend-metal`, etc.
- **Performance tuning**: `#joltfx-perf`