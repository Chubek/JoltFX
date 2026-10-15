# Progress

## 3D viewport stability and shading (2026-10-15)

- Found a feedback loop: conditional render-progress text changes the requested
  viewport size, invalidates the worker revision and clears its display buffer.
- Added a regression that fails on the original render-status size oscillation;
  stable status height/scrollbar width now passes it. All preview renderers use
  owned scheduler snapshots and retain the prior completed frame on resize,
  stale results or failure, publishing pixels and dimensions together.
- Added viewport-local Rendered/Gouraud/Wireframe selection and Modeling3D 1.6
  CPU shading API, retaining render-options ABI and production export behavior.
- Actual OpenGL texture readback verifies stable idle frames, retention during
  pending/stale/failed renders, exact Gouraud/wireframe pixels, half-resolution
  navigation and full-quality restoration. Run this SDL test offscreen so the
  physical desktop cursor cannot interfere with injected ImGui input.
- Verification: all 18 main-build modeling/rendering, UI, frontend/CLI and
  desktop smoke checks passed. Clang Debug ASAN/UBSan (with Vulkan disabled,
  leak detection enabled) passed all 10 focused engine, shading, UI, native
  OpenGL, frontend conformance and desktop smoke checks. All four focused
  sanitizer checks also passed with MonkeyTrace/Vulkan/Filament disabled.
  Main-build window/MonkeyTrace regressions passed three consecutive runs;
  the rebuilt main desktop retains MonkeyTrace.
- New C API header compiles as strict C11; the optimized/SIMD renderer path
  passes Clang syntax checks. Existing unrelated warnings remain in other code.

## Optional Filament 3D renderer (2026-10-13)

- Implemented optional `JFX_FILAMENT` vendored SDK build, private C bridge,
  sampled mesh/normal/quaternion-camera conversion, desktop selector, and
  PNG/video renderer snapshot dispatch. Added scene/GPU and ImGui/export checks.
- Bridge C++20 syntax passes Clang `-Wall -Wextra -Werror`; private C header
  compiles independently, material embedding script compiles, and diff whitespace
  checks pass.
- Multiple server restarts erased `/tmp/opencode` build artifacts. Recreated
  `/tmp/opencode/joltfx-filament` using serial builds and reduced debug data.
- Built the SDK and bridge; the real GPU smoke check exposed desktop-vs-ES
  material mismatch on headless Linux EGL. Corrected the Linux shader profile
  and private GLES dispatch linkage. Disabled the SDK's optional parallel shader
  compiler pool to keep frame-scoped shared-context teardown ordered (Mesa
  crashed during compiler-context release with the pool enabled).
- Vendored matc successfully compiled the surface material. Modeling3D syntax
  checks also pass with Filament/no-Vulkan and optional-off/no-Vulkan defines.
- Verification complete on Linux: Clang Debug ASAN/UBSan passed `filament_scene`
  and `filament_ui_tests`, including actual GPU output, vertical orientation,
  animation, cloners/camera, exact PNG readback and lossless video snapshots.
  Desktop headless smoke passed. Seven modeling/UI/frontend/backend-fallback
  regressions passed. Both Filament tests also passed with `JFX_FILAMENT=OFF`.
  Restored the final build to `JFX_FILAMENT=ON` and reran both GPU tests successfully.
  macOS Metal runtime remains untested on this Linux host.

## MonkeyTrace desktop 3D integration (2026-10-13)

- Added native MonkeyTrace ABI 1.0 and the optional `JFX_MONKEYTRACE` CMake build,
  enabled by default on supported Linux/macOS hosts with Go. Desktop 1.9 selects
  MonkeyTrace CPU in the 3D workspace, with raster/Vulkan alternatives and ray
  sample, reflection-depth and mirror-fraction controls.
- Modeling3D 1.4 converts sampled visible mesh/cloner triangles and quaternion
  camera state into owned Ingagi ECS scenes. Render output is failure-atomic;
  native tracing uses a deadline and preserves the 4096-primitive bound.
- Preview/PNG jobs own scene clones on scheduler workers. Revision checks discard
  stale previews; navigation/playback reduce quality and idle restores it. Defer
  scheduler-draining engine ticks during these heap-owned jobs to keep UI frames
  responsive. Preview failures report raster fallback; exports fail explicitly.
- Export 1.2 copies renderer quality into video snapshots before the first step.
  Desktop video jobs serialize scheduler frame steps and defer cancel/destruction
  until the active step completes. PNG/video publication remains atomic.
- Verification: full Go suites and MonkeyTrace `go vet` passed; Go suites also
  passed with CGO disabled. Clang Debug
  ASAN/UBSan passed scene/PNG/capacity/driver/camera checks, real ImGui renderer
  controls, animated snapshot video decoded to exact RGBA, modeling tools and
  portable frontend conformance. Eight registered UI/media checks passed.
  `JFX_MONKEYTRACE=OFF` passed scene, renderer UI and modeling UI checks.
  Final ASAN/UBSan build with both `JFX_MONKEYTRACE=ON` and `JFX_INGAGI=ON`
  passed MonkeyTrace scene/UI and Ingagi tab checks; desktop headless smoke
  completed successfully. Changes are uncommitted and unpushed.
- Host recovery: a restart cleared temporary builds and exposed partial writes
  on a full root filesystem. Freed the Go build cache, repaired affected files
  and rebuilt externally. GCC 16 hit a vendored optimizer ICE; Clang's initial
  sanitizer build passed. A later vendored Clang crash cleared on a lower-
  parallelism retry; optional-off sanitizer checks passed as well.

## MonkeyTrace BVH acceleration (2026-10-13)

- Implemented a deterministic median-split, preorder-threaded BVH in owned ECS
  snapshots. CPU and Ingagi compute shader traversal are stackless; closest-hit
  ties preserve entity-ID ordering and shadow queries exit early. Conservative
  bounds include both CPU and quantized GPU geometry, with outward rounding and
  explicit parallel slab handling. Added BVH storage binding 3 and raised scene
  capacity to 4096 primitives / 8191 nodes.
- Added `many_spheres.ing`, a budgeted 1026-primitive procedural scene. Tests
  compare 2000 randomized closest-hit/shadow queries against brute force, cover
  empty/inside/parallel/planar/coincident rays, validate full-capacity skip links
  and check primitive pruning. Added an allocation-free query microbenchmark:
  on this host, 4096-sphere BVH queries took 356.7 ns vs 31820 ns brute force
  (~89x for that workload, not a full-frame speedup).
- Full-capacity browser testing caught a fixed-origin-bias precision issue at
  large world coordinates. CPU and shader now share a scale-aware secondary-ray
  offset; added a float32 regression and explicit opt-in Go browser integration.
- Verification: full Go suites passed with/without CGO; the native NovoParse
  script and final focused tagged MonkeyTrace suite passed. Actual tracer SPIR-V
  validation and Naga WGSL round trips passed. Chromium 134 / SwiftShader passed
  empty/demo/procedural/full-capacity traversal and repeat-readback tests: 17x9
  empty/demo images matched exactly, the 1026-primitive scene had mean byte
  difference 0.0343 (0.49% of channels differing by more than 2), and the
  4096-primitive large-coordinate scene had mean 0.00163 / maximum 1 byte.

## MonkeyTrace ray-tracing engine (2026-10-13)

- Implemented `ingagi/monkeytrace`: owned Ingagi ECS scene snapshots, spheres
  and triangle meshes, diffuse/emissive materials, directional hard shadows,
  bounded mirror reflections, deterministic supersampling and tone mapping.
  IngagiVM scene authoring (`demo.ing`, `LoadScene`, CLI `-scene`) creates real
  ECS handles through typed host functions with instruction/frame budgets.
  Added a portable CPU reference backend, PNG/shader/export CLI, and an Ingagi
  compute shader with a dedicated self-contained WebGPU browser host (dispatch,
  aligned readback, canvas presentation, repeat rendering and resource teardown).
- Added bounded fixed-size arrays to shader struct/buffer fields using existing
  grammar syntax. Published layout/API/run documentation and optional browser
  CPU/GPU pixel comparison coverage under `monkeytrace/scripts`.
- Verification: full Go suites passed with CGO and without CGO; the native
  NovoParse script and focused tagged shader/MonkeyTrace suites passed. Actual
  MonkeyTrace SPIR-V validates, WGSL round-trips through Naga 29.0.4 and
  `spirv-val`, and a 320x180/16-sample CPU demo PNG renders successfully.
  Chromium 134 / SwiftShader executed the exported 17x9/4-sample shader with
  exact CPU pixel parity (mean/max difference 0) and a successful second
  dispatch/readback. CLI checks also confirmed failed renders preserve existing
  output and remove temporary files. VM-authored and Go-authored demo pixels
  match; returned handles work with Ingagi's `despawn` builtin.

## Joltscript Phase 4 tests (2026-10-13)

- Completed the Phase 4 test roadmap. Added `joltscript/tests/integration/test_kernels.c`
  to compile and execute shipped color kernels (`brightness`, `contrast`,
  `opacity`, `threshold`, `grayscale`, `invert`) plus a real brightness→opacity
  pipeline over multiple pixels. Added `joltscript/tests/benchmarks/bench_compiler.c`
  as a deterministic compiler benchmark smoke test over legacy f32, typed-core
  and typed-stdlib sources. Both are wired into `tests/unit/CMakeLists.txt`, and
  `bench_compiler` carries a `benchmark` label. `JOLTSCRIPT_PLAN.md` now marks
  Phase 4 as implemented.
- Verification: main build reconfigured and passed `test_joltscript`,
  `test_pipeline`, `test_compiler`, `test_typed`, `test_stdlib`, `test_vm`,
  `test_integration`, `test_kernel_execution`, `test_conformance`,
  `test_benchmarks` and `bench_compiler`. The no-SLJIT build in
  `/tmp/opencode/joltfx-no-sljit` also passed `test_vm`,
  `test_kernel_execution` and `bench_compiler`.

## Joltscript SLJIT backend (2026-10-13)

- Added optional `third_party/sljit` integration for Joltscript JBC1 version-1
  f32 bytecode (`JFX_JOLTSCRIPT_SLJIT`, default on for native SLJIT CPU
  families, off for Emscripten/unsupported processors). `jolt_vm_t` now supports
  `auto`, `interpreter` and `sljit` backend selection; `auto` JITs with a bounded
  per-VM cache and falls back to the interpreter if executable-code allocation is
  unavailable. Version-2 typed execution remains on the portable typed VM.
- Verification: reconfigured the main build, built `jfx_sljit` and the
  Joltscript C test targets, and passed `test_compiler`, `test_typed`,
  `test_stdlib`, `test_vm`, `test_integration` and `test_conformance`. Also
  configured `/tmp/opencode/joltfx-no-sljit` with `JFX_JOLTSCRIPT_SLJIT=OFF`,
  built `test_vm`, and confirmed the no-JIT capability path passes.

## Remaining work: GPU viewport + real libosg link (2026-10-13)

- Vulkan offscreen mesh renderer (`backends/vulkan/src/vk_scene.c`,
  `jvk_scene_render` in `vk_backend.h`): transient device, offscreen
  `R8G8B8A8` + `D32` images, one graphics pipeline from GLSL compiled
  offline (`vk_scene.vert/frag` → embedded SPIR-V in
  `vk_scene_shaders.h`, `spirv-val` clean), per-draw UBOs, fence-waited
  submit, host readback. Graphics ABI subset appended to `vk_minimal.h`
  (verified value by value; one wrong constant,
  `DEPTH_STENCIL_ATTACHMENT_BIT`, was caught by the fail-closed format
  check) with the `vk_loader.c` table extended 48→67 in matching order.
  Staging caps (4096 draws, ~1M verts) fail closed to the CPU path.
- Core integration (`src/src/modeling3d.cpp`, Modeling API 1.3):
  `jfx_scene3d_render_gpu` / `jfx_scene3d_write_png_gpu` flatten visible
  instances to object-space triangles with model/normal matrices and a
  y-down VP sharing the CPU camera/focal/0.05-near. Debugging caught a
  transposed view matrix (empty frame, no Vulkan error); fixed and verified.
  CLI `3d render ... gpu` plus a desktop "GPU viewport (Vulkan)" checkbox
  (default off, per-frame CPU fallback with status note).
- Real `libosg`: vendored OSG 3.7.0 configures (with
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`) and its `osg` core target builds
  clean under GCC 16. `JFX_USE_OSG=ON` builds it isolated via
  `ExternalProject` (the `add_subdirectory` attempt collides on the
  `uninstall` target with VTK; vendored tree is git-ignored so it stays
  unpatched) and links it into `jfx_core`. Differential testing then caught
  a genuine composition-order divergence (OSG multiplies reversed vs GLM):
  fixed, and locked with reference constants both builds must satisfy.
  Verified in-tree: `have_osg=1` passes; default OFF tree restored after.
- Verification: `ctest` 10/10 (incl. new `test_vulkan_scene`: RX 580/RADV
  coverage 0.205 vs 0.203, diff fraction 0.006, on-device), manual CLI GPU
  PNG comparison, Go `iap`/`native_extension_api` suites, `git diff --check`
  clean, zero new warnings. Remaining future work: `osgViewer` embedding
  (needs a display; none here), pooled GPU device for sustained desktop
  framerates, MSAA/instancing in the offload path.

## .iap player loop: import/apply + headless CLI (2026-10-13)

- Closed the `.iap` player loop with `jfx_iap_import` (`jfx_iap.h`) plus a
  `3d.iap_import` scene command: validates the pack, requires matching
  object counts, and replaces clock/cloner layouts/key streams per object
  index on the `next` candidate, so failures leave the document and history
  untouched and success commits exactly one undoable step. Meshes, base
  transforms, materials and Joltscript drivers stay in place; names are
  informational. Decoder is shared via private `src/src/iap_decode.h`
  (asset payloads walked, not copied); `iap.cpp` now reuses it for
  info/validate.
- Headless pipeline: `joltfx iap info|validate PACK`,
  `iap export SCENE.jfx OUT.iap [ASSET...]`,
  `iap import IN.jfx PACK OUT.jfx` (`editor_commands.c`, `main.c`,
  `commands.h`, help text, `frontends/AGENTS.md`). 3D tab gained an
  "Import .iap" button next to Export.
- Made `JFX_USE_OSG=ON` safe: vendored OSG ships no generated `osg/Config`,
  so the option now warns and stays on the GLM fallback instead of breaking
  the build; documented in `docs/osg_viewport.md`. Full `libosg` linkage
  remains future work.
- Local build note: cache had `JFX_ANDROID_JNI_HOST_TESTS=ON` pointing at a
  wiped `/tmp` JDK with no system Java; reconfigured with the upstream
  default `OFF` (that test cannot run here).
- Verification: `test_iap` import round trips (exact sample equality across
  scenes, count/CRC/missing failures touch nothing, undo/redo granularity),
  `ctest -R "osg_viewport|^iap$|ingagi_tab|modeling3d"` 8/8 including the
  extended `modeling3d_cli` headless pack round trip, manual CLI end to end
  (`new` → keyed spin/cloner → export → import → renders at t=0/1.5
  differ), Go `iap` suite passes, `git diff --check` clean.

## OSG/Ingagi/.iap second pass (2026-10-13)

- Wired the OSG bridge into the production orbit path: `3d.orbit` /
  `3d.orbit_axis` in `src/src/modeling3d.cpp` now route through
  `jfx_osg_trackball_orbit` / `jfx_osg_axis_orbit`, with the legacy inline
  form as fallback for out-of-range gestures (identical semantics, so every
  frontend shares the bridge math). Fixed `M_PI` portability in
  `src/src/osg_viewport.cpp` (local `kPi`) and normalized-input validation
  in `jfx_osg_axis_orbit`.
- Viewport feel: hovered gimbal ring redraws thicker/brighter in a second
  pass (the old "hovered draws thicker" comment was dead code), and
  per-frame drag deltas clamp to 10px (≈4°/axis) so fast flicks stay smooth.
- Hardened `.iap`: locale-independent `key` line parser (no `sscanf`),
  cleaned redundant CRC read, two-object round trip in `test_iap`, Go
  `Encode` mirroring the Core writer (`math.Float32frombits` cleanup) with
  round-trip + corruption/ordering/clock/name rejection tests, and
  `ingagi/native_extension_api/joltfx_3d_test.go` executing all six shipped
  drives against a stub scene.
- Extended `engine_scene_add` to the full 17-primitive catalog
  (cube…metaball) with matching `ingagi/README.md` table; fixed the two
  pre-existing reconfigure breaks (`sdk/`, `jfx_add_plugin` guards).
- Verification: fresh `cmake` reconfigure succeeds; `test_osg_viewport`,
  `test_iap`, `test_modeling3d`, extended `test_ingagi_tab` all pass, and
  `ctest -R "osg_viewport|^iap$|ingagi_tab|modeling3d"` is 8/8 (includes UI,
  frontend-conformance and CLI suites, so the orbit change is regression
  free). Go `iap` + `native_extension_api` suites pass; all six `.ing`
  drives parse clean; `git diff --check` clean. Remaining future work: full
  `osgViewer` embedding, GPU shader linkage in the viewport, `.iap` player.

## OSG viewport blend + Ingagi 3D drive + .iap pack (2026-10-13)

- Blended `third_party/openscenegraph` into the 3D Modeling & Animation tab
  through a new opt-in bridge (`src/include/jfx/jfx_osg_viewport.h`,
  `src/src/osg_viewport.cpp`, API 1.0, `cmake/Modeling3D.cmake` `JFX_USE_OSG=OFF`
  default). Without OSG it runs an identical GLM fallback; with
  `-DJFX_USE_OSG=ON` it uses vendored `osg::Quat`/`osg::Vec3` math (no
  osgViewer link, so preview/export stay deterministic). The bridge adds
  OSG-style trackball/axis orbits, velocity damping, half-res drags via
  `jfx_osg_preview_size`, sphere/frustum cloner culling and a 14px gimbal
  pick radius. `modeling3d_panel.inc` now renders half-res while
  `scene_navigation` is true (quarters the 4x-supersampled CPU work per
  drag event, the main slow/jagged fix), shows
  `Viewport WxH | OSG headers/fallback`, and keeps camera-only history
  grouping. See `docs/osg_viewport.md`; `test_osg_viewport` covers math,
  damping, adaptive sizes, culling and pick radius.
- Wired Ingagi into the 3D tab for ECS/shader/animation/cloner work. Desktop
  adds four scalar host callbacks through shared editor history
  (`engine_scene_cloner/cloner_real/procedural/color`), a 3D-tab
  "Ingagi drive" runner (`jfx_desktop_frontend_drive_3d`) and `.iap`
  export/inspect (`jfx_desktop_frontend_export_iap/iAP_info`). New script
  library in `ingagi/stdlib` (`ecs/joltfx_scene_ecs.ing`,
  `shader/joltfx_material.ing`, `gameplay-animation-ui/joltfx_drive.ing`,
  `assets-resources/joltfx_iap.ing`) plus runnable
  `ingagi/examples/joltfx_3d_drive.ing` and `joltfx_iap_pack.ing`; all parse
  (only expected undefined-host-function diagnostics, same as
  `game_scene.ing`). `test_ingagi_tab.cpp` extended for cloner/procedural/
  color/drive/export/info paths.
- Implemented Ingagi Animation Pack (`.iap`, v1) for the animation event
  stream plus packed assets: Core `src/include/jfx/jfx_iap.h` +
  `src/src/iap.cpp` (validated export, atomic replace, CRC32, bounds mirror
  `jfx_modeling3d.h`; `test_iap` covers export/validate/corrupt-CRC/assets),
  Go mirror `ingagi/iap` for tooling/future player (`Decode/Load/Summary`
  with identical checks), format doc `docs/iap.md`, 3D-tab Export/Inspect UI
  and `docs/modeling3d.md` library row. C++→Go interop verified on a real
  pack (fps/frames/objects/keys round-trip).
- Also fixed two pre-existing build breaks that blocked reconfigure:
  root `CMakeLists.txt` guards missing `sdk/`, and `tests/unit` guards
  `jfx_add_plugin` (removed with `sdk/` in eb4adec).
- Verification: `test_osg_viewport` passes standalone; `test_iap` passes
  linked against the existing `build/bin/libjfx_core.a` vendor set;
  C++→Go `.iap` interop passes; desktop frontend syntax-checks;
  `ingagi` full Go suite passes; `go vet`/`gofmt`/`git diff --check` clean;
  prebuilt `test_modeling3d` and `test_ingagi_tab` still pass. Full CMake
  rebuild and updated `test_ingagi_tab` execution remain for CI (local
  `build/` is a stale copy from `/home/chubak-edsac/joltfx` and still
  configures that path). No `.iap` player yet, no `osgViewer` embedding,
  no GPU shader linkage in the viewport — all documented as future work.

## Ingagi opcode verification and enum payload signatures (2026-10-13)

- Finished the two pending validation items from `ingagi/AGENTS.md`. `TypeInfo`
  now carries `EnumArity` (nil means unknown, as decoded from v1 images),
  populated from `EnumDecl` payload types. Snapshot format bumped to v2 with a
  parallel variant-arity table; v1 images still decode with unknown signatures.
- `runtime/validate.go` adds `verifyOpcodeTypes`: struct/enum/closure/call
  descriptor operands, enum arities for `MakeEnum` and static `Type::Variant`
  calls, method-name checks and unused-operand hygiene. `OpBind`/`OpForEachBegin`
  have dedicated operand kinds (their pipeline/world operands travel on the
  stack, so C must be 0). Descriptors check signature consistency; `hMakeEnum`
  and `callNamed` enforce arities at runtime; snapshot values check payload
  lengths; bare globals are only created for zero-arity variants. CLI `check`
  now validates the lowered tape, so static mismatches fail without execution.
- New `runtime/verify_types_test.go` covers compiled signatures, call arities,
  descriptor references, unused operands, descriptor consistency, snapshot
  round trips with tampered payloads, and v1 backward compatibility.
- Verification: standard and CGO-disabled full Go suites pass, `go vet` clean,
  race checks for runtime/cmd pass, full NovoParse-tagged suite passes via
  `bash ingagi/scripts/test-native.sh`, and all six standalone CMake/CTest
  embedding checks pass. `gofmt`/`git diff --check` clean. The C embedding ABI
  is unchanged (still 1.0.0). GPU execution, wider native types, aggregate C
  marshalling and automatic scene associations remain future work as documented.

## Web and Android frontend development (2026-10-13)

- Inspecting the real Emscripten/Android builds, shared editor bridges, editing,
  playback and Joltscript workflows. Preserving the earlier language work.
- Initial environment: Emscripten source at `/mnt/warble/emscripten` requires
  bootstrap; LLVM is installed at `/opt/emscripten-llvm`. Root filesystem is
  full, so build/download caches use `/tmp/opencode`. ADB is installed with no
  connected device; JDK/SDK/NDK are not on PATH.
- Bootstrapped a writable Emscripten 6.0.13-git distribution and JDK 17 under
  `/tmp/opencode`. Google SDK repository requests return HTTP 404 and Gradle
  cannot resolve AGP 8.7.3 from Google Maven, blocking APK assembly here.
- Real cross-build fixes: standalone Boost and FFmpeg source overrides, early
  missing-FFmpeg diagnostics, application builds separated from VTK's unused SDK
  install dependencies, consistent native WASM C++ exceptions, scalar fallback
  when wasm SIMD is not enabled, and consolidated scene-state exports.
- Web project loading now uses owned heap UTF-8 storage rather than cwrap's
  1-MiB stack. Added allocation/failure tests and real-WASM large-project and
  typed Joltscript/regex/closure animation cases.
- Android adds system file pickers for projects, media, LUTs, Joltscript and PLY;
  bounded persistent asset copies preserve referenced files. Platform-managed
  I/O runs apart from main-thread Core calls. Native sessions/history survive
  rotation; pause stops playback and project loading refreshes 3D state too.
- A host JVM test reproduced JNI modified-UTF-8 corruption of supplementary
  characters. Standard UTF-8 conversion now preserves scripts/labels/paths and
  rejects NUL/unpaired surrogates. Project saves use serialize-then-rename rather
  than truncating the destination. Initial real-JVM bridge test and Kotlin
  storage tests pass; all Android Kotlin sources type-check against API 35.
- Verification complete for available tooling: Emscripten app build with FFmpeg
  8.0 succeeds; 14 web unit tests and all 10 actual-WASM tests pass, including
  audio/video encode/decode/cancel. A new Playwright/Firefox browser gate passes
  launch, pixel-changing typed animation, seeking, undo/redo, download and reopen.
  Scalar and `-msimd128` numeric executables both pass in Node. Nine native
  modeling/numeric/frontend/JNI checks pass. Android Kotlin compiles with
  `-Werror` against API 35 (R constants generated from resources for this host
  type-check); standalone storage tests cover bounds, Unicode, cancellation and
  provider/close failure cleanup. The real JNI host suite passes with `-Xcheck:jni`.
- JNI bridge/host objects also pass standalone ASan/UBSan linked to the existing
  native engine archives (engine archives uninstrumented; JVM leak detection and
  ASan SIGSEGV interception disabled). Native JNI final build has no compiler
  warnings; `git diff --check` passes. APK assembly/device UI remains unverified:
  SDK/NDK are absent, Google downloads/dependency resolution fail, and ADB lists
  no devices. No claim of Android packaging or on-device verification is made.
- Updated frontend/Core instructions and web/mobile build/workflow documentation.
  Build/test logs and artifacts are under `/tmp/opencode/{web-*,android-*}`.
  The staged web preview is served on port 8080 from
  `/tmp/opencode/joltfx-web-site/public/index.html`; original public WASM artifacts
  were preserved. Browser screenshot: `/tmp/opencode/joltfx-web-browser.png`.

## Joltscript reference refresh (2026-10-13)

- Updated CLI/compiler/REPL help, desktop creative and 3D guidance, and web/mobile
  animation guidance for completed Phases 1–2 (execution ABI 0.9): typed core
  language, six built-in modules and 262 exports.
- Refreshed overview, roadmap, architecture, build, creative, language/tool and
  contributor references; repaired the documentation index's language link.
  Documented exact-value APIs, typed CPU fallback, the separate image profile,
  Zoltan version-aware exports and the desktop binding-scanner/export boundaries.
  Distinguished current tool capabilities from Phase 3 semantic-tooling plans.
- Corrected example imports, fixed-arity expressions, delimiters and local names;
  added a typed function/loop/collection example and library-based animation.
  Typed examples now carry CPU metadata. All 20 complete kernels from the example,
  creative, modeling and stdlib guides compile with identical C/Rust bytecode
  (test metadata supplied where the documentation omits it). Four evaluator
  examples produce their documented values; 36 added relative links resolve.
- Verification: native CLI/compiler/REPL/desktop and affected UI targets build
  without warnings; eight UI/CLI/creative/headless CTest checks pass. Zoltan
  builds, Rust formatting passes, and five help entry points smoke-test cleanly.
  Web TypeScript build and 13 interaction tests pass; tracked web output rebuilt.
  `git diff --check` passes. Android/iOS guidance changes were source-reviewed;
  platform builds were not run for this text-only refresh.

## Phase 2 standard library completion (2026-10-13)

- Audited all six existing specification files. They contain invalid syntax and
  incorrect formulas (including smoothstep, matrix multiplication and easing).
  Replaced these with 262 checked intrinsic exports backed by the shared typed
  runtime. The six `.jolt` interfaces compile independently in C and Rust and
  generate the embedded catalogue at build time. Imports support selective and
  qualified names, ambiguity diagnostics and checked module declarations.
- Implemented math/interpolation and closure-local deterministic random; color
  spaces, gamma/temperature transforms, compositing and sample filters; points,
  vectors, row-major matrices, quaternions, splines and bounds; easing/curves and
  unsorted keyframe interpolation; immutable lists/vectors/maps/sets, higher-order
  callbacks and bounded sequence generators; Unicode-indexed strings, exact
  parsing/formatting and vendored PCRE2 UTF/property-aware regular expressions.
- Execution ABI 0.9 adds aggregate views and signature-only dynamic values while
  retaining the tagged-value struct layout. Invocations use a 16 MiB bounded
  arena and existing step limits, including regex callouts. Aggregate/string
  outputs are recursively owned by the VM, preserve prior storage on failure,
  and support aliased invocation. The REPL prints collections recursively.
- CMake and Cargo build the catalogue with Python 3 and the vendored PCRE2 archive
  with CMake. Zoltan remains offline with no registry dependencies; CC/AR/PYTHON
  and a PCRE2 CMAKE_TOOLCHAIN_FILE are configurable. Install rules include all six
  interfaces, the PCRE2 archive/exported dependency and its license.
- Verification: 11 native suites pass, 34 Rust tests pass, Clippy with warnings
  denied and Rust formatting pass, native compiler output has no diagnostics,
  and `git diff --check` is clean. The export-coverage gate checks all 262 exports
  in 298 C/Rust parity + exact/tolerant execution cases, 12 rejection cases and
  six module parity checks. The registered bytecode corpus passes 27/27; 298
  image-profile sources are explicitly not applicable. Phase 1's 20 execution/
  parity cases and 12 rejection cases also pass.
- Standalone Clang ASan/UBSan compiler/VM/stdlib and typed tests pass with leak
  detection and halt-on-UB, including aggregate lifetime/aliasing, callback
  failures, numeric/dimension edges, regex backtracking limits and successive
  step-budget boundaries. PCRE2 is sanitizer-instrumented in these runs; the
  existing Tilly archives are not. Logs/artifacts: `/tmp/opencode/phase2-*` and
  `/tmp/opencode/joltscript-{stdlib,typed}-san`.
- Updated the plan, library/language references and subsystem instructions.
  Library execution uses the typed CPU path; typed shader/HTML/WAT lowering
  retains Phase 1's documented backend boundary. General user-module loading
  is outside this built-in import mechanism. Next planned phase: Phase 3 tools.

## Phase 1 core-language completion (2026-10-13)

- Implemented all remaining Phase 1 forms/types together. Version-2 typechecked
  tree bytecode adds annotations/casts, all eleven listed primitive types plus
  unit, bounded `for`/`while`/`loop`, `break`/`continue`, named/anonymous functions,
  forward calls, annotated recursion, and lexical closures with captured mutation.
- Added execution ABI 0.8 exact tagged values, typed program/VM invocation and
  signature queries. Integer math has defined width/wrap behavior; conversions,
  Unicode and finite floating values are checked. Invocation budgets cover node/
  iteration steps, depth and lexical-cell storage. Failed calls publish nothing;
  string outputs have explicit VM ownership and lifetime.
- C/Rust independently parse and serialize version 2. Zoltan's safe bridge uses
  the same portable C typechecker/VM with Rust allocation callbacks, built offline
  by `build.rs` using a C11 compiler and archiver. Native preview/metadata and
  object export support typed kernels. Legacy version-1 emission remains intact.
- Vulkan selects CPU fallback for typed programs and reports `used_gpu=false`;
  the version-1 shader decoder rejects version 2. HTML/WAT exports explicitly
  reject typed programs before writing; typed shader/export lowering is follow-up
  backend work. `jolti` prints exact values and CLI/disassembly describe both
  bytecode versions.
- Verification: 10 native suites (language, pipeline, Vulkan and CLI), 33 Rust
  tests, Clippy with denied warnings, Rust formatting, and clean native compiler
  output. The typed ASan/UBSan suite passes, including malformed-tree mutation/
  truncation and string lifetimes; existing Tilly archives remain uninstrumented.
  Registered C/Rust parity covers 20/20 sources; 298 image-profile sources remain
  explicitly not applicable. Additional checks pass 20 Phase 1 exact-execution/
  parity cases and 12 rejection cases, plus the earlier 19 JS/assembled-Wasm
  conditional executions and 14 rejection cases.
- Updated the plan checkpoint, language/API/tool documentation and subsystem
  instructions. Next planned phase: the standard library.

## Joltscript lazy conditionals (2026-10-13)

- Implemented f32 `if`, `cond`, `when` and `unless` in both JBC1 emitters.
  Tests/arms have private declaration scopes; selected outer mutations persist.
  Unmatched cond and skipped when/unless return zero. Select/and/or stay eager.
- Execution ABI 0.7 adds absolute forward-only branches. Validation merges
  stack depth/output counts and intersects initialized locals; rejects invalid
  targets, unreachable code and incomplete output paths. VM preserves failure
  atomicity and program-size execution bounds.
- Added bounded stack-machine GLSL lowering for branching programs while
  retaining the existing straight-line path. GPU tests exercise nested branches,
  conditions, mutations and unselected/selected numerical errors.
- Zoltan preview and HTML honor branches; WAT reconstructs structured f32 `if`s.
  Corrected WAT comparison/select typing and output consumption needed for
  conditionals. Numerical results are checked; removed the fake zero-returning
  WAT pow stub in favor of explicit unsupported-operation failure.
- Verification: 19/19 registered C/Rust bytecode parity sources; 30 Rust tests;
  eight rebuilt native compiler/VM/integration/conformance/benchmark/pipeline/
  Vulkan tests pass, including real Vulkan dispatch. Clippy and formatting pass.
  Repeatable conformance script passes 19 JS/assembled-Wasm execution cases and
  14 C/Rust rejection cases. Wasmtime 49 was installed in /tmp/opencode for this
  check (download succeeded through the existing SOCKS proxy).
- Both conformance fixtures survive joltfmt round trips with identical bytecode;
  the documented full-kernel jolti conditional example evaluates to 7.
- Standalone ASan/UBSan compiler and VM suites pass with changed C sources
  instrumented and existing uninstrumented Tilly archives linked, as before.
- Next Phase 1 work: bounded loops, functions and primitive types. The bounded
  image profile and the full-language stdlib retain their existing contracts.

## Joltscript Phase 1 continuation (2026-10-12)

- Implemented f32 `let`, `var`, `set!`, scoped vector bindings, `do` and
  implicit scalar kernel sequences in matching C/Rust emitters. Initialization
  is eager and once-only; lexical shadowing, duplicate rejection and immutable
  binding checks are covered by execution/error tests.
- Added execution ABI 0.6 local opcodes (29–31), initialization/stack validation,
  VM storage, Vulkan GLSL lowering, disassembly and Zoltan preview/HTML/WAT
  lowering. Zoltan body validation now delegates to its compiler.
- Added a pinned conformance fixture: bytecode parity passes 18/18 registered
  sources; 298 image-profile sources are explicitly reported not applicable.
- Eight rebuilt native compiler/VM/integration/conformance/benchmark/pipeline/
  Vulkan checks pass. Zoltan's 28 tests, Clippy with warnings denied and formatting
  pass. Generated HTML executes the binding fixture correctly under Node.
- Standalone ASan/UBSan compiler and VM suites pass, including the 128-local
  boundary and malformed local instructions. These instrument the changed C
  sources and link existing uninstrumented Tilly archives. The relocated full
  sanitizer CMake cache was unusable. WAT lowering has generation smoke coverage;
  binary assembly/execution was unavailable because wat2wasm is not installed.
- Updated the plan checkpoint, language/API documentation and subsystem guides.
- The bounded image profile remains a separate language contract. Full primitive
  types, control flow, functions and loadable stdlib remain later Phase 1 work.

## Ingagi image descriptors and callable restoration (2026-10-12)

- Continued the existing uncommitted implementation. Added AST-independent
  descriptor checks for type kinds, duplicate fields/variants, defaults, methods,
  hook/system/entry references, events and prefab constructor arities.
- Decode rejects duplicate prefab records and map/struct keys. Both decoded and
  hand-built snapshots validate object representations, enum variants, closure
  entries, defaults and cyclic/deep aggregate values before installation.
- Fixed decoded builtin values retaining nil callable pointers. Image loading
  resolves standard/registered host callbacks recursively and copies aggregate
  storage with alias preservation; separate VM loads cannot overwrite each other's
  callback bindings. Binding failures preserve the previous VM script state.
- Added regressions for malformed descriptors/values, nested builtin round trips,
  per-VM host callbacks, cyclic snapshots, invalid defaults and duplicate keys.
- Standard and CGO-disabled full Go suites pass. Runtime race and ASan suites,
  go vet, full NovoParse-tagged suite with native/shader tools enabled and
  deterministic grammar regeneration pass.
- Full documented scope remains in progress: opcode-level type and enum payload
  signature verification, GPU execution/resource adapters, wider native types,
  aggregate C marshalling, additional shader targets/stages, automatic VM tiering
  and automatic scene-resource associations remain unfinished.

## Ingagi completion continuation (2026-10-12)

- User reconfirmed full documented scope, including planned targets and desktop
  integration. Baseline Go tests pass; implementation exceeds some stale notes.
- Added failing verifier regressions and fixed missing conditional target traversal,
  missing-argument compatibility and deferred operand-stack isolation. Full Go
  suite passes with these fixes. Dispatch also checks all handler operand reads
  against the activation base. Fixed distinct-depth '?' sites to use separate
  return epilogues, with raw/optimized closure/defer regression coverage.
- Fixed taint declaration-order dependence, lost branch/loop flows, expression
  short-circuit traversal, aggregate writes and lexical shadow leakage. Expanded
  security traversal into defaults, extern signatures, entities and shader bodies.
- Added deterministic standalone WebAssembly binary emission for the QBE scalar
  subset; exported ingagi_entry, no implicit imports/WASI. Node executes loops,
  recursion, short-circuit booleans, scopes, arithmetic and deliberate trap tests.
  CLI/native embedding share the backend selector and filesystem import resolver.
- Added context-bounded SPIRV-Cross artifact compilation for HLSL, OpenGL/ES GLSL
  and MSL. HLSL/OpenGL round trips pass glslang/spirv-val; ES syntax validates.
  Added Naga-based WGSL emission (29.0.4, matching wgpu-native's lockfile); real
  vertex/fragment/compute WGSL round trips pass Naga and spirv-val. Readable storage
  images now emit the required rgba32f qualifier, verified with glslang.
- Desktop API 1.8 adds a Game Engine workspace, bounded .ing source editing/I/O,
  owned native-ABI sessions, transactional reloads, budgeted entry calls and an
  engine_time callback. ingagi_tab, creative_tab and workspace_ui_tests pass with
  JFX_INGAGI=ON. Recovered zero-byte generated objects in the existing build and
  switched its local linker cache from crashing gold to bfd. mruby was disabled
  in this local build because its vendor checkout lacks mrb_open_allocf.
- Before server restart: portable, race and native NovoParse suites and grammar
  regeneration passed. Sources survived; /tmp/opencode tool builds were lost.
  Restored tools only for remaining/new checks. Updated support documentation.
- Full scope remains in progress: full image type/descriptor verification, GPU
  resource execution/renderer adapters, automatic VM native tiering, broader
  native types, aggregate C marshalling, remaining shader stages/formats and
  automatic ECS/scene-resource associations.
- Resumed verification: expanded source-I/O checks pass with Ingagi disabled and
  enabled; enabled creative/workspace checks pass. Target-selected WASM CLI output
  is now covered for wasm32/WASM and the full Go suite passes.
- Desktop executable smoke exposed Go c-shared's missing SONAME: correcting the
  imported Linux target to link by name, allowing RUNPATH lookup from any cwd.
- Added validated scalar engine_scene_* callbacks for shared 3D creation/removal,
  transform reads/writes, keyframes and sampling. Edits use the shared editor's
  failure-atomic commands/history; the Game Engine tab previews the shared scene.
- Scene callback integration, animation sampling, validation and shared undo pass;
  enabled ingagi/creative/workspace/headless CTests and root-cwd executable smoke
  pass after correcting the no-SONAME imported-library linkage.
- Added explicit QBE/Keystone scalar JIT with owned RW-to-RX mappings, a typed
  eight-i64 trampoline, boolean normalization and serialized Call/Close lifecycle.
  Real recursion/stack-argument/boolean/lifetime checks pass with restored native
  tools; all existing QBE/Keystone execution tests pass too. Portable full suite
  passes. Automatic VM tiering and wider native types remain unfinished.
- Latest full Go suite passes with QBE/Keystone/SPIRV-Cross/Naga execution enabled;
  full portable and ASan suites, JIT/compiler race checks and go vet pass. All six
  standalone embedding checks pass after the Linux library-linkage correction.
  Expanded desktop integration also covers ECS components carrying scene indices
  and real Game Engine viewport drawing. Optional-off tests/headless smoke pass;
  root build restored to Ingagi enabled; its four relevant CTests pass. Added
  game_scene.ing example; CLI dump verifies its bytecode lowering.
- The example check exposed zero-progress recovery in entity/system/pipeline
  bodies, causing unbounded diagnostics on declaration keywords. Added bounded
  subprocess regressions (four failed before the fix) and guaranteed recovery
  progress. Corrected a keyword variable in the example. Full standard/portable
  suites, native NovoParse frontend and ASan frontend checks pass after the fix.
- Full tagged NovoParse suite and deterministic grammar regeneration passed with
  native execution enabled before this recovery fix; no grammar artifacts changed.

## Ingagi full implementation effort (2026-10-12)

- Confirmed requested scope: all documented features, including language/module
  tooling, analyses, native/shader targets and engine integration.
- Audited current implementation: imports and shader packages contain empty
  stubs; nine semantic passes are unavailable; ten target descriptors are planned.
- Baseline Go suite passes with GOROOT=/usr/lib/go and build cache/temp files
  under /tmp/opencode (the system partition has no free space).
- Implementing module resolution/linkage first, with executable regression tests.
- Added filesystem-backed module resolution, dependency deduplication/cycle checks,
  public exports, namespace qualification and lexical binding preservation.
  CompileFile/CompileNativeFile and CLI file commands share the resolver.
- Fixed qualified expressions in call arguments and namespace loss in struct,
  event and ECS lowering; QBE now accepts resolved qualified scalar calls.
- Implemented seven formerly unavailable passes: gradual types, syntax, imports,
  ECS/query/system-order checks, shader/pipeline checks, control flow and constant
  integer overflow. Fixed scope leaks across branches/loops/match arms and added
  missing deferred/unsafe block traversal. CLI check runs available passes.
- Made source reload transactional for VM script state on initializer failure;
  source/image reload during execution is rejected. World/output/callback effects
  remain externally visible.
- Added GLSL 450 lowering and context-bounded glslang SPIR-V compilation, exposed
  through CLI shader artifacts. Real vertex/fragment/compute binaries pass
  spirv-val --target-env vulkan1.1. Unsupported syntax is diagnosed.
- Full requested scope is still in progress: security/taint analysis, full image
  stack/type verification, remaining shader languages/stages, renderer/native/JIT
  adapters, aggregate C marshalling and desktop integration remain unimplemented.
- Verification: portable CGO_ENABLED=0, race-detector and AddressSanitizer Go
  suites passed; go vet and git diff --check passed. Native NovoParse suite and
  deterministic grammar regeneration checks passed. Standalone CMake build and
  all six available CTest checks passed (C, C++, Go, Python, Rust, D); Java's
  optional compiler was unavailable. QBE/Keystone machine-execution tests remain
  skipped without their required environment/library; imported native calls are
  covered at QBE IL lowering. No C ABI layouts or statuses were changed.

## novogfx third implementation regiment (2026-10-12)

- Completed device-local weak handle interning: overlapping C resource lists
  share pointer identity and own strong references; the last owner releases
  the handle. Partial enumeration failures clean up automatically. Fresh owned
  Device::resource_snapshot queries avoid retaining reference-cache generations.
- Dumb-buffer unmap now releases mmap, and remap preserves GEM/framebuffer
  identity and contents. Rust map/unmap APIs retain ownership, fill_solid
  remaps when necessary, and lifecycle tests cover repeated unmap/remap.
- Reject cross-device atomic/page-flip handles and C integer narrowing overflow
  before queuing. Selected plane fields are validated; unselected fields are
  ignored. Atomic request creation requires enabled atomic capability.
- Corrected CRTC-sequence parsing: Rust retains the full 64-bit sequence and
  nanosecond timestamp in a distinct CrtcSequence event. C exposes it as VBLANK
  rather than a page-flip completion. Both readers use the shared checked
  parser, discard corrupt records, and clear stale event views on failures.
- Connector status/current routing and framebuffer getters query live state;
  owned getter-created CRTC snapshots include modes. C++ batch stages own
  copied mode names across source destruction and batch copy/move.
- Versioned high-level crates/CMake/IDL to 0.3.0 (ABI 300), retaining the
  independent sys version. Added migration notes, C ownership documentation,
  connector type constants, and a C++ runtime test/Makefile target.
- Verification completed via proxychains: 67 Rust library tests passed under
  AddressSanitizer; formatting, all-target/all-feature Clippy with -D warnings,
  no-default-features build, rustdoc, and production/fuzz dependency audits
  passed. The updated ASan/coverage event fuzz target processed approximately
  7.36 million inputs in a 30-second campaign without crashes.
- C/C++ runtime clients passed on amdgpu /dev/dri/card1, including resource
  pointer identity, partial-owner destruction, actual mapping residency,
  remap data preservation, overflow, and foreign-device rejection. Both clients
  also passed with ASan/UBSan and the linked Rust library ASan-instrumented
  (Rust itself is not UBSan-instrumented). All five hardware smoke tests passed.
- CMake release static build/install passed using a proxied Cargo wrapper;
  a client compiled against installed headers/static library linked and ran.
- Remaining verification limits: the nonempty C++ TEST_ONLY request reaches
  the driver but returns PERMISSION_DENIED. Applied modesets, flip completion,
  and vkms conformance remain unverified. Exact Rust 1.82 mirror installation
  failed after component download timeouts; exact-MSRV testing remains blocked.
  Full IDL conformance (including generic buffer ownership), broader typed-ID
  migration, and privileged modeset coverage remain follow-up work. All changes
  remain uncommitted and unpushed.

## novogfx second implementation regiment (2026-10-12)

### Proxied verification follow-up

- Retrying toolchain and Cargo tooling downloads with proxychains at the
  user's request. A temporary config uses SOCKS5 127.0.0.1:30301 because
  the default 0.0.0.0 endpoint initially refused connections; an HTTPS probe
  to static.rust-lang.org succeeded through the loopback endpoint.
- Added novogfx/deny.toml with MIT/Apache-2.0 licenses, duplicate-version
  rejection, and unknown registry/Git source rejection.
- Proxied Cargo fetch succeeded for the isolated fuzz workspace, and both
  targets built. To avoid the tooling install's package-cache lock, build-only
  checks use a temporary CARGO_HOME with access to the downloaded registry.
- Built coverage-instrumented ASan libFuzzer binaries using the installed
  stable compiler with RUSTC_BOOTSTRAP=1. Seeded 30-second smoke campaigns
  completed without crashes: EDID 4,045,521 executions; events 3,675,122.
  These are real libFuzzer runs with sancov instrumentation, not corpus-only
  unit checks. Proxied workspace tests also passed (60 tests).
- Installed cargo-deny 0.20.2 via proxied Cargo; `cargo deny --all-features
  check` passed advisories, bans, licenses, and sources with the new policy.
- Exact Rust 1.82/nightly component downloads stalled on static.rust-lang.org.
  Retrying 1.82 components from rsproxy.cn, still validating each archive
  against the hashes in the official Rust distribution manifest. Cargo-fuzz
  tooling installation continued independently of the completed fuzz runs.
- Installed cargo-fuzz 0.13.2 via proxied Cargo. Its normal `cargo fuzz run`
  workflow passed for both EDID and events with 10,000 inputs each, using
  RUSTC_BOOTSTRAP=1 on the existing stable compiler and the seeded corpus.
  The targets run with the tool's standard ASan/coverage instrumentation.
- Audited the isolated fuzz workspace as well. libfuzzer-sys requires NCSA
  for bundled LLVM code, so fuzz/deny.toml has a version-scoped exception for
  libfuzzer-sys@0.4.13 and the manifest pins that version. Its advisories,
  bans, licenses, and sources checks all pass; production policy is unchanged.

- Audited the first implementation against ownership and IDL contracts.
- Fixed resource-cache dangling borrows after eight hotplug generations;
  retained snapshots now survive until Device destruction, and invalidation
  serializes with cache fills. Added a 32-generation allocation-liveness test.
- Dumb buffers now own a duplicated fd, unwind through Drop exactly once,
  validate kernel pitch/size/offset before mmap, and enforce fill pixel depth.
  Added mock lifecycle failure-injection checks for create/map/ADDFB2.
- Corrected C commit flag translation, invalid flag combinations, atomic
  property index zero handling, primary-plane FB routing, missing-property
  errors, disabled MODE_ID, plane update masks/zpos, and enabled atomic caps.
- Corrected C mode-name representation and current-mode snapshots, marked
  pointer-taking Rust exports unsafe, retained C reader fds independently,
  bounded malformed event buffering, and preserved legacy page-flip cookies.
- Corrected ABI version encoding; moving high-level crates to 0.2.0 for the
  breaking mode layout and PlaneUpdate field change. MSRV is now honestly
  declared as 1.82 (unsafe extern blocks); installing that toolchain failed
  because static.rust-lang.org timed out. Exact-MSRV verification is pending.
- CMake now links libdrm and installs DSLtk and both library build variants;
  C++ atomic requests use RAII on error paths.
- Added all IDL format constants/names, NULL empty resource arrays, the vkms
  feature gate, isolated EDID/event libFuzzer targets, and full Apache-2.0 text.
- Verification: 60 Rust library tests passed; the same 60 passed with Rust
  AddressSanitizer using RUSTC_BOOTSTRAP=1. Formatting, all-target/all-feature
  Clippy with -D warnings, no-default-features build, and rustdoc passed.
- C runtime smoke checks passed both without hardware and against amdgpu
  /dev/dri/card1; the C client also passed with ASan/UBSan instrumentation
  (the linked Rust shared library was not UBSan-instrumented). C++20 DSLtk
  instantiation passed. CMake release static build/install passed, and an
  installed-header/static-library C++ client linked and ran successfully.
- All five hardware smoke tests passed on /dev/dri/card1. Default card0 is
  absent, so its five checks skipped. This is amdgpu coverage, not vkms.
  A nonempty atomic TEST_ONLY modeset reached drmModeAtomicCommit but returned
  permission denied; actual modesetting and flip completion remain unverified.
- Blocked checks: cargo-deny is absent; exact Rust 1.82 installation timed out;
  libFuzzer targets were added but not built/run (nightly/cargo-fuzz unavailable).
- Remaining conformance work: C resource handles are allocated per snapshot
  rather than reused across calls; unmap releases the borrowed view while the
  owner retains its mmap; generic buffer_free is reserved with no owning-buffer
  producer. Full C ABI conformance, typed IDs throughout high-level APIs, and
  vkms modeset/flip verification remain follow-up work. README now states these
  limits instead of claiming exact IDL conformance. Changes remain uncommitted.

## novogfx initial implementation regiment (2026-10-12)

- Scaffolded the full `novogfx/` workspace from `AGENTS.md` §2 (previously only
  `AGENTS.md`, `DSLtk.hpp`, `NovoGFX-API.idl` existed): `Cargo.toml` workspace,
  `crates/novogfx-sys` (raw `-ldrm` FFI, `drm`/`drm_mode`/`xf86drm` with no safe
  abstractions), `novogfx-core` (`Device` with `OwnedFd` + hotplug-invalidated
  resource cache, `DriverInfo`, `DeviceCaps`, typed `Error`), `novogfx-kms`
  (`Connector`/`Crtc`/`Encoder`/`Plane`/`Mode`/`Property`/`Edid` with `*Id`
  newtypes and `&Device` lifetimes), `novogfx-atomic` (`AtomicRequest` builder
  holding `&mut Device`, `CommitFlags`, `AtomicState`, blobs created/destroyed
  per-commit), `novogfx-buffer` (`DumbBuffer` CREATE→MAP→mmap→ADDFB2 with unwind
  and RMFB→munmap→DESTROY Drop, `Framebuffer`, PRIME import/export),
  `novogfx-event` (non-blocking `EventReader` with partial-read buffering,
  `Vblank`/`PageFlip`/`Hotplug`), and the `novogfx` facade (`prelude`, pinned
  versions).
- Implemented the C ABI in `capi/novogfx-c` (`src/lib.rs`, shim `novogfx-c.rs`)
  following `NovoGFX-API.idl`: `novogfx.h`, DSLtk-based `novogfx.hpp`
  (RAII + `operator|` batch DSL), `CMakeLists.txt` (Cargo-built staticlib
  import). Added `Makefile` (`build`/`test`/`test-all`/`lint`/`fmt`/`doc`/
  `c-headers`), `examples/` (`enumerate`, `dumb_buffer`, `atomic_modeset`,
  `page_flip`), `tests/integration/` (`vkms_smoke` + `common`, skips without a
  device), `README.md`, `CHANGELOG.md`, `LICENSE-MIT`/`LICENSE-APACHE`,
  `docs/properties.md`.
- crates.io was throttled (~1 KB/s), so the workspace is intentionally
  dependency-free (`std` only; hand-rolled flag/error types mirroring
  `bitflags`/`thiserror` APIs, `OwnedFd`/`AsFd` from `std` instead of `rustix`,
  direct `-ldrm` link instead of `pkg-config` crate). Documented in
  `Cargo.toml`; migrating to the external crates later is drop-in.
- Found and fixed a segfault on real hardware: `xf86drm::drmVersion` used
  `usize` for `name_len`/`date_len`/`desc_len`, but `xf86drm.h` declares them
  `int`. The oversized struct shifted every pointer field and crashed
  `drmGetVersion` on `/dev/dri/card1` (amdgpu 3.64). Fixed to `c_int` and added
  a layout regression test (`size 56`, `name` offset 16).
- Verification (offline, Linux x86-64, system libdrm 2.4.134): `cargo test
  --workspace --lib` **55/55** pass; `vkms_smoke` **5/5** pass by skip without a
  device and **5/5** pass against `/dev/dri/card1` (amdgpu: 6 CRTCs, 5
  connectors, 3 connected, 12 planes after `enable_atomic`); `cargo build
  --workspace --no-default-features` clean; `cargo fmt --check` clean; `cargo
  clippy --all-targets -- -D warnings` clean; `cargo doc --no-deps` zero
  warnings; `gcc -fsyntax-only` and `g++ -std=c++20 -fsyntax-only` accept
  `novogfx.h`/`novogfx.hpp`; `make c-headers` passes. `cargo deny` not run (tool
  absent; zero dependencies so licenses/advisories/bans are trivially clean).
  Every `unsafe` block carries a `// SAFETY:` comment.

## Vulkan shader cache no longer fails dispatch (2026-10-12)

- Fixed "Vulkan backend execution failed". The GPU path compiles generated GLSL
  to SPIR-V through glslc using files under `XDG_CACHE_HOME/joltfx` (falling back
  to `$HOME/.cache/joltfx`). `load_spv` treated that cache as a hard requirement:
  when the directory was unwritable or the filesystem was full, `fopen` failed
  and the function returned NULL with no diagnostic, so `jvk_compute_run` reported
  a generic backend failure for what was really a disk/permission condition.
- The cache is now an optimization rather than a precondition. When the cache
  root cannot be written, shader compilation degrades to scratch files under
  `TMPDIR` (default `/tmp`), keyed by pid and content hash, and those files are
  unlinked on every exit path so nothing accumulates. The two silent
  `return NULL` paths now log the actual cause and name `XDG_CACHE_HOME` and
  `TMPDIR`, per the backend rule against swallowing errors.
- Added a regression case to `test_vulkan_backend` that points `XDG_CACHE_HOME`
  at a read-only directory, asserts the GPU path still returns the same pixels,
  and restores the environment. Verified it fails with the fix reverted. The
  normal caching path is unchanged and still hits `XDG_CACHE_HOME` first.
- `vk_compute.c` and the test compile warning-free under the project's strict
  flags. Full suite: 373/374 pass; `memory_allocation_policy` fails identically
  with these changes stashed and is unrelated.

## Ingagi extension ABI, target catalog and embedding build (2026-10-09)

- Implemented all six requested language files against native-module ABI 1.0.0:
  C declarations, C++ RAII, D imports, Rust owning/raw interfaces, Python ctypes,
  and Java 22+ FFM. The Go Session and c-shared bridge implement source loading,
  scalar calls, callback registration, instruction budgets, diagnostics and
  opaque handle lifecycle. Reentrant/concurrent calls return Busy.
- Reload installs a replacement VM only after successful initialization; native
  registrations/world persist, and external initializer side effects cannot be
  rolled back. Callback buffers are synchronous borrows; Go shared libraries
  remain loaded for the process lifetime. Python/Java ownership gates protect
  callback and handle lifetime operations.
- Added twelve validated, embedded target descriptors, catalog APIs and the
  `ingagi targets` CLI. VM/QBE are implemented; ten adapters/shader emitters are
  explicitly planned. QBE artifact selection uses the catalog capability gate.
  Fixed S-expression unmatched-close handling, empty lists and nesting bounds.
- Added standalone CMake CLI/shared-library build, installation and optional
  language smoke tests; root integration is opt-in via JFX_INGAGI. Current CMake
  host support is Linux/macOS; cross builds/Windows import libraries are pending.
- After restart, all worktree sources survived; temporary builds were lost.
  Rebuilt only to verify the remaining changes. Standard and CGO-disabled suites,
  race suite, go vet and all six available CTest tests passed (C ABI, C++, Python,
  Rust, D and Go). Install was verified before restart. Java smoke test is wired
  into CMake but cannot run here because Java 22+ is absent.
- NovoParse artifacts regenerated identically. All tagged packages passed after
  correcting the new concurrency test to enter its callback through a loaded
  script (the first background run had compiled the earlier test fixture).
  The corrected native-extension package passed its targeted tagged retry.
- Clang ASAN plus UBSan Go suite passed, as did the independently instrumented
  c-shared library and C ABI callback/reentry/reload/error executable. Java
  compilation and macOS execution remain unverified in this Linux environment.
  Documentation and subsystem instructions describe ABI and target boundaries.
  Changes remain uncommitted; no commands are pending for this implementation.

## Ingagi hardening pass (2026-10-09)

- Resumed across server restarts without discarding the saved fuzz corpus.
- Added structural module validation before LoadModule/LoadDecoded installation,
  covering opcode dispatch, constant indices, branch/entry targets, negative
  counts, boolean/observer flags, nil types and bounded frame slots/arities.
  The dispatch loop also checks unknown opcodes and constant indices at runtime.
- Hardened image decoding with checked lengths/full reads, bounded collections
  and nesting, exact type/method record lengths, int32 operand checks, bounded
  entry arities, duplicate-record checks and trailing-byte rejection. Initialized
  decoded prefab maps to fix a fuzz-discovered nil-map panic.
- Aggregate/call/closure/query allocations now require enough operands in the
  current activation. Repeated arrays and local/argument storage have explicit
  bounds. A saved fuzz image previously allocated gigabytes from an aggregate
  count; another carried an enormous arity. Both now reject safely.
- Encoding uses deterministic function/type/method ordering and rejects cyclic
  or deeply nested values and captured closures that image v1 cannot preserve.
- Reload clears old globals, arities and optimization caches/profiles. Added
  regressions for reload, roundtrips, truncation, allocation failures and dispatch
  errors; saved fuzz findings remain checked in as reproducible inputs.
- Retained the grammar's required return/break/continue semicolons; corrected
  test fixtures instead of expanding handwritten-only accepted syntax. Added
  parser truncation/junk tests and parser/expression fuzz targets.
- Native Build now rejects nil/cancelled contexts, missing signatures, invalid
  entry symbols and unsupported ABI types before constructing artifacts.
- Verification: full standard, CGO-disabled and race-enabled suites passed;
  ASan/UBSan runtime/compiler/bridge checks and `go vet ./...` passed. Decoder
  fuzzing completed 520,098 executions, expression fuzzing 163,711 and source
  fuzzing 10,831, without failures in the short foreground runs.
- Full NovoParse script regenerated matching grammar artifacts and passed other
  packages, but the initial prefix sweep timed out after exercising native GLR
  recognition on every byte prefix. Changed prefix/junk recovery tests to target
  the recursive-descent AST builder directly; the full tagged frontend/runtime/
  compiler retry passed. Normal fixtures and fuzz seeds still use tagged Parse.
  Native malformed-prefix recognition cost remains a known follow-up.
- QBE generation checks passed for all five targets. Tool-dependent Keystone
  execution tests were skipped in this pass because restart removed its external
  build library; its machine-code backend was verified in the prior native-target
  work. Runtime allocation/frame/corpus regressions also passed with GOARCH=386
  and CGO disabled after widening count arithmetic to int64.
- Structural decoding fuzzing does not execute arbitrary accepted tapes.
  Validation is not full stack/type verification or a memory sandbox. Added
  support/limits/test documentation to README and subsystem instructions.

## Ingagi QBE + Keystone native target (2026-10-09)

- Implemented typed scalar QBE lowering in `ingagi/compiler/qbe.go`: `i64`/`bool`
  signatures, initialized lexical locals, direct/recursive calls, arithmetic,
  comparisons, short-circuit expressions, ternaries, assignments and structured
  control flow. Unsupported constructs receive source-positioned diagnostics.
- Added QBE subprocess code generation, a private Keystone C-ABI bridge, and an
  ELF64 object writer. CLI `build` and embedding `CompileNative` export QBE IL,
  target assembly, AMD64 objects or raw machine code with an `ingagi_entry` ABI.
  Keystone handles all generated-code assembly; the C compiler only builds test
  drivers and links objects. Object/bin output currently requires Linux + CGO and
  targets AMD64 SysV; QBE assembly generation covers all five vendored targets.
- Fixed QBE/Keystone compatibility: explicitly encode decimal operands as hex
  for Keystone's AT&T parser, and retain CET ENDBR64 using exact instruction bytes.
- Fixed ternary/postfix-`?` ambiguity in handwritten AST construction using
  memoized speculative parsing; nested ternaries and propagation are covered.
- Using vendored `third_party/qbe` and `third_party/keystone`; tool builds and
  verification artifacts live outside the source tree under `/tmp/opencode`.
- Keystone's vendor tree lacks `llvm/lib` and `llvm/utils`. Added a reproducible
  build script using a hash-verified 0.9.2 archive to supplement only missing
  directories in the external build tree. Provenance is recorded in
  `ingagi/DEPENDENCIES.md` (the repository ignores the `third_party/` directory).
- Verification: standard full-module suite with native tools enabled, portable
  CGO-disabled suite, full NovoParse-tagged suite and grammar regeneration check
  pass. Native C-ABI execution agrees with VM results for recursion, loops,
  short-circuiting, signed arithmetic, ternaries and stack arguments. Deliberate
  arithmetic traps and Keystone failure paths are exercised. Newly added trap
  and nested-ternary tests also pass under NovoParse in a targeted retry.
- ASan/UBSan bridge/compiler tests, `go vet ./...`, C bridge warning checks and
  `git diff --check` pass. The native example builds through the CLI and prints
  `33`; readelf confirms its relocatable AMD64 object and exported entry symbol.
- Updated implementation/build documentation and subsystem instructions. Full
  native ECS/shaders, floating-point/aggregate lowering, JIT loading and desktop
  integration remain future work. All Go commands used `GOROOT=/usr/lib/go`.

## Ingagi runtime recovery (2026-10-09)

- Resumed the existing Ingagi draft. Baseline `go test ./... -timeout 20s`
  reproduces closure and struct-pattern failures; `sema` does not build because
  nine referenced passes are absent. Frontend and ECS tests pass.
- Fixed inline lambda skip jumps, transitive captures (including postfix `?`),
  function-owned slots/propagation fixups/loop contexts, and the inverted
  struct-field pattern condition. Added raw/optimized nested-function regressions.
- Reworked optimizer compaction to relocate function entries and all branch and
  deferred-block targets. Jump cycles terminate during optimization; code after
  a halt is retained because it can contain independently callable entries.
- Fixed VM instruction budgets across nested calls and reset counts per external
  invocation. Errors now unwind execution stacks while retaining their traces.
  Early returns flush mutable ECS rows; failed iterations discard unfinished
  snapshots. Added budget, recovery and nested-iterator regressions.
- Restored the whole-module build: semantic defaults run the implemented draft
  scope pass; requests for the nine absent analyses return explicit E0001 errors.
  Their placeholder files remain unfinished.
- Added a real NovoParse C-runtime bridge behind `-tags novoparse`. `Parse` then
  requires scannerless recognition and `ParseConcrete` returns native syntax-tree
  JSON. Grammar regeneration is checked by `ingagi/scripts/test-native.sh`.
  Typed AST construction still uses the Go parser after recognition; the untagged
  frontend and expression parser remain handwritten. Direct CST lowering and
  complete grammar parity are pending.
- Added synchronous CGO math calls into vendored GLM and simdette. GLM handles
  matrix multiplication/projection/view/point transforms; simdette's scalar
  `vec3<double>` handles batch addition/scaling. No hardware SIMD is claimed:
  this vendor's hardware batches do not implement double precision. Portable Go
  fallbacks remain available; randomized reference/in-place/tail tests cover both.
- Added `ingagi.Compile`, a working `run/check/dump` CLI, a closure example,
  embedding/host-callback tests and `ingagi/README.md` / `ingagi/AGENTS.md` with
  accurate support boundaries. Shader codegen, full static analyses, validated
  serialization, native targets and desktop wiring are still unfinished.
- Verification: standard and CGO-disabled full-module suites pass; NovoParse
  tagged suites pass after correcting a CLI test that assumed handwritten error
  wording (targeted CLI/iterator retry passed). Generated artifacts match the
  grammar. Runtime ASan + native UBSan checks pass, including the new iterator
  test; `go vet ./...` and `git diff --check` pass. Running the CLI closure example
  prints `42`. All Go commands used `GOROOT=/usr/lib/go`; no commits were made.

## Creative Programming tab + Zoltan sketch layer (2026-10-09)

- Corrected the user's toolchain assumptions first: Emscripten exists at
  `/mnt/warble/emscripten` (6.0.13-git) but has no config/`wasm-opt`, so full
  app bundling is not runnable; no Android SDK is present (`ANDROID_HOME`
  empty); `third_party/novoparse` is a GLR parser generator, not a
  highlighter; LIEF is vendored source, not a built library. Per user answers:
  parity fix + sketch layer, object-file-first exports, side-by-side
  collapsible panes, hand-rolled highlighting (no novoparse linkage).
- Zoltan 0.2.0 -> 0.3.0: `validate.rs` now accepts every op the Glue C
  compiler implements (`> <= >= = != and or not bitwise-and/or/xor shl shr`;
  previously only `+ - * / min max pow < abs floor sqrt select`), closing a
  silent divergence. New `src/sketch.rs` (`x y t` + optional `mx my`
  convention, `@canvas`/`@fps`/`@duration` metadata, bounded CPU preview with
  checksum) and `src/stdlib.rs` (6 paste-ready fragments). New commands: `new`
  (scaffold), `run` (<=16 frames, <=320x180), `export --as obj|html|wasm`
  (minimal ELF64 with `.jolt` section / standalone Canvas page / WAT for
  `wat2wasm`), `stdlib list|show`. Added `zoltan/stdlib/` (3 valid sketches +
  README) and `docs/creative.md`.
- Fixed the bytecode-parity gate as a side effect: the 3 failing
  `examples/modeling3d/*.jolt` drivers were rejected for missing `@kernel`
  metadata (pre-existing, unrelated to ops). Added comment-only metadata
  headers (bytecode unchanged); gate now passes **17/17, 0 failed** (298
  image-profile sources correctly reported n/a).
- Desktop API 1.6.1 -> 1.7.0: appended `JFX_DESKTOP_PANEL_CREATIVE` /
  `JFX_DESKTOP_WORKSPACE_CREATIVE` (prior values untouched). New
  `creative_panel.inc` (code pad + result view, each collapsible; read-only
  syntax-color strip; Run/Animate; t/mx/my/canvas controls; obj/html/wat
  export mirroring the CLI). Preview compiles via Glue `jolt_compile` and
  evaluates per pixel with `jolt_vm_run` (the engine pipeline needs 4+N
  RGBA+param programs, so it cannot run `[x y t]` sketches); rasters bounded
  to 320x180 with dirty-flag caching. Opening a `.jolt` file loads the sketch
  buffer and selects the tab. New `jfx_desktop_frontend_creative_*` C API.
- Verification: `cargo test` 26/26, `cargo clippy -D warnings` clean,
  `cargo fmt` clean; `creative_tab` + `desktop_frontend` + `workspace_ui_tests`
  (extended with the 13th tab name) pass; `modeling3d` 5/5 pass after the
  metadata annotations; `readelf` confirms `.shstrtab`/`.jolt` sections.
  Full native/sanitizer suites and on-device/browser lanes not run in this
  pass. Emscripten bundling and Android packaging remain documented
  follow-ups; LIEF is used as a format convention (`lief.parse`-able),
  not a linked dependency.

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

### 2026-10-13 — Joltscript Phase 3 tools

- Implemented the Phase 3 tool integration pass for Joltscript's existing C tools:
  - `joltfmt` now preserves semicolon comments/metadata, removes the old `"( "`
    spacing artifact, separates top-level forms, applies 2-space indentation and
    wraps long token runs near the 100-column style target.
  - `joltdoc` now links the Joltscript compiler/execution libraries and validates
    complete kernels/imports/stdlib module interfaces through `jolt_compile`
    before generating documentation. It documents module declarations and
    `(intrinsic ...)` stdlib signatures in addition to kernels/functions.
  - `joltscript-lsp` now keeps document text, publishes compiler-backed diagnostics
    on open/change, and provides core/stdlib completion and hover data over the
    bounded stdio LSP transport.
- Added `tests/integration/joltscript/test_tools_phase3.py` and registered the
  `joltscript_tools_phase3` CTest case to cover formatter comment preservation,
  stdlib docs extraction, documentation validation failures, and LSP diagnostics/
  completion behavior.
- Updated `JOLTSCRIPT_PLAN.md`, `joltscript/AGENTS.md`, and
  `joltscript/docs/tools.md` so the repository no longer describes Phase 3 tools
  as pending/preliminary.
