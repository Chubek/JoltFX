# Vulkan viewport path (GPU offload)

An opt-in offscreen Vulkan rasterizer for the 3D Modeling & Animation scene,
parallel to the default CPU viewport. No SDK is required at build time and no
display is needed at run time: like the compute path, the graphics ABI subset
in `backends/vulkan/src/vk_minimal.h` is resolved with `dlsym`, and rendering
targets an offscreen image that is copied back to host memory.

## Pieces

- `backends/vulkan/src/vk_scene.vert` / `vk_scene.frag` — minimal GLSL:
  per-draw model/normal matrices, object color and sun direction through one
  uniform buffer; `0.25 + 0.75*|dot|` lambert term matching the CPU look.
  Precompiled with `glslc` and embedded as SPIR-V in `vk_scene_shaders.h`
  (validated with `spirv-val`; GLSL sources are checked in for regeneration).
- `backends/vulkan/src/vk_scene.c` — `jvk_scene_render`: transient device,
  offscreen color (`R8G8B8A8`) + depth (`D32`) images, one graphics pipeline,
  one descriptor set per draw, fence-waited submit, readback. Staging is
  capped (4096 draws, ~1M vertices); anything larger, any missing driver /
  graphics queue / format, or any non-success `VkResult` fails closed with
  output untouched.
- `src/src/modeling3d.cpp` — `jfx_scene3d_render_gpu` (Modeling API 1.3):
  flattens visible instances to object-space triangles with column-major
  model/normal matrices and a y-down view-projection matrix sharing the
  CPU path's camera, focal length and 0.05 near plane, then calls the
  backend. `jfx_scene3d_write_png_gpu` wraps it for PNG output.

## Use

```sh
joltfx 3d render scene.jfx out.png 1.0 gpu
```

The desktop 3D viewport has a "GPU viewport (Vulkan)" checkbox (default
off); when enabled it renders the same scene/time through the GPU path and
falls back to CPU per frame with a status note if the device is unavailable.

## Verification

`test_vulkan_scene` renders a keyed cube-over-plane scene at 320x180 on both
paths: without a driver it asserts the fail-closed contract (output
untouched); with a device it asserts foreground coverage (>5% of frame),
coverage agreement within 20% and fewer than 10% of pixels differing by
more than 12/255 (single-sample edges vs the CPU's 4x SSAA account for the
gap). Verified on AMD RX 580 (RADV): coverage 0.100 vs 0.099, diff fraction
0.001. Without `JFX_BACKEND_VULKAN` the API returns `NOT_IMPLEMENTED`.

## Limits

- Single-sample only, no MSAA; edge pixels differ from the CPU preview.
- No instancing: cloner instances are flattened to one draw each.
- Transient device per call: fine for CLI/tests, not yet pooled for
  sustained 60fps desktop use; the desktop checkbox re-initializes per dirty
  frame.
- Far plane is fixed at 1e5 with standard depth range; the CPU path has no
  far clip.
