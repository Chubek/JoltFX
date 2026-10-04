# AGENTS.md — JoltFX Core Engine

## Overview

This guide covers contributions to the JoltFX core engine: the scheduler, memory subsystem, plugin interface, event bus, and the Glue and Execution layers. Read the kernel-level AGENTS.md (`kernels/AGENTS.md`) before working on anything in `kernels/`. This document governs everything underneath that.

---

## Repository Layout

```
engine/
  core/
    scheduler/        # frame and task scheduling
    memory/           # arena, pool, and scratch allocators
    event/            # event bus and subscription API
    plugin/           # plugin host and lifecycle
    registry/         # kernel and effect registry
  glue/
    compiler/         # Joltscript → IR compilation
    linker/           # IR → platform shader/binary linking
    intrinsics/       # built-in Joltscript intrinsic implementations
  execution/
    cpu/              # CPU execution path (SIMD interpreter)
    gpu/              # GPU command encoding and dispatch
    sync/             # cross-path synchronization primitives
  platform/
    vulkan/
    metal/
    d3d12/
    webgpu/
  public/
    joltfx.h          # public C API surface
    joltfx_ext.h      # extension/plugin author API
tests/
  engine/             # unit and integration tests for core
  perf/               # microbenchmark harness
docs/
  engine/             # generated API docs
```

---

## Ownership Rules

| Area                    | Gate Before Merge                          |
|-------------------------|--------------------------------------------|
| `public/joltfx.h`       | API review from platform lead              |
| `public/joltfx_ext.h`   | API review from platform lead + extensions lead |
| `glue/compiler/`        | Two reviewer sign-offs (compiler team)     |
| `glue/linker/`          | Two reviewer sign-offs (compiler team)     |
| `execution/gpu/`        | Platform lead + perf sign-off              |
| `execution/sync/`       | Platform lead + architecture review        |
| `platform/vulkan/`      | Vulkan backend owner + one reviewer        |
| `platform/metal/`       | Metal backend owner (macOS/iOS team)       |
| `platform/d3d12/`       | D3D12 backend owner (Windows team)         |
| `platform/webgpu/`      | WebGPU backend owner + one reviewer        |
| `core/scheduler/`       | Scheduler owner + one reviewer             |
| `core/memory/`          | Memory subsystem owner + one reviewer      |
| `core/event/`           | Event system owner + one reviewer          |
| `core/plugin/`          | Plugin system owner + one reviewer         |
| Everything else         | One reviewer sign-off                      |

If you are unsure of the current gate owner, ask in `#joltfx-engine` before opening a PR.

---

## Build and Toolchain

The engine targets **C11** and **C++17** (the public API is pure C11). Do not introduce C++20 features without an explicit decision in the architecture channel.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DJFX_ASAN=ON -DJFX_UBSAN=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Always build with `-DJFX_ASAN=ON -DJFX_UBSAN=ON` before pushing.** The CI gate rejects submissions that introduce ASAN or UBSan findings.

**Supported compilers:** Clang 15+, GCC 12+, MSVC 19.34+. Do not use compiler-specific extensions without an `#ifdef` guard.

**CMake options:**
- `-DJFX_BACKEND_VULKAN=ON` — Enable Vulkan backend
- `-DJFX_BACKEND_METAL=ON` — Enable Metal backend (macOS/iOS only)
- `-DJFX_BACKEND_D3D12=ON` — Enable D3D12 backend (Windows only)
- `-DJFX_BACKEND_WEBGPU=ON` — Enable WebGPU backend
- `-DJFX_EXT_LUA=ON` — Enable Lua extension
- `-DJFX_EXT_MRUBY=ON` — Enable mruby extension
- `-DJFX_EXT_QUICKJS=ON` — Enable QuickJS extension
- `-DJFX_EXT_PYTHON=ON` — Enable Python extension (default OFF)

---

## Memory Model

The engine owns all allocations. External callers and kernels never call `malloc`/`free` directly.

| Allocator            | Header                       | Use When                                              |
|----------------------|------------------------------|-------------------------------------------------------|
| `jfx_arena_t`        | `core/memory/arena.h`        | Per-frame scratch, freed in bulk at frame end         |
| `jfx_pool_t`         | `core/memory/pool.h`         | Fixed-size live objects (nodes, handles, descriptors) |
| `jfx_heap_alloc`     | `core/memory/heap.h`         | Long-lived, unpredictable-size allocations            |

