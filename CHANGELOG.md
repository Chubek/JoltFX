# Changelog

All notable changes to JoltFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- DAW MIDI clips and VST3 instruments, native plugin editor windows, opaque
  component/controller state persistence, stereo input recording and track-gain/
  plugin-parameter automation lanes. Shared project/history/playback/export
  snapshots, sample-offset events and note chasing, atomic WAV takes and native
  view/run-loop lifecycle. Timeline 1.3, Editor 1.7, Audio 1.2, VST3 1.1 and
  Desktop 1.4; MIDI/Automation/Recording APIs 1.0.

- Extension Script API 1.0: language-neutral typed FFI, sandboxed editor/resource
  services, owned callbacks, cached runtime-local batch functions, allocator and
  instruction budgets, GC controls and structured diagnostics. Expanded Lua/mruby
  adapters preserve their numeric entry points; new vendored QuickJS/MicroPython
  and optional WAMR adapters share the same contract.
- Validated binary WASM execution with instruction metering, budgeted Tilly memory, scalar
  exports and typed `joltwasm` ABI 1. CLI `scripts list/run/edit`, runnable grading
  examples in five languages, installed extension headers/archives and a
  dependency-resolving `JoltFX` CMake package. Cross-language/resource/lifetime,
  sandbox/budget/CLI conformance and all-adapter sanitizer CI.
- Replaced the external Wasmtime SDK with vendored `third_party/wasm-micro-runtime`.
  The interpreter archive ships in the CMake package. Compile WAT to `.wasm`
  before loading; start sections and automatic constructors are rejected.
- Correct auto-backend lifetime during probing, retaining the CPU fallback and
  replacing it with a later GPU backend instead of destroying the selected handle.

- Tabbed Dear ImGui desktop workspace with NLE, Layer Effects, Color Calibration,
  Color Grading, Node Compositing, Plugins, Console and Statistics interfaces;
  shared preview, transport, clip selection, undo/redo and export. Resolve-inspired
  grading wheels/master dials, rotary scalar controls and one-step gesture history.
- Plugin SDK 1.0: C/C++ host-service ABI, native image effects, compiled Joltscript
  image kernels, transactional editor actions, owned events, allocation/logging,
  static attachment and legacy module compatibility. Copied descriptors and
  graph/timeline/history/export lifetime retention; busy unload and deferred teardown.
- Installable `JoltFXPluginSDK` CMake package, `JoltFX::plugin_sdk` target,
  `jfx_add_plugin` helper and standalone Warm Tint example. Desktop Plugins/
  Extensions integration, repeatable startup loading, CLI inspect/render, terminal
  plugin commands and plugin-aware encoded export. Plugin API 1.1, Editor API 1.5
  and Desktop API 1.1; lifecycle, CLI and real ImGui regression coverage.

- Shared timeline audio mixing: audio-only/video clips, clip/track gain, stereo
  balance, fades, source in-points, mute/solo, sample-rate conversion and
  split/trim-preserving rational clocks. Persistent controls, history and JSON;
  audio API 1.0, timeline API 1.2 and editor API 1.4.
- Vendored miniaudio 0.11.23 and FFmpeg release/8.0; Glue streaming media readers
  and encoded writer, a new `audio_mix.jolt` kernel and failure-atomic stereo
  Execution task (Glue ABI 0.4, Execution ABI 0.5).
- Encoded MP4/MOV/MKV export with mixed audio, immutable document snapshots,
  incremental progress/cancellation and transactional output. Export API 1.0,
  optional system codecs and bundled native/WASM/Android/iOS FFmpeg builds.
- Desktop SDL2, browser WebAudio, Android AudioTrack and iOS AVAudioEngine
  playback; shared encoded export controls, CLI routes and portable host APIs.
  Native/sanitizer/frontend/real-WASM and independent FFmpeg A/V regression tests.

- Node-based composition tool across desktop, CLI/terminal, web, Android, iOS and
  all three host bridges. Typed canvas wiring, persistent node layout, generated
  inspectors, duplicate/rename/reset, interior/output preview and PPM export.
  Composition API 1.1, editor API 1.3 and shared graph-state/node-catalog JSON.
- Shared bounded graph/sequence undo/redo, empty graph persistence, exact output
  selection, quoted labels/resource strings and full-precision graph parameters.
  Reachable-only graph frame allocation with a 512-MiB scratch bound; native,
  cross-frontend/host, CLI and web interaction regression coverage.
