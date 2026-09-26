# AGENTS.md — JoltFX Backends

## Overview

The JoltFX backend layer provides platform-specific GPU and compute implementations. Each backend translates the engine's abstract rendering and execution commands into the native graphics API for a given platform. This guide covers contributing to existing backends and adding new ones.

---

## Repository Layout

```
backends/
  vulkan/
    src/
      device.c           # Physical/logical device selection and creation
      swapchain.c        # Swapchain management and presentation
      pipeline.c         # Graphics and compute pipeline compilation
      memory.c           # Vulkan memory allocator (VMA wrapper)
      commands.c         # Command buffer recording and submission
      sync.c             # Semaphores, fences, pipeline barriers
      descriptors.c      # Descriptor set layout and pool management
      renderpass.c       # Render pass and framebuffer management
      shader.c           # SPIR-V loading and shader module creation
    include/
      vk_backend.h       # Public backend interface
      vk_internal.h      # Internal types and helpers
    tests/
      test_pipeline.c
      test_memory.c
      test_sync.c
  metal/
    src/
      device.m           # MTLDevice selection and feature query
      pipeline.m         # MTLRenderPipelineState / compute pipeline
      heap.m             # MTLHeap and resource allocation
      commands.m         # MTLCommandBuffer encoding
      sync.m             # MTLFence and MTLEvent
    include/
      mtl_backend.h
    tests/
      test_pipeline.m
      test_heap.m
  d3d12/
    src/
      device.cpp         # D3D12 device and adapter enumeration
      pipeline.cpp       # PSO compilation and caching
      heap.cpp           # Descriptor heaps and committed resources
      commands.cpp       # Command list recording
      sync.cpp           # D3D12 fences
    include/
      d3d12_backend.h
    tests/
      test_pipeline.cpp
      test_heap.cpp
  webgpu/
    src/
      device.c           # WebGPU adapter and device
      pipeline.c         # Render and compute pipeline
      buffer.c           # Buffer and texture management
      commands.c         # Command encoder and pass encoding
    include/
      wgpu_backend.h
    tests/
      test_pipeline.c
  common/
    backend_interface.h  # HAL abstract interface all backends implement
    format_table.c       # Format mapping table (jfx_format_t → native)
    caps.h               # Capability flag definitions and denylists
  tests/
    conformance/         # Cross-backend conformance tests
    perf/                # Micro-benchmarks
```

---

## Backend Interface Contract

Every backend implements the HAL declared in `common/backend_interface.h`. **No backend may expose its native types to engine code above the HAL boundary.**

```c
typedef struct jfx_backend_t {
    /* Lifecycle */
    jfx_result_t (*init)(const jfx_backend_desc_t *desc, jfx_backend_t **out);
    void         (*shutdown)(jfx_backend_t *backend);

    /* Resources */
    jfx_result_t (*create_buffer)(jfx_backend_t *, const jfx_buffer_desc_t *, jfx_buffer_t **);
    jfx_result_t (*create_texture)(jfx_backend_t *, const jfx_texture_desc_t *, jfx_texture_t **);
    jfx_result_t (*create_pipeline)(jfx_backend_t *, const jfx_pipeline_desc_t *, jfx_pipeline_t **);
    jfx_result_t (*create_shader)(jfx_backend_t *, const jfx_shader_desc_t *, jfx_shader_t **);
    void         (*destroy_buffer)(jfx_backend_t *, jfx_buffer_t *);
    void         (*destroy_texture)(jfx_backend_t *, jfx_texture_t *);
    void         (*destroy_pipeline)(jfx_backend_t *, jfx_pipeline_t *);
    void         (*destroy_shader)(jfx_backend_t *, jfx_shader_t *);

    /* Commands */
    jfx_cmd_buf_t *(*begin_frame)(jfx_backend_t *);
    jfx_result_t   (*submit)(jfx_backend_t *, jfx_cmd_buf_t *, const jfx_submit_desc_t *);
    jfx_result_t   (*present)(jfx_backend_t *, jfx_swapchain_t *);

    /* Caps */
    void (*query_caps)(jfx_backend_t *, jfx_caps_t *out);
} jfx_backend_t;
```

