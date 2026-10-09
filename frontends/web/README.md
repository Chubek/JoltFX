# JoltFX web player

The dependency-free TypeScript player renders WASM-provided RGBA frames to a
canvas, uses `requestAnimationFrame` for playback, supports seeking/looping,
and accepts drag-and-drop package input. It enforces a 256 MiB package limit
and rejects cross-origin package URLs; host applications should proxy trusted,
CORS-validated content through their own origin.

The player and editor canvases support 0.25x–8x workspace zoom through
`setZoom()` or Ctrl/Cmd plus mouse wheel. Zoom changes presentation size without
changing the frame dimensions returned by the WASM bridge.

`jfx_web_session` now renders bundled effects to caller-owned RGBA8 memory.
`EmscriptenJoltBridge` connects its exports to `JoltPlayer`. The beta transport
is a UTF-8 JSON envelope such as `{"effect":"brightness","parameter":0.1}`;
production `.joltpkg` decoding and signature verification should feed the same
validated effect request. The bridge also loads `.jfx` documents and exposes
editing and color metadata. Build and run the browser-independent tests:

```sh
cd frontends/web
npm install
npm test
```

## 3D modeling and animation

The named **3D Modeling & Animation** section edits the shared scene with object
selection, primitives, transforms, materials, vertex coordinates, subdivision,
principal axes, keyframes, camera and physics controls. Expanded primitives,
NURBS/metaball generator controls, cloners and Joltscript channel drivers are
shared with desktop. Drag the preview to orbit; Shift/right-drag pans, Alt-drag
rolls and the wheel zooms. Quaternion orientation supports pole traversal.
Preview/transport/save
switch to the scene document; playback uses its FPS and suppresses NLE audio.
Imported PLY meshes live in the WASM filesystem; Export PLY downloads the native
mesh output. Scene `.jfx` files embed geometry. See
[3D usage](../../docs/modeling3d.md) for commands and rendering limits.

## NLE editor

`JoltEditor` uses `NLETimeline` and the shared native sequence-state JSON for
selection, ruler scrubbing, clip dragging between tracks, edge trimming and
one-edit commits. Scroll pans; Ctrl/Cmd+scroll zooms. Fit/snapping and numeric
controls expose split, duplicate, trim, move, slip, track controls, ripple edits,
undo/redo and rational-rate sequence setup. Open/save `.jfx` files and download
an exact-frame PPM at the bridge's preview raster.

Editing shortcuts: S split; Delete delete; Shift+Delete ripple delete;
Ctrl/Cmd+Z undo; Ctrl/Cmd+Shift+Z redo. Use the toolbar for playback.
See [NLE usage and timing](../../docs/nle.md). Imported image/LUT paths refer to
the module's virtual filesystem and must be reimported in a fresh module.

## Node-based composition

`CompositionCanvas` displays native graph-state JSON with persistent node
positions and typed ports. Drag nodes or output-to-input wires; right-click an
input to disconnect. Background/middle/Alt-drag pans and the wheel zooms. The
editor supplies a searchable native catalog and generated inspectors, rename,
duplicate/reset/delete, shared undo/redo, output/interior preview, raster setup,
project I/O and PPM download. Graph-active Delete/Ctrl+D shortcuts operate on
nodes. See [composition usage](../../docs/composition.md).

`EmscriptenJoltBridge.nodeKinds()`, `.graphState()` and `.renderGraphNode()` bind
the native catalog, document and evaluator. Graph state remains available while
the sequence is active; `.sequenceDocument()` keeps clip/color inspectors
populated while the graph is active. Node tests cover port/body hit-testing, one-edit drag
commits, cancellation, mounted inspector controls and FFI validation/cleanup.

## Color editor

`JoltEditor` / `mountJoltEditor` mount independent Color Calibration and Color
Grading sections beside NLE, Layer Effects and Node Compositing. Operator controls
are generated from the native catalog; LUT files are imported into the module's
virtual filesystem. See [editor usage](../../docs/editor.md).

## Build and launch the browser editor

Requires Node.js, npm, CMake 3.20+ and an activated Emscripten toolchain. The
module is tested with Emscripten 6.0.10. Configure from the repository root:

```sh
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DJFX_BACKEND_VULKAN=OFF -DJFX_BACKEND_METAL=OFF -DJFX_BACKEND_D3D12=OFF \
  -DJFX_BACKEND_WEBGPU=ON -DJFX_FRONTEND_WEB=ON \
  -DJFX_FRONTEND_DESKTOP=OFF -DJFX_FRONTEND_MOBILE=OFF -DJFX_FRONTEND_CLI=OFF \
  -DJFX_PLUGIN_HOST_BRIDGES=OFF -DJFX_EXT_LUA=OFF -DJFX_EXT_MRUBY=OFF \
  -DJFX_COLOR_OCIO=OFF -DJFX_VIDEO_FFMPEG=ON -DJFX_MEDIA_FFMPEG_BUNDLED=ON \
  -DJOLTFX_BUILD_TESTS=OFF -DJOLTFX_BUILD_EXAMPLES=OFF
cmake --build build-web --target joltfx_web
npm --prefix frontends/web install
JFX_WASM_MODULE="$PWD/build-web/frontends/web/joltfx_web.js" \
  npm --prefix frontends/web run test:wasm
cp build-web/frontends/web/joltfx_web.{js,wasm} frontends/web/public/
npm --prefix frontends/web run build
python3 -m http.server 8080 --directory frontends/web
```

Open <http://localhost:8080/public/index.html>. Serve both `public/` and `dist/`
under that directory; the launch page imports the generated TypeScript modules.

The `joltfx_web` module factory exports session editing, color and graph APIs,
heap views and filesystem access. It has a 1-MiB stack and growable 32–256-MiB
linear memory. Its single-threaded scheduler drains tasks during engine ticks;
this build runs the synchronous CPU reference renderer and needs no cross-origin
isolation headers. The WebGPU adapter name identifies the selected backend;
graph/color evaluation uses the shared CPU path.

Transfer buffers use exported `_jfx_web_alloc` / `_jfx_web_free` over MemTKX.
Release through the matching export, never `_free`. Rebuild the native module
and TypeScript package together. The bridge copies output before releasing it,
refreshes heap views after growth, and rejects session calls after idempotent
disposal. See [memory ownership](../../docs/memory.md).

`npm test` covers UI gestures, inspectors and bridge validation with native-call
doubles. `test:wasm` executes the actual compiled module through
`EmscriptenJoltBridge`: exports/catalogs, editing/history, native pixel parity,
independent graph/sequence previews, virtual image/LUT resources, heap growth and
error propagation. Set `JFX_WASM_MODULE` to an **absolute** module path.

Imported media and LUT files live in the module's virtual filesystem. Reimport
them when reopening a project in a fresh module instance. PPM exports contain
RGB pixels. The **Audio mixing** panel exposes clip/track gain, stereo balance,
fades and clip audio enable. User-initiated playback resumes AudioContext and
schedules shared stereo mixer blocks; seek/edit/stop/dispose release queued audio.

**Encoded video export** processes one snapshot frame per animation turn, with
range, encoder, mixed-audio/silent output and cancellation controls. MP4/MOV/MKV
work with the bundled build above; WebM needs FFmpeg's external VP9/Opus encoders.
`EmscriptenJoltBridge.renderAudio()` retains a mixer until edits change its
snapshot, and `.beginVideoExport()` returns a step/cancel/dispose handle.
Real-WASM tests cover audio persistence/timing, media decoding, nonzero muxed
audio, encoded output and cancellation. See [media usage](../../docs/media.md).
The web player and editor canvases support 0.25x–8x workspace zoom through
`setZoom()` or Ctrl/Cmd plus mouse wheel. Zoom changes presentation size without
changing the frame dimensions returned by the WASM bridge.