- Runnable Android Gradle/JNI application project with a checksum-pinned wrapper,
  and iOS UIKit library/application CMake targets with launch entry points.
- Real compiled-WASM integration tests through the production JavaScript bridge,
  including virtual resources, growable memory and native render parity.

- Shared kernel-backed Color Calibration and Color Grading sections across the
  desktop, CLI/terminal, web and mobile surfaces, plus SDK-independent host color
  descriptors and frame processing. The catalog exposes 29 image-profile color
  operators and derives parameter metadata from their Joltscript declarations.
- `grade_primary`, domain-aware `grade_lut` and `calib_lut` kernels; a transactional
  CPU image-task executor (execution ABI 0.4), color API 1.0.0 and section-local
  editor commands (now API 1.4). Optional vendored OpenColorIO build integration.
- Cross-frontend pixel conformance, executor failure/budget tests, LUT/alpha/path
  persistence tests and an actual OpenColorIO transform integration fixture.
- Frame-accurate NLE split, move, duplicate, trim, source-slip and track-local
  ripple edits; track ordering and bounded sequence undo/redo (now editor API 1.4,
  timeline API 1.2). Shared sequence-state JSON and exact-frame preview/PPM export.
- Interactive desktop/web/Android timelines, an embeddable iOS NLE controller
  linked to color panels, CLI/terminal `nle new/edit/info/render` workflows and
  NLE sessions for all three host bridges. Raster/rational-rate sequence setup
  and frame export controls accompany the shared Calibration/Grading surfaces.
- Complete NLE state persistence, with quoted names/media paths, exact integer
  timing, source in-points, track/clip/effect states and animation reference
  offsets. Native/frontend/host/CLI and browser-independent NLE regression tests.

- **Colour grading, non-linear editing and node compositing, defined bottom-up
  through the strata.** Four new modules in `core`, each with a conformance
  suite, all reachable from every frontend through one shared text format and
  from the command line today.
  - `jfx_lut.h` — a colour lookup table library with no dependencies, so it runs
    identically on the desktop, through WASM in a browser and on a phone. Three
    shapes (1D curve, 2D strip, 3D cube) with correct interpolation for each, an
    explicit sampling domain, `mix` to dial a grade back, and identity/curve
    generators. Readers **and writers** for Adobe `.cube` (1D and 3D), Autodesk
    `.3dl` (1D and 2D), Sony `.spi1d` and `.spi3d`, DaVinci `.look` (a container
    with an embedded cube) and Hald CLUT images. Iridas `.csp` is detected and
    explicitly rejected with a message naming what *is* supported, rather than
    being misread; it is the one common LUT container still unparsed.
  - `jfx_compose.h` — a node graph: typed ports, cycle rejection, topological
    evaluation with a per-node cache, and a 26-kind built-in library covering the
    kernel catalog's COLOR and COMPOSITING categories (sources, exposure,
    contrast, saturation, vibrance, white balance, lift/gamma/gain, levels,
    curves, channel mixer, LUT, parametric curve, transform, luma and chroma
    keying, 14 separable blend modes, and the usual adjustments). **Every kind
    declares its own ports and parameter ranges**, so the desktop panel, the web
    canvas, the CLI and a host plugin all build their controls from that one
    table instead of each hardcoding a list.
  - `jfx_timeline.h` — the NLE model: tracks, clips with in/out points, and a
    per-clip ordered effect stack with per-effect enable, blend mode, opacity and
    keyframes (linear, hold and smoothstep). Time is counted in frames against
    an exact rational rate, so 30000/1001 does not drift, and a clip's
    keyframes are evaluated in the clip's own time base. Clip sources are the
    graph's source nodes and effect stacks are applied by reusing the node
    evaluator, so there is one implementation of every operator.
  - `jfx_project.h` — a plain-text interchange format for a grade (a graph) and
    an edit (a sequence), in core so every frontend and host plugin shares one
    parser. A project can be diffed, generated by a script, and a render is
    reproducible from a file alone. Sequence round-trips preserve the exact
    rate, placements, effects and keyframes, and render byte-identically.
- **New CLI commands**, driving the same model the GUIs do:
  `joltfx nodes [NAME]`, `joltfx lut info|convert|apply`,
  `joltfx render-graph`, `joltfx render-sequence`, `joltfx project info|render`.
- `jfx_image.h`, still-image decoding through the vendored `stb_image`, shared by
  the Hald CLUT reader and the NLE's image clips.

