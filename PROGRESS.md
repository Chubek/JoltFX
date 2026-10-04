# Progress

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

- Fixed `test_plugin_module`'s include path to use `mograph/include`, where `jfx/jfx_plugin.h` is defined.
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
  outputs last, per `mograph/AGENTS.md`. Discovery is explicit
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
