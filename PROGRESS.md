# Joltscript development progress

Updated September 29, 2026. This pass expanded Joltscript from a minimal MVP
into a full language implementation with standard library, tools, tests, and
documentation.

## What was done

### Phase 1: Expanded the compiler (core language features)

- Added new opcodes to the VM: GT, LE, GE, EQ, NE, AND, OR, NOT, BITWISE_AND,
  BITWISE_OR, BITWISE_XOR, SHL, SHR
- Updated the compiler to support all new operations
- Updated the bytecode validator to handle new opcodes
- Updated Zoltan's `bytecode.rs` to match (byte-identical output required)
- Added comprehensive tests for all new operations

### Phase 2: Added standard library

Created `joltscript/stdlib/` with six modules:
- `math.jolt` — Trigonometry, linear algebra, interpolation, random
- `graphics.jolt` — Color spaces, transforms, filters, blend modes
- `geometry.jolt` — Points, vectors, matrices, quaternions, splines
- `time.jolt` — Curves, easing functions, keyframe interpolation
- `collections.jolt` — Lists, vectors, maps, sets, sequences
- `strings.jolt` — Manipulation, parsing, formatting, regex

### Phase 3: Added tools

Created `joltscript/tools/` with five tools:
- `joltc` — Compiler driver (compile .jolt to .jbc)
- `jolti` — REPL (interactive evaluation)
- `joltfmt` — Formatter (enforce style guide)
- `joltdoc` — Documentation generator (Markdown/HTML)
- `joltscript-lsp` — LSP server (hover, completion, diagnostics)

### Phase 4: Added tests

Created `joltscript/tests/` with four test suites:
- `unit/test_compiler.c` — Compiler unit tests
- `unit/test_vm.c` — VM unit tests
- `integration/test_integration.c` — End-to-end kernel tests
- `conformance/test_conformance.c` — Cross-target consistency tests
- `benchmarks/test_benchmarks.c` — Performance benchmarks

### Phase 5: Added documentation

Created `joltscript/docs/` with four documents:
- `language.md` — Language reference
- `stdlib.md` — Standard library documentation
- `tools.md` — Tool documentation
- `examples.md` — Usage examples

## Verification

- Clean build: **0 warnings** across every target
- **7/7 Joltscript tests pass** (test_joltscript, test_pipeline, test_compiler,
  test_vm, test_integration, test_conformance, test_benchmarks)
