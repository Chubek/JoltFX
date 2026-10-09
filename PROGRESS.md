# Progress

## 3D tab visibility and primitive additions (2026-10-09)

- Reproduced the blank windowed viewport: the desktop render entry point rejected
  scene rasters larger than the legacy 320x180 effect preview. Document rendering
  now uses the shared renderer's allocation and dimension limits.
- Added tubes, capped hemispheres, wedges, tetrahedra, octahedra and icosahedra
  through shared commands and desktop/web/mobile controls. Desktop creation,
  duplication and import select the new object. Updated API versions and docs.
- Native 3D checks pass 6/6; all five ImGui UI suites, desktop frontend and real
  window smoke pass. Web tests pass 13/13. Clang ASan/UBSan checks pass all six
  selected cases, including the CLI retry after correcting its test syntax.
- Regressions cover display-size/high-DPI/portrait rendering, failed output
  preservation, menu creation/selection/history, narrow layout, welded manifold
  geometry, outward winding/volume, segment limits and cross-client persistence.
- Visually verified the real shaded workspace and expanded menu. Evidence lives
  in `/tmp/opencode/jfx-3d-tab-*` logs and `{window,menu}.png` screenshots.

## Shared CPU performance work (2026-10-09)

- Measuring 3D rendering and camera navigation before optimization. Adding a
  private xsimd-backed C bridge for shared image/audio loops, scalar fallbacks,
  reference/tail tests and a repeatable benchmark. Preserving prior 3D work.
- Implemented xsimd exposure, constant-gain audio accumulation, recording finite
  validation and optimized-build 3D raster batches. Byte widening stays in the
  faster compiler-vectorizable loop. Added `JFX_CPU_SIMD=OFF` and scalar tails.
- Cached immutable mesh normals with geometry/shading invalidation; camera-only
  gestures avoid mesh copies and full-scene validation. Debug eight-sphere preview
  improved from 187.35 to 168.35 ms/frame; camera updates from 1.053 to 0.002 ms.
- Optimized renderer comparison: 23.13 ms scalar vs 15.69 ms xsimd (1.47x).
  Exposure: 5.97 ms scalar vs 3.17 ms xsimd (1.88x). Both optimized renderer
  variants passed 3D tool tests. See `docs/performance.md` for scope/methodology.
- Targeted ASan/UBSan checks pass 5/5, including scalar/SIMD numeric reference
  tests and cache invalidation. Allocation-policy and whitespace checks pass.
  Full native build and suite pass **373/373**; logs are
  `/tmp/opencode/jfx-simd-{full-build,native-tests,asan-tests}.log`.
  Changes remain uncommitted. Removed the now-unused scalar exposure helper.

## 3D workspace expansion (2026-10-09)

- Implemented smooth curved primitives, four-sample antialiasing and near-plane
  clipping, cylinders/cones/tori/capsules/pyramids/disks, GLM quaternion viewport
  navigation and desktop gimbal rings. Desktop previews follow viewport size;
  camera gestures commit as one undo step and support cancellation.
- Added editable rational bicubic NURBS patches, welded marching-tetrahedra
  metaballs, procedural linear/radial/grid cloners and make-real/editable tools.
- Added per-channel embedded Joltscript source/compiled JBC1 drivers with
  time/frame/index/keyed-value inputs, file loading and runnable examples.
  Source, generator controls, layouts and quaternion orientation persist across
  save/load/history and are shared by preview/export and all editor clients.
- Added desktop/web/mobile controls and mouse/touch navigation; expanded native,
  frontend, real ImGui, CLI and mounted web tests. Added real-WASM cases for a
  future rebuilt module; Emscripten and Android/iOS toolchains remain unavailable.
- Sanitizer verification found an Eigen temporary-expression lifetime issue in
  clone placement. Replaced the calculation with GLM quaternion/vector math;
  engine/CLI/frontend checks pass after the fix.
- Final targeted Clang ASan/UBSan checks pass **6/6**, with leak detection and
  halt-on-error enabled, including real ImGui navigation/one-step undo. Web tests
  pass **13/13**; all three example animation kernels compile. Allocation-policy
  and whitespace checks pass. Visually checked a sphere/torus PNG for smooth
  shading and antialiased edges, plus edited NURBS/metaball output.
- Full native build completed. Native suite: **370/371** passed on the first run;
  `ext_conformance_resources` could not start because its executable was still
  being linked (`text file is busy`). After the build completed, the sole failed
  check passed on retry, so all **371** checks have passed. No test assertion or
  sanitizer failure remains. Changes are uncommitted; nothing was pushed.
- Evidence: `/tmp/opencode/jfx-3d-tools-sanitizer-tests.log`,
  `jfx-3d-tools-web-tests.log`, `jfx-3d-tools-native-tests.log`,
  `jfx-3d-tools-native-retry.log`, and `jfx-3d-tools-preview/{smooth,surfaces}.png`.

## Shared 3D modeling and animation workspace (2026-10-09)

- Added an editor-owned 3D scene document, embedded mesh/key/camera persistence,
  failure-atomic commands, independent bounded history and CPU shaded rendering.
- Integrated CGAL triangle validation, libigl normals/subdivision, VTK principal
  axes, Bullet rigid-body baking, tinyply mesh interchange and stb PNG export.
- Added desktop 1.6 tab/outliner/inspector/transport, web and Android sections,
  an iOS controller, CLI/terminal commands and portable host scene APIs. Shared
  frame/video exports dispatch scenes using their own FPS/duration.
- Added engine, real ImGui, cross-frontend and CLI round-trip checks, mounted web
  control checks and a real-WASM test. All 3D checks pass natively and under
  Clang ASan/UBSan, including encoded export and failure-preservation coverage.
- Pinned Eigen 3.4.1 headers to satisfy vendored libigl, with an offline override.
  Limited VTK to CommonCore; reduced external template debug data after the full
  native build exhausted disk from large repeated static test links.
- Hardened PNG/PLY exports with exclusive temporary ownership and Windows
  destination replacement; rejected nonmanifold subdivision edges before libigl.
  Added failed-load/output preservation, interpolation, malformed/truncated PLY,
  nonmanifold and physics-floor regressions. Engine, real ImGui, CLI and web tests
  pass. Corrected the cross-frontend fixture to respect single-runtime ownership.
- Full-build verification encountered native linker segmentation faults and a
  GCC 16 internal compiler error in VTK's sanitizer build. Completed native builds
  using GNU gold, sanitizer builds using Clang/LLD. iOS scene loading now checks
  document kind before replacement.
- Resolved VTK installation dependencies by building its required CommonDataModel
  export and disabling wrapper/remote tools. Added explicit private Boost header
  discovery and installed modeling documentation and dependency license notices.
  Switched sanitizer verification to Clang/LLD and native linking to GNU gold after
  repeated GCC/BFD toolchain failures. Added shared CLI project-info/render dispatch
  and mobile name/visibility/material/clock controls; extended encoded-export tests.
- Clang ASan/UBSan validation passed all five selected checks: modeling3d engine,
  real ImGui workspace, desktop/web/mobile/host conformance, CLI and desktop
  headless smoke, with leak detection and halt-on-UB enabled. LLD was unpacked
  into `/tmp/opencode` from the matching Arch package after verifying its SHA-256;
  no system compiler/linker installation was changed. Web tests pass 13/13.
- Installation and relocated `find_package(JoltFX)` C/C++ consumer checks pass,
  including rendering, PNG output, subdivision and principal-axis alignment.
  Fixed VTK subproject export filenames by setting its package/destination explicitly.
- Full native suite passes **370/370**, including the existing plugin and workspace
  tests. Preserved sequence/graph mode switching inside plugin transactions while
  blocking scene transitions; extended tab-name assertions for the appended workspace.
- Fixed allocation-policy scanning of directories with source-file suffixes by
  checking `is_file()` before reading. The policy check now passes, and the separate
  changed-production-file audit passes for 27 files. Android/iOS platform builds
  and real Emscripten execution have not been run; their toolchains are absent.
- Final evidence: `/tmp/opencode/jfx-3d-native-complete-tests.log` (370/370),
  `jfx-3d-sanitizer-final-tests.log` (5/5, leak detection and halt-on-UB),
  `jfx-3d-web-tests.log` (13/13), and `jfx-3d-package-final-{install,configure,build}.log`.
  Relocated consumer executes successfully; `git diff --check` passes. Changes are
  uncommitted; pre-existing extension/joltbundle work is retained.

## Application-wide MemTKX memory migration (2026-10-08)

- Audited allocation entry points and the vendored MemTKX contract. Most engine,
  Glue, Execution, backend and extension storage already routed through Tilly;
  CLI/tools and C++ animation/drawing containers bypassed it before this migration.
