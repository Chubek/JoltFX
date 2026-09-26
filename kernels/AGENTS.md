```markdown
# AGENTS.md — JoltFX Kernel Implementation Guide

## Overview

This file guides agents implementing Joltscript kernels for JoltFX. Each kernel
is a self-contained compute unit executed by the Execution Layer. Kernels are
authored in Joltscript and compiled to GPU-executable code via the Glue Layer.

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
  kernels/        # one test file per kernel
docs/
  kernels/        # auto-generated from kernel metadata

---

## Kernel File Structure

Every kernel lives in its category folder as `<kernel_name>.jolt`. The file must
contain three sections in order: metadata block, input/output declarations, and
the kernel body.

joltscript
// @kernel      gaussian_blur
// @category    blur_sharpen
// @description Applies a separable Gaussian blur to an image layer.
// @complexity  Medium
// @gpu         Yes

input  image   src
output image   dst

param float radius      = 2.0   // blur radius in pixels, range [0.5, 64.0]
param int   quality     = 3     // number of passes, range [1, 8]
param bool  alpha_aware = true  // preserve alpha channel during blur

kernel {
  // implementation
}

Required metadata fields: `@kernel`, `@category`, `@description`, `@complexity`,
`@gpu`. No field may be left blank.

---

## Naming Conventions

- Kernel names: `snake_case`, all lowercase, no abbreviations unless listed in
  the approved shortlist below.
- Parameter names: `snake_case`. Boolean params use a positive assertion
  (`alpha_aware`, not `no_alpha`).
- Input/output names: `src` for the primary image input, `dst` for the primary
  image output. Secondary inputs are named by role (`mask`, `displacement`,
  `lut`, `overlay`).

Approved abbreviations: `lut`, `rgb`, `hsl`, `uv`, `ao`, `ifs`.

---

## Input and Output Types

| Type token | Description                        |
|------------|------------------------------------|
| `image`    | RGBA float32 framebuffer           |
| `audio`    | Mono or stereo float32 audio frame |
| `mask`     | Single-channel float32 mask        |
| `path`     | Vector path data                   |
| `geometry` | 3D mesh data                       |
| `scalar`   | Animated scalar value              |
| `lut`      | 3D color lookup table              |

A kernel may declare multiple inputs but exactly one `output image dst` unless
it is a Data/Analysis kernel, which may output `scalar` or `mask` instead.

---

## Parameter Rules

- Every parameter needs a default value and a comment with its unit and range.
- Range `[min, max]` is enforced at runtime; pick tight ranges rather than
  open-ended ones.
- Keep parameters between 3 and 6 per kernel. If more are genuinely needed,
  group them into a sub-struct rather than expanding the flat list.
- Do not add a parameter for something that can be derived from an existing one.

---

## GPU vs CPU Kernels

Mark `@gpu Yes` when the kernel maps naturally to parallel pixel or vertex work.
Mark `@gpu No` for serial algorithms or kernels that branch heavily on scalar
data (e.g., `histogram_compute`, `corner_detect`). Mark `@gpu Partial` when the
hot path is parallel but setup or readback is serial.

GPU kernels must be written without heap allocation inside the kernel body. Use
only stack variables and the provided shared-memory API.

---

## Complexity Guidelines

| Level  | Meaning                                                  |
|--------|----------------------------------------------------------|
| Low    | Single-pass, O(1) per pixel, no texture fetches beyond src |
| Medium | Multi-pass or small neighbourhood fetch (≤ 64 samples)  |
| High   | Iterative, large neighbourhood, FFT, or geometry pass   |

---

## Writing a New Kernel — Step-by-Step

1. Pick the correct category folder. If no folder fits, raise it in review
   before creating a new one.
2. Create `kernels/<category>/<kernel_name>.jolt`.
3. Fill in all metadata fields first, before writing any logic.
4. Declare inputs, outputs, and parameters. Verify types against the table above.
5. Implement the kernel body. Prefer built-in Joltscript intrinsics over manual
   math where available (e.g., `js_sample_bilinear`, `js_luminance`,
   `js_blend_mode`).
6. Add a test file at `tests/kernels/<kernel_name>.test.jolt` (see Testing).
7. Run `jolt verify kernels/<category>/<kernel_name>.jolt` before opening a PR.

---

## Testing

Each test file must cover:

- Default parameters on a 4×4 solid-color input — assert output is not all zeros
  and not identical to input (unless the kernel is a passthrough).
- At least one edge-case parameter (minimum, maximum, or boolean toggled).
- For compositing and mask kernels: a fully transparent input layer.

Test helper functions are in `tests/helpers.jolt`. Use `assert_pixel`,
`assert_channel_range`, and `assert_equals_ref` rather than rolling manual
comparisons.

joltscript
// tests/kernels/gaussian_blur.test.jolt
import "../helpers.jolt"

test "default blur on solid red" {
  src = solid_color(1.0, 0.0, 0.0, 1.0, 4, 4)
  dst = run_kernel("gaussian_blur", src, radius: 2.0)
  assert_channel_range(dst, channel: RGB, min: 0.9, max: 1.0)
}

test "zero radius is identity" {
  src = checkerboard(4, 4)
  dst = run_kernel("gaussian_blur", src, radius: 0.5)
  assert_equals_ref(dst, src, tolerance: 0.01)
}

---

## Categories and Assigned Kernels

Refer to the canonical kernel spreadsheet for the full list. A brief summary:

| Category       | Example kernels                                    |
|----------------|----------------------------------------------------|
| Transform      | `translate2d`, `rotate2d`, `perspective_warp`      |
| Color          | `curves`, `lut_apply`, `color_temperature`         |
| Blur/Sharpen   | `gaussian_blur`, `bilateral_filter`, `tilt_shift`  |
| Distortion     | `displacement_map`, `turbulence_warp`, `twist`     |
| Generative     | `voronoi`, `plasma`, `mandelbrot`                  |
| Compositing    | `alpha_composite`, `chroma_key`, `luma_matte`      |
| Particle       | `particle_emit`, `particle_trail`                  |
| Text           | `text_render`, `text_kinetic`                      |
| 3D             | `depth_map_shade`, `ambient_occlusion`             |
| Audio-Reactive | `audio_spectrum_bars`, `audio_beat_flash`          |
| Mask/Matte     | `bezier_mask`, `track_matte`                       |
| Time           | `time_remap`, `frame_blend`                        |
| Noise          | `perlin_noise`, `fractal_noise`                    |
| Fractal        | `julia_set`, `ifs_fractal`                         |
| Transition     | `luma_wipe`, `cube_flip`                           |
| Light          | `bloom`, `lens_flare`, `shadow_cast`               |
| Geometry       | `stroke_path`, `shape_morph`                       |
| Data/Analysis  | `optical_flow`, `motion_detect`                    |
| Utility        | `channel_split`, `remap_range`, `format_convert`   |

---

## PR Checklist

Before requesting review, confirm:

- [ ] Metadata block complete, no blank fields
- [ ] Parameter ranges tight and documented
- [ ] `jolt verify` passes with no warnings
- [ ] Test file present and all tests green
- [ ] No new external dependencies introduced
- [ ] GPU/CPU designation matches the algorithm's actual parallelism
- [ ] Kernel name matches the canonical spreadsheet exactly

---

## Common Mistakes

**Overly wide parameter ranges.** A `radius` of `[0, 99999]` is not useful and
slows the bounds-check optimizer. Anchor ranges to the largest value that
produces visually meaningful output.

**Missing alpha handling.** Most image kernels must treat alpha-premultiplied
inputs correctly. Check whether `src` is premultiplied before sampling; use
`js_unpremultiply` / `js_premultiply` at the boundary.

**Heap allocation in GPU kernels.** Allocating a dynamic array inside a GPU
kernel body is a compile error. Use fixed-size stack arrays or the tile-shared
buffer API.

**Copying logic from a similar kernel instead of calling it.** If kernel A
is a special case of kernel B, implement A as a thin wrapper that calls B with
fixed parameters. Do not duplicate the math.

---

## Contacts

Raise questions about the kernel spec in the `#joltfx-kernels` channel. For
Glue Layer or Execution Layer integration issues, tag the platform team.