- **27/28 total tests pass** (the one failure is a pre-existing `compose` test
  unrelated to Joltscript — it's a video rendering issue in the color module)
- Zoltan bytecode parity maintained (C and Rust compilers produce identical output)

## Files created/modified

### Modified
- `joltscript/layers/execution/include/joltscript/vm.h` — Added new opcodes
- `joltscript/layers/execution/src/vm.c` — Implemented new opcodes
- `joltscript/layers/execution/src/bytecode.c` — Updated validation
- `joltscript/layers/glue/src/compiler.c` — Expanded compiler
- `joltscript/layers/glue/include/joltscript/compiler.h` — Updated API docs
- `zoltan/src/bytecode.rs` — Matched C implementation
- `joltscript/CMakeLists.txt` — Added tools and tests
- `tests/unit/CMakeLists.txt` — Registered new tests

### Created
- `joltscript/stdlib/math.jolt`
- `joltscript/stdlib/graphics.jolt`
- `joltscript/stdlib/geometry.jolt`
- `joltscript/stdlib/time.jolt`
- `joltscript/stdlib/collections.jolt`
- `joltscript/stdlib/strings.jolt`
- `joltscript/tools/joltc/src/main.c`
- `joltscript/tools/jolti/src/main.c`
- `joltscript/tools/joltfmt/src/main.c`
- `joltscript/tools/joltdoc/src/main.c`
- `joltscript/tools/joltscript-lsp/src/main.c`
- `joltscript/tools/joltc/CMakeLists.txt`
- `joltscript/tools/jolti/CMakeLists.txt`
- `joltscript/tools/joltfmt/CMakeLists.txt`
- `joltscript/tools/joltdoc/CMakeLists.txt`
- `joltscript/tools/joltscript-lsp/CMakeLists.txt`
- `joltscript/tests/unit/test_compiler.c`
- `joltscript/tests/unit/test_vm.c`
- `joltscript/tests/integration/test_integration.c`
- `joltscript/tests/conformance/test_conformance.c`
- `joltscript/tests/benchmarks/test_benchmarks.c`
- `joltscript/docs/language.md`
- `joltscript/docs/stdlib.md`
- `joltscript/docs/tools.md`
- `joltscript/docs/examples.md`

---

# Phase 6 correctness and de-stubbing progress

Updated September 27, 2026. This pass was a correctness and de-stubbing sweep
driven by one reported defect and a review of the whole tree. Everything below
was reproduced from the current source on Linux with a display server present
(`DISPLAY=:0`, X11, AMD Radeon RX 580 / RADV).

## The reported defect: the Desktop frontend exited on launch

`frontends/desktop/src/main.cpp` created the frontend, composed exactly one
ImGui frame, printed a line and returned. `jfx_desktop_frontend_draw` called
`ImGui::Render()` and then threw the draw data away: the target linked only the
four core ImGui sources, with no platform or renderer backend, so there was no
way to present anything even if it had tried. `--headless-smoke` was not a
smoke test of a working program; it was the whole program.

Fixed. The frontend now:

- opens a real OS window through SDL2 with an OpenGL 3.3 core context
  (`frontends/desktop/src/host_window.cpp`), stepping down through 3.2 and 3.0
  compatibility for drivers that refuse a core profile;
- runs a frame loop with an SDL event pump, HiDPI-aware drawable sizing, and
  quit handling from both the window close button and the File menu;
- rasterizes and presents through the vendored Dear ImGui SDL2 and OpenGL3
  backends;
- previews the selected effect by rendering it through the engine's backend and
  uploading the result as a GL texture;
- advances the engine clock while playing, looping at the end of the range;
- binds a live effect combo and parameter slider in the properties panel;
- shows real engine events and Tilly log output in the console, captured through
  a log sink and engine event subscriptions rather than a fixed string;
- persists its panel layout in the platform's per-user config directory instead
  of writing `imgui.ini` into the source tree.

The SDL2 and ImGui backends are optional at configure time. Without SDL2 the
frontend still builds and `--headless-smoke` still exercises the UI path, so a
host with no display toolchain is not blocked.

Three real defects were found and fixed while making the windowed path work:

- the frontend built the ImGui font atlas before the renderer backend existed,
  which trips `ImFontAtlas::Build`'s `RendererHasTextures` assertion on the
  first frame; the backend owns font texture creation, so the manual build is
  now done only on the headless path, where there is no backend;
- the OpenGL3 backend was shut down twice, asserting on the second call;
- `main` read a string owned by the frontend after destroying it. AddressSanitizer
  caught this one (`heap-use-after-free` at `main.cpp:157`).

Verified by screenshot: a 1200x760 window showing a menu bar, a viewport
rendering the selected effect, a timeline with a frame counter, a properties
panel reporting `Device: AMD Radeon RX 580 Series (RADV POLARIS10)` /
`GPU available: yes` / `Last frame on GPU: yes`, and a console with captured
engine events. `desktop_window` is a CTest case that drives 30 real frames and
fails if the loop does not run; it skips (exit 77) where there is no display.

## The engine had no render path

The engine created a backend, stored it, and never called it. `jfx_engine_tick`
published two events, reset the frame arena and drained the scheduler. Every
frontend therefore rendered through its own private CPU path while *printing* a
backend name: `joltfx render --backend vulkan` reported Vulkan while the pixels
came from `jolt_effects_apply`.

Fixed. `jfx_engine_execute_bytecode` and `jfx_engine_execute_source` dispatch
JBC1 through the engine's backend, and the CLI, web, mobile and desktop
frontends all render through the engine. The CLI now reports the device the
frame actually ran on, and `joltfx render` on this host produces the gradient
on the Radeon through Vulkan, not on the CPU.

This required breaking a dependency cycle: `joltscript_glue` linked `jfx_core`
without using a single `jfx_` symbol, which prevented the engine from linking
the compiler it needs to run kernels. The Glue layer now sits below the engine.

## Backend HAL

`backends/common/include/jfx/backend_interface.h` (moved to
`core/include/jfx/`, because the engine owns the contract and the backends
implement it) defines one `jfx_backend_ops_t`. All four backends implement it
and the engine drives it through an erased handle, so engine code never sees a
backend's concrete type.

Four identical copies of the caps struct and four identical copies of the config
struct collapsed into `jfx_backend_caps_t` and `jfx_backend_config_t`; the
per-backend names are typedefs. The CPU-path backends' ~45 near-identical lines
each collapsed into one `jfx_software_adapter` in `backends/common`.

Two capability reports were wrong and are fixed: Vulkan reported
`cpu_fallback = true` even with a device present, and the CPU-path adapters
reported a device name that did not identify which backend it was.

## Public API that did not link

`jfx_buffer_create`, `jfx_buffer_destroy`, `jfx_buffer_write`, `jfx_buffer_read`,
`jfx_texture_create`, `jfx_texture_destroy`, `jfx_kernel_load`,
`jfx_kernel_destroy` and `jfx_kernel_execute` were declared in the public
headers and defined nowhere, so any caller failed to link. They are now real
engine-owned objects, and the `max_buffers`, `max_textures` and `max_kernels`
configuration fields that were read by nothing are enforced. `max_buffers` was
additionally being used as the scheduler's worker-thread count; worker sizing
now has its own `max_worker_threads` field and defaults to the hardware
concurrency.

`jfx_texture` implements RGBA8, f32 and f16 storage with real conversion
(including IEEE binary16 encode/decode), so `JFX_FORMAT_R16G16B16A16_SFLOAT` is
a format rather than a decoration.

## Other correctness fixes

- `tilly_realloc` returned `NULL` with no explanation for arena, pool and stack
  allocators, which reads as out-of-memory. It now logs that an in-place grow is
  impossible and names the strategy.
- The Tilly logger invoked sinks while holding the registry lock, so a sink that
  logged or registered a sink deadlocked. It also indexed its level-name table
  out of bounds for a negative level, and its default sink printed
  `unknown:0 [legacy]` for every call without a source location. All fixed;
  `tilly_log_init` is now idempotent instead of an empty body, and a full sink
  registry reports itself instead of dropping the sink.
- mruby stored a script memory limit and never enforced it: the allocator
  ignored it entirely. It now enforces it with a per-block size header, so the
  accounting is exact and returns to its starting figure after a collection.
- mruby's sandbox was a substring blocklist on script text
  (`"File"`, `"system"`, `"eval"`, backtick), which missed indirect access and
  rejected innocent identifiers such as `evaluate`. Replaced with capability
  removal: the build has no stdio or filesystem, and the process-control methods
  are undefined. A test now asserts that `evaluate` works and `File.open`,
  `Dir.entries` and `system` do not.
- The Lua allocator's budget test could underflow `size_t` and silently disable
  the limit. Rewritten so it cannot.
- `memory_init` failed with an unrelated out-of-memory when a second engine was
  created; it now says the subsystem is already owned.
- The host-application bridges advertised `IMPORT | EXPORT | EFFECT`, none of
  which exists in the shipped SDK-independent stubs. They now advertise
  nothing, and the conformance test pins it.
- `scripts/submodules-init.sh` aborted partway through on a bare `add_submodule`
  call, and its PCRE2 and wgpu-native URLs and paths disagreed with
  `.gitmodules`.
- `scripts/format.sh` walked `third_party/` and would have rewritten 30+
  vendored libraries. It now formats git-tracked files only.
- `scripts/scaffold.sh` is deleted. With `--force` it overwrote the entire
  repository, including the working engine, backend and CLI sources, with 1951
  lines of stale stubs.
- `CompilerWarnings.cmake` selected flags on `CMAKE_C_COMPILER_ID` for a project
  with C and CXX, so a split toolchain left C++ targets warning-free, and it
  passed Clang-only flags to MSVC. Now selected per language and per compiler.
- `jfx_result_to_string` did not cover the codes it could be given; the enum
  gained `JFX_ERROR_NOT_IMPLEMENTED` and the table covers every value.

## De-stubbing

- `frontends/common` was a five-line log function behind an empty include
  directory. It now holds the frontend contract the guides specify: a vtable, a
  capability bitmask derived from that vtable, and dispatchers that turn a
  missing operation into `JFX_ERROR_NOT_IMPLEMENTED`. The CLI implements it
  (`frontends/cli/src/headless_frontend.c`) and drives `render` and the new
  `export` through it, so the CLI and the GUI run the same code.
- `zoltan compile -o` wrote its input back with a comment header. It now emits
  real JBC1 bytecode, byte-identical to the C compiler's.
- The mobile player advanced a clock and produced no pixels. It now has
  `jfx_mobile_player_set_effect` and `jfx_mobile_player_render_rgba8`, so the
  Android and iOS surfaces have a real render to bind.
- The effect catalog moved from khash to a flat array with a linear scan: 12
  immutable entries built once, where the map was indirection without benefit.

## Build and CI

- The `JFX_BACKEND_*`, `JFX_EXT_*`, `JFX_FRONTEND_*`,
  `JFX_PLUGIN_HOST_BRIDGES` and `JFX_DESKTOP_WINDOW` options the subsystem
  guides document now exist. A backend that is switched off is not built and is
  not offered by the engine; `jfx_core` learns the set through
  `JFX_BACKEND_<NAME>` compile definitions, so tests select a backend the build
  actually contains instead of hardcoding one.
- `cmake/toolchains/` is no longer an empty directory: it holds Emscripten and
  Android toolchain files matching the documented workflows.
- Presets: added `sanitizers` and `minimal`, plus `unit` and `conformance` test
  presets. `JOLTFX_BUILD_DOCS` now builds something.
- Install rules ship the engine, the Joltscript layers, the backends and
  `jfx_desktop`; the previous rules omitted the runtime the engine dispatches
  through, so a package could not run a kernel. A `JoltFXTargets.cmake` export
  set is installed alongside.
- Every CTest case carries labels and a timeout.
- CI: job timeouts, a concurrency group, a build matrix that exercises the
  option combinations, Rust caching, a bytecode-parity job, and leak detection
  enabled rather than disabled. The release artifact is tested before it is
  published.

## Two defects found while verifying, both silent

**Every test was being compiled with `NDEBUG` in RelWithDebInfo and Release.**
The tests are plain `assert()` programs, and several of them rely on `assert()`
for the *side effect* of its expression (`assert(jfx_frame_alloc(32, 16))`).
With `NDEBUG` defined, `assert(expr)` expands to `((void)0)`: the call is deleted
outright. `test_containers` and `test_core` therefore segfaulted on an
unoptimized build and, in a Release build that happened to survive, would have
passed while asserting almost nothing. In other words the suite's coverage was
silently zero in exactly the configurations most likely to ship.

Fixed structurally rather than case by case: `tests/unit/TestHelpers.cmake`
defines `jfx_test_target`, which is now the only way a test executable is
created and which always passes `-UNDEBUG` (`/UNDEBUG` on MSVC).
`test_containers` additionally has a `#error` if `NDEBUG` is defined, so a
regression is a *build* failure rather than a silently empty test run.

**The CLI integration script only failed for relative binary paths.** Several of
its checks run the CLI from inside its temporary directory, so passing a
relative path (which is what a developer does by hand) broke exactly one check
and passed under CTest, which substitutes an absolute `$<TARGET_FILE:...>`. The
script now resolves the binary to an absolute path up front and fails loudly if
it is not executable. Its failure message also reported `exit 0` for a failing
check, because `$?` was read after the `if`; the status is now captured
explicitly.

Both are in the class of defect that does not announce itself, which is why the
release configuration is now part of the verification matrix rather than only
Debug and sanitizers.

## Verification on Linux

Configuration: Debug, `-j$(nproc)`, X11 display available, Vulkan driver
present (AMD Radeon RX 580 Series, RADV, instance version reported by the
loader).

- Clean Debug build: **0 warnings** across every target. Before this pass the
  same build emitted 214, all of which were either our own defects or vendored
  sources being held to our warning set.
- `ctest --test-dir build --output-on-failure`: **18/18 pass** (was 16; added
  `desktop_headless`, `desktop_window` and `frontend_contract`).
- ASan + UBSan Debug build (`cmake --preset sanitizers`): **0 warnings**,
  **18/18 pass** with `ASAN_OPTIONS=detect_leaks=1`. Every test binary and
  both shipped executables were additionally run individually under
  `detect_leaks=1` and are leak-free. The earlier phases recorded that
  LeakSanitizer "does not work under ptrace" in this environment; that no
  longer holds, and leak checking is now on in CI.
- Four CMake configurations build and pass: full; Vulkan-only with no mruby;
  no Lua; and a minimal configuration with the desktop frontend, mobile player,
  web session, plugin bridges and examples all off. A configuration with no
  backend at all fails at configure time with an actionable message.
- `cmake --install` and `cpack` both produce a complete package
  (`JoltFX-0.5.0-beta.1.tar.gz` and `.zip`).
- `scripts/check-bytecode-parity.sh`: **13/13 kernels** produce byte-identical
  JBC1 from the Zoltan and C compilers.
- `zoltan`: `cargo fmt --check` clean, `cargo clippy --all-targets -- -D
  warnings` clean, **23/23 tests pass** (was 7).
- CLI integration suite: **45 checks pass** (was 38), covering the new
  `export` and `capabilities` commands and the accuracy of the capability
  report.
- `bash -n` clean on every script in `scripts/`; `submodules-init.sh` and
  `format.sh` run to completion.
- **Release (`RelWithDebInfo`) configuration: 0 warnings, 18/18 pass.** This is
  the configuration that exposed the `NDEBUG` problem; it is verified because
  optimisations changed behaviour, not only Debug.
- Six option combinations build clean and pass, matching the CI matrix: Vulkan
  only with no mruby; no Lua; no desktop frontend; no mobile or web frontend;
  no plugin bridges; and `-DJFX_DESKTOP_WINDOW=OFF` (headless-only desktop
  build, which is the path a host with no SDL2 would take).
- The CLI suite passes with both an absolute and a relative binary path.

## Remaining work and limitations

- **Metal, D3D12 and WebGPU still run the CPU pipeline.** They are honest
  about it (`gpu_available` false, `device_name` names the backend and the
  fallback), and each is one function away from native dispatch: replace
  `ops_execute_bytecode` and make `ops_query_caps` report the real device. The
  proprietary SDKs and wgpu-native are not available in this environment, so
  they were not attempted.
- **The engine is single-instance.** Memory, the scheduler and the event
  registry are process-global, so a second concurrent engine is refused. This
  is now reported clearly rather than surfacing as a confusing -3, but the
  underlying design is unchanged.
- **The desktop frontend has no docking, no undo/redo and no node editor.** The
  panel layout is a fixed default that the user can move; it is not a dockable
  workspace. `--project` records a path and the File > Open Project dialog
  records one, but neither loads a node graph, because there is no project
  format yet.
- **No `.joltpkg` container, packaging or signature verification.** The web
  player accepts a JSON effect envelope, which `frontends/AGENTS.md` marks as a
  security requirement. This is the most significant outstanding correctness
  gap: an untrusted blob still selects a kernel by name.
- **Host application bridges** still need the After Effects, Premiere and
  DaVinci SDKs.
- **The web player has no shipped WASM bridge**, so it has not been run in a
  browser. Emscripten is not available here.
- **QuickJS and MicroPython are absent.** Enabling their options is a configure
  error rather than a silent no-op.
- **The kernel catalog is 12 colour effects** out of the ~300 rows in
  `kernels/JoltFX-Kernels.csv`. The `js_*` intrinsics in `kernels/AGENTS.md` do
  not exist.
- **Vulkan recompiles its compute shader per call** (cached by content hash on
  disk, but not in memory), and there is no async submit. The GPU path is
  correct but not fast; the benchmark numbers in `tests/perf` measure an
  empty frame and should not be read as performance.
- **`cmake --preset default` writes into the source tree** at
  `<sourceDir>/build`. This is the pre-existing preset layout and was left
  alone to avoid surprising anyone with a script that depends on it.
- **Cross-platform builds are unverified.** The macOS and Windows CI lanes are
  YAML-valid but were not executed here.

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

---

# Phase 4 expansion progress

Updated September 27, 2026. Phase 4 is **partially implemented with tested
cross-platform fallbacks**. The missing ImGui, Lua, and mruby submodules were
initialized during this work; mruby's source build additionally needs Ruby's
`rake` package.

## Implemented

- Backend expansion: Metal, D3D12, and WebGPU have public lifecycle,
  capability, source, and JBC1 bytecode APIs. They execute through a shared,
  validated CPU pipeline and report `software-fallback`; Core now accepts
  `metal`, `d3d12`, and `webgpu` in addition to `vulkan` and `auto`.
  `backend_fallback_conformance` verifies identical brightness pixels and
  error behavior for all three adapters.
- Desktop frontend: `jfx_desktop_frontend` owns Core and composes a Dear ImGui
  menu, viewport, timeline, properties, and console frame. The public API
  supports open-project state, resize, play/pause, seek, and rendering a
  headless UI frame; `jfx_desktop --headless-smoke --backend webgpu` exercises
  the executable without a display server.
- Lua and mruby bindings: both use Tilly-backed allocation, explicit GC
  controls, `jfx.clamp`/`JFX.clamp`, numeric function calls, source-load
  errors, and instruction budgets. Lua opens only selected safe libraries.
  mruby builds with `MRB_NO_STDIO` through `default-no-stdio`, enables its
  debug instruction hook, and rejects filesystem/process/eval source forms.
  `ext_conformance` verifies numeric invocation, rejected I/O, and bounded
  infinite loops in both hosts.
- Documentation: new Phase 4 guide plus backend, desktop, and extension
  READMEs document build paths, capability reports, sandbox rules, and the
  remaining native integrations.

## Verification on Linux

- Clean Debug CMake build passes after initializing the ImGui, Lua, and mruby
  submodules. The mruby build is invoked through CMake and produces the
  vendored static library.
- `ctest --test-dir /tmp/joltfx-phase4 --output-on-failure`: 12/12 pass,
  including backend fallback, extension sandbox/budget, and desktop ImGui
  composition tests.
- ASan/UBSan Debug build (`-DJFX_ASAN=ON -DJFX_UBSAN=ON`,
  `ASAN_OPTIONS=detect_leaks=0`): 12/12 pass. As in earlier phases, LSan is
  disabled because it cannot run under this environment's ptrace restriction.

## Remaining Phase 4 work and limitations

- Metal, D3D12, and WebGPU do not yet submit work to native GPUs. They need
  their platform SDK device setup, SPIR-V translation, resource management,
  command submission, synchronization, and hardware capability queries.
- The desktop target creates ImGui draw data but has no native window/input
  loop or renderer backend. A GLFW/SDL or platform-native host plus a GPU
  presentation path remains necessary for an interactive editor.
- Lua/mruby currently expose the intentionally constrained numeric scripting
  API only. Typed marshalling, buffer/texture wrappers, event callbacks,
  diagnostics stack traces, and broader extension conformance remain open.

---

# Phase 5 production progress

Updated September 27, 2026. Phase 5 is **implemented as a portable public-beta
foundation**, with proprietary SDK/platform integrations clearly retained as
release-blocking follow-up work.

## Implemented

- Native plugin host: versioned C ABI (`jfx_plugin.h`), bounded plugin registry,
  dynamic load/unload, descriptor and API-version validation, duplicate-ID
  rejection, optional shutdown callbacks, and XAS plugin lifecycle events. The
  host is exercised with a real dynamically loaded test module.
- Host application bridges: After Effects, Premiere Pro, and DaVinci Resolve
  build as portable SDK-independent libraries with stable identifiers and
  import/export/effect capability contracts. They accurately report that a
  proprietary host SDK is still required for an installable host binary.
- Web player: a strict, dependency-free TypeScript canvas player with WASM
  bridge contract, playback/seek/loop, frame and state events, drag/drop,
  256 MiB package limit, and same-origin URL enforcement. A portable C web
  session owns the Core lifecycle for an Emscripten integration.
- Mobile player: a tested portable Core-owning playback surface for JNI and
  Swift/Objective-C wrappers. Tap toggles playback, swipe scrubs, pinch zooms,
  and render advances the timeline and engine frame.
- Performance: public engine frame timing metrics plus an engine benchmark;
  immutable pipeline-owned JBC1 bytecode is validated at registration and no
  longer revalidated for every pixel during pipeline execution.
- Beta release support: version `0.5.0-beta.1`, package install rules and TGZ/
  ZIP CPack artifacts, Phase 5 guide/changelog, and CI jobs for the web player
  and beta package.

## Verification on Linux

- ASan/UBSan Debug CMake build: pass. `ctest` 15/15 pass, including new plugin
  lifecycle, mobile gesture/state, and three host-bridge contract tests.
  Leak detection remains disabled because LeakSanitizer cannot run under this
  environment's ptrace restriction.
- `npm test` in `frontends/web`: TypeScript compile and Node web-player test
  pass.
- `jfx_engine_benchmark` runs 1,000 WebGPU-fallback engine ticks and reports
  timing metrics. The observed timing is informational, not a performance
  target.
- `cpack --config /tmp/joltfx-phase5/CPackConfig.cmake` generated both
  `JoltFX-0.5.0-beta.1.tar.gz` and `JoltFX-0.5.0-beta.1.zip`.

## Remaining Phase 5 release blockers

- The After Effects, Premiere, and DaVinci adapters require the respective
  proprietary SDK implementations and host-version certification.
- The web runtime requires its Emscripten-generated `JoltWasmBridge`; it has
  not been run in a browser here. Android/iOS JNI/Swift presentation wrappers
  likewise require their native SDK/toolchain validation.
- Metal, D3D12, and WebGPU retain the Phase 4 software fallback rather than
  native GPU dispatch. Public beta distribution/telemetry/support operations
  are not performed by repository code.

### Follow-up implementation started September 27, 2026

Implemented the platform-gated source surfaces that do not require proprietary
SDKs: `jfx_web_session` now renders bundled effects to RGBA8 caller memory and
the TypeScript `EmscriptenJoltBridge` transfers a validated beta effect
envelope through the real C exports. A new `web_session` CTest covers Core
rendering; the TypeScript bridge test covers WASM heap transfer and effect
selection. Android now has a JNI lifecycle/gesture bridge plus a
`Choreographer`-driven `SurfaceView` activity; iOS has an Objective-C bridge
for an `MTKView` controller.

Re-probed this Linux environment on September 27, 2026: Emscripten, Android
SDK/NDK, Xcode, Adobe/Blackmagic SDK headers, the WebGPU C header/library, and
Metal/D3D12 headers are unavailable. Consequently, host-certified plugins,
browser/device builds, and true Metal/D3D12/WebGPU GPU dispatch cannot be
compiled or tested here without those external SDKs. The portable WebGPU,
Metal, and D3D12 fallback conformance remains intact.

Follow-up verification: an ASan/UBSan Debug build passes all 16 CTests,
including `web_session`; `npm test` compiles TypeScript and passes both player
and Emscripten-bridge tests. Leak detection remains disabled under ptrace.

### Colour grading, NLE and node compositing work — started September 27, 2026

Bottom-up through the strata, per the new feature brief. Stage 1 is complete
and tested; the remaining stages are in progress.

**Stage 1 — LUT library (`core/include/jfx/jfx_lut.h`, `core/src/lut.c`) — done.**

A self-contained lookup-table library: no dependencies, so it runs identically
on the desktop, through WASM in a browser, and on a phone. Three shapes (1D
per-channel curve, 2D strip, 3D cube) with correct interpolation for each, an
explicit sampling domain, `mix` for dialling a grade back, and identity/curve
generators.

Readers and writers for every common on-disk format:

| Format          | Shapes read | Notes                                                     |
|-----------------|-------------|-----------------------------------------------------------|
| Adobe `.cube`   | 1D, 3D      | `LUT_1D_SIZE`/`LUT_3D_SIZE`, `DOMAIN_MIN/MAX`, `TITLE`    |
| Autodesk `.3dl` | 1D, 2D      | Headerless, so the shape is a parameter, not guessed      |
| Sony `.spi1d`   | 1D          | Raw LE float32 triples                                    |
| Sony `.spi3d`   | 3D          | Raw LE float32 triples                                    |
| DaVinci `.look` | 1D, 3D      | Container; parses the index and the embedded `.cube`      |
| Hald CLUT       | 3D          | Decodes the image; correct level-geometry (see below)      |

Two correctness points worth recording, because both were wrong in the first
draft and a test caught them:

- **`.3dl` and `.spi*` are headerless, so the file does not say whether it holds
  a curve or a cube.** Sniffing the entry count silently misreads an 8-entry 1D
  LUT as a 2x2x2 cube, and 8 triples genuinely satisfy *both* readings. These
  formats therefore take the shape as a parameter (`jfx_lut_load_3dl_as`,
  `jfx_lut_load_spi_as`); the convenience loaders take it from the file name and
  say so.
- **A Hald CLUT of level L is an L^3-square image carrying a cube of edge L^2**
  (level 2 = 8x8 image, 4-cube; level 8 = 512x512, 64-cube; level 16 =
  4096x4096, 256-cube), written as one continuous buffer with red varying
  fastest. This is the convention specified by the format's author, not the
  tile layout one might guess from the name. The reader derives the level from
  the image side and rejects sides that are not a perfect cube.

Iridas/Cinespace `.csp` is detected and explicitly rejected with a message
naming what is supported, rather than being misread. It is the one common LUT
container still unparsed.

`core/src/image.c` (`jfx_image.h`) adds still-image decoding through the
vendored `stb_image`, which the Hald reader and the NLE clip sources both use.
`tests/unit/color/test_lut.c` is the conformance suite: container contract,
sampling maths per shape, every reader and writer, round-trips, and the
truncation, overflow, domain and shape-rejection paths.

**Stage 2 — Node compositing (`core/include/jfx/jfx_compose.h`, `core/src/compose.c`) — done.**

A DAG of typed nodes with a 26-kind built-in library covering the catalog's
COLOR and COMPOSITING categories: sources (solid, linear gradient, checker,
test pattern, image), grading (exposure, contrast, saturation, vibrance, white
balance, lift/gamma/gain, levels, curves, white clip, channel mixer, LUT,
parametric curve), transform, keying (luma, chroma with spill suppression) and
compositing (14 separable blend modes plus alpha, opacity, posterize, invert).
Evaluation is pull-based with a per-node frame cache, so a node feeding several
consumers is computed once.

The design decision that matters for the brief's "add to all frontends" is that
**the node table is self-describing**: every kind declares its label, category,
input ports (name, type, required, default) and parameters (name, range, default,
step, integral flag). The desktop panel, the web canvas, the CLI and a host
plugin all build their widgets from that one table, so adding a node kind makes
it appear everywhere rather than needing a UI change per frontend.

Three real defects the conformance suite caught, all now fixed:

- **Node removal used swap-with-last while the reference rewriting assumed
  shift-down**, so after removing a node the graph silently miswired. It now
  shifts down, keeping creation order, which is also what a node list displays.
- **The topological sort decremented a consumer's indegree only once even when it
  read the same source from several ports**, so a `blend` with two inputs from
  one node looked like a cycle and refused to render.
- **`ctx_release` walked the full node capacity over arrays allocated for the
  graph's actual node count**, freeing past the end of the allocation. ASan
  caught this as a plain `free(): invalid pointer`.

Two API-consistency problems were also fixed rather than worked around: a failed
`jfx_graph_add_node` and a failed `jfx_lut_create` used to write `0`/`NULL` to
the out-parameter, which orphaned a live object when a caller reused one handle
across attempts. Every out-handle in both modules is now published only on
success.

`tests/unit/color/test_compose.c` covers the kind table's self-consistency,
port typing, cycle rejection (including that a diamond is *not* a cycle),
index stability across removal, the numeric result of every grading operator,
the alpha compositing rule, keying, the node budget, and that every kind renders
both fully wired and with nothing connected at all.

Both new suites pass under ASan+UBSan with leak detection on.

**Stage 3 — NLE model (`core/include/jfx/jfx_timeline.h`, `core/src/timeline.c`) — done.**

Tracks, clips with in/out points, the per-clip effect stack, and keyframes. Time
is counted in frames against an exact rational rate, so 30000/1001 is not rounded
to 29.97 and drift never accumulates; a clip's `in_point` is the source frame
that lands on the clip's own frame zero, which is what makes trim and speed
expressible.

The layer-based effects panel's model lives here: a clip's effect stack is an
ordered list of stages, each with on/off, a blend mode against the stage below,
an opacity, a parameter set, and keyframes. Every operation a panel needs -
add, disable, reorder by drag, set a blend, load a LUT path, key a parameter -
is an API call.

Clip sources are the graph's source nodes, and a clip's effect stack is applied
by assembling a small graph (source, then one node per enabled effect) and
rendering it. So a grading operator behaves identically whether it is reached
through the node panel, a clip's layer stack, or the timeline; there is one
implementation, not three.

Keyframes support linear, hold and smoothstep interpolation, are held sorted
with a binary search because evaluation looks up the bracketing pair on every
frame, hold their value outside the keyed range rather than extrapolating, and
are evaluated in the **clip's own time base** - so a wipe written against a clip
behaves the same wherever the clip is dragged on the sequence.

Four real defects the conformance suite caught:

- **`move_clip`'s downward branch memmoved the wrong range**, so dragging a clip
  earlier overwrote it in place and dropped it from the track, leaking whatever
  it owned. The first version of the test missed this because the two clips it
  reordered had the same name; the test now uses distinct names and checks the
  full permutation.
- **`relocate_clip` went through `remove_clip`**, which freed the clip's path and
  effect strings and then reinserted the copy still pointing at them: a
  use-after-free, and a double free at destroy. The move now shifts the array
  and clears the vacated slot instead of releasing.
- **The track loop iterated top-down while compositing each track onto the
  accumulator**, which put the *bottom* track on top. It now composites bottom-up
  so a higher index is above a lower one.
- **`jfx_blend_rgba8`'s opaque-over-clear fast path ran a 0..1 float-to-byte
  conversion over bytes that were already 0..255**, so any value above 1.0
  saturated to 255 and a 25% grey solid rendered as white.

Also corrected: timecode used `fps_num / fps_den`, which truncates 29.97 to 29
rather than rounding to the nominal 30 the frames actually play at.

`tests/unit/color/test_timeline.c` covers construction and rational rates,
timecode, track/clip/effect structural edits and their index rules, the exact
frames a clip covers, keyframe insertion/removal/interpolation/hold behaviour,
track and clip compositing with mute/solo, that a clip's effect stack changes
the pixels in order, and that a clip's keyframes follow the clip when it is
moved.

All three new suites pass under ASan+UBSan with leak detection on, and the
`default`, `release` and `minimal` presets build with zero warnings and pass
21/21 tests.

**Stage 4 — Project interchange (`core/include/jfx/jfx_project.h`, `core/src/project.c`) — done.**

One line-oriented plain-text format for a graph (a grade) and a sequence (an
edit), in core so the desktop editor, the CLI, the web player, a phone app and a
host plugin all read and write the same documents. Plain text means a project
can be diffed, reviewed, generated by a script, and read by a person debugging a
render - and a render is reproducible from a file alone.

The parser is strict about what it does not understand: an unknown directive, a
bad value or a cycle is an error naming the line, not something silently
rendered. Out-parameters are published only on success.

Sequence round-trips preserve the exact rational frame rate (30000/1001, not
29.97), every track, clip placement and in-point, each effect's kind, text fields
and parameters, its enabled flag and opacity, and every keyframe - and the test
asserts that a reloaded document renders **byte-identical** frames to the original.

Two defects found while writing it: the graph reader accepted `param` and `link`
in a different shape from the one the writer emits and the header documented
(node indices were 0-based on the way in, 1-based on the way out), and the
sequence writer appended an image clip's path and parameters with no bounds
checks, which ASan reported as a heap overflow.

**Stage 5 — CLI surface (`frontends/cli/src/compose_commands.c`) — done.**

- `joltfx nodes [NAME]` — the compositing node library, or one node's ports and
  parameters (and whether it is usable as a clip effect)
- `joltfx lut info FILE` — format, shape, size, domain of any LUT this build reads
- `joltfx lut convert IN OUT [--as cube|3dl|spi]` — convert between LUT formats
- `joltfx lut apply LUT IMAGE OUT --mix M` — grade a still image
- `joltfx render-graph DOC.jfx -o OUT.ppm` — render a grade
- `joltfx render-sequence DOC.jfx -o PREFIX` — render an edit to a PPM sequence
- `joltfx project info DOC.jfx` / `project render` — inspect or render either kind

### What is not done yet

The strata are complete, tested and reachable from the command line, but the
**panels are not written**. The desktop frontend still has its old Properties
panel; it has not been converted to the layer-based effects panel, and the NLE
timeline, colour-grading and node-compositing panels do not exist yet. The web
and mobile frontends have no editor UI. The model each panel would edit is in
place and tested (`jfx_timeline_t`, `jfx_graph_t`, `jfx_lut_t`, and the
self-describing node table the widgets are generated from), so this is UI work
against a finished API rather than more engine work.

## Editor frontend integration (in progress)

- Inspected existing timeline, compositing, LUT and project APIs and frontend implementations.
- Preserving the pre-existing desktop header changes; implementing their declared operations.
- Adding a shared editor session for project loading, frame rendering and timeline/graph access,
  then connecting frontend panels and adapters to it. Existing LUT format claims need correction;
  broad format support must be reported from an actual decoder, not filename acceptance.
- Validation will include rendered pixel changes, project round trips, invalid edits and headless UI.

## CSV kernel implementation — 2026-09-29

- User requires kernel implementations in Joltscript, grouped by CSV category.
- Audited 322 inventory entries across 29 categories: exposure, invert, and
  saturation already exist; 319 entries are missing. Existing sources unchanged.
- Read kernels/AGENTS.md and inspected the compiler, VM, and standard library.
- Runtime prerequisite: the actual compiler accepts one scalar defkernel with
  up to 32 float inputs and one or four float outputs. It has no frame sampling,
  arrays, loops, function calls, imports, strings, or structured output support.
  The richer standard-library sources and documented kernel examples are not
  supported by this compiler. Full CSV implementations need language/runtime
  work before they can compile and execute.
- No new kernels implemented or tests run yet. Resolving whether the user's
  Joltscript requirement permits necessary C compiler/runtime extensions.

### First batch of 20 — implementation and validation

- User authorized C runtime extensions and limited this batch to the first 20
  missing CSV entries. Added 8 transform, 9 color, and 3 blur kernels in their
  category directories, preserving all existing kernel sources.
- Effect algorithms and shared helpers are implemented in Joltscript. Added a
  bounded CPU image-profile interpreter providing scalar expressions, lexical
  functions/bindings, loops, sampling, resource reads, and full-frame passes.
- Added a separate frame-aware catalog/API so callers provide real dimensions,
  resource arrays, named uniforms, and memory/evaluation budgets. Legacy JBC1
  bytecode and the existing 12-effect catalog remain unchanged.
- Added 20 executable .test.jolt regression files and C tests for all defaults,
  transparent inputs, non-default transforms/color operations, curve/LUT data,
  blur impulses and iterations, in-place operation, invalid inputs, and budgets.
  Initial normal-build image tests pass. Sanitizer/regression runs are pending.
- Added compiler-driver image-source checking and documented parameter units,
  enum values, flattened vector/matrix groups, resource layouts, and limits.
  GPU execution and frontend wiring are outside this CPU batch; no GPU parity
  claim is made. 299 CSV entries remain after this batch.

### First batch validation complete

- Confirmed the new source set is exactly the first 20 missing CSV names, with
  required metadata and an executable `.test.jolt` file for each. All original
  `.jolt` implementations are unchanged.
- `cmake --build build --target validate_kernels`: passed, including legacy
  effects and both new image test executables. Gaussian uses two separable
  Joltscript passes per iteration, selected by the generic `pass` binding.
- Normal CTest kernel selection: 23/23 passed (legacy effects, image behavior,
  image runtime, and 20 compiler-driver source checks).
- ASan + UBSan + LeakSanitizer selection: 26/26 passed, including compiler, VM,
  and bytecode conformance regressions. LeakSanitizer initially failed under
  sandbox ptrace supervision; rerunning through the approved unsandboxed CTest
  command passed without disabling leak detection.
- Full normal build succeeded. Full suite: 44/50 passed. The six failures were
  reproduced in a separate HEAD baseline under `/tmp/jolt-kernel-baseline-i_464u89`:
  `test_core`, `compose`, `cli_integration`, `example_hello_world`,
  `desktop_headless`, and `desktop_window`. Existing engine teardown double
  frees, an unconfigured video-node expectation, and missing window-device
  access account for the reported failures; these are not introduced by this
  batch. Logs: `/tmp/jolt-tests.log` and `/tmp/jolt-baseline-tests.log`.
- No warnings in the new kernel/runtime targets. The full build reports two
  pre-existing integer-to-double conversion warnings in
  `joltscript/tests/benchmarks/test_benchmarks.c:18`.
- `git diff --check`: clean. CPU execution and verification are complete for
  this batch. Remaining CSV work: 299 kernels. GPU compilation/dispatch and
  UI integration for the image profile remain explicitly unsupported.

## Kernel batch 11 — 32 more CSV kernels (colour, keying, stylize)

Implemented the last 32 `Frame -> Frame` entries in COLOR_GRADING, KEYING,
COLOR_CALIBRATION and STYLIZE. Registered kernel count is now **189**; CSV
coverage is 192 of 322, leaving 130.

- COLOR_GRADING (10): `grade_tritone`, `grade_gradient_map`, `grade_color_lookup`,
  `grade_hue_vs_hue`, `grade_hue_vs_sat`, `grade_hue_vs_luma`, `grade_sat_vs_sat`,
  `grade_luma_vs_sat`, `grade_rgb_curves`, `grade_luma_curves`. The `*_vs_*` family
  shares a domain/target split (hue, saturation or luminance on the curve's x
  axis; the remapped value on its y) and a common intensity mix.
- KEYING (9): `keying_luma_keyer`, `keying_extract_keyer`, `keying_linear_color`,
  `keying_color_difference`, `keying_screen_matte`, `keying_refine_hard`,
  `keying_refine_soft`, `keying_inner_outer`, `keying_edge_blend`.
- COLOR_CALIBRATION (8): `calib_hdr_maxcll`, `calib_hdr_maxfall`,
  `calib_luminance_match`, `calib_chroma_match`, `calib_monitor_profile`,
  `calib_primaries_adjust`, `calib_1d_lut_calib`, `calib_3d_lut_calib`.
- STYLIZE (5): `stylize_mosaic_stained`, `stylize_pointillism`, `stylize_low_poly`,
  `stylize_relief`, `stylize_metal`.

Real algorithms rather than approximations where the profile allowed it:
`calib_primaries_adjust` builds the linear-RGB-to-XYZ forward matrix from CIE xy
chromaticities, solves the source white point out of it by Cramer's rule, and
applies a Von Kries diagonal gain onto D65. `calib_monitor_profile` converts
through linear light. `keying_linear_color` keys in linear light rather than
gamma-encoded values. `stylize_relief` and `stylize_metal` implement a real
signed-slope Lambert term and a Blinn-Phong lobe with a Fresnel rim.

### Non-obvious profile constraints, now documented in `kernels/README.md`

- **Arity is not uniform.** `+ - *` are variadic; `/`, `min`, `max`, `and`, `or`
  are strictly binary; `sample` takes exactly five arguments. A wrong count fails
  the whole program with `wrong function arity`, which is easy to misread as a
  parser bug. `max3`, `all2..all4` and `any2..any4` exist for this reason.
- **There is no fold.** `sum` is the only reduction, so minima, maxima and
  nearest-cell lookups need workarounds: `alpha-min`/`alpha-max`,
  `worley-soft{,-x,-y}` (exponentially weighted 3x3 average, converging to the
  true nearest cell), and a log-sum-exp soft maximum. Reaching for `sum` where a
  maximum is meant silently returns a sum; `calib_luminance_match` would have done
  exactly that.
- `def` is not a top-level form.

### Harness and registration defects fixed

- `tests/unit/CMakeLists.txt` hand-mirrored the `IMAGE_KERNELS` list and had
  already drifted once. It is now regenerated from `kernels/CMakeLists.txt` and
  asserted identical (189 entries, no duplicates).
- The `gen_noise` list was being derived with each kernel's *test* parameters,
  but the harness's zero-input check uses **defaults**. Recomputing against
  defaults removed `clamp_values` (it had only qualified because its test set
  `min_val=0.5`) and added five keying kernels, for 31 total.

### Verification on Linux

- `cmake --build build --target validate_kernels`: pass.
- `ctest --output-on-failure`: **218/219 pass**. The single failure, `compose`, was
  confirmed to fail identically at pristine HEAD with the working tree stashed
  (`kind 'video' did not render: -5`); it is unrelated to this batch.
- All 189 sources verify individually through the shipping `joltc` driver as
  separate CTest cases, and all 189 render through the behavioural harness with
  their test parameters without a single apply failure.
- 0 passthrough stubs remain across the 189 registered kernels.

## Kernel batch 12 — the last 105 implementable CSV kernels

Registered image-profile kernels went from **189 to 294**; CSV coverage is
**297 of 322**. Every `Frame -> Frame` entry in the inventory is now implemented.
Work was done by five parallel agents (one per family) against a written brief, then
verified centrally rather than trusted: `scripts/audit-kernels.py` checks all 306
sources for compilation, metadata, param count/range, unused params, passthrough
bodies, category-folder agreement, test presence, and registration parity.

- TEMPORAL (18) + `optical_flow`; NLE (17); VIDEO_EFFECTS (15) + 3D (4);
  VIDEO_COMPOSITING (24); TEXT (5) + TEXT_OVERLAY (14) + AUDIO_REACTIVE (2) +
  PARTICLE (1); plus four written directly because they had been dropped from the
  agent briefs (`video_glitch_pixel_sort`, `nle_lower_third`, `obj_render`,
  `track_motion_tracker`).

### Bugs found by verifying instead of trusting

- `calib_white_balance` returned the **red** channel for green and blue, and ignored
  temperature, tint, strength and color_space entirely. It compiled and "passed"
  because a per-channel test cannot see a channel swap.
- `remap_range` was an identity wrapped in `finish`, ignoring all five of its params.
  The stub detector missed it because the body is not literally `(pixel x y c)`; the
  audit now also flags any kernel that uses *none* of its params.
- `lens_blur` and `tilt_shift` divided their sum by `4*r*r` for a `(2r+1)^2` tap count;
  `box_blur` divided by `2*r` for `2r+1` taps. A gradient source cannot detect this, so
  the suite now asserts that a **constant image survives every blur exactly**, which
  is the assertion that catches a wrong divisor.
- `temporal_exposure_blend` gated frame weights on near-equality with the source, so
  the window was discarded whenever the frames differed from it — the normal case —
  leaving the kernel an identity for all real input.
- `track_motion_tracker` compared the source frame against **itself** (`luminance`
  reads `src` for both operands), so every candidate cost was identical and the
  estimate was always zero. It also carried a positive bias because edge probes read
  as 0 for negative displacements; the probe region is now inset by the search radius.
- `obj_render` used `edge t 1` (`v2 - v1`) as the second edge where Möller-Trumbore
  and the face normal need `v2 - v0`; its hit weight was anchored at the camera, so
  every real hit sat ~400 exponent units below zero and underflowed to zero; and
  `inv (/ 1 det)` divided before the `det` guard, which `let` evaluates eagerly.
- Nine dead `/* TODO: fix implementation */` blocks in the behavioural harness are now
  live assertions. Two of them were wrong rather than merely dead: `polar_coords` is a
  gather, so forward-then-inverse is not the identity, and the `bilateral_filter`
  "large range_sigma is identity" claim was simply false.

### Two profiling mistakes worth recording

- I chased a `JOLT_ERR_NUMERIC` from `polar_coords` to a "stale library" and then to a
  suspected build-dependency bug. Both were wrong. The real cause was my own test
  harness leaving the green and blue array elements uninitialised, which tripped the
  interpreter's `isfinite` input check. The build does correctly reconfigure and
  recompile on a `.jolt` edit via `CMAKE_CONFIGURE_DEPENDS`; I had read `tail -1` of
  the build output, which hid the recompile lines.
- I "optimised" `track_motion_tracker` by zeroing samples inside the loop body. That
  changed nothing, because `let` bindings evaluate eagerly: filtering the *result*
  still computes every sample. The stride has to shorten the `sum` ranges.

### What is deliberately not implemented

25 CSV rows remain and are not omissions. This profile's only output is an image, so
17 rows producing `AudioBuffer`, `Mesh`, `ParticleSystem`, `Path`, `Point[]` or
`Vec2[]` cannot be expressed, and 8 more return a scalar or vector that would have to
be published as a uniform image — a change to the output contract that belongs to the
API owner. `scripts/audit-kernels.py` lists both groups by name.

`optical_flow` and `track_motion_tracker` are the FlowField rows that *are*
implemented, publishing the field as a 2-channel image, documented in each file.

### Verification on Linux

- Fresh from-scratch configure and build: **0 warnings** (the two pre-existing
  `test_benchmarks.c` integer-to-double conversions are fixed), `ctest` **323/324**.
  The single failure, `compose`, was confirmed earlier to fail identically at pristine
  HEAD with the working tree stashed.
- All 294 sources verify individually through `joltc` as separate CTest cases, and all
  294 render through the behavioural harness with no apply failure.
- 0 passthrough stubs across 294 registered kernels; `IMAGE_KERNELS` and the
  `tests/unit` verify list are both 294 and generated from the same source, so they
  cannot drift.