### Fixed

- Emscripten bootstrap arena allocation/cleanup and monotonic time; serial
  priority-queue scheduling for modules built without pthread support.
- Graph image sampling now preserves the last source row/column and every texel
  at native resolution, with full-extent nearest-neighbour scaling.
- Android API-26 native builds: avoid the system `key_t` typedef collision and
  use a Bionic-supported clock function for engine timestamps.

- Honor explicit graph outputs during loading; preserve node labels, layout and
  empty/quoted paths across save/load/history. Reject fractional integer node
  parameters and disconnects on nonexistent ports before changing the graph.

- Preserve empty and quoted LUT effect paths through project save/load; surface
  assigned missing LUTs as render errors.
- Preserve smooth animated pixels across split/head trim/move and undo/redo;
  carry clip-relative keys rather than translating them as sequence timestamps.
- Honor the first clip's opacity, clear history after successful load, preserve
  exact frames during scaled export, treat persisted source in-points as absolute,
  and avoid unintended trims on web tail clicks.
- Expand the bounded image interpreter's function capacity for the shared LUT
  helpers, correct the image-kernel test's text-resource stack overflow, and
  align the generic graph test with required video-resource errors.

Defects found by the new conformance suites while this work landed, all of which
had shipped or would have shipped as wrong pixels, lost edits or crashes:

- **Dragging a clip earlier in a track dropped it.** The move shifted the wrong
  range, overwriting the clip in place and leaking whatever it owned.
- **Moving a clip between tracks was a use-after-free.** It went through the
  remove path, which freed the clip's path and effect strings and then reinserted
  a copy still pointing at them.
- **A track's own layer was copied over the accumulated frame instead of
  composited**, and the track loop ran top-down, so the *bottom* track ended up
  on top.
- **The 8-bit blend fast path ran a 0..1 float-to-byte conversion over bytes that
  were already 0..255**, saturating anything above 1.0 to 255: a 25% grey solid
  rendered as white.
- **Timecode truncated the frame rate** (`30000/1001` became 29 rather than the
  nominal 30 the frames actually play at).
- **`.3dl` and `.spi` readers guessed the shape from the entry count**, so an
  8-entry 1D LUT was read as a 2x2x2 cube — a case where both readings are valid.
  The shape is now a parameter, or taken from the file name.
- **The Hald CLUT geometry was wrong.** A Hald of level L is an L³-square image
  carrying a cube of edge L², laid out as one buffer with red varying fastest.
- **Node removal used swap-with-last while its reference rewriting assumed
  shift-down**, so removing a node silently miswired the graph.
- **The topological sort under-counted indegree** when a node read the same
  source from two ports, so a `blend` with two inputs from one node looked like a
  cycle and refused to render.
- **The graph renderer's teardown walked the full node capacity over arrays
  allocated for the graph's actual node count**, freeing past the end of the
  allocation.
- **A failed call that produced a `jfx_lut_t *` or a node index cleared the
  caller's out-parameter**, orphaning a live object. Every out-handle in the new
  modules is now published only on success.

- **The desktop frontend no longer exits on launch.** It composed a single
  ImGui frame, discarded the draw data and returned. It now opens a real
  window (SDL2 + OpenGL 3.3) and runs a frame loop with input, present, and
  quit handling, driven by the vendored Dear ImGui SDL2 and OpenGL3 backends.
  The viewport image is produced by rendering the selected effect through the
  engine's backend and uploaded as a texture, the timeline advances the engine
  clock while playing, the properties panel edits the live effect, and the
  console shows captured engine events and log output. `--headless-smoke`
  remains the display-server-free path, and `--frames` / `--duration` bound a
  run so the loop cannot hang CI.
- **The engine now has a render path.** It created a backend and never used it.
  `jfx_engine_execute_bytecode` and `jfx_engine_execute_source` dispatch JBC1
  programs through the selected backend, and the CLI, web, mobile and desktop
  frontends all render through the engine rather than through private CPU
  paths. `joltfx render --backend` now selects the real execution path and
  reports the device the frame actually ran on.
- `jfx_vk_query_caps` no longer reports `cpu_fallback` unconditionally; a
  backend with a device says so, and the new `used_gpu` field reports whether
  the last dispatch really reached the device.
- The CPU-path backends name the backend in their fallback `device_name`, so a
  capability report from three adapters is distinguishable.
- `tilly_realloc` no longer returns `NULL` silently for arena, pool and stack
  allocators; it logs that an in-place grow is impossible and names the
  strategy.
- The Tilly logger no longer deadlocks when a sink logs, registers a sink or
  removes a sink, and no longer indexes its level-name table out of bounds.
  `tilly_log_init` is idempotent instead of an empty no-op, the default sink
  no longer prints `unknown:0` for calls without a source location, and a full
  sink registry reports itself rather than dropping the sink.
- mruby's allocator now enforces the script memory budget it was storing. It
  carries a per-block size header so the accounting is exact and returns to its
  starting figure after a collection.
- The Lua allocator's budget test can no longer underflow and silently disable
  the limit.
- `memory_init` reports that the memory subsystem is already owned by another
  engine instead of failing with an unrelated out-of-memory.
- The ImGui font atlas is no longer built before the renderer backend exists,
  which tripped an assertion on the first windowed frame; and the OpenGL3
  backend is shut down exactly once.
- `jfx_desktop` no longer reads a string from the frontend after destroying it
  (found by AddressSanitizer).
- `scripts/submodules-init.sh` no longer aborts partway through on a malformed
  `add_submodule` call, and its PCRE2 and wgpu-native URLs and paths now match
  `.gitmodules`.
- `CompilerWarnings.cmake` selects flags per language, so a toolchain with
  different C and C++ compilers no longer leaves C++ targets warning-free, and
  MSVC no longer receives Clang-only flags.
- CTest tests carry labels (`unit`, `conformance`, `frontend`, `integration`)
  and timeouts, so a hung worker thread or a blocked display server fails the
  run instead of stalling it.
- **Test executables are no longer compiled with `NDEBUG` in RelWithDebInfo and
  Release.** The tests are `assert()` programs and several rely on `assert()` for
  the side effect of its expression, so `NDEBUG` deleted the calls and left the
  suite asserting almost nothing. All test targets are created through
  `jfx_test_target`, which always disables `NDEBUG`, and `test_containers` has
  an `#error` guard so a regression is a build failure.
- The CLI integration script resolves its binary to an absolute path, so passing
  a relative path no longer breaks the checks that run the CLI from a temporary
  directory, and its failure messages report the real exit status.

### Added

- A backend HAL (`jfx/backend_interface.h`): one operations table that every
  backend implements, so the engine selects and drives backends without knowing
  their concrete types, and the four duplicated per-backend caps and config
  structs are now aliases of one definition.
- `jfx_engine_get_caps` and `jfx_engine_used_gpu` for capability reporting,
  and `jfx_engine_caps_t` with a size guard for ABI versioning.
- `jfx_buffer`, `jfx_texture` and `jfx_kernel`: the public API declared these
  nine functions but defined none of them, so any caller failed to link. They
  are now real engine-owned objects, and the `max_buffers`, `max_textures` and
  `max_kernels` configuration fields are enforced instead of ignored. Textures
  support RGBA8/f32/f16 storage with conversion, and kernels compile and
  dispatch through the engine's backend.
- `jfx_engine_config_t::max_worker_threads`. `max_buffers` was silently being
  used as the worker-thread count; worker sizing now has its own field and
  defaults to the hardware concurrency.
- The shared frontend contract (`frontends/common/include/jfx_frontend.h`):
  a vtable, a capability bitmask derived from that vtable, and safe dispatchers
  that turn a missing operation into `JFX_ERROR_NOT_IMPLEMENTED`. The CLI's
  `render` and the new `export` command are implemented against it by a
  headless frontend, so the CLI and the GUI frontends run the same code.
- `joltfx export` and `joltfx capabilities`. `export` renders a frame range to
  a PPM sequence; `capabilities` reports what this build actually implements
  and refuses to claim what it does not.
- `joltfx info` reads compiled JBC1 artifacts, not just source.
- `zoltan compile -o` now emits real JBC1 bytecode instead of copying its input
  back with a comment header. It is byte-identical to the Glue Layer compiler's
  output, enforced over every bundled kernel by
  `scripts/check-bytecode-parity.sh` and a CI job.
- `jfx_mobile_player_set_effect` and `jfx_mobile_player_render_rgba8`, so the
  Android and iOS surfaces have a real render to bind instead of a clock tick.
- Build options that the subsystem guides document but that did not exist:
  `JFX_BACKEND_*`, `JFX_EXT_*`, `JFX_FRONTEND_*`, `JFX_PLUGIN_HOST_BRIDGES`
  and `JFX_DESKTOP_WINDOW`. A backend that is switched off is neither built nor
  offered by the engine, and a request for it is rejected by name.
- `sanitizers` and `minimal` CMake presets, plus `unit` and `conformance` test
  presets.
- Emscripten and Android toolchain files, which `cmake/toolchains/` referenced
  as an empty directory.
- `jfx_diagnostic_format`, so the `file:line:column: message` form is defined
  once instead of per call site.
- Integration tests for the desktop frontend: `desktop_headless` and, where a
  windowing backend is available, `desktop_window`. The windowed test skips
  rather than fails on a host with no display server.

### Changed

- The Glue layer no longer depends on `jfx_core`. The compiler and the ABI
  registry sit below the engine, which is what lets the engine depend on them
  to run kernels; the previous edge was unused and blocked a real render path.
- The desktop frontend's ImGui layout persists in the platform's per-user
  config directory (`~/.config/joltfx`, `%APPDATA%\JoltFX`,
  `~/Library/Application Support/JoltFX`) instead of writing `imgui.ini` into
  the source tree.
- The host-application plugin bridges advertise no capabilities. They
  previously claimed import, export and effect registration, none of which
  exists in the shipped SDK-independent stubs.
- mruby's sandbox is capability removal (an unopened stdio/filesystem build,
  undefined process-control methods) instead of a substring blocklist on the
  script text, which both missed indirect access and rejected innocent
  identifiers containing a blocked word.
- `kernels`' effect catalog is a flat array with a linear scan instead of a
  hash map. It holds 12 immutable entries built once, so the map was indirection
  without benefit.
- Install rules ship the engine, the Joltscript layers, the backends and the
  `jfx_desktop` binary. The previous rules installed the frontend and bridge
  libraries but omitted the runtime the engine dispatches through, so a package
  could not run a kernel.
- `scripts/submodules-init.sh` only clones. It no longer takes a revision or a
  branch: every repository is cloned at whatever revision its upstream default
  branch points at, and no `git checkout` follows. Pinned versions (tags such as
  `v1.5.6`, branches such as `docking` and `Catch1.x`) were being requested but
  not honoured consistently, and pinning every dependency by name in a script is
  not a substitute for recording the revision where it is used. Nested
  dependencies are still initialized after each clone.
- `JOLTFX_BUILD_DOCS` builds a `docs` target instead of being a dead option.
- CI has job timeouts, a concurrency group, a build matrix that actually
  exercises the option combinations, cargo and ccache-style caching, and leaks
  are checked rather than disabled. The release artifact is now tested before
  it is published.

### Removed

- `scripts/scaffold.sh`. With `--force` it overwrote the whole repository,
  including the working engine, backend and CLI sources, with 1951 lines of
  stale stubs. There was no safe way to run it.
- `scripts/format.sh` no longer walks `third_party/`; it formats only
  git-tracked files, so vendored submodules cannot be rewritten.

### Known limitations

- Metal, D3D12 and WebGPU still execute through the shared validated CPU
  pipeline. They are portable, SDK-independent and honest about it, and each is
  a single function away from native dispatch, but they do not reach a GPU.
- The engine's memory, scheduler and event subsystems are process-global, so
  only one engine may be alive at a time. Concurrent independent engines and
  concurrent shutdown remain unsupported.
- The host-application bridges still need their proprietary SDKs.
- The web player still uses a JSON effect envelope rather than a signed
  `.joltpkg` container.
- The QuickJS and MicroPython extension runtimes are still absent;
  `JFX_EXT_QUICKJS` and `JFX_EXT_PYTHON` are a configure error rather than a
  silent no-op.

## [0.5.0-beta.1] - 2026-09-27

### Added
- Versioned native plugin host with lifecycle events and host adapter contracts
  for After Effects, Premiere Pro, and DaVinci Resolve.
- Dependency-free web player runtime and portable C WASM session bridge.
- Portable mobile playback core with touch gesture semantics.
- Frame timing metrics, a repeatable engine microbenchmark, beta install rules,
  and public-beta documentation.

### Changed
- Pipeline execution now skips redundant bytecode validation for immutable,
  already-validated pipeline stages while preserving validation at public API
  boundaries.

### Known limitations
- Host application SDK adapters, Emscripten export glue, Android/iOS wrappers,
  and native GPU implementations for Metal/D3D12/WebGPU remain release-blocking
  work for a general-availability release.