### Rules

- **GPU kernel bodies** must use only the tile-shared buffer API; no `jfx_heap_alloc` inside a kernel.
- **Arena memory** must not outlive the frame that allocated it. Do not cache arena pointers across `jfx_frame_begin` / `jfx_frame_end`.
- **Pool objects** must be returned before the pool is destroyed. Leaked pool slots are a hard error in debug builds.
- **All allocations** go through Tilly's memory manager; backend code uses the engine's resource budget layer.

---

## Threading Model

The scheduler owns all worker threads. Engine-internal code must not spawn threads. Synchronization primitives live in `execution/sync/`.

Non-pthread Emscripten builds retain the bounded priority queue but drain tasks
on the caller during `scheduler_wait_idle`, engine ticks and shutdown. Native
and pthread-enabled Emscripten builds use worker threads. The scheduler unit
test covers priority/FIFO order, queue capacity and draining in serial builds.

Auto-backend probing retains the first CPU fallback until a usable GPU backend
replaces it. Destroy unused probes only; the selected handle must remain live
through execution and engine shutdown. `test_core` exercises auto execution and
the no-Vulkan CI matrix covers CPU-only lifetime.

```c
// Crossing the CPU/GPU boundary
jfx_fence_t fence = jfx_gpu_submit(ctx, cmd_list);
jfx_fence_wait(ctx, fence, JFX_TIMEOUT_INFINITE);
```

- Write to shared state only through the provided lock-free queues or under an explicit `jfx_mutex_t`.
- **Never hold a mutex across a GPU submission call.** This deadlocks when the submission blocks waiting for the GPU and the GPU's completion callback tries to acquire the same lock.
- Scheduler callbacks run on worker threads; do not call back into the scheduler from inside a callback. Defer with `jfx_sched_defer` instead.
- The Execution Layer uses lock-free queues for task submission and completion callbacks. GPU synchronization is handled via fences and semaphores provided by the backend.

---

## Public API Conventions

The implemented color API is `include/jfx/jfx_color.h` (1.0.0). Its immutable
descriptors come from `cmake/ColorKernels.cmake` and `.jolt` parameter declarations.
`src/color.c` marshals straight RGBA and LUT resources into the budgeted image
runner. Editor 1.4 includes section-local `grade.*`/`calibration.*` commands; effect
storage and render order remain in the timeline. Preserve quoted/empty LUT paths
when changing project serialization. See `docs/editor.md` and color/frontend tests.

Timeline 1.2 and editor 1.4 provide the NLE edit primitives, sequence-state JSON,
exact-frame rendering/export and bounded sequence history. Keys use a persistent
clip-reference offset so split/head trims preserve interpolation samples. Do not
shift raw key timestamps when repositioning clips. Command keyframes are in
sequence time; raw key APIs remain in reference time. Preserve `track_state`,
`clip_state`, `clip_keys` and `effect_state` during interchange. Borrowed timeline
handles must be reacquired after load/new/undo/redo. See `docs/nle.md` and
`tests/unit/color/test_nle.c` / frontend conformance tests.

Composition 1.1 and editor 1.4 add persistent node positions, validated scalar
editing, deep duplication, graph-state/node-catalog JSON and output/interior
preview/export. Graph and sequence edits share a 32-step/32-MiB history; snapshots
restore their target document and prior mode while retaining the other model.
Preserve explicit output, layout, quoted/empty labels/strings and float precision
in `.jfx`. Empty graphs use `graph` and `output 0`. The CPU evaluator allocates
reachable frames only, under a 512-MiB scratch bound. See `docs/composition.md`,
`tests/unit/color/test_composition_editor.c` and composition frontend conformance.

Audio/export APIs 1.0 are `jfx_audio.h` and `jfx_export.h`. Preserve positional
`track_audio`/`clip_audio` state, original fade reference length and signed sample
clock offsets when editing split/trim timing. The mixer compiles the bundled
audio kernel through Glue and executes stereo JBC1 in Execution. Media readers
are lazy/bounded; missing/unsupported assigned media fails, while video with no
audio stream is silent. Snapshot mixers/jobs own model settings, not file bytes.
Jobs run on one owner thread, never create workers, and replace output only after
codec/trailer/stream success. See `docs/media.md` and audio/media conformance tests.