- Replaced Tilly allocation with a private C++20 MemTKX adapter behind the
  existing C struct/function ABI. Stable-address free lists serve heaps/pools;
  bump regions serve arenas/stacks. Added exact out-of-band ownership, checked
  sizes/alignment, live-payload budgets, failure-atomic aligned realloc, locking,
  idle-page reclamation and weak-token invalidation of destroyed TLS bindings.
- Added C11 process helpers and C++17 STL/RAII adapters. Migrated context/CLI/tool
  buffers, animation scenes/bytecode, desktop drawing/history, stb decoding and
  browser transfer buffers. Hardened tool/animation input boundaries, failed
  image-load output ownership, drawing OOM commits and post-disposal web calls.
- Staged checked fixes to the ignored vendor headers through `cmake/MemTKX.cmake`:
  failure-atomic reservation, safe extents, sorted cell splitting/adjacent merging
  and allocation-free release. The original checkout is preserved. The low-memory
  regression caught normal vendor includes taking precedence over staged system
  includes; fixed the interface target to reference only reviewed headers.
- Full-suite testing exposed decoded/rendered image ownership mismatches at stb
  release; configured all stb allocation macros to Tilly. Added native decode/
  release coverage and a real CLI LUT round trip, also fixing dimensions read
  after image release. No test failures were suppressed.
- Added ASan guards/free/reset poisoning and `TILLY_CHECK_LEAKS` process-object
  census. Regressions cover ownership/budgets/over-alignment, metadata OOM,
  freeing with allocations disabled, randomized fragmentation, concurrent use,
  TLS lifetime, UI/history failures, malformed inputs and native allocation policy.
  Negative subprocess checks prove overrun/underrun/use-after-free/use-after-reset
  and leak diagnostics actually fire.
- Final native Debug suite passes **366/366**; full ASan+UBSan all-five-runtime
  suite passes **369/369**, with leak/strict-string/stack-use checks enabled.
  Node/TypeScript bridge/UI checks pass **12/12**. One earlier GCC ASan run failed
  during pre-main sanitizer global registration; subsequent complete runs passed
  with identical options, including the final run after all allocator/CLI edits.
- Targeted ThreadSanitizer heap/realloc concurrency and TLS-destruction handoff
  checks pass. GCC builds and the Clang adapter/headers syntax check have no
  diagnostics. Installed/relocated C11 and strict C++17 consumers link and run
  with object leak checks; exports contain neither MemTKX paths nor C++20 features.
- Release mixed-size, 64-byte-aligned 256-slot fragmentation benchmark measures
  **204 ns** per alloc/write/free pair (libc baseline **28 ns**, without ownership/
  guards/over-alignment). Sorted split/merge reduced the first MemTKX measurement
  from **2.03 microseconds**. Native engine tick benchmark: **96 ns** average over
  1,000 ticks. These are local microbenchmarks, not end-to-end rendering claims.
- Updated `docs/memory.md`, README, changelog and subsystem contracts, including
  allocator API **2.0**: default pointers require Tilly release, general capacity
  bounds live payload, scratch capacity includes guards/padding. Logs and package
  fixtures: `/tmp/opencode/jfx-memory-*`. Linux native/headless UI verified;
  Emscripten and `xvfb-run` are absent, so actual browser WASM/window lanes and
  Windows/macOS/mobile builds were not run. MemTKX does not itself establish
  complete raw-pointer safety; documented ownership plus the verified checks
  define the evidence and coverage. `git diff --check` and allocation policy pass.

## Desktop vector animation workspace (2026-10-07)

- Confirmed the animation tab only exposes skeleton statistics and bytecode;
  it has no canvas or drawing tools.
- Implemented a full-width 960 x 540 vector stage with brush, line, rectangle,
  ellipse, selection/move, whole-shape eraser, hand/pan, zoom, stroke/fill styles
  and applying styles to existing shapes. The animation tab no longer shows an
  unrelated NLE preview or routes undo to the NLE history.
- Added eight-layer authoring with visibility/locking, a 120-frame exposure
  sheet, held cels, duplicate/blank keys (F6/F7), onion skins and independent
  looping playback. Drawing and move gestures commit once or cancel on Escape;
  tab switches preserve artwork and cancel gestures/playback.
- Added 32-step drawing history, validated/undoable `.jfxdraw` loading, saving
  through a temporary file, and visible-current-frame SVG export. Animation's
  File menu routes to drawing persistence; skeleton controls remain in a popup.
- Documented separate drawing files and current limits (no Bezier/node editing,
  tweening, pressure sensitivity, audio sync or animated video export).
- Validation: desktop builds without warnings; all eight targeted animation,
  desktop, composition, DAW, workspace UI and headless checks pass. Five focused
  ASan/UBSan checks also pass with leak detection outside the sandbox (LSan
  cannot run under sandbox process tracing). No public ABI changes.

## DAW instruments, native editors, capture and plugin state (2026-10-06)

- Added MIDI note clips/piano-roll display and VST3 instruments, sample-offset
  note events, seek note chasing and latency-aligned audio/instrument summing.
- Native editor containers support HWND/NSView/X11 parents, lifecycle, resize,
  focus/key/wheel events and Linux timers/FDs. Deferred native gestures/dirty
  notifications persist bounded JVS1 component/controller state in shared history.
- Added SDL input selection/capture, sample-clocked take recording, incremental
  atomic float WAV completion/cancel and one-step clip insertion. Added gain/
  plugin automation lanes with persistent keys/interpolation and offset queues.
- Model snapshots own notes/state/curves; project parsing never loads native code.
  APIs/docs updated and regression coverage includes instrument/automated WAV
  export/decoding, chunked binary state, rack ownership, native SDL view/input and
  real ImGui note/state/automation interactions.
- Final native build and **360/360** tests pass. Focused ASAN+UBSan checks pass
  **6/6** with leak/strict-string/stack-use checks enabled, including the native
  SDL/X11 view and dummy-input capture. VST3/FFmpeg/window-disabled build and
  model/mixer/recording/workspace/headless checks pass **5/5**. All three builds
  have no diagnostics; package install and `git diff --check` pass. Logs:
  `/tmp/opencode/joltfx-daw-features-*`. Verified Linux/X11; macOS/Windows native
  parent paths are implemented but were not built here. Recording is stereo take
  capture with sequence playback stopped; MIDI input devices/CC, sidechains and
  multichannel routing remain outside this feature set. Completed 2026-10-07.

## DAW workspace and VST3 audio inserts (2026-10-06)

- Added a DAW workspace with Arrangement, Mixer, VST3 Inserts and Mixdown views,
  frame/beat-snapped audio editing, tempo/4/4 transport, track/clip/master gain,
  mute/solo, balance/fades and stereo meters. Existing Audio Mixing includes the
  same track insert controls. Appended panel/workspace IDs; desktop API 1.3.
- Added SHA-256-pinned MIT Steinberg VST3 interfaces and native bundle/module
  discovery. Stereo float32 hosting supports combined/separate controllers,
  state synchronization, messages/attributes, parameter queues, reference-counted
  module lifetime and generic parameter editing. All host buffers/instances use
  Tilly allocation; the VST3 process call creates no host threads or allocations.
- Racks persist eight ordered inserts and 64 double-precision overrides each.
  Editor 1.6 commands group parameter gestures in history; project parsing keeps
  descriptors without loading native code. Mixer snapshots run inserts before
  track/master faders for playback and export and compensate up to two seconds
  of rack latency. Discontinuities reset plugin instances/tails; unsupported
  layouts, unavailable enabled plugins and dynamic latency fail explicitly.
- Export 1.1 adds incremental/cancellable stereo float WAV mixdown, independent
  of FFmpeg, with snapshot isolation and atomic completion. Updated build/host
  contracts and `docs/daw.md`; installed public headers, guide and SDK license.
- Full native build and **358/358** tests pass. Real VST3 module regressions cover
  controllers/messages/lifetime/parameters, delay alignment, snapshots, project
  precision/history and WAV completion/cancel/failure. Real ImGui tests cover
  DAW selection, browsing/insertion, parameter gestures and bypass. Focused
  ASAN+UBSan checks pass **4/4** with leak/strict-string/stack-use checks enabled;
  VST3/FFmpeg-disabled model/mixer/WAV round-trip checks pass **2/2**. Package
  installation succeeds; final native and sanitizer builds have no diagnostics.
  Linux x86-64 verified; native plugin editors, MIDI/instruments, recording,
   automation lanes and opaque preset/sample-state persistence were outside this
   initial delivery; they are covered by the follow-up section above.
  Logs are `/tmp/opencode/joltfx-daw-*`.

## Full-build MicroPython compatibility fix (2026-10-06)

- Reproduced `cmake --build build`: the enabled MicroPython adapter calls the
  removed `mp_obj_int_to_bytes_impl` API. The vendored checkout now provides
  `mp_obj_int_to_bytes` with explicit signedness and overflow behavior.
