# Jolt Renderer 1.0

An SDK renderer plugin, built both as a standalone `JoltRenderer` native module
and as the same implementation linked into JoltFX's scene renderer. No private
engine headers or engine libraries are linked into the module.

- **CPU / Ingagi:** uses the host's native MonkeyTrace service, which builds an
  owned Ingagi ECS world and BVH from sampled scene triangles. Hard shadows,
  bounded reflections, supersampling, keyed transforms, cloners and Joltscript
  drivers use the existing scene renderer. Maximum 4096 visible instance
  triangles, 64 samples and 8 reflections; timeout at most 60 seconds.
- **SIMD:** if `third_party/xsimd` and a supported target ISA are available,
  xsimd batches perform the RGB exposure/pixel resolve. Scalar tails support
  arbitrary sizes and unaligned output. This does **not** SIMD-accelerate the
  Go ray-intersection kernel. No `-march=native` requirement. Set
  `JFX_JOLT_RENDERER_SIMD=OFF` for the scalar implementation.
- **GPU / Vulkan:** real offscreen depth-tested rasterization via the host's
  Vulkan service. This is GPU rasterization, not Ingagi GPU ray tracing. Ingagi's
  browser WebGPU tracer remains a separate implementation. No silent CPU
  fallback on an explicit GPU request.

Both paths publish tightly packed RGBA8 only on success, use host-owned scratch
and expose optional display-referred exposure through the standalone SDK frame.
The desktop uses neutral exposure. Max dimensions 2048 x 2048. The CPU path needs
`JFX_MONKEYTRACE=ON`; the GPU path needs `JFX_BACKEND_VULKAN=ON` and a working
driver. Both are independently optional. `JFX_JOLT_RENDERER=OFF` disables the
in-tree plugin without removing prior renderer choices or changing their IDs.

Choose **Jolt Renderer (CPU / Ingagi)** or **Jolt Renderer (GPU / Vulkan)** in
the 3D workspace's **Rendering engine** combo. Modeling and Animation sub-tabs
share this choice. Preview, PNG and encoded animation export all use the same
owned scene snapshots and renderer options; navigation uses reduced CPU quality.

Standalone build instructions and module loading are in `JoltFX-SDK/README.md`.
When xsimd is outside this tree, set `JFX_JOLT_RENDERER_XSIMD_ROOT`. Without its
headers the module builds the scalar path; CPU/GPU service availability is
provided by the embedding host, not hardwired into the standalone module.