All public symbols use the `jfx_` prefix. Types end in `_t`. Opaque handles end in `_handle_t`. Return codes are `jfx_result_t`; success is `JFX_OK`.

```c
// Correct pattern
jfx_result_t jfx_effect_create(
    jfx_context_t*           ctx,
    const jfx_effect_desc_t* desc,
    jfx_effect_handle_t*     out_handle
);
```

### Rules

1. **Every public function validates its pointer arguments** and returns `JFX_ERR_INVALID_ARG` rather than crashing on null.
2. **Output parameters come last** and are named `out_*`.
3. **Structs passed by pointer use a `size` field as the first member for ABI versioning:**

```c
typedef struct jfx_effect_desc_t {
    size_t         size;      // set to sizeof(jfx_effect_desc_t)
    const char*    name;
    jfx_category_t category;
    uint32_t       flags;
} jfx_effect_desc_t;
```

4. **Changing the layout or semantics of any existing public symbol** requires an API review and a version bump in `joltfx.h`.
5. **Enums are prefixed** (e.g., `JFX_CATEGORY_*`, `JFX_FORMAT_*`, `JFX_RESULT_*`).
6. **Callbacks use the pattern:** `typedef jfx_result_t (*jfx_callback_fn)(void* userdata, ...);`

---

## Glue Layer

The Glue Layer turns `.jolt` source into platform-executable code. The pipeline is: **parse → typecheck → IR emit → platform lower → link**.

**Key types** live in `glue/compiler/jfx_glue.h`. Do not add new IR opcodes without updating:
- The interpreter in `execution/cpu/interp.c`
- The GPU lowering pass for **every active platform backend** (Vulkan, Metal, D3D12, WebGPU)

**Intrinsic implementations** (`glue/intrinsics/`) must be pure functions with no side effects and no allocations beyond the arena passed to them. If you add an intrinsic:
1. Add it to `intrinsics/registry.c`
2. Write at least one round-trip test that compiles a `.jolt` fragment using it
3. Verify it works on all four GPU backends

---

## Execution Layer

CPU and GPU paths share a common dispatch table initialized at context creation. New execution features go through this table rather than direct calls, so both paths can be exercised by the same test.

**GPU command recording** happens in `execution/gpu/encoder.c`. Encoder functions must be re-entrant across threads that hold separate `jfx_cmd_list_t` objects; shared context state must go through the synchronization primitives, not raw globals.

**Platform backends** live under `platform/`. Adding a new backend requires:
1. A new directory under `platform/`
2. A `jfx_platform_<name>.h` header declaring the backend init function
3. Registration in `platform/platform_select.c`
4. A CI lane with the new platform active

---

## Event Bus

Subscribers register at context creation and are invoked synchronously on the thread that fires the event. **Do not fire events from inside an event handler;** defer through the scheduler queue instead.

```c
jfx_event_subscribe(ctx, JFX_EVENT_FRAME_BEGIN, my_handler, userdata);
```

**Event codes** are defined in `core/event/jfx_events.h`. Adding a new code requires:
1. Updating the documentation table in that header
2. Adding a test that fires the event and asserts the handler is called

**Event categories:**
- `JFX_EVENT_FRAME_*` — Frame lifecycle (begin, end, present)
- `JFX_EVENT_KERNEL_*` — Kernel execution (submit, complete, error)
- `JFX_EVENT_RESOURCE_*` — Resource allocation, destruction
- `JFX_EVENT_TIMELINE_*` — Timeline playback, keyframe changes
- `JFX_EVENT_ASSET_*` — Asset load, unload, reload
- `JFX_EVENT_UI_*` — User input, viewport interactions
- `JFX_EVENT_PLUGIN_*` — Plugin load, unload, error

---

## Plugin Interface

The implemented loader is `src/plugin_host.c` and the SDK header is
`include/jfx/jfx_plugin_sdk.h` (SDK ABI 1.0; additive Plugin API 1.1). Modules
export a size-guarded definition and use the host-service table, without linking
the engine:

```c
JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(uint32_t sdk_major,
    uint32_t sdk_minor, jfx_plugin_definition_t *out_definition);
```