- CMake now selects the conversion API declared in `py/objint.h` and reconfigures
  when that header changes. The current API writes signed little-endian bytes
  without throwing on overflow; exact round-trip comparison rejects values
  outside int64 as TYPE_ERROR. The older API remains supported.
- Added INT64_MAX/large-negative round trips and Python overflow regressions on
  both boundaries and values beyond 64 bits, including output preservation and
  recovery. Updated the MicroPython build guide and extension contract.
- The exact `cmake --build build` command succeeds with no compiler diagnostics.
  All six focused native extension/CMake checks pass with all five runtimes
  enabled. ASAN+UBSan bridge/resource/CLI checks pass **3/3** with leak detection,
  strict strings and stack-use-after-return enabled. `git diff --check` passes.
  Logs: `/tmp/opencode/joltfx-build-failure.log`, `joltfx-build-fix.log` and
  `joltfx-micropython-sanitizer-build.log` in the same directory.

## WAMR extension runtime replacement (2026-10-06)

- Replaced the external Wasmtime SDK with the vendored
  `third_party/wasm-micro-runtime` interpreter. `JFX_EXT_WASM=ON` builds
  `jfx_wamr_runtime`; optional `JFX_WAMR_ROOT` selects another source checkout.
  Upstream build settings are scoped to a dedicated subdirectory. The runtime
  archive and license ship with the installed CMake package.
- Ported scalar exports and typed `joltwasm` ABI 1, instruction metering and
  Tilly allocation callbacks (including linear-memory allocation usage). Shared
  bootstrap state is reference-counted; failed loads free all module state.
  Imports are explicitly restricted to `joltfx.call` and `joltfx.clamp`.
- WAMR loads binary `.wasm`; added the compiled grading example and binary
  fixtures/embedding example. WAT authoring uses external `wat2wasm`. Reject
  start sections and automatic constructors to keep guest execution metered.
  Updated CI, build guides, CLI help and subsystem contracts; removed the
  Wasmtime find module, package dependency and SDK-discovery regression.
- Warning-free full Debug build with ASAN+UBSan. Focused checks pass **5/5**:
  extension bridge/resources/WASM/CLI conformance and WAMR CMake validation,
  with leak detection, strict strings and stack-use-after-return enabled.
  WAMR uses its upstream UBSan alignment exception for four-byte VM stack
  records; the adapter retains full sanitizer instrumentation.
- WASM regressions cover forbidden imports, invalid/text modules, start/constructor
  rejection, failed-load memory recovery, large initial memory, growth limits,
  int64/typed precision, budgets/recovery and overlapping runtime lifetimes.
- Installed and relocated `/tmp/opencode/joltfx-wamr-relocated`; the standalone
  embedding consumer links and runs Lua/WAMR without an external WASM SDK.
  Installed CLI example/pixel/budget checks pass. Sanitized WAMR profile:
  0.052 ms initialization, 13,480 initial charged bytes, 0.838 µs cached calls.
  Linux x86-64 verified; other host platforms were not built here.
- Reconfigured the existing `build/` and built `jfx_wasm` successfully against
  WAMR. A separate all-extensions-disabled configuration also succeeds.
  `git diff --check` passes.

## Default mruby dependency selection (2026-10-06)

- Reproduced the configure failure with the vendored mruby checkout: its removed
  custom allocator API is incompatible with the sandbox memory budget.
- Added automatic selection of SHA-256-pinned mruby 3.3.0 when the default
  vendored source is missing or incompatible, including existing caches.
  Explicit external roots retain compatibility validation. Added seven offline
  configure cases for default, fallback and explicit-root selection.
- Default configuration and the downloaded runtime/adapter build succeed with
  ASAN/UBSan. Reconfiguration also succeeds with FetchContent fully disconnected,
  reusing the downloaded source. All six focused tests pass: source selection,
  build isolation and four Lua/mruby extension conformance tests (including CLI),
  with leak detection enabled. The existing build-isolation fixture now compares
  canonical paths so workspace aliases do not cause a false failure.
- Updated the mruby README and extension build contract. The Vulkan SDK status
  line is informational and does not block configuration. `git diff --check`
  passes.

## Core engine directory renamed to `src` (2026-10-05)

- Renamed the top-level `mograph/` directory to `src/` with `git mv`, so history is
  preserved for all 44 tracked files (20 translation units, 2 internal headers,
  21 public headers, `CMakeLists.txt` and `AGENTS.md`). The engine now lives at
  `src/src/` and `src/include/jfx/`.
- Updated every tracked reference: `add_subdirectory(src)` and the `jfx_api`
  include path in the root `CMakeLists.txt`, the `jfx_plugin_sdk` interface path
  in `sdk/CMakeLists.txt`, the `test_plugin_module` include path in
  `tests/unit/CMakeLists.txt`, the `jfx` header install rule in
  `cmake/packaging/install_rules.cmake`, and the prose references in `MEMORY.md`,
  `PROGRESS.md` and `frontends/plugins/ofx/README.md`.
- Relative source paths inside `src/CMakeLists.txt` and the private
  `CMAKE_CURRENT_BINARY_DIR` include still resolve, so no build-file logic changed.
- `kernels/common/image.jolt` matched only inside the word "homography" and was
  deliberately left untouched.
- Verified with a fresh out-of-tree configure and generate; the generated
  `jfx_core` and `test_plugin_module` flags resolve to `src/include`, and the
  install rule to `src/include/jfx`. All 20 engine translation units and
  `test_plugin_module` compile with zero diagnostics under the project warning
  set. A full link/test run is still blocked in this checkout by two pre-existing
  missing third-party dependencies (`third_party/miniaudio`, an mruby 3.3.0
  checkout), unrelated to this rename; pre-existing `build*/` and
  `frontends/mobile/android/app/.cxx/` trees still carry stale `mograph/` paths
  and must be reconfigured or removed.

## Audio mixing workspace (2026-10-05)

- Adding a desktop Audio Mixing tab over the existing Core audio mixer and
  editor commands: track gain/mute/solo, clip gain/pan/fades, audio import and
  playback output metering. No additional third-party library is required.
- Extending real ImGui workspace tests before implementation; preserving enum
  values and configuration layout with appended identifiers and a minor bump.

## Runtime ownership audit (2026-10-05)

- Continuing with module reference counting, context ownership and lifecycle
  callbacks, plus allocator edge cases. Adding regressions before fixes.
- Confirmed duplicate initialization for repeated module loads and a deadlock
  when initialization queries the registry. Added private per-module ownership
  and path metadata, native-handle alias reuse, reference-count overflow guards,
  registry membership validation on unload, and separate recursive lifecycle
  serialization so dependency callbacks can query/load without registry locks.
- Expanded native fixtures to cover two contexts, logging survival, aliases,
  cyclic and nested dependency loads, failed initialization, removed/foreign
  unloads and teardown with outstanding references. The dependency fixture
  exposed premature dependency destruction; completed entries now preserve
  initialization order and shutdown visits dependents first.
- Confirmed loss of log sinks after shutting down a second context and a stale
  calling-thread allocator pointer after destruction. Runtime logging now lasts
  through the last context; allocator destruction clears the local binding.
  Custom general allocator storage is no longer passed to libc realloc.
- Focused ASAN/UBSan + leak-detection regressions pass **3/3**. The full native
  Debug rebuild is warning-free. Updated runtime contracts and comments.
- The first full sanitizer run passed 353/354 tests, with one ASan
  `unknown-crash` in `kernel_image_batch` while accessing a live, unpoisoned
  evaluator stack field. The isolated rerun passed with identical leak,
  stack-use-after-return and strict-string settings (59.43 seconds). No
  source-level cause is established. The full-suite rerun passed **354/354**
  tests with identical settings in 62.82 seconds. Keep the initial report as an
  unresolved intermittent observation; no evaluator code was changed. Logs
  are retained in ignored `build-audit/runtime-first-tests.log`,
  `build-audit/runtime-final-tests.log` and `build-audit/image-retest.log`.
- `git diff --check` passes. Verification covers this native Linux build with
  Lua/mruby enabled; optional QuickJS, MicroPython, Wasmtime and other platforms
  were not exercised.
- Earlier `/tmp` dependency checkouts disappeared; moved test dependencies to
  ignored persistent `build-audit-deps/miniaudio` and `build-audit-deps/mruby`
  and reconfigured `build-audit` to those absolute roots. User third-party
  deletions and prior changes remain intact.

## Bug audit (2026-10-05)

- Started a fresh ASAN/UBSan build because existing build caches refer to a
  different checkout. Configuration exposed a missing mandatory miniaudio
  checkout; obtained a separate test dependency in `/tmp`.
- Inspecting allocator accounting, synchronization and pool reuse; adding
  regression tests before implementation changes.
- Confirmed three independent failures against the original allocator: heap
  realloc accounting, duplicate pool frees, and arena/stack peak reporting.
  Fixed live-byte tracking with aligned heap headers, locked heap operations
  and usage snapshots, pool slot bookkeeping, and persistent peak counters.
- Added configurable `JFX_MINIAUDIO_ROOT` and early source/header validation
  with actionable setup guidance. Verified successful external-root configuration
  and the failure message for a missing root. Updated media setup instructions
  to use standalone clones after the existing submodule removals.
- Standalone allocator regressions pass normally and with ASAN/UBSan; the
  sanitizer run required execution outside the sandbox for LeakSanitizer.
- Full sanitizer build resumed after working around a host library mapping
  stall with a private library copy in `/tmp`; mruby fetched its missing Prism
  dependency. No tracked third-party deletions were restored.
- Found and fixed mruby's missing generated-header include path. The current
  vendor checkout is mruby 4.0, which removes the adapter's required custom
  allocator API; added an early compatibility diagnostic and `JFX_MRUBY_ROOT`
  for supported external checkouts, and tested with mruby 3.3.0 in `/tmp`.
  Version 4.0 remains unsupported so memory budgets are preserved.
- Kept mruby's Rake configuration/lockfile in its build directory and isolated
  runtime artifacts by source root, preventing source lockfile edits and stale
  archive reuse when changing checkouts. Added a CMake regression covering
  missing/incompatible roots, paths with spaces, generated includes, checkout
  switching and lockfile isolation.
- Final validation: warning-free native Debug build with ASAN+UBSan; **352/352
  CTest tests passed** in 61.77 seconds, with leak detection, stack-use-after-return
  detection and strict string checks enabled. Desktop window, media/CLI/plugin
  integration and enabled Lua/mruby conformance all passed. Optional QuickJS,
  MicroPython and Wasmtime runtimes and other platforms were not exercised.
- `git diff --check` passes. Existing user changes in `CHANGELOG.md`, prior
  `PROGRESS.md` entries and third-party deletions remain intact. Test build:
  `build-audit`, configured with `JFX_MINIAUDIO_ROOT=/tmp/joltfx-audit-miniaudio`
  and `JFX_MRUBY_ROOT=/tmp/joltfx-audit-mruby-3.3-supported`; keep real dependency
  checkouts in persistent directories for future builds.

## Extension language layer

- Inspected the existing Lua/mruby numeric adapters, common placeholder, vendored QuickJS/MicroPython sources, engine editor/event/resource APIs and extension contribution rules.
- Implemented Script API 1.0: a shared typed FFI bridge, capability-checked editor/resource services, scoped resource handles, owned callbacks, cached runtime-local batch functions, per-invocation budgets and GC/error controls. Expanded Lua/mruby and added optional QuickJS, MicroPython and Wasmtime adapters; Lua/mruby numeric entry points remain compatible.
- Implemented validated WASM/WAT, fuel limits, Tilly linear memory and typed `joltwasm` ABI 1 alongside scalar exports. MicroPython uses isolated saved states and a fixed heap with host-boundary GC. Hardened mruby callable roots and nested MicroPython stack accounting; added callback self-removal, nil/function reference and recursion regressions.
- Added CLI `scripts list/run/edit` with `scripts` help, grading examples and a shared sequence fixture in all five languages, a checked-in embedding example, a dependency-resolving `JoltFX` CMake package with `FindWasmtime`, `ext_perf` profiling, `docs/extensions.md`, per-adapter READMEs and an all-language sanitizer CI lane (including a no-extension configure matrix entry).
- Fixed two host-ownership defects found by review: mruby rooted host callables through Ruby's script-mutable `$_gc_root_` (now a private native arena-rooted array, with `GC` removed), and nested MicroPython captures overwrote the outer stack limit (now restored, with ASan-safe native stack anchoring). Also fixed a Core auto-backend lifetime bug that destroyed the retained CPU fallback handle, which broke CPU-only configurations.

### Verification

- Full native CTest with all five runtimes enabled: **349/349 passed** (23.6 s). Full ASAN+UBSan CTest with all five runtimes: **349/349 passed** (90.6 s), leak detection, `detect_stack_use_after_return` and strict string checks on, no findings.
- Extension conformance covers bridge/marshalling, budgets, sandbox, recursion and GC (`ext_conformance`), resources/lifetimes/batches/callbacks (`ext_conformance_resources`), Wasmtime scalar + typed ABI (`ext_conformance_wasm`) and CLI example execution with real grading pixels and preserved project output (`ext_conformance_cli`) — **5/5** each configuration. Lua/mruby legacy numeric entry points also pass `ext_conformance`.
- Reduced CPU-only ASAN+UBSan builds pass end to end: default Lua/mruby **335/335**, and every runtime plus every frontend and backend disabled **329/329**. Both include the corrected auto-backend lifetime regression.
- Installed and relocated package at `/tmp/opencode/joltfx-ext-relocated`: the exported extension targets resolve the consumer's own Wasmtime prefix, include `docs/extensions.md`, per-adapter READMEs and the embedding example, and the example host runs all five runtimes over the bridge table. The installed CLI runs the full example/pixel suite. A sanitizer-instrumented consumer links and runs against the ASAN+UBSan installation. The all-disabled install produces an importable package that reports no enabled languages.
- `ext_perf` profile (Debug, x86-64): initial charged memory 23 KB (Lua), 101 KB (mruby), 168 KB (QuickJS), 3.1 MB (MicroPython reserved heap), 15 KB (Wasmtime); initialization 0.03–0.31 ms; 10,000 cached `gain(0.25)` invocations 0.35–1.09 µs each.
- All four build logs contain no compiler diagnostics (mruby's vendored GCC heuristic warning is suppressed through its build config). 31 local documentation links and the workflow YAML validate; `git diff --check` is clean.
- Not verified here: macOS/Windows and mobile consumers (Wasmtime, Ruby and MicroPython host requirements differ per platform), Wasmtime static builds, and AE/Premiere/Resolve host-adapter verification. Script kernels are runtime-local batch functions, not engine-catalog kernels; Wasmtime accepts one module per runtime; `joltvm.js` remains separate from this host-side Wasmtime adapter.

## Tabbed desktop and plugin SDK

- Inspected the Dear ImGui workspace, shared color descriptors/commands and existing metadata-only native plugin loader.
- Implemented workspace tabs for NLE, Layer Effects, Color Calibration, Color Grading, Node Compositing, Plugins, Console and Statistics, with shared transport/clip selection/preview/history/export. Grading has opponent-space color wheels, master dials, descriptor-backed rotary controls and grouped live gesture history.
- Implemented SDK 1.0 with versioned host services, native effects, compiled Joltscript image kernels, transactional editor actions, owned event subscriptions, module diagnostics, static attachment and legacy loader compatibility. Graphs, timeline copies and history retain plugin descriptors; unload reports BUSY while referenced and host teardown defers finalization.
- Desktop plugin manager/Extensions menu and CLI inspect/edit/render paths are integrated. SDK CMake package/helper and a standalone Warm Tint example are present.
- Targeted native checks pass: legacy/SDK lifecycle, editor transactions, composition UI and actual ImGui tab/dial/wheel interactions (including Ctrl-Z and switching tabs mid-gesture). Fixed example image-profile sampling syntax and a missing shared undo shortcut found by these checks.
- Export snapshots, copied/split/duplicated clips, graph copies and undo/redo references now have exercised unload protection. Cancellation reserves the baseline model before live edits, so failed multi-command actions roll back without allocation.
- Full native CTest: **345/345 passed**. Full ASAN+UBSan CTest: **345/345 passed**, including legacy/SDK lifecycle, real ImGui tab/dial/wheel input, native/Joltscript pixel checks, failure-atomic output, event ownership, deferred teardown and CLI/ffprobe plugin export checks. Build logs contain no compiler diagnostics.
- Installed SDK at `/tmp/opencode/joltfx-plugin-sdk-prefix`: standalone C example builds and passes CLI actions/history/render/encoded export from installed tools. A C++17-only consumer builds MODULE/STATIC, renders expected graph pixels and verifies PIC, unmangled entry and rejection of accidental direct engine linkage. Neither module has undefined engine symbols.
- Documented SDK ABI/lifetimes/commands and tabbed grading controls in `docs/plugins.md`, `sdk/README.md`, frontend guides, changelog and subsystem agent guides. SDK package/helper/example and guide are included in install rules.
- CPack TGZ generation passed; archive contents include SDK headers, versioned CMake config/target/helper, standalone example and documentation. Final native build has no warnings and `git diff --check` is clean.
- Current mobile/WASM builds have not been rerun; native static attachment and portable frontend rendering are covered by conformance tests. SDK image evaluation is synchronous CPU execution; native modules expose the implemented effects/kernels, editor-action and event registration capabilities described in `docs/plugins.md`.

- Fixed `test_plugin_module`'s include path to use `src/include`, where `jfx/jfx_plugin.h` is defined.
- Build and test results are recorded in the subsystem sections below.

## Color grading subsystem

- Inspected the shared editor, graph/timeline renderer, all frontend adapters, image kernels, and OpenColorIO glue.
- Found existing CPU calibration/grading kernels that are not exposed by the graph/editor; current grading panels use a small C-only subset.
- Implemented 29 kernel-backed color operators (26 reused kernels plus `grade_primary`, `grade_lut`, `calib_lut`). Parameter descriptors are generated from Joltscript declarations.
- Added the execution-layer image task runner, per-call budgets, finite validation, transactional output, straight/premultiplied alpha marshaling and domain-aware LUT resources.
- Integrated separate Calibration/Grading panels in desktop/web/Android, terminal commands, an embeddable iOS controller and SDK-independent descriptors/processing for AE, Premiere and Resolve.
- Added section-local add/param/path/enabled/reset/remove/move commands, validated LUT assignment, quoted/empty path persistence and render-error propagation for missing LUTs.
- Integrated optional `third_party/opencolorio` builds through Glue, including consistent ZLIB selection for its minizip dependency. The real ASC CDL `.cc` fixture passes the OCIO → LUT → Joltscript → executor path.
- Fixed integration findings: shared helper function capacity, generic graph test assumptions about video resources, and an existing stack overflow in the image-kernel test's text fixture.
- Documented usage, CPU/domain limits and versions in `docs/editor.md`, frontend READMEs and subsystem agent guides.

### Verification

- Native build and full CTest: **330/330 passed**, including all 297 image kernels, new color/executor tests, cross-frontend pixel conformance, CLI integration, host bridges, desktop headless and window smoke tests.
- Debug build with **ASAN + UBSan**: **330/330 passed**, no sanitizer findings.
- Vendored OpenColorIO build at `/tmp/opencode/joltfx-ocio`: `color_grading` passed with the real library available.
- Web TypeScript build and Node tests: **3/3 passed**, including section selection, bypass indices and quoted/empty LUT paths. Tracked `dist/` artifacts regenerated.
- Final API-version/warning cleanup: native rebuild and targeted tests **4/4 passed**; sanitizer rebuild and targeted color/executor/kernel tests **3/3 passed**. Final build logs contain no diagnostics, and `git diff --check` is clean.
- Android/iOS platform UI builds and browser WASM compilation could not be run: this environment lacks the NDK/Xcode/Emscripten toolchains. Portable mobile/web rendering is covered by native conformance tests.
- Host SDK registration is still supplied by downstream adapters; these changes provide working host-independent color processing, not host-installed plugin binaries.
- Execution is synchronous CPU interpretation with per-call compilation. Extended OCIO transforms are baked to a bounded 65³ table; GPU dispatch and unbounded OCIO configuration processing are outside the implemented path.

## Non-linear editor

- Built on the existing shared track/clip renderer, effect stacks and `.jfx` editor session.
- Added frame-accurate split, move between tracks, duplicate, trim, source slip, track ordering and track-local ripple delete/gap insertion. Split/duplicate deeply copy owned paths and keys; invalid/conflicting edits preserve the model.
- Added 32-step/32-MiB bounded sequence undo/redo, new rational-rate sequences, shared timeline-state JSON, exact-frame rendering and PPM frame export (editor API 1.2, timeline API 1.1).
- Preserved clip-reference animation clocks with a persistent split/head-trim offset. Linear/hold/smooth curves carry with moves/ripple edits; raw key APIs keep their original reference clock and editor key commands convert sequence frames.
- Extended project persistence with quoted track/clip names and media paths, exact integer timing, source in-points, track/clip/effect state and animation offsets. Successful loads clear history; repeated source-in directives set absolute positions.
- Desktop: interactive clip dragging between tracks, edge trimming, snapping, zoom/pan, ruler scrubbing, track controls, edit/history shortcuts, sequence setup and frame export alongside Calibration/Grading panels.
- Web: canvas NLE with one-edit drag commits, edge trims, snapping, zoom/pan, exact-frame editing/export, sequence setup and shared project/state/color controls. Fixed tail-click trimming and first-color-layer selection after index validation.
- Android: touch timeline plus JNI editing/state/seek/project/export methods and numeric NLE controls; color controls invalidate after edits and selection changes.
- iOS: embeddable `JFXNLEViewController` with playback/preview, frame scrubbing, track/clip controls, project I/O, setup/export and navigation to the selected clip's color panels. Playback stops on disappearance/inactivity.
- CLI/terminal: `joltfx nle new/edit/info/render`, piped commands, timeline JSON, undo/redo and exact-frame scaled export.
- AE/Premiere/Resolve: SDK-independent `jfx_host_nle_*` sessions for editing/history/state/project I/O/render/export, sharing color commands and descriptors.
- Added `docs/nle.md`, updated color/editor versions, all frontend guides, subsystem instructions and the changelog. Guides describe timing, command arguments, persistence, history ownership and export behavior.

### Verification

- Full native build and CTest: **332/332 passed**, including NLE editing/history/persistence, smooth-curve/rational-rate pixel preservation, desktop/mobile/web state and pixel conformance, PPM export/error preservation, all three host bridges, CLI integration and desktop headless/window smoke tests.
- Debug **ASAN + UBSan** build and full CTest: **332/332 passed**, no sanitizer findings.
- Web TypeScript build and Node tests: **7/7 passed**, including mounted first-color-layer controls, frame-consistent split commands, hit testing/snapping/drag/tail clicks and exact-frame FFI validation/buffer cleanup. Tracked `dist/` artifacts regenerated.
- Native and sanitizer configure/build/test logs are clean of compiler diagnostics and sanitizer reports. Logs: `/tmp/opencode/joltfx-nle-native-tests.log` and `/tmp/opencode/joltfx-nle-sanitizer-tests.log`.
- Executed the documented create/edit workflow and a scaled exact-frame export, validated local guide links, and verified CMake installs both editor guides. Final `git diff --check` is clean.
- Platform UI and browser WASM builds require unavailable NDK/Xcode/Emscripten toolchains. Portable adapters are covered by native conformance; installed AE/Premiere/Resolve UI registration still requires their SDK adapters.
- At this milestone, NLE/color rendering used the shared synchronous CPU reference path with optional FFmpeg video decoding and PPM export. The later audio/encoded-export delivery is recorded below.

## Node-based composition tool

- Inspected the typed DAG evaluator, node library, project format and every frontend's existing node controls.
- Added Composition API 1.1 and editor API 1.3: persistent graph-space positions, validated scalar edits, deep duplication, rename/reset/remove, typed connection editing, graph-state/node-catalog JSON and output/interior-node preview/PPM export.
- Graph and sequence commands share 32-step/32-MiB bounded undo/redo. Snapshots restore their edited model and prior mode while retaining the other document. Failed edits/loads preserve history; successful loads clear it.
- Fixed explicit-output loading, quoted/empty labels and paths, full float precision and empty graph persistence. Missing assigned image/LUT/video sources report errors while unassigned sources/incomplete image inputs preview transparent. Render failures preserve caller pixels and existing export files.
- The CPU evaluator allocates reachable node frames only, caches shared branches once per render, and preflights its 512-MiB float-frame scratch budget.
- Desktop: searchable typed library, draggable/wired canvas, pan/zoom/Fit, generated inspector, edit/history shortcuts, output/interior preview and composition raster/export controls.
- Web: `CompositionCanvas`, native catalog/state/render exports, generated typed inspectors and resource import, graph-aware shortcuts, project I/O and PPM download. Sequence color controls retain their data while a graph is active.
- Android: `CompositionGraphView` with touch wiring/drag/pan/pinch/Fit, native-generated inspectors and JNI graph state/catalog/preview/export alongside NLE and color panels.
- iOS: embeddable `JFXCompositionViewController` with touch canvas, typed menus/inspectors, history, previews, setup, project paths and export, reachable from the NLE controller.
- AE/Premiere/Resolve: SDK-independent `jfx_host_composition_*` sessions share graph editing, history, persistence, state, interior/output previews and export.
- Added `docs/composition.md`, a composition fixture, core and cross-frontend/host regressions, actual Dear ImGui gesture tests, CLI integration and browser-independent mounted inspector/canvas/FFI tests. Updated all frontend guides, API comments, packaging and subsystem instructions.

### Verification

- Full native build and CTest: **335/335 passed**, including composition history/persistence/validation/error handling, desktop canvas gestures, cross-frontend graph-state/pixel/export parity, all three host bridges, CLI integration and desktop headless/window smoke tests.
- Debug **ASAN + UBSan** build and full CTest: **335/335 passed**, no sanitizer findings. Both build logs contain no compiler warnings or errors.
- Web TypeScript build and Node tests: **11/11 passed**. Tracked `dist/` artifacts regenerated.
- Executed the documented create/edit/save/render workflow and desktop graph smoke. Verified explicit-output/color pixels and empty-graph exports through `compose render`, `project render` and `render-graph`.
- Validated 26 local documentation links and CMake installation of the composition, NLE and color/editor guides. Final `git diff --check` is clean.
- Checked all 20 Android native declarations against JNI names/arity and all 20 iOS bridge selectors against their implementations. Android/iOS UI and browser WASM builds require the unavailable NDK/Xcode/Emscripten toolchains; portable adapters are covered by native conformance.
- Installed AE/Premiere/Resolve panels and SDK registration require their proprietary adapters. The SDK-independent composition/NLE/color APIs are implemented and tested.
- Logs: `/tmp/opencode/joltfx-composition-native-tests.log`, `/tmp/opencode/joltfx-composition-sanitizer-tests.log` and corresponding `*-build.log` files. Documented workflow outputs are in `/tmp/opencode/joltfx-composition-docs`.

## Frontend delivery follow-up

- Continuing platform integration: the mobile source wrappers lacked a runnable Android project and iOS build/application entry points.
- Added an Android Gradle/CMake project, checksum-pinned Gradle 8.9 wrapper and launch manifest targeting the shared JNI editor, plus an iOS UIKit library/application target linking all three editor controllers.
- Obtained isolated Emscripten 6.0.10/JDK 17/Kotlin tools under `/tmp/opencode`. The actual WASM build exposed inconsistent bootstrap `mmap` guards and a scheduler that required unavailable pthread workers; fixed owned-map cleanup/time and added a bounded serial priority-queue path for non-pthread Emscripten modules.
- Added real-WASM bridge integration tests for exports/catalogs, graph and NLE/color state/history/native pixel parity, independent previews, virtual image/LUT resources, heap growth and failures. These also caught graph image sampling that omitted the final row/column; fixed full-extent sampling with native-size/upscale/downscale regressions.
- Native and ASAN+UBSan builds and full CTest: **336/336 passed** each, no compiler diagnostics or sanitizer findings. Web unit tests: **11/11 passed**; real-WASM integration: **4/4 passed**. Emscripten bootstrap and serial scheduler unit programs also passed.
- Headless Firefox 156 launched the actual browser editor and passed grading/calibration, frame-accurate split/undo/redo, graph/sequence switching, `.jfx` import, output/interior preview and exact-raster PPM download checks. Results: `/tmp/opencode/joltfx-browser-smoke.json`.
- Downloaded and checksum-verified the actual Android SDK 35/build tools, NDK 27.2.12479018, CMake 3.22.1 and platform tools through Google's SDK redirect service. Resolved the Android Gradle plugin through a test-only mirror init script outside the repository.
- The NDK build caught the private timeline `key_t` collision with Bionic and engine timestamps calling `timespec_get` below its API-29 availability. Renamed the private key storage type and used Android's supported realtime clock. Cleaned Clang diagnostics, including aligned SPIR-V word storage and explicit JNI float conversion.
- The Android app builds with its pinned Gradle/AGP/Kotlin/NDK toolchains for **arm64-v8a and x86_64**. `assembleDebug lintDebug` succeeds with **no compiler diagnostics and no lint issues**. Restored tap/swipe handling on the visible preview, supplied launcher/resources and reused graph drawing paths. Numeric command fields preserve locale-independent full precision.
- Verified the debug APK's signature, launch activity, API-26 minimum/API-35 target, all **20 JNI exports on both ABIs**, **16-KiB ELF load-segment alignment** and APK ZIP alignment. Artifact: `frontends/mobile/android/app/build/outputs/apk/debug/app-debug.apk` (3,334,495 bytes). Log: `/tmp/opencode/joltfx-android-apk-verification.log`. No Android device/emulator is connected for on-device UI execution.
- Final native and ASAN+UBSan rebuilds plus affected core/NLE/composition/backend/frontend conformance tests: **32/32 passed** each after the Android portability fixes, with no compiler diagnostics or sanitizer findings; the earlier full delivery suites passed **336/336** each.
- Optimized Release WASM build and integration tests: **4/4 passed**, no compiler diagnostics. Repeated the real Firefox editor checks against the release assets in `frontends/web/public/`; output/preview/export pixels match native conformance. Generated JS/WASM files are ignored build artifacts.
- Updated mobile/web launch/build/test guides, subsystem instructions and the changelog; pinned TypeScript 6.0.3 and verified a fresh npm install/build/test (**11/11 passed**). Validated Android XML resources, the iOS bundle template/source list and local guide links. Final `git diff --check` is clean.
- iOS UIKit/application targets are present but platform compilation/runtime validation requires unavailable Xcode/iOS SDKs. Installed AE/Premiere/Resolve panels still require proprietary SDK adapters; portable host APIs are implemented and tested.

## Audio mixing and encoded export

- Implemented sample-accurate shared timeline audio mixing and encoded audio/video export. Cloned and registered miniaudio 0.11.23 and FFmpeg release/8.0 as `third_party/` submodules.
- Persistent clip/track audio controls, editor history/JSON/commands, bounded streaming mixers and transactional incremental export with progress/cancellation are shared by the frontend adapters.
- Added `audio_mix.jolt`, compiled through Glue and dispatched by the failure-atomic stereo Execution task; existing image/color kernels render the encoded frames. Fractional-rate split preservation, overlap/solo/mute, block/seek equivalence and audio task vectors pass native and ASAN+UBSan checks.
- Full native and ASAN+UBSan suites: **342/342 passed** each; cleanup media/desktop/NLE/composition suites **11/11 passed** each and final media suites **6/6 passed** each. Native/WASM/Android build logs have no compiler warnings/errors. Bundled FFmpeg media checks: **5/5 passed**; FFmpeg-disabled mixing/capability checks: **3/3 passed**.
- Independent FFmpeg/ffprobe checks confirm MP4/MPEG-4/AAC, MOV/ProRes/PCM and MKV/FFV1/PCM, nonzero stereo audio/balance, rational-rate A/V durations, exact lossless raster parity, FLAC/MP3/AAC input, silent video and transactional failures. Portable media conformance passes desktop/mobile/web and all three host sessions.
- Web unit tests **11/11 passed**; Release real-WASM tests **6/6 passed**, including media decoding, audio settings/history/snapshot resets, replacement of imported audio bytes, encoded output and cancellation. Refreshed public JS/WASM assets. Actual Firefox 156 passes trusted-click WebAudio playback, queued-source cleanup, encoded download and cancellation/reuse; independent decode confirms FFV1/PCM, stereo balance and 48,048 audio frames for 30 frames at 30000/1001 fps. Results: `/tmp/opencode/joltfx-media-browser-smoke.json`.
- Android bundled-FFmpeg `assembleDebug lintDebug` passed for both ABIs after final source changes. Debug APK signature, API-26/API-35 launch manifest, **29 JNI exports on both ABIs**, **16-KiB ELF load-segment alignment** and APK ZIP alignment pass. APK: `frontends/mobile/android/app/build/outputs/apk/debug/app-debug.apk` (22,254,684 bytes). Packaging log: `/tmp/opencode/joltfx-media-android-apk-verification.log`.
- Added `docs/media.md`, refreshed frontend/API help and build guides, and installed media guides/dependency license texts. Both bundled and system-FFmpeg installs pass an out-of-tree CMake/C consumer that mixes audio and encodes a video. Bundled CPack TGZ creation succeeds.
- iOS AVAudioEngine playback and incremental sequence/composition export controls are implemented; compilation/runtime verification requires macOS/Xcode/iOS SDKs. Android on-device playback/UI verification requires a device/emulator; none is connected. Installed AE/Premiere/Resolve verification requires proprietary SDK adapters; portable host audio/export APIs pass conformance.

## Joltscript plan audit (`JOLTSCRIPT_PLAN.md`)

Audited the five-phase plan against the shipped tree. Baseline first: in-tree `build/`
rebuilds clean and the 34 Joltscript/kernel tests pass. Evidence gathered with a
standalone probe linked against `libjoltscript_glue.a`, compiling sources exactly as
`kernels/src/image_kernels.c` does (`common/image.jolt` prepended as the library).

- The plan (2025-09-29) has diverged from the implementation. `joltscript/AGENTS.md`
  and the Glue `AGENTS.md` describe `compiler/{frontend,middle,backend/{c,rust,python,go}}`,
  `runtime/{arc,arena,intrinsics}` and `abi_registry/marshal_engine/capability_dispatch/
  lifetime_bridge/error_adapter/extension_host`. None of those files exist. The real
  language surface is a 377-line bounded AST interpreter (`image_program.c`) plus a
  193-line JBC1 bytecode compiler (`compiler.c`).
- **Phase 1 — partial.** Present: `let`, `if`, `defn`, `defkernel`, plus unplanned
  `param`/`sum`/`passes`. Absent: 9 of the 12 listed special forms (`var`, `cond`,
  `when`, `unless`, `for`, `while`, `loop`, `fn`, `do`). Types: **0 of 11** — the
  profile is untyped `f64`/`f32` throughout. Comparison and logical ops exist; bitwise
  ops exist in JBC1 (`compiler.c:86`) but **not** in the live image profile.
  Source-located diagnostics are present on both paths.
- **Phase 1.5 (Zoltan parity) — the gate is vacuous.** `scripts/check-bytecode-parity.sh`
  exits 0 reporting `failed=0`, but only actually compares **14 of 312** `.jolt`
  sources; the other **298 are silently `SKIP`ped** because the JBC1 compiler rejects
  them. That is 4.5% coverage presented as a passing gate.
- **Phase 2 (stdlib) — all six modules are dead code.** Each of `math/graphics/geometry/
  time/collections/strings.jolt` fails to compile with `unknown top-level form` on its
  `(module ...)` line. They are written against the aspirational `AGENTS.md` language
  (`module`, `import`, `defconst`, variadic `&`, `for`, `set!`, `break`, `nth`,
  `slice`, `concat`), none of which the interpreter implements. Nothing in the build,
  tests or tools references `joltscript/stdlib/` at all.
- **Phase 3 (tools) — `joltfmt` weak, `joltdoc`/`joltscript-lsp` drift-prone.**
  `joltfmt` is semantically safe — 296/296 kernels round-trip and still compile — but
  emits each file as one line with a space after every `(`, discarding all layout.
  `joltdoc` and `joltscript-lsp` contain **zero** references to
  `jolt_compile`/`jolt_image_compile`; both re-implement their own parse, so they can
  silently disagree with the language.
  **Correction:** this section originally also claimed `joltc` "fails
  `expected defkernel` on all 297 shipping image kernels". That was wrong — I had
  omitted the required flag. `joltc` already implemented the image profile via
  `--image-library`, and `--check --image-library kernels/common/image.jolt` validates
  **296/296** image kernels. The defect was discoverability, not capability; see
  "joltc discoverability" below.
- **Phase 4 (tests) — done.** All five plan test files exist, are registered from
  `tests/unit/CMakeLists.txt`, and pass.
- **Phase 5 (docs) — done and accurate.** `joltscript/docs/language.md` documents the
  real bounded profile (`defkernel`, eager `let`, lazy `if`, `sum`, `sample`), not the
  aspirational spec.

### Correction: the stdlib is a different language, not a missing-builtins gap

I first recommended making the six stdlib modules loadable, then measured what that
requires. That recommendation was wrong and is withdrawn. The modules are not the
bounded profile with some builtins absent; they are written in a **mutually
incompatible dialect**, and the incompatibility is syntactic, not incremental.
Verified with a probe against `libjoltscript_glue.a`:

| construct | stdlib writes | profile accepts |
|---|---|---|
| `let` | `(let name value)` | `(let [name value ...] body)` |
| body | several forms (implicit `do`) | exactly one form |
| values | vectors `[h s v]`, 4-tuples | scalars only (`f64`) |
| top level | `module`, `import`, `export`, `defconst` | `param`, `defn`, `defkernel`, `passes` |

`graphics.jolt` is the cleanest module -- no collections, no FFI, no mutation -- and it
still fails on line 10 on the `let` convention alone. Beyond that, every module needs
collections (all six), `strings.jolt` needs a string value type, and `math.jolt` needs
**23 `extern-c` calls**. Those FFI calls would contradict the profile's documented
sandbox guarantee that programs have "no filesystem, FFI, imports, allocation, or system
resources" (`image_program.h:19-20`). Loading the stdlib as written therefore means
implementing a second, general-purpose language with values, mutation and FFI -- a
multi-phase project, not an increment -- or weakening a security boundary the profile
currently advertises.

Treat the stdlib as a **specification artefact for the full `AGENTS.md` compiler**, not
as broken code. The defect is that it is unlabelled: nothing states that it targets an
unimplemented compiler rather than the bounded CPU profile.

### Delivered

- `scripts/check-bytecode-parity.sh` rewritten. It previously exited 0 while comparing
  only 14 of 312 sources, skipping 298 and reporting `failed=0`. It now derives the
  JBC1 corpus from `kernels/CMakeLists.txt` (`EFFECT_NAMES`, the audio kernel, shipped
  examples), reports `n/a` for image-profile sources with an explicit reason, prints
  coverage over the pinned corpus, and **fails** if any registered JBC1 source drops out.
  Verified: green at 14/14 with 298 reported n/a; a deliberately corrupted
  `kernels/color/invert.jolt` produced `FAIL`, `13/14`, and exit 1; file restored clean.

Not attempted: implementing the Phase 1 type system or the nine missing special forms.
Both would rewrite the language the 297 kernels are written against, and `MEMORY.md`
records the small profile (`let`/`if`/`sum`, no fold, no tuples, eager `let`) as a
deliberate per-pixel design constraint rather than an MVP gap.

## Joltscript stdlib labelling and `joltc` discoverability

Follow-on from the plan audit above. Two changes, both additive.

### Stdlib labelled as specification artefacts

- Added an identical `STATUS:` banner to all six `joltscript/stdlib/*.jolt` files
  stating that they target the full `AGENTS.md` compiler, that the current
  implementation is the bounded CPU image profile, that the incompatibility is
  syntactic (`let` arity, implicit `do`, vectors/tuples, `module`/`import`/`export`/
  `defconst`), and that `math.jolt` needs 23 `extern-c` calls the profile forbids.
  Each banner also warns against "fixing" errors by editing call sites, since kernels
  written against the bounded profile would stop compiling.
- Added a "Status: not loadable by the current implementation" section to
  `joltscript/docs/stdlib.md`, ahead of the module reference, because its Overview
  previously implied the modules worked. It points at `language.md` for the dialect
  that does compile and at `kernels/common/*.jolt` for the helpers actually in use.
  `tools.md`, `examples.md` and `language.md` were checked and make no stdlib claims,
  so no other caveat was needed.
- Re-verified all six still fail exactly as documented (`unknown top-level form` on
  `module`, now at line 24 after the banner) — the labels describe real behaviour
  rather than papering over it.

### `joltc` discoverability

- The `--image-library` mode existed and worked; it was simply undocumented in the
  file header and easy to miss, so the failure mode for anyone editing a shipping
  kernel was a bare `expected defkernel` naming neither the real dialect nor the flag.
- Documented both dialects in the `joltc` header comment and usage text, with the
  image-profile invocation as a worked example.
- Added an actionable hint on JBC1 compilation failure: when the diagnostic is
  `expected defkernel` **and** the source leads with a `param`/`defn`/`passes` form
  (a cheap syntactic check), it prints the exact `--check --image-library` command to
  run. Gated on both conditions so a genuine JBC1 syntax error still reports only
  itself.

### Verification

- In-tree `build/` rebuilds clean with no compiler diagnostics.
- Full native CTest: **349/349 passed**. The 34 Joltscript/kernel tests pass.
- `scripts/check-bytecode-parity.sh` green at 14/14 with 298 reported n/a.
- `joltc --check --image-library kernels/common/image.jolt` validates **296/296**
  image kernels; JBC1 emission, `--dump`, `--check` and the 14-kernel JBC1 corpus are
  unchanged.
- Hint verified to fire on `kernels/blur_sharpen/box_blur.jolt` and **not** to fire on
  a genuine JBC1 syntax error or on the JBC1 kernels; the command the hint prints was
  run and succeeds.

## OpenFX (OFX) host adapter

- Implemented the host side of the OpenFX 1.5 image-effect API at
  `frontends/plugins/ofx/`, gated by `JFX_PLUGIN_OPENFX` (default ON). Unlike the
  AE/Premiere/Resolve bridges, this needs no proprietary host SDK: the property,
  parameter and image-effect suites are implemented here, and only the engine's
  C API is linked. OFX 1.5.1 comes from the `third_party/openfx` submodule and is
  included privately so it never leaks into a consumer's include path; configuring
  with the option ON and no submodule fails with an explicit message.
- Public API `jfx/jfx_ofx.h`, `jfx_` prefix with size-guarded structs, `out_*`
  outputs last, per `src/AGENTS.md`. Discovery is explicit
  (`jfx_ofx_host_scan`), the standard per-platform bundle layout is honoured, and
  pixels cross the boundary as tightly packed float RGBA.
- Action lifecycle: load, describe, describe-in-context, create-instance, render,
  destroy. Two OFX subtleties were load-bearing and are documented in the README:
  `describe` must receive a real handle (plugins call `getPropertySet` on it to
  publish supported contexts, pixel depths and label, so `NULL` breaks every real
  plugin), and one opaque handle denotes both the plugin descriptor and an
  instance. Every handle therefore begins with a `jfx_ofx_owner_t` tag, and an
  untagged handle is rejected rather than cast.
- The host advertises only what it can service: float RGBA, no tiles, no
  multi-resolution, no temporal clip access, no overlays and no parameter
  animation, with `fetchSuite` returning NULL for every unimplemented suite. This
  keeps plugins inside the supported path instead of failing at render time.
  Parameter values are seeded from `kOfxParamPropDefault` read as either int or
  double, since plugins write it both ways, and setters clamp to the declared
  range. Rendering is transactional: output goes to a scratch frame and reaches
  the caller only on success, matching the engine's CPU image rule.
- All limits are fixed and small because a property bag is embedded in every clip
  and parameter, so a plugin cannot drive host allocation by asking for large
  values. Non-OFX modules, wrong `pluginApi`, and plugins declaring no context
  are skipped rather than half-loaded.
- `ofx_host` builds a real `.ofx.bundle` from a real shared object
  (`frontends/plugins/tests/gain_plugin.c`) and drives it through `dlopen`;
  nothing is stubbed, because symbol resolution, `setHost`, suite vtables and
  pixel output only exist when the plugin is a separate binary. It covers invalid
  arguments on every entry point, discovery, description, parameter metadata,
  pixel output, clamping, transactional failure, the General context, instance
  independence, and rejection of a non-OFX module. Assertions are on real pixels,
  so a plugin that is never invoked fails the test.
- Installed at `/tmp/opencode/ofxinstall`: `jfx_ofx.h`, `libjfx_ofx_host.a` and
  the README. An out-of-tree consumer compiles and links against the installed
  header alone, confirming the OFX headers are not required downstream.

### Verification

- In-tree `build/` rebuilds with no compiler diagnostics. Full native CTest:
  **350/350 passed** (349 before, +1 for `ofx_host`).
- Debug **ASAN + UBSan** build with leak detection: full CTest **350/350 passed**,
  no sanitizer findings.
- Pixels verified to come from the loaded plugin: a 1x1 frame with `gain=2.0` and
  a white tint over `src = (0.5, 0.25, 0.75, 0.4)` yields `(1.0, 0.5, 1.5, 0.4)`,
  confirming the render path executed and alpha passed through untouched. A
  directory containing a non-OFX `.so` in bundle layout is rejected with zero
  plugins and no crash.
- Not verified here: Windows and macOS discovery (`Windows/x86-64`,
  `Windows/arm64`, `MacOS/x86-64`, `MacOS/arm64` are implemented but only
  `Linux-x86-64` was run), and interaction with a real third-party OFX bundle such
  as the ASWF example plugins. The adapter is a library plus tests; no frontend or
  CLI surface exposes it yet, so there is no UI path to exercise. Additional input
  clips a `General`-context plugin declares (a `Mask`, for example) are declared
  but never populated, so reading one fails rather than returning stale pixels.

## Wasmtime missing-header build fix (2026-10-04)

- Diagnosed `jfx_wasm` using stale cached SDK paths under `/tmp/opencode`;
  the SDK no longer existed, so `wasmtime.h` was absent at compilation.
- Added stale header/library cache invalidation to `FindWasmtime.cmake` and
  actionable C API SDK setup guidance on configuration failure.
- Added a dependency-discovery regression covering recovery from stale cache
  paths and failure after SDK removal; confirmed it failed before the fix and
  passed after it. Updated the extension build documentation.
- Restored Wasmtime 38.0.4 C API to the ignored persistent directory
  `third_party/wasmtime-v38.0.4-x86_64-linux-c-api` and reconfigured `build/`.
- Verified `jfx_wasm`, extension test executables and `joltfx_cli` build;
  extension conformance plus the new CMake regression pass **6/6**.
- Reconfigured `build-san/` to the restored SDK and built `jfx_wasm` with
  ASAN+UBSan enabled, with no compiler warnings or errors. Sanitizer runtime
  tests and the full repository suite were not rerun for this CMake-only fix.
- `git diff --check` passes.

## Submodule initialization cache reuse (2026-10-05)

- Added `--force` to both `git submodule add` paths in
  `scripts/submodules-init.sh` so repositories retained in `.git/modules` can
  be reactivated after their checkouts are removed.
- Local Git checks exposed absolute `.gitmodules` paths breaking the final
  update; switched additions to repository-relative paths with compatibility
  for caches stored under the script's older absolute submodule names, and
  normalized existing absolute registrations before reusing those names.
- Staged the pinned gitlink after checkout so the final recursive update does
  not reset a selected tag to the revision initially added by Git.
- Verification passed: Bash syntax and whitespace checks; eight local Git
  scenarios covering legacy caches with missing/existing registrations,
  standard caches and fresh clones, each with a pinned tag or `docking` branch.
  Cached reuse succeeds with the upstream unavailable; recursive updates retain
  pins, and reruns preserve local edits. Fixture paths include spaces.

## Standalone third-party repository cloning (2026-10-05)

- Converted `scripts/submodules-init.sh` to clone the listed repositories as
  standalone Git checkouts, with an optional destination directory and
  `--help`. The default destination is resolved from the script location.
- Preserved revision/branch selection, initialized nested dependencies after
  checkout, and allowed cloning into empty directories while skipping existing
  Git checkouts.
- Verification passed: shell syntax, help/argument handling and whitespace
  checks; full-script local Git fixtures cover all 54 entries with default and
  custom destinations, branch/tag selection and revision-specific nested
  dependencies. Also verified absolute/relative paths with spaces, empty
  destination directories, existing Git-file checkouts, offline reruns retaining
  local edits and preservation of nonempty non-Git directories. The parent
  repository's `.gitmodules`, index and configuration stay byte-identical.

## Clone-only third-party initialization (2026-10-05)

- `scripts/submodules-init.sh` now only clones. `clone_repository` takes just a
  url and a path; the revision and branch parameters, the `git clone --branch`
  form and the post-clone `git checkout` are gone, so every repository lands on
  its upstream default revision. All 54 call sites lost their pin argument and
  the header comment describes the new behavior.
- Nested dependencies are still initialized with
  `git submodule update --init --recursive` after each clone; the
  destination-directory argument, `--help`, the skip-if-already-checked-out
  guard and `set -euo pipefail` are unchanged.
- Verification: `bash -n` passes. No other file references the removed
  arguments, and the run was not executed against the network, so the clone
  list is verified by inspection only.
# Progress

## 2026-10-08

- Differentiated the desktop **Layer Effects** and **Color Calibration** tabs,
  which previously shared one generic `effect_stack` layout. Layer Effects is
  now the per-clip composite stack (category-grouped add menu, layer/clip-order
  numbering, blend, opacity, keyframes). Calibration is now the technical
  normalization pipeline (stage numbering, suggested white/levels/gamma/range/
  space/LUT order, labeled combos for channel/space/curve/clamp/preserve
  options, Kelvin temperature slider, one-step bypass-all toggle). Grading
  keeps its wheels/dials. Updated `frontends/desktop/README.md` and
  `docs/editor.md`.
- Solidifying pass: full test suite green in both the default build and an
  ASan/UBSan build (361/361 each). The sanitizer run exposed a real latent bug:
  `tests/unit/color/vst3_fixture.cpp` buried side-effecting VST3 calls inside
  `assert()`, so NDEBUG builds skipped host `queryInterface`/`createInstance`,
  timer registration, `resizeView`, parameter/event reads and timer
  unregistration, causing null dereferences (`vst3_audio_host`,
  `daw_ui_tests`, `daw_native_window_capture` under sanitizers). Rewrote the
  fixture to run every host call unconditionally with explicit error returns;
  pure state validations stay as asserts. Verified shipped host code
  (`src/src/vst3*.cpp/inc`) contains no assert-with-side-effect.

## 2026-10-07

- Added the first 2D animation API slice in `jfx_animation.h`.
- Added deterministic skeletal scene evaluation with hierarchical transforms,
  keyframes, step/linear/smooth curves, and a renderer-neutral editor tab clock.
- Added JFA1 bytecode compilation, validation, and playback APIs. The format is
  architecture-independent and suitable for a later LIEF object-file section
  writer and native/WASM player.
- Wired the implementation into `jfx_core` and exposed the vendored math/render
  include roots, including `third_party/simdette`.
- Added a dedicated desktop **2D Animation** workspace tab with transport,
  timeline scrubbing, root-bone creation, scene statistics, and JFA1 bytecode
  compilation feedback.
- Standardized viewport zoom across the shared frontend contract, headless CLI,
  desktop workspace, web player/editor canvases, and mobile player APIs. Zoom is
  clamped to 0.25x–8x and supports Ctrl/Cmd-wheel on desktop and web.
- Hardened animation scene input validation and bytecode playback error
  propagation, and added shared frontend contract coverage for zoom dispatch.