**A new backend must implement every function pointer.** Stubs are not acceptable for shipped backends; use `JFX_RESULT_NOT_SUPPORTED` only for features that the underlying API genuinely lacks and that the capability flags correctly advertise as absent.

---

## Ownership Rules

| Area                              | Gate Before Merge                              |
|-----------------------------------|------------------------------------------------|
| `common/backend_interface.h`      | Backend lead + architecture review             |
| `common/format_table.c`           | All active backend owners sign off             |
| `common/caps.h`                   | All active backend owners sign off             |
| `vulkan/`                         | Vulkan owner + one reviewer                    |
| `metal/`                          | Metal owner (macOS/iOS platform team)          |
| `d3d12/`                          | D3D12 owner (Windows platform team)            |
| `webgpu/`                         | WebGPU owner + one reviewer                    |
| New backend directory             | Architecture review + two backend owners       |
| Conformance tests                 | QA sign-off                                    |

**Changes to `backend_interface.h` are breaking for every backend.** Coordinate across all owners before touching it.

---

## Build and Toolchain

Backend builds are gated by platform:

```bash
# Vulkan (Linux / Windows)
cmake -DJFX_BACKEND_VULKAN=ON ..
cmake --build . --target jfx_vulkan

# Metal (macOS / iOS, requires Xcode)
cmake -DJFX_BACKEND_METAL=ON ..
cmake --build . --target jfx_metal

# D3D12 (Windows, requires Windows SDK 10.0.22621+)
cmake -DJFX_BACKEND_D3D12=ON ..
cmake --build . --target jfx_d3d12

# WebGPU (all platforms, requires wgpu-native or Dawn)
cmake -DJFX_BACKEND_WEBGPU=ON ..
cmake --build . --target jfx_webgpu
```

**Run conformance tests before submitting any backend change:**

```bash
ctest --test-dir build -R conformance -V
```

All four backends must pass conformance on their respective platforms. A change that breaks conformance on any platform is blocked.

---

## Memory Management

All backends must allocate GPU memory through the engine's resource budget layer, not directly from the driver. Use the arena and pool allocators provided in Tilly for CPU-side allocations. **Never call `malloc` or `free` directly in backend code.**

| Backend | Allocation Mechanism |
|---------|---------------------|
| Vulkan | VMA wrapper in `vulkan/src/memory.c` |
| D3D12 | Committed and placed resources through heap wrappers in `d3d12/src/heap.cpp` |
| Metal | `MTLHeap` for aliased resources |
| WebGPU | `wgpuBuffer`/`wgpuTexture` with engine pool |

**Resource lifetimes** must follow the engine's frame graph. Do not hold a reference to a resource past the frame in which it was retired unless the resource is explicitly marked persistent.

**Transient resources** — render targets, intermediate buffers — must be aliased where the API allows it. Every backend is expected to alias aggressively to stay within the frame memory budget.

---

## Shader Compilation

JoltFX shaders are distributed as **SPIR-V**. Each backend translates to its native format at load time:

| Backend | Translation |
|---------|-------------|
| **Vulkan** | SPIR-V consumed directly via `VkShaderModule` |
| **Metal** | SPIR-V cross-compiled to MSL using `spirv-cross` at pipeline compile time; MSL source cached to disk keyed by SPIR-V hash |
| **D3D12** | SPIR-V cross-compiled to HLSL, then compiled to DXIL via DXC; DXIL cached to disk |
| **WebGPU** | SPIR-V cross-compiled to WGSL |

### Rules

- **Do not embed the `spirv-cross` compilation in the hot path.** Compile on first use, cache the result, and load from cache on subsequent runs.
- **Cache invalidation is by SPIR-V hash;** do not use file timestamps.
- **Shader reflection data** (binding slots, push constant layout, workgroup size) must be extracted from SPIR-V and stored alongside the native shader object. The engine's descriptor binding path depends on this metadata.

---

## Synchronization Rules

Follow these rules for every backend to avoid data races and GPU hangs:

- **Use timeline semaphores** (Vulkan), `MTLEvent` (Metal), or D3D12 fences with monotonically increasing values. Binary semaphores are allowed only for swapchain acquire/present.
- **Insert barriers at resource state transitions, not speculatively.** Let the frame graph's dependency resolver decide what barriers are needed; backends translate the abstract transition into the native barrier call.
- **Never stall the CPU waiting for the GPU inside `submit`.** Submit returns immediately; callers use the fence value to synchronize explicitly.
- **Double-buffer or triple-buffer all per-frame data** (uniform buffers, staging buffers). Never write to a buffer that may still be in flight.

---

## Format Table

`common/format_table.c` maps the engine's `jfx_format_t` enum to each backend's native format. All four backends share this table. When adding a new `jfx_format_t`:

1. Add the enum value to `caps.h`.
2. Add a row in `format_table.c` with a valid mapping for all backends.
3. Mark the format `JFX_FORMAT_UNSUPPORTED` for backends that cannot represent it natively.
4. Add a conformance test that creates a texture in the new format, uploads pixel data, and reads it back.

**Never remove or reorder entries in the format table; it is indexed by enum value.**

---

## Capability Flags

Backends advertise optional features through `jfx_caps_t` returned from `query_caps`. The engine checks caps before using any optional feature.

```c
typedef struct jfx_caps_t {
    bool ray_tracing;
    bool mesh_shaders;
    bool variable_rate_shading;
    bool shader_int64;
    bool shader_float16;
    bool timestamp_queries;
    bool indirect_dispatch;
    uint32_t max_texture_dimension;
    uint32_t max_compute_workgroup_x;
    uint32_t max_compute_workgroup_y;
    uint32_t max_compute_workgroup_z;
    size_t min_uniform_buffer_offset_alignment;
    size_t min_storage_buffer_offset_alignment;
} jfx_caps_t;
```

### Rules

- **Never advertise a capability the hardware does not support.**
- If the driver reports a feature as present but it misbehaves on a known device, add a **device denylist entry in `common/caps.h`** and document the driver bug with a reference to the issue tracker.
- Caps are queried once at backend initialization and cached.

---

## Adding a New Backend

1. Create a directory under `backends/<name>/` with `src/`, `include/`, and `tests/` subdirectories.
2. Implement every function pointer in `jfx_backend_t`.
3. Add a `CMakeLists.txt` gated by a `JFX_BACKEND_<NAME>` option.
4. Add format table entries for all `jfx_format_t` values.
5. Implement `query_caps` with accurate hardware queries.
6. Port the full conformance test suite; all tests must pass.
7. Add a CI lane for the new backend's target platform.
8. Document platform requirements, minimum API version, and any known limitations in `backends/<name>/README.md`.
9. Open a PR against `main` with the architecture review gate satisfied **before** the branch is created.

**A new backend will not be merged without conformance tests passing and a CI lane in place.**

---

## Conformance Tests

Conformance tests live in `tests/conformance/` and run against every backend through the HAL. They cover:

- Buffer create, write, read, destroy
- Texture create (all formats), sample, render-to, destroy
- Compute dispatch and readback
- Graphics pipeline draw and color output
- Resource state transitions
- Synchronization (fence wait, multi-queue if supported)
- Format round-trip (upload pixel data, read back, compare)
- Caps query completeness (no zero-initialized caps struct)

**When fixing a backend bug, add or extend the conformance test that would have caught it before the fix was needed.**

---

## Performance Guidelines

- **Keep the driver-call count per frame proportional to the draw call count.** Batch descriptor updates; do not update one descriptor at a time.
- **Prefer persistent mapped buffers** for streaming data (uniforms, instance data). Avoid staging copies that are not necessary.
- **Pipeline state object (PSO) compilation must be asynchronous.** Block only when the PSO is first needed on screen; use a placeholder or skip the draw before that. Cache compiled PSOs to disk.
- **GPU timestamp queries are the source of truth for backend profiling.** CPU timers are informational only.
- Run `ctest -R perf` and post results in the PR description for any change that touches the hot path (command recording, submit, barrier insertion).

---

## Error Handling

Every backend function that can fail returns `jfx_result_t`. Map all native error codes to the closest `JFX_RESULT_*` value. **Never return `JFX_RESULT_OK` after a failure, and never swallow errors.**

