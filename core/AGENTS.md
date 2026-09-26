```markdown
# AGENTS.md — JoltFX Core Engine

## Overview

This guide covers contributions to the JoltFX core engine: the scheduler,
memory subsystem, plugin interface, event bus, and the Glue and Execution
layers. Read the kernel-level AGENTS.md before working on anything in
`kernels/`. This document governs everything underneath that.

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
    cpu/              # CPU execution path
    gpu/              # GPU command encoding and dispatch
    sync/             # cross-path synchronization primitives
  platform/
    vulkan/
    metal/
    d3d12/
  public/
    joltfx.h          # public C API surface
    joltfx_ext.h      # extension/plugin author API
tests/
  engine/             # unit and integration tests for core
  perf/               # microbenchmark harness
docs/
  engine/             # generated API docs

---

## Ownership Rules

| Area               | Gate before merge                        |
|--------------------|------------------------------------------|
| `public/joltfx.h`  | API review from platform lead            |
| `glue/compiler/`   | Two reviewer sign-offs                   |
| `execution/gpu/`   | Platform lead + perf sign-off            |
| `platform/`        | Backend owner for the affected platform  |
| Everything else    | One reviewer sign-off                    |

If you are unsure of the current gate owner, ask in `#joltfx-engine` before
opening a PR.

---

## Build and Toolchain

The engine targets C11 and C++17 (the public API is pure C11). Do not introduce
C++20 features without an explicit decision in the architecture channel.

sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure

Always build with `-DJFX_ASAN=ON` before pushing. The CI gate rejects
submissions that introduce ASAN or UBSan findings.

Supported compilers: Clang 15+, GCC 12+, MSVC 19.34+. Do not use
compiler-specific extensions without an `#ifdef` guard.

---

## Memory Model

The engine owns all allocations. External callers and kernels never call
`malloc`/`free` directly.

| Allocator            | Header                      | Use when                                     |
|----------------------|-----------------------------|----------------------------------------------|
| `jfx_arena_t`        | `core/memory/arena.h`       | Per-frame scratch, freed in bulk at frame end |
| `jfx_pool_t`         | `core/memory/pool.h`        | Fixed-size live objects (nodes, handles)      |
| `jfx_heap_alloc`     | `core/memory/heap.h`        | Long-lived, unpredictable-size allocations    |

Rules:
- GPU kernel bodies must use only the tile-shared buffer API; no `jfx_heap_alloc`
  inside a kernel.
- Arena memory must not outlive the frame that allocated it. Do not cache arena
  pointers across `jfx_frame_begin` / `jfx_frame_end`.
- Pool objects must be returned before the pool is destroyed. Leaked pool slots
  are a hardr threads. Enginr in debug builds.

---

## Threading Model

The scheduler owns all worker threads. Engine-internal code must not spawn
threads. Synchronization primitives live in `execution/sync/`.

c
// crossing the CPU/GPU boundary
jfx_fence_t fence = jfx_gpu_submit(ctx, cmd_list);
jfx_fence_wait(ctx, fence, JFX_TIMEOUT_INFINITE);

- Write to shared state only through the provided lock-free queues or under an
  explicit `jfx_mutex_t`.
- Never hold a mutex across a GPU submission call.
- Scheduler callbacks run on worker threads; do not call back into the scheduler
  from inside a callback.

---

## Public API Conventions

All public symbols use the `jfx_` prefix. Types end in `_t`. Opaque handles end
in `_handle_t`. Return codes are `jfx_result_t`; success is `JFX_OK`.

c
// correct pattern
jfx_result_t jfx_effect_create(
    jfx_context_t*         ctx,
    const jfx_effect_desc_t* desc,
    jfx_effect_handle_t*   out_handle
);

- Every public function validates its pointer arguments and returns
  `JFX_ERR_INVALID_ARG` rather than crashing on null.
- Output parameters come last and are named `out_*`.
- Structs passed by pointer use a `size` field as the first member for ABI
  versioning:

c
typedef struct jfx_effect_desc_t {
    size_t         size;      // set to sizeof(jfx_effect_desc_t)
    const char*    name;
    jfx_category_t category;
    uint32_t       flags;
} jfx_effect_desc_t;

Changing the layout or semantics of any existing public symbol requires an API
review and a version bump in `joltfx.h`.

---

## Glue Layer

The Glue Layer turns `.jolt` source into platform-executable code. The pipeline
is: parse → typecheck → IR emit → platform lower → link.

Key types live in `glue/compiler/jfx_glue.h`. Do not add new IR opcodes without
updating the interpreter in `execution/cpu/interp.c` and the GPU lowering pass
for every active platform backend.

Intrinsic implementations (`glue/intrinsics/`) must be pure functions with no
side effects and no allocations beyond the arena passed to them. If you add an
intrinsic, add it to `intrinsics/registry.c` and write at least one round-trip
test that compiles a `.jolt` fragment using it.

---

## Execution Layer

CPU and GPU paths share a common dispatch table initialized at context creation.
New execution features go through this table rather than direct calls, so both
paths can be exercised by the same test.

GPU command recording happens in `execution/gpu/encoder.c`. Encoder functions
must be re-entrant across threads that hold separate `jfx_cmd_list_t` objects;
shared context state must go through the synchronization primitives, not raw
globals.

Platform backends live under `platform/`. Adding a new backend requires:
1. A new directory under `platform/`.
2. A `jfx_platform_<name>.h` header declaring the backend init function.
3. Registration in `platform/platform_select.c`.
4. A CI lane with the new platform active.

---

## Event Bus

Subscribers register at context creation and are invoked synchronously on the
thread that fires the event. Do not fire events from inside an event handler;
defer through the scheduler queue instead.

c
jfx_event_subscribe(ctx, JFX_EVENT_FRAME_BEGIN, my_handler, userdata);

Event codes are defined in `core/event/jfx_events.h`. Adding a new code
requires updating the documentation table in that header and adding a test that
fires the event and asserts the handler is called.

---

## Plugin Interface

Plugins are loaded through `core/plugin/`. A plugin exposes a single entry
point:

c
jfx_result_t jfx_plugin_init(jfx_plugin_host_t* host, uint32_t api_version);

The host handle provides the subset of the API available to plugins (declared in
`public/joltfx_ext.h`). Plugins must not access engine-internal headers.
Version compatibility: plugins declare their minimum required `api_version`; the
host refuses to load plugins built against a newer API than the running engine.

---

## Testing

Unit tests go in `tests/engine/`. Integration tests that require a full context
use the fixture in `tests/engine/fixture.h`, which spins up a headless context
with the nullre in `tests/engine/fixture.h`, which spins up a headless context
with the null platform backend.

Performance-sensitive paths need a .h`; they are not gated on CI by default but
are run manually before merging changes to the scheduler or memory subsystem.

Minimum test coverage for a new subsystem: one happy-path test, one invalid-arg
test for every public function, and one test for each documented error return.

---

## PR Checklist

- [ ] Builds clean with `-DJFX_ASAN=ON`, zero ASAN and UBSan findings
- [ ] All existing tests pass
- [ ] New public symbols follow the naming and struct-versioning conventions
- [ ] New IR opcodes have CPU interpreter and GPU lowering implementations
- [ ] New intrinsics registered and round-trip tested
- [ ] New events documented in `jfx_events.h`
- [ ] Gate requirement for the changed area met (see Ownership Rules)
- [ ] No `malloc`/`free` calls outside `core/memory/` area met (see Ownership Rules)
- [ ] No `malloc`/`free` calls outside `core/memory/`
- [ ] No new threads spawned outside the scheduler

---

## Common Mistakes

**Arena lifetime violations. for anything that
needs to survive a frame boundary.

**Mutex held across GPU submission.** This deadlocks when the submission blocks
waiting for the GPU and the GPU's completion callback tries to acquire the same
lock. The sync primitives doc in `execution/sync/README.md` lists the safe
acquisition order.

**Adding a public symbol without a size-guarded struct.** The ABI versioning
scheme only works if every public struct has `size` as its first field. Omitting
it causes silent corruption when the engine and a plugin are built against
different header versions.

**Calling back into the scheduler from a scheduler callback.** The scheduler
uses a non-reentrant lock. Defer with `jfx_sched_defer` instead.

**Platform-specific code in `glue/` or `execution/` core files.** The core must
compile and run on every platform. Platform-specific code belongs under
`platform/` and is reached through the dispatch table.

---

## Contacts

- Engine architecture and scheduling: `#joltfx-engine`
- Glue Layer / compiler: `#joltfx-compiler`
- GPU backends: `#joltfx-gpu`
- Public API changes: tag the platform lead directly in the PR
