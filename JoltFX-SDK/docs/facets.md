# SDK facets and capability inventory

SDK 1.3 packages the existing public contracts. All 28 headers under
`src/include/jfx` are installed and represented by the adapters below. Names in
the header column are relative to `include/jfx` unless explicitly qualified.
Include adapters as `<joltfx/sdk/NAME.h>` and link `JoltFXSDK::NAME`.

| Facet | Canonical headers | Capabilities and boundaries |
| --- | --- | --- |
| `plugins` | `jfx_plugin_sdk.h`, `jfx_plugin.h` | Host-service native RGBA effects, bounded Joltscript image effects, editor actions, owned events. Other registration capabilities may return NOT_IMPLEMENTED. |
| `module` | `jfx_plugin.h`, `jfx_plugin_sdk.h` | Plugin definitions, load/unload, static attachment and legacy lifecycle. Native modules execute trusted native code. |
| `renderer` | SDK `renderer.h` | Separate renderer ABI 1.0; opaque scene snapshots, allocator/backend services, synchronous rendering. |
| `loader` | SDK `loader.h` | Native renderer loading/static attachment and engine-free renderer/plugin metadata peek; one serialized owner thread per handle; BUSY on recursive render/close. |
| `openfx` | SDK `openfx.h`; canonical `openfx/*.h` | Standalone native OpenFX Load/Describe inspection with caller allocators. No effect instances or rendering yet; see [OpenFX](openfx.md). |
| `backend` | `backend_interface.h` | Backend HAL and bytecode dispatch contract. Actual support depends on the linked engine build and selected execution profile. |
| `frontend` | `jfx_editor.h`, `jfx_export.h` | Shared commands/history, documents, previews, snapshot exports, cancellation and progress. This is the shared engine-facing API. |
| `gui` | `jfx_editor.h`, `jfx_color.h`, `jfx_osg_viewport.h` | Transactional controls/color descriptors and trackball/culling helpers. Toolkit widgets belong to frontend implementations. |
| `facet` | `jfx_compose.h`, `jfx_color.h`, `jfx_modeling3d.h` | Composition DAGs, grading/calibration and 3D scenes, modeling, animation and rendering. |
| `libraries` | `jfx_engine.h`, `jfx_image.h`, `jfx_lut.h`, `jfx_buffer.h`, `jfx_texture.h` | Engine access, decoded image/LUT data, buffer/texture ownership and transfers. Third-party implementation types remain private. |
| `tools` | `jfx_project.h`, `jfx_modeling3d.h`, `jfx_iap.h` | Project/scene interchange and IAP1 animation/asset pack export and inspection. An IAP player is future work. |
| `audio` | `jfx_audio.h`, `jfx_automation.h`, `jfx_midi.h`, `jfx_recording.h`, `jfx_vst3.h` | Stereo mixing, MIDI notes, automation, WAV capture, insert state and native VST3 hosting. Device/native-view support is platform-dependent. |
| `timeline` | `jfx_timeline.h`, `jfx_animation.h` | Sequences/tracks/clips, transitions/effects and keyframe interpolation. Editor commands provide document history. |
| `events` | `jfx_events.h` | Typed event codes, synchronous subscriber snapshots and exact-userdata unsubscribe. Current symbols include `event_subscribe` and `event_publish`. |
| `runtime` | `jfx_engine.h`, `jfx_memory.h`, `jfx_scheduler.h`, `jfx_result.h`; `tilly/allocator.h` | Engine lifecycle, scheduler jobs and engine-managed heaps/pools/frame scratch. Release storage with the matching API. |
| `ingagi` | `ingagi/Ingagi-Module.h`, optional `.hpp` | Scalar C embedding with native callbacks; C++17 owning session wrapper. See [Ingagi](ingagi.md). |
| `joltscript` | `jfx_kernel.h`; `joltscript/{compiler,bindings,vm,value,image_program,image_task,audio_task,pipeline}.h` | JBC1 compiler/runtime and separate CPU image execution. See [Joltscript](joltscript.md). |
| `jolttype` | `jolttype/api.h` | Page/source/history, tagged dependencies, Typelets, custom output targets, WASM and frontend action descriptors. Separate `JoltFX::jolttype` runtime. See [JoltType](jolttype.md). |

The installed Joltscript headers also include `audio_io.h`, `video_io.h`,
`media_writer.h` and `color_io.h` for direct Glue media adapters. They may be
included individually when linking the Glue implementation.

## Linking and ownership

- **Engine applications:** find the full `JoltFX` CMake package and link
  `JoltFX::jfx_core` in addition to SDK facets. This brings its runtime dependencies.
- **Image/renderer modules:** use the provided `joltfx_sdk_add_plugin` or
  `joltfx_sdk_add_renderer` helpers and host-service tables. These modules need
  no direct engine linkage. Renderer and image-effect entry points are distinct.
- **Language hosts:** supply the runtime described in each language guide. The
  SDK package builds the renderer loader and OpenFX inspector archives.
- Initialize size/version fields as required by the individual headers. Use each
  subsystem's status type (`jfx_result_t`, `jolt_status_t`, `ingagi_status`).
- Borrowed editor documents must be reacquired after load/new/undo/redo. Keep
  callback code and userdata alive for their documented registration lifetime.
- Graphs, history and exports retain plugin effect descriptors; unload can return
  BUSY. Snapshots own their model/settings, while referenced media files remain
  external. Keep lifecycle and mutable document access serialized.

## Build-dependent features

Backend enums and availability reports describe build support. They do not
guarantee a usable GPU driver. The 3D API includes CPU raster, MonkeyTrace,
Vulkan, optional Filament and SDK Jolt Renderer CPU/GPU choices; shader artifact
generation in Ingagi does not enable those renderers automatically.

Encoded media requires configured codecs/FFmpeg. PNG sequences and WAV export
have paths independent of FFmpeg. VST3 native hosting is controlled by
`JFX_AUDIO_VST3`; Ingagi desktop integration by `JFX_INGAGI`; MonkeyTrace's native
CPU renderer by `JFX_MONKEYTRACE`; Filament by `JFX_FILAMENT`. Native renderer
loading is platform-dependent; static attachment serves mobile/WASM hosts.

## Wider ecosystem

The CLI, TUI, desktop, browser and mobile frontends consume shared engine APIs.
Their application/toolkit adapters are built in their own frontend packages.
Zoltan is the Rust creative-programming toolchain and an independent Joltscript
bytecode emitter; this C/C++ SDK does not ship its tool binaries.

Lua 5.4, MRuby, MicroPython, QuickJS and WAMR are separate extension runtimes
under `extif`. They use the Glue capability boundary and their own build flags;
Ingagi is a separate Go-hosted language with its own embedding ABI. The browser
JoltVM target is also separate from host-side WAMR/QuickJS.

This SDK does not create new general-purpose asset-importer, arbitrary render-pass
or diagnostics-sink registration APIs. Consult the canonical headers for actual
entry points, and the repository guides (`docs/plugins.md`, `docs/editor.md`,
`docs/composition.md`, `docs/nle.md`, `docs/modeling3d.md`, `docs/media.md`,
`docs/daw.md`, `docs/memory.md`) for subsystem workflows.