```c
/* Example: Vulkan error mapping */
static jfx_result_t vk_map_result(VkResult vk) {
    switch (vk) {
    case VK_SUCCESS:                         return JFX_RESULT_OK;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:      return JFX_RESULT_OUT_OF_MEMORY;
    case VK_ERROR_DEVICE_LOST:               return JFX_RESULT_DEVICE_LOST;
    case VK_ERROR_INITIALIZATION_FAILED:     return JFX_RESULT_INIT_FAILED;
    case VK_ERROR_INCOMPATIBLE_DRIVER:       return JFX_RESULT_UNSUPPORTED;
    default:                                 return JFX_RESULT_UNKNOWN;
    }
}
```

- **Log the raw native error code before mapping it.** The error stack from Tilly is available to all backends; push a context string that names the backend and the operation before returning the error upward.
- **Device loss (`JFX_RESULT_DEVICE_LOST`)** must be propagated immediately to the engine; do not attempt recovery inside the backend.

---

## Debugging Helpers

Each backend should integrate with the platform's GPU debugging layer when `JFX_DEBUG_GPU` is defined at compile time:

| Backend | Debug Integration |
|---------|-------------------|
| **Vulkan** | Enable validation layers and `VK_EXT_debug_utils`; set object names via `vkSetDebugUtilsObjectNameEXT` |
| **Metal** | Enable Metal Validation and set object labels via `setLabel:` |
| **D3D12** | Enable the D3D12 debug layer; use PIX event markers via the PIX runtime |
| **WebGPU** | Enable the `wgpu` error callbacks and push debug group labels |

**Object labels must match the engine's resource name when one is provided.** This makes GPU captures readable without cross-referencing source.

In release builds all debug instrumentation must compile away completely. Use `JFX_IF_DEBUG_GPU(...)` wrapper macros, defined in `common/caps.h`, rather than bare `#ifdef`.

---

## PR Checklist

- [ ] Conformance tests pass on the target platform
- [ ] No new validation layer errors or Metal Validation warnings
- [ ] Format table updated if a new format is involved
- [ ] Capability flags accurately reflect hardware support
- [ ] Memory allocations go through the engine allocators
- [ ] No direct `malloc`/`free` calls
- [ ] Native errors mapped to `jfx_result_t` and logged before mapping
- [ ] Object labels set in debug builds
- [ ] PSO compilation is async and cached
- [ ] Perf numbers posted for hot-path changes
- [ ] Gate requirement met (see Ownership Rules)
- [ ] `backends/<name>/README.md` updated for new backend or platform change

---

## Common Mistakes

### Leaking Native Handles
Every `VkPipeline`, `MTLRenderPipelineState`, `ID3D12PipelineState`, or equivalent must be released in the corresponding `destroy_*` function. Use RAII wrappers in C++ backends; use explicit tracking structs in C backends.

### Exposing Native Types Above the HAL
Engine code above the backend layer must never see a `VkBuffer`, `id<MTLBuffer>`, or `ID3D12Resource`. Wrap everything in opaque `jfx_buffer_t` handles.

### Synchronous PSO Compilation
Compiling a graphics PSO on the draw-call path causes multi-second hitches. Compile on a background thread, cache the result, and skip the draw until the PSO is ready.

### Missing Barriers
Transitioning a texture from a compute write to a fragment read without a barrier produces undefined results on tiled GPUs and intermittent corruption on desktop. Always let the frame graph insert transitions; never assume a resource is in the right state.

### Returning Success After a Partial Failure
If any step of a multi-step create operation fails, clean up what was already allocated, push an error, and return the failure code. A partially initialized object is worse than no object.

### Hardcoding Device Limits
Never assume `min_uniform_buffer_offset_alignment` is 256 bytes. Always query caps and align accordingly.

### Ignoring Denylists
A driver may report support for a feature that is broken on specific hardware. Always check `common/caps.h` denylists before advertising capabilities.

---

## Contacts

- **Vulkan backend**: `#joltfx-backend-vulkan`
- **Metal backend**: `#joltfx-backend-metal`
- **D3D12 backend**: `#joltfx-backend-d3d12`
- **WebGPU backend**: `#joltfx-backend-webgpu`
- **Cross-backend / HAL design**: `#joltfx-backend-core`
- **Conformance tests**: `#joltfx-qa`

For questions about the HAL interface or adding a new backend, post in `#joltfx-backend-core`.