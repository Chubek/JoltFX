# Phase 1 foundation progress

Updated September 27, 2026. Phase 1 is **partially implemented**. This document replaces the earlier completion claim with results reproduced from the current source tree. The roadmap dates Phase 1 to Q1 2027.

## Implemented and tightened

- TillyZ: platform bootstrap and arena allocation, including caller-owned arena storage. Initialization now rejects undersized or misaligned buffers; shutdown does not unmap caller-owned memory. Arena allocations reject invalid alignment and overflow.
- Tilly: arena, fixed 64-byte pool, general and stack allocators; logger and dynamic module registry. Allocations reject invalid alignment; unsafe realloc copies for non-general allocators have been removed. Runtime shutdown unloads every module even if it was loaded repeatedly.
- Core: engine init/shutdown, frame boundaries, persistent and frame memory, CPU worker scheduler, synchronous event bus, result strings. Engine init checks and unwinds failed subsystem startup. Tick waits for submitted CPU tasks. Scheduler tracks executing tasks as well as queued tasks, drains accepted work at shutdown, and selects the highest priority queued task. Full queues reject submissions. Event dispatch snapshots subscribers and invokes callbacks outside the registry lock; the registry resets between engine lifetimes.
- CMake: library, executable, and test targets build. C++ specific warning flags are now applied to C++ only.

## Verification on Linux

- `cmake -S . -B /tmp/joltfx-phase1 -DCMAKE_BUILD_TYPE=Debug` and `cmake --build /tmp/joltfx-phase1 -j 4`: pass.
- `ctest --test-dir /tmp/joltfx-phase1 --output-on-failure`: 2/2 pass. Tests exercise invalid inputs, engine restart, event unsubscribe inside callback, scheduler task completion, frame arena reset, resource pool usage, and caller-owned TillyZ memory.
- AddressSanitizer and UndefinedBehaviorSanitizer build: 2/2 tests pass with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer cannot run in this environment (`LeakSanitizer does not work under ptrace`); leak freedom has not been verified.

## Remaining Phase 1 work and limitations

- TillyZ still links libc and OS runtime facilities; the zero-libc/~50KB bootstrap claim is not implemented or verified. The bootstrap formatter supports only a subset of printf and does not yet match standard snprintf semantics.
- Core memory, event registry, and scheduler are process-global. Concurrent independent engine instances and concurrent shutdown/API calls are unsupported. The engine has no render/frame-graph execution yet; a tick supplies frame events, arena reset, and a CPU task barrier.
- The pool has fixed 64-byte blocks rather than the previously claimed 4KB blocks. General allocator accounting is incomplete; a comprehensive tracked heap, robust pointer validation, and portable high-alignment allocations remain open.
- Dynamic module loading depends on POSIX `dlopen`; Windows, WASM, and bare-metal builds and runtime behavior have not been verified. A production module lifecycle needs callback reentrancy and concurrency design.
- The build still emits warnings in other, unfinished subsystems. CI, cross-platform builds, full leak checks, and a broader concurrency stress suite remain outstanding.

Phase 2 components listed in `ROADMAP.md` are outside this foundation verification.

---

# Phase 2 core functionality progress

Updated September 27, 2026. Phase 2 is **implemented with documented limitations**.
Results below were reproduced from the current source tree on Linux.

## Implemented

- JoltScript execution layer (`joltscript/layers/execution/`): JBC1 bytecode
  validator, stack VM (`jolt_vm_run` with explicit input/output bindings),
  pipeline DAG runner with cycle detection, and per-pipeline memory budgets.
- Glue layer (`joltscript/layers/glue/`): capability-gated ABI binding
  registry (`jolt.` symbol namespace, freeze-before-share) and MVP S-expression
  compiler (`defkernel` with scalar f32 expressions, `(rgba ...)` output form).
- Essential kernel library: 12 color effects (brightness, contrast, invert,
  grayscale, saturation, sepia, opacity, threshold, posterize, exposure, tint,
  gamma) embedded at configure time and executed through the pipeline
  (`kernels/src/effects.c`, `jolt_effects_apply` with parameter clamping and
  in-place safety).
- Vulkan backend (`backends/vulkan/`): always-built `jfx_backend_vulkan`
  library with a public HAL-style API (`jfx/vk_backend.h`: create/destroy,
  caps query, source and bytecode execution over RGBA float pixels, plus
  `jfx_vk_used_gpu()` path reporting). Without a driver, pixels execute
  through the validated CPU fallback; with a driver they run on the device
  (see follow-up note below). Driver presence is probed at runtime with
  `dlopen("libvulkan.so.1")` so no SDK is required at build or run time.
  The SDK links in only when `find_package(Vulkan)` succeeds.
- Zoltan compiler MVP (`zoltan/`, std-only Rust so it builds offline):
  `zoltan compile FILE [-o OUTPUT] [--emit-metadata]` validates a kernel and
  stages its source; `zoltan verify FILE` only validates. Diagnostics are
  `file:line:column` form; staged output re-verifies (round-trip checked).
  All 12 bundled kernels verify, agreeing with the C compiler.

## Verification on Linux

- `cmake -S . -B /tmp/joltfx-phase2 -DCMAKE_BUILD_TYPE=Debug` and
  `cmake --build /tmp/joltfx-phase2 -j 4`: pass. Remaining warnings are all
  pre-existing (Tilly/TillyZ conversions, klib macro expansions, an empty
  CLI translation unit); none originate from the new backend, test, or
  Zoltan sources. ASan/UBSan build (`-DJFX_ASAN=ON -DJFX_UBSAN=ON`,
  `ASAN_OPTIONS=detect_leaks=0`): 7/7 pass.
- `ctest --test-dir /tmp/joltfx-phase2 --output-on-failure`: 7/7 pass
  (`test_tillyz`, `test_core`, `test_containers`, `test_joltscript`,
  `test_pipeline`, `kernel_color_conformance`, `test_vulkan_backend`).
- `cargo build --offline` and `cargo test --offline` in `zoltan/`: pass,
  7/7 Rust tests; `cargo fmt --check` and `cargo clippy` clean.

## Remaining Phase 2 work and limitations (follow-up status, same day)

- Vulkan device dispatch: **done**. `backends/vulkan/src/vk_compute.c`
  code-generates JBC1 bytecode to GLSL compute shaders (all 15 opcodes),
  compiles with `glslc --target-env=vulkan1.0` (SPIR-V cached on disk by
  FNV-1a hash), and dispatches to the device with host/device barriers and
  fence wait. Verified on AMD RX 580 (RADV): brightness pixels match at
  1e-6 and `jfx_vk_used_gpu` confirms the device path. Numeric failures
  (div-by-zero, overflow) are caught by host finite-checks plus a readback
  finite-check, so the error contract matches the CPU path. The SDK is
  still not required: the ABI subset in `vk_minimal.h` is `dlopen`/`dlsym`
  resolved. Caught during this work: `vkEnumerateInstanceVersion` takes an
  out-pointer (returns `VkResult`); the earlier `uint32_t (*)(void)`
  declaration segfaulted and is fixed. Still open: pipeline caching across
  calls (pipelines compile per call), async submit, other backends.
- Engine-to-backend selection: **done**. `jfx_engine_init` resolves
  `backend_name` (NULL/`"auto"`/`"vulkan"`; anything else is
  `JFX_ERROR_INVALID_ARGUMENT`), owns the backend lifetime, and exposes
  `jfx_engine_backend_name()`. Covered in `test_core`.
- Stale kernels: **done**. `examples/hello_world/effect.jolt` migrated to
  the MVP syntax (identity passthrough, verifies with Zoltan);
  `kernels/transform/scale.jolt` removed — sampling kernels need an image
  execution model beyond the point-wise MVP expression set, so the
  transform category re-lands with sampler support.
- Submodule hygiene: **done**. `.gitmodules` used absolute paths from the
  interrupted `submodule add` run, which broke `git submodule status`;
  rewritten to relative paths, and the missing gitlinks
  (`imgui`, `lua`, `nanosvg`, `stb`) are staged. Status command works;
  unpopulated submodules show `-` (expected offline). Staged, uncommitted.
- Zoltan `clap`/`serde`: **still blocked**. Re-probed 2026-09-27: the
  sparse index answers but crate downloads stall, so the std-only MVP
  stays and `Cargo.toml` keeps no dependencies.

---

# Phase 3 integration progress

Updated September 27, 2026. Phase 3 is **implemented with documented
limitations**. Results below were reproduced from the current source tree
on Linux.

## Phase 2 remainder check (no work left)

- Vulkan device dispatch, engine-to-backend selection, stale-kernel cleanup
  (`kernels/transform/scale.jolt` still absent; `examples/hello_world/effect.jolt`
  still MVP syntax), and submodule hygiene all still hold: `vk_compute.c`
  present, `test_core` covers backend-name selection, `git submodule status`
  runs with unpopulated entries showing `-` (expected offline).
- Zoltan `clap`/`serde` remains network-blocked (`Cargo.toml` std-only);
  nothing actionable offline. No other Phase 2 remainder found.

## Implemented

- CLI frontend (`frontends/cli/`, `frontends/cli/README.md`): `compile FILE
  [-o OUTPUT]` (validates via `jolt_compile`, writes JBC1 bytecode),
  `verify FILE` (validate only), `effects` (lists the 12 bundled kernels),
  `info EFFECT|FILE` (sample render for an effect; kernel/bytecode size for
  a file), `render [--effect NAME] [--param VALUE] [--width W --height H]
  [-o OUTPUT.ppm] [--backend NAME]` (gradient input, validated CPU pixel
  path, engine init + tick, binary PPM output), plus `version`,
  `help [COMMAND]`, `--help`/`-h`, and the legacy `run` entry point.
  Invalid backends, unknown effects, bad dimensions, and missing files exit
  1 with a stderr diagnostic. Links `joltscript_glue`, `jolt_effects`,
  `jfx_backend_vulkan`; builds warning-free.
- Unit and integration tests: existing 7 unit tests untouched; new
  `tests/integration/cli/test_cli.sh` (38 checks: help/version,
  compile/verify incl. missing/broken inputs, all 12 effects listed and
  rendered, info, render error paths, P6 magic, legacy `run`) registered as
  CTest `cli_integration`, plus `example_hello_world` running the example
  against `effect.jolt`. Total 9/9 CTest.
- CI/CD pipeline (`.github/workflows/ci.yml`, validated YAML): `build-test`
  matrix (ubuntu/macos/windows, default preset), `sanitizers` job
  (ASan+UBSan on Ubuntu), `zoltan` job (fmt check, clippy `-D warnings`,
  build, test, verify of all bundled kernels + hello effect), `cli-smoke`
  job (integration suite, hello example, render artifact upload).
- Hello World example (`examples/hello_world/`): `main.c` now initializes
  the engine, compiles `effect.jolt` (or an embedded passthrough fallback),
  runs a 2x2 gradient through a single-stage pipeline, prints per-channel
  in/out values, verifies identity, ticks, and shuts down; new `README.md`
  with build/run instructions and CLI equivalents.

## Verification on Linux

- `cmake -S . -B <dir> -DCMAKE_BUILD_TYPE=Debug` +
  `cmake --build <dir> -j 4`: pass; no new warnings (remaining warnings are
  pre-existing in `kernels/src/effects.c`, Tilly/TillyZ, klib).
- `ctest --test-dir <dir> --output-on-failure`: 9/9 pass
  (`test_tillyz`, `test_core`, `test_containers`, `test_joltscript`,
  `test_pipeline`, `kernel_color_conformance`, `test_vulkan_backend`,
  `cli_integration`, `example_hello_world`).
- ASan/UBSan build (`-DJFX_ASAN=ON -DJFX_UBSAN=ON`,
  `ASAN_OPTIONS=detect_leaks=0`): 9/9 pass. LSan still disabled here
  (`ptrace` restriction, as in Phase 1).
- `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`,
  `cargo test --offline` in `zoltan/`: all clean, 7/7 Rust tests.

## Remaining Phase 3 work and limitations

- `render` writes binary PPM (P6) only: no FFmpeg/MP4/MOV/GIF export, no
  resolution/FPS/codec flags from the frontend spec — video export is a
  Phase 4+ item pending codec wiring.
- No `.joltpkg` packaging, signing, or `export` command; `compile -o`
  writes raw JBC1 bytecode, not a staged package.
- CI is config-only verification here: the macOS/Windows lanes and the
  artifact upload were YAML-validated, not executed; cross-platform and
  WASM builds remain unverified locally.
- Phase 2 carry-overs unchanged: per-call Vulkan pipeline compilation
  (no cross-call cache), async submit, and non-Vulkan backends.
