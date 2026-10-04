# Color calibration and grading

Color Calibration and Color Grading are separate sections over the shared
timeline effect stack. Each selected clip retains its operator order, parameters,
bypass state, LUT paths, blending and keyframes in its `.jfx` project.
The [non-linear editor](nle.md) supplies the shared clip selection, timing edits,
undo/redo, project workflow and frame export on every frontend.
The [composition tool](composition.md) exposes the same color operators as typed
nodes, alongside sources, keys, transforms and blends.

## Frontends

- **Desktop:** select a clip in NLE or the shared selector, then choose **Color
  Calibration** or **Color Grading** in the tab bar. Add an operator, set keys,
  bypass, reset base parameters, reorder or remove it. Grading uses rotary scalar
  dials and Resolve-inspired Lift/Gamma/Gain or Shadows/Midtones/Highlights wheels
  for the corresponding operators. Drag a wheel puck for color balance or its
  master dial for a common RGB adjustment. Dial drags use right/up to increase,
  Shift for precision, double-click to reset and arrows/numeric entry. Live
  gestures and resets undo in one step; transport, selection, preview and Export
  stay shared across tabs. Enter a LUT path and press Enter to validate and load
  it. Clear the path for identity LUT processing. SDK effects can join either
  section by category; see [plugins](plugins.md).
- **CLI:** `joltfx calibration list` and `joltfx grade list` print available
  operators and parameter defaults/ranges. Apply an operator to a still image:

  ```sh
  joltfx grade apply grade_primary input.png output.ppm exposure=1 saturation=0.8
  joltfx grade apply grade_lut input.png output.ppm --lut look.cube mix=0.75
  joltfx calibration apply calib_lut input.png output.ppm --lut display.cube
  ```

  Output is binary PPM. Use `joltfx nodes KIND` for descriptor information.
- **Terminal editor:** `joltfx edit INPUT.jfx OUTPUT.jfx` provides independent
  `grade` and `calibration` catalogs and command prefixes (below).
- **Web:** `mountJoltEditor(bridge)` mounts separate metadata-driven sections,
  with per-section color-layer selection, numeric controls, local LUT import,
  bypass, reset and reorder. Select the track and clip in NLE first.
- **Android:** the activity supplies separate sections and operator selectors.
  Enter the selected clip and section-local layer index, then use **Show selected
  color layer controls**. Controls are cleared after edits/selection changes so
  they cannot accidentally edit a different layer. LUTs use accessible file paths.
- **iOS:** embed `JFXColorViewController`, initialized with a
  `JFXMobilePlayerBridge`; set `track` and `clip` and call `refresh`. The controller
  presents both sections, numeric controls, LUT paths, bypass and reorder.
- **Host bridges:** AE, Premiere and Resolve share `jfx_host_color_kind_count`,
  `jfx_host_color_kind_at` and `jfx_host_color_process`. SDK adapters can build
  separate groups from the section enum. The repository supplies descriptors
  and processing, but does not register installable effects in those hosts.

## Shared editing commands

`jfx_editor_command(editor, op, track, clip, layer, value, text)` accepts the
following suffixes after `grade.` or `calibration.`. Indices are zero-based;
`layer` is local to that section on the selected clip.

| Suffix | Value | Text |
| --- | --- | --- |
| `add` | ignored | operator name, e.g. `grade_primary` |
| `param` | new scalar value | parameter name |
| `path` | ignored | LUT path; empty string clears it |
| `enabled` | 0 bypasses; nonzero enables | ignored |
| `reset` | ignored | ignored; restores base parameter defaults |
| `move` | destination section-local index | ignored |
| `remove` | ignored | ignored |

Reset preserves keyframes, LUT path, opacity and blending. Reordering moves the
operator through the actual shared stack to the target operator's position;
intervening operators retain their relative order. Rendering always follows that
shared order, including non-color effects.

The terminal syntax is `OP TRACK CLIP LAYER VALUE TEXT`. For example:

```text
grade.add 0 0 0 0 grade_primary
grade.param 0 0 0 1 exposure
calibration.add 0 0 0 0 calib_lut
calibration.path 0 0 0 0 /looks/display.cube
save
```

`show` prints the document, `save` writes the output project, and `quit` exits
without saving. Invalid parameter ranges, fractional integer controls and failed
LUT loads are rejected before changing the corresponding value/path.

Effect paths serialize as quoted tokens, including `""` for an unassigned LUT.
Spaces, `#`, quotes and backslashes round-trip. Older unquoted paths still read.
An assigned missing LUT reports a render error instead of silently bypassing.

## Kernel, glue and execution path

`cmake/ColorKernels.cmake` exposes 29 executable image-profile operators: 26
existing kernels and three new kernels (`grade_primary`, `grade_lut`, `calib_lut`).
It generates parameter descriptors from `.jolt` declarations at configure time.
The shared JSON catalog also includes the older graph color operators for
project compatibility. The 12-effect JBC1 catalog remains separate.

The path for new color nodes is:

```text
frontend / graph / timeline
  -> jfx_color_apply
  -> Glue: bundled Joltscript compilation + LUT/library resource preparation
  -> Execution: jolt_image_task_run (budget, transactional output, finite checks)
  -> image-profile CPU interpreter
```

Graph and host buffers are straight float RGBA. The adapter premultiplies at
entry, restores straight RGB at exit, preserves alpha and keeps hidden RGB at
zero alpha. Processing does not implicitly decode/encode sRGB. Insert explicit
calibration transforms when a working-space conversion is needed.

LUT kernels share one implementation in `kernels/common/image.jolt`: 1D curves
and red-fastest 3D cubes, per-channel domain normalization, nearest or
linear/trilinear sampling, domain-edge clamping and a mix control. These kernels
reject 2D tables; the legacy `lut` node still supports the existing 2D sampler.

Execution is currently synchronous CPU interpretation, with per-call compilation,
a 512 MiB scratch budget, 4096-pixel axis limits and 10,000 AST evaluations per
pixel. This is a functional reference path, not a real-time GPU grading pipeline.
Legacy graph color operators still use their existing evaluators.

## OpenColorIO

Built-in `.cube`, `.3dl`, `.spi1d`, `.spi3d`, `.look` and Hald readers work without
OpenColorIO. Glue additionally uses a detected OpenColorIO 2 package. To build the
supplied source tree when no system package is available:

```sh
cmake -S . -B build-ocio -DJFX_COLOR_OCIO_BUNDLED=ON
cmake --build build-ocio --parallel
ctest --test-dir build-ocio -R '^color_grading$' --output-on-failure
```

OpenColorIO's build may fetch its missing dependencies. Its minizip dependency
uses the same ZLIB ABI as OCIO. Set `JFX_COLOR_OCIO=OFF` for built-in readers only.
Extended file transforms (for example ASC CDL `.cc`) are baked by the existing
Glue adapter to a 65³ LUT on `[0,1]`, then sampled by the same color kernels. This
is an approximation over that domain, not an unbounded/HDR OCIO processor or an
OCIO display/view configuration UI.

## Validation and API versions

Color API 1.0.0 is in `jfx/jfx_color.h`; editor commands are API 1.4 and the
additive image-task runner increments the execution ABI minor to 0.4. Existing
public structure layouts are preserved.

Tests cover kernel defaults, exposure, alpha, 1D domains, nearest/trilinear LUTs,
mix, invalid input and failure atomicity, project paths/persistence, section
selection, host processing and identical desktop/web/mobile preview bytes.
`tests/fixtures/color_sequence.jfx` is the shared small project;
`tests/fixtures/color_half.cc` exercises real OCIO when it is available.