Register native straight-RGBA effects or bounded Joltscript image kernels with
`JFX_PLUGIN_CAP_KERNELS`, editor actions with `JFX_PLUGIN_CAP_EDITOR`, and owned
event subscriptions with `JFX_PLUGIN_CAP_EVENTS`. Other registration capabilities
return NOT_IMPLEMENTED. Static attachment uses the same definition/services;
legacy `jfx_plugin_init(host, api_version)` modules retain version 1 compatibility.

Descriptors are copied. Effect/parameter IDs must fit the 63-byte project token
limit. Graphs, timeline copies, transaction baselines, history and export snapshots
retain custom kinds; unloading returns BUSY while referenced. Host destruction
removes subscriptions and defers finalization until all references release.
Lifecycle, events and documents use one serialized owner thread; no lifecycle
mutation inside callbacks. Use exact-userdata event cleanup for shared relays.

Editor 1.5 transactions group gestures/plugin actions into one history step and
reserve restoration models before edits so cancel needs no allocation. Undo,
cancel, load and reset invalidate borrowed document handles; reacquire them.
See `docs/plugins.md`, `sdk/examples/tint`, `plugin_sdk` and
`plugin_cli_integration` for the service and lifetime contracts.

---

## Testing

**Unit tests** go in `tests/engine/`. **Integration tests** that require a full context use the fixture in `tests/engine/fixture.h`, which spins up a headless context with the null platform backend.

**Performance-sensitive paths** have benchmarks in `tests/perf/`; they are not gated on CI by default but are run manually before merging changes to the scheduler or memory subsystem.

**Minimum test coverage** for a new subsystem:
- One happy-path test
- One invalid-arg test for every public function
- One test for each documented error return
- One cross-backend conformance test (if applicable)

---

## PR Checklist

- [ ] Builds clean with `-DJFX_ASAN=ON -DJFX_UBSAN=ON`, zero ASAN and UBSan findings
- [ ] All existing tests pass (`ctest --test-dir build`)
- [ ] New public symbols follow the naming and struct-versioning conventions
- [ ] New IR opcodes have CPU interpreter and GPU lowering implementations (all backends)
- [ ] New intrinsics registered and round-trip tested
- [ ] New events documented in `jfx_events.h` with test coverage
- [ ] Gate requirement for the changed area met (see Ownership Rules)
- [ ] No `malloc`/`free` calls outside `core/memory/`
- [ ] No new threads spawned outside the scheduler
- [ ] No mutex held across GPU submission
- [ ] Arena pointers not cached across frame boundaries
- [ ] Format table updated if new `jfx_format_t` added
- [ ] Capability flags accurately reflect hardware support

---

## Common Mistakes

### Arena Lifetime Violations
Using arena-allocated memory after `jfx_frame_end`. Fix: allocate from heap or pool for anything that needs to survive a frame boundary.

### Mutex Held Across GPU Submission
This deadlocks when the submission blocks waiting for the GPU and the GPU's completion callback tries to acquire the same lock. The sync primitives doc in `execution/sync/README.md` lists the safe acquisition order.

### Adding a Public Symbol Without a Size-Guarded Struct
The ABI versioning scheme only works if every public struct has `size` as its first field. Omitting it causes silent corruption when the engine and a plugin are built against different header versions.

### Calling Back Into the Scheduler From a Scheduler Callback
The scheduler uses a non-reentrant lock. Defer with `jfx_sched_defer` instead.

### Platform-Specific Code in `glue/` or `execution/` Core Files
The core must compile and run on every platform. Platform-specific code belongs under `platform/` and is reached through the dispatch table.

### Leaking Pool Objects
Every `jfx_pool_acquire` must have a matching `jfx_pool_release` before the pool is destroyed. Debug builds assert on leaked slots.

### Ignoring `jfx_result_t` Return Values
Every fallible API call returns `jfx_result_t`. Check and propagate errors; do not assume success.

---

## Contacts

- **Engine architecture and scheduling**: `#joltfx-engine`
- **Glue Layer / compiler**: `#joltfx-compiler`
- **GPU backends**: `#joltfx-gpu` (Vulkan), `#joltfx-backend-metal`, `#joltfx-backend-d3d12`, `#joltfx-backend-webgpu`
- **Public API changes**: Tag the platform lead directly in the PR
- **Scheduler internals**: `#joltfx-scheduler`
- **Memory subsystem**: `#joltfx-memory`
