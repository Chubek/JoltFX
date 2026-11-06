# Future of the JoltFX SDK

Working plan for solidifying the SDK and dogfooding it inside the repository.
Keep this file current: mark items done when they land, record follow-ups in
the root `PROGRESS.md`, and bump the SDK package minor version for additive
component changes (see `AGENTS.md`).

## Current state (SDK 1.3)

- Seventeen header-only facet targets re-exporting the 28 canonical `jfx/*.h`
  contracts, plus Ingagi, Joltscript and Tilly allocator headers.
- JoltType C API 1.1 facet: staged page projects, tagged inventories, Typelets,
  target emitters, WASM modules and frontend-neutral controls. Runtime linkage is
  separate; the standalone package installs only canonical headers and guidance.
  Relocated SDK C11/C++17 checks, native callback/package/module tests and targeted
  ASan/UBSan checks pass. Desktop owns the implemented toolkit adapter.
  Final `pdf` export defaults to libharu; `pdf-podofo` retains PoDoFo as an
  alternative. The desktop progress preview continues to use PoDoFo/PDFium.
- Two implementation libraries: `JoltFXSDK::loader` (renderer-module loading,
  static attachment, serialized render/close with BUSY semantics and engine-free
  renderer/plugin metadata peek) and `JoltFXSDK::openfx` (native OpenFX inspection).
- OpenFX property, memory and message suites; lazy Load/Describe, cached copied
  metadata, failure rollback/retry, allocator ownership and duplicate-module/reentry
  guards. Canonical vendor headers are installed under `include/openfx`.
- Standalone fixture tests (`JOLTFX_SDK_BUILD_TESTS=ON`) cover OpenFX inspection
  and loader peek, including allocation failures and invalid ABI/metadata.
- Standalone, relocatable CMake package validated by
  `tests/integration/sdk/test_install.cmake`; helpers `joltfx_sdk_add_plugin`
  and `joltfx_sdk_add_renderer`.
- Dogfooding already in place: `plugins/JoltRenderer` (SDK renderer ABI,
  consumed by `src/src/modeling3d.cpp` via `jfx_jolt_renderer_entry`), the
  `jfx_sdk_gain` example plugin loaded by `test_sdk_gain`, and the CLI
  `plugins inspect/render` commands over the plugin-SDK host services.

## In progress

- **CLI loader dogfooding**: add a `plugins inspect` fast path using the tested
  peek APIs before creating an engine; retain the legacy engine path as needed.
- **OpenFX execution**: SDK 1.2 implements inspection, not rendering. The
  proposed full host contract is preserved, uninstalled, in
  `openfx/design/effect-host-draft.h`. Its intended profile remains:
  - filter context, straight full-frame RGBA8, double/integer/boolean params,
    synchronous single-threaded render; no tiles/overlays/GPU/keyframes.
  - `jfx_ofx_host_*` load/negotiate (`OfxSetHost`, `OfxGetNumberOfPlugins`,
    `OfxGetPlugin`), `jfx_ofx_effect_*` describe/instance/param/render.
  Remaining work, in order:
  1. Extend `openfx/src/ofx_host.c` with parameter, multithread (serial), clip
     and image-memory suites, descriptor cloning and instance ownership.
  2. Add DescribeInContext → CreateInstance → BeginSequenceRender → Render →
     EndSequenceRender sequencing and validated parameter/clip negotiation.
     Promote only implemented execution declarations from the design draft.
  3. Unit test with an in-tree fixture plugin built from the OpenFX headers
     (invert-style), covering negotiation, params, render, BUSY/reentry and
     error paths; register in `tests/unit/CMakeLists.txt` behind a
     non-WASM/non-cross guard.
  4. Dogfood: CLI `ofx` command (`ofx inspect MODULE`, `ofx apply MODULE
     IN OUT [name=value ...]`) mirroring `grade apply`; optionally bridge an
     OpenFX effect into the plugin-host effect registry so OFX filters appear
     as graph nodes.

## Next (ordered)

1. Finish CLI loader integration and the OpenFX execution profile above.
2. **Engine dogfooding of the loader**: `src/src/plugin_host.c` and the SDK
   loader both duplicate dlopen/LoadLibrary glue. Reuse the SDK loader's
   module-handle layer (or at least its peek) for the engine's renderer-module
   path once one exists; keep plugin_host.c's engine-owned lifecycle.
3. **IAP player**: `jfx_iap.h` documents the player as future work and
   `docs/facets.md` repeats it. Import already applies event streams; the
   remaining piece is a playback clock/evaluation helper suitable for
   frontends (`tools` facet).
4. **WASM/static parity**: renderer/plugin peek return NOT_IMPLEMENTED on
   WASM; document static-attachment equivalents and add an attach-based
   inspection helper if frontends need it.
5. **SDK examples**: add an `examples/renderer` standalone renderer module
   (the gain example covers plugins only) and an OpenFX rendering example once
   execution lands.
6. **Docs**: keep README component table, `docs/facets.md` inventory and
   `JoltFXSDKConfig.cmake.in` component list aligned on every addition; the
   install test compiles every facet in C11 and C++17 and must stay green.

## Non-goals

- No new general-purpose asset-importer, render-pass or diagnostics-sink
  registration APIs (consistent with `docs/facets.md`).
- No GUI toolkit widgets, no new language bindings, no sandboxing of native
  OpenFX/renderer/plugin binaries.
- The SDK never duplicates engine implementation or vendors third-party
  implementation types into public headers.
