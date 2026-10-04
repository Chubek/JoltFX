# AGENTS.md — JoltFX Frontends

## Overview

JoltFX frontends are user-facing applications built on top of the core engine and Zoltan toolchain. This guide covers contributing to the official desktop GUI, CLI, web player, mobile apps, and host application plugins, as well as integrating third-party frontends with the engine.

All frontends implement a common contract defined in `common/include/jfx_frontend.h`.

---

## Repository Layout

```
frontends/
  common/
    include/
      jfx_frontend.h       # Abstract frontend interface all UIs implement
      jfx_viewport.h       # Shared viewport/preview rendering
      jfx_timeline.h       # Timeline widget and playback control
      jfx_properties.h     # Property inspector widget
      jfx_export.h         # Export dialog and progress
    src/
      viewport.c           # GPU-accelerated preview rendering
      timeline.c           # Timeline scrubbing, keyframe editing
      properties.c         # Parameter editing and reflection-based UI
      export_dialog.c      # Export settings and progress UI
  desktop/
    src/
      main.cpp             # Qt6-based desktop application entry point
      mainwindow.cpp       # Main window, menu bar, docking system
      node_editor.cpp      # Visual node graph editor
      asset_browser.cpp    # Asset library and import UI
      settings.cpp         # Preferences dialog
    ui/
      mainwindow.ui        # Qt Designer files
      node_editor.ui
      settings.ui
    resources/
      icons/               # Application icons and toolbar graphics
      themes/              # Light/dark themes
    CMakeLists.txt
    README.md
  cli/
    src/
      main.c               # Command-line interface entry point
      commands.c           # Compile, render, export, verify commands
      args.c               # Argument parsing
      progress.c           # Terminal progress bar and status
    tests/
      test_cli.sh          # CLI integration tests
    CMakeLists.txt
    README.md
  web/
    src/
      main.ts              # TypeScript entry point
      player.ts            # WebAssembly player with canvas output
      controls.ts          # Playback controls, scrubbing, fullscreen
      jfx_wasm.c           # WASM bindings to core engine
      wasm_exports.h       # Exported functions for JS interop
    public/
      index.html           # Player embed example
      styles.css
    package.json
    tsconfig.json
    webpack.config.js
    README.md
  mobile/
    android/
      app/src/main/
        java/              # Kotlin/Java wrapper around native engine
        cpp/               # JNI bindings
        res/               # Android resources, layouts, icons
      build.gradle.kts    # AGP/Kotlin project with Gradle wrapper
      settings.gradle.kts
    ios/
      CMakeLists.txt      # UIKit library and application targets
      App/                # Application delegate, main and bundle plist
      Sources/             # Swift/Objective-C bindings
      Resources/           # iOS resources, icons
    shared/
      jfx_mobile.h         # Shared mobile platform interface
      touch_gestures.c     # Touch input handling
    README.md
  plugins/
    after_effects/
      src/
        jfx_ae_plugin.cpp  # After Effects plugin entry point
        ae_export.cpp      # Export AE comp as .jolt project
        ae_import.cpp      # Import .joltpkg as AE footage
      AEGP_SuiteHandler.h
      CMakeLists.txt
    premiere/
      src/
        jfx_premiere.cpp   # Premiere plugin entry point
      CMakeLists.txt
    davinci/
      src/
        jfx_davinci.cpp    # DaVinci Resolve plugin entry point
      CMakeLists.txt
    README.md
  tests/
    frontend_conformance/  # Cross-frontend conformance tests
    ui_tests/              # Automated UI testing (desktop only)
```

---

## Frontend Contract

Every frontend implements the interface in `common/include/jfx_frontend.h`:

```c
typedef struct jfx_frontend_t {
    /* Lifecycle */
    jfx_result_t (*init)(const jfx_frontend_desc_t *desc, jfx_frontend_t **out);
    void         (*shutdown)(jfx_frontend_t *frontend);
    void         (*run)(jfx_frontend_t *frontend); /* event loop */

    /* Project management */
    jfx_result_t (*open_project)(jfx_frontend_t *, const char *path);
    jfx_result_t (*save_project)(jfx_frontend_t *, const char *path);
    jfx_result_t (*close_project)(jfx_frontend_t *);

    /* Playback control */
    jfx_result_t (*play)(jfx_frontend_t *);
    jfx_result_t (*pause)(jfx_frontend_t *);
    jfx_result_t (*seek)(jfx_frontend_t *, double time_sec);
    jfx_result_t (*set_loop)(jfx_frontend_t *, bool loop);

    /* Viewport */
    jfx_result_t (*render_frame)(jfx_frontend_t *, jfx_texture_t *out);
    jfx_result_t (*resize_viewport)(jfx_frontend_t *, uint32_t width, uint32_t height);

    /* Export */
    jfx_result_t (*export_video)(jfx_frontend_t *, const jfx_export_desc_t *desc,
                                 jfx_export_progress_fn progress_cb, void *user_data);

    /* UI state */
    jfx_result_t (*get_selection)(jfx_frontend_t *, jfx_selection_t *out);
    jfx_result_t (*set_selection)(jfx_frontend_t *, const jfx_selection_t *sel);
    jfx_result_t (*get_viewport_state)(jfx_frontend_t *, jfx_viewport_state_t *out);
} jfx_frontend_t;
```

**Implementation scope:**
- **Desktop GUI**: Full interface
- **CLI**: Omits UI state and viewport functions
- **Web**: Player plus shared NLE/color/composition editor, audio mixing and encoded export
- **Mobile**: Portable editor, Android app and embeddable/standalone iOS NLE/color/composition UI
- **Plugins**: Host-independent NLE/color/composition APIs with SDK registration

---

## Ownership Rules

| Area                          | Gate Before Merge                          |
|-------------------------------|--------------------------------------------|
| `common/jfx_frontend.h`       | Frontends lead + architecture review       |
| `common/jfx_viewport.h`       | Frontends lead + one reviewer              |
| `common/jfx_timeline.h`       | Frontends lead + one reviewer              |
| `common/jfx_properties.h`     | Frontends lead + one reviewer              |
| `desktop/`                    | Desktop owner + one reviewer               |
| `cli/`                        | CLI owner + one reviewer                   |
| `web/`                        | Web owner + one reviewer                   |
| `mobile/android/`             | Android owner + one reviewer               |
| `mobile/ios/`                 | iOS owner + one reviewer                   |
| `plugins/after_effects/`      | Plugins owner + one reviewer               |
| `plugins/premiere/`           | Plugins owner + one reviewer               |
| `plugins/davinci/`            | Plugins owner + one reviewer               |
| Frontend conformance tests    | QA sign-off                                |

**Changes to `jfx_frontend.h` affect every frontend.** Coordinate across all owners before modifying it.

---

## Build and Toolchain

### Desktop (Dear ImGui + SDL2 + OpenGL)

**The Qt6 design below is not what the code does.** The desktop frontend is
implemented with Dear ImGui, SDL2 and an OpenGL 3.3 core context; see
`frontends/desktop/README.md` for the working build and run instructions, and
`PROGRESS.md` for what the current UI does and does not cover. This section is
kept as the design target, not as a description of the current build.

The working path today:

```bash
cmake --preset default
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop
```

SDL2 and OpenGL are detected with `find_package(SDL2)` and a `pkg-config sdl2`
fallback. Without them the target still builds and `--headless-smoke` still
exercises the UI, so a host with no display toolchain is not blocked.

The Qt6 path below requires Qt 6.5 or later. On macOS and Windows, download the official Qt installer. On Linux, install via package manager or build from source.

```bash
# Configure with Qt6
cmake -DJFX_FRONTEND_DESKTOP=ON \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.5.0/gcc_64 \
      ..

# Build
cmake --build . --target jfx_desktop

# Run
./build/frontends/desktop/jfx_desktop
```

For development, use Qt Creator or any IDE that supports CMake. The desktop frontend links against the core engine (`libjfx_engine.so`), Zoltan Rust library (`libzoltan.a`), and Qt6 modules (Widgets, OpenGL, Multimedia).

### CLI

The CLI has zero GUI dependencies and builds on all platforms.

```bash
cmake -DJFX_FRONTEND_CLI=ON ..
cmake --build . --target jfx_cli
./frontends/cli/jfx_cli --help
```

**CLI commands shipped today** (binary is `joltfx`, see
`frontends/cli/README.md`):
- `joltfx compile FILE [-o OUTPUT]` — validate a `.jolt` kernel, write JBC1
- `joltfx verify FILE` — validate without writing
- `joltfx effects` — list the bundled kernels
- `joltfx info EFFECT|FILE` — describe a kernel, or inspect a compiled `.jbc`
- `joltfx render [options]` — one frame to a binary PPM (P6)
- `joltfx export [options]` — a frame range to a PPM sequence
- `joltfx capabilities` — report what this build actually implements
- `joltfx nodes [NAME]` — the compositing node library, or one node's ports and
  parameters
- `joltfx lut info FILE` — format, shape, size and domain of any LUT this build
  reads (Adobe `.cube`, Autodesk `.3dl`, Sony `.spi1d`/`.spi3d`, DaVinci
  `.look`, Hald CLUT)
- `joltfx lut convert IN OUT [--as cube|3dl|spi]` — convert between LUT formats
- `joltfx lut apply LUT IMAGE OUT --mix M` — grade a still image
- `joltfx render-graph DOC.jfx -o OUT.ppm` — render a grade
- `joltfx render-sequence DOC.jfx -o PREFIX` — render an edit to a PPM sequence
- `joltfx project info DOC.jfx`, `joltfx project render DOC.jfx -o OUT.ppm` —
  inspect or render either document kind
- `joltfx nle new/edit/info/render` — create, edit, inspect and export sequences
- `joltfx compose new/edit/info/render` — typed node graphs, terminal editing and PPM export
- `joltfx scripts list/run/edit` — sandboxed Lua/mruby/QuickJS/MicroPython/Wasmtime over the shared Core editor
- `joltfx version`, `joltfx help [COMMAND]`

`DOC.jfx` is the plain-text project format in `core/include/jfx/jfx_project.h`:
a graph document is a grade, a sequence document is a non-linear edit. Both are
read and written by that one format, so a render is reproducible from a file
alone and a project can be diffed or generated by a script.

**Implemented editor surfaces:** desktop, web and Android expose NLE, Layer
Effects, Color Calibration, Color Grading and Node Compositing sections. iOS has
embeddable NLE, color and composition controllers plus a UIKit app target. Host bridges expose NLE and
composition sessions plus color descriptors/processing for SDK adapters. See `docs/nle.md` for timing,
history, state and export; see `docs/editor.md` for color commands and the
CPU execution limits. Generate color controls from `jfx_color_catalog` or the
node descriptors; `grade.*` and `calibration.*` indices are section-local.

Desktop API 1.1 presents each interface in a workspace tab, with shared
transport/clip selection/preview/history/export. Color Grading uses descriptor
dials and RGB wheels with editor 1.5 gesture transactions. Finish gestures before
tab/selection/reset/history/load changes; reacquire documents after cancel.
`workspace_ui_tests` sends real ImGui input and verifies pixels/one-step undo.

### Plugins (OpenFX)

`frontends/plugins/ofx/` implements the **host** side of the OpenFX 1.5
image-effect API and needs no proprietary host SDK: it discovers OFX bundles,
drives the plugin action lifecycle, provides the property, parameter and
image-effect suites, and returns frames as tightly packed float RGBA. Gated by
`JFX_PLUGIN_OPENFX` (default ON); the `third_party/openfx` submodule must be
present. It is a CPU, single-image, non-animated, non-tiled host and advertises
exactly that, so plugins stay inside what it can service. Public header
`jfx/jfx_ofx.h`; see `frontends/plugins/ofx/README.md` for the suite table,
handle-tagging rules and limits. Tested by `ofx_host` against a real bundle.

The desktop Plugins tab/Extensions menu and CLI expose the native/static
JoltFXPluginSDK 1.0. SDK modules register image effects/kernels, transactional
editor actions and owned events through a header-only host-service table.
Repeatable `--plugin` loads before opening a project. The plugin host owns module
lifetime; graphs, clips, history and export snapshots keep referenced modules
busy. See `docs/plugins.md`, `sdk/` and `plugin_cli_integration`. AE/Premiere/Resolve
host bridges retain their separate host-SDK adapters.

The CLI script host uses the language-neutral `jfx_extif` API. Build-time language
availability comes from the common factory; scripts share editor/event capabilities,
budgeted values and scoped resources. Load/call failures must return before project
output is opened. See `docs/extensions.md`, `extif/examples` and
`ext_conformance_cli`; keep interpreter APIs inside `extif` adapters.

NLE widgets commit through `jfx_editor_command` and refresh the shared sequence
state after edits/undo/redo/load. Reacquire borrowed model handles after resets.
Use integer-frame rendering for export and the same floored frame for preview,
playhead and split commands. Keep color selection tied to track/clip selection.
Native `nle_frontend_conformance`, host/CLI checks, desktop headless/window smoke
tests and `frontends/web` Node tests exercise these paths.

Composition widgets use `jfx_editor_graph_state` independently of active mode and
generate typed ports/inspectors from `jfx_node_catalog` / `jfx_node_kind_*`.
Commit node layout once on drag release through `node.position`; keep pan/zoom in
the UI. Node operations participate in shared bounded history (editor API 1.4).
Render an interior node with `jfx_editor_render_graph`, preserving output and
mode. Reacquire borrowed handles after resets/history. See `docs/composition.md`,
`composition_editor`, `composition_frontend_conformance` and web interaction tests.

`render` and `export` run through the shared frontend contract
(`frontends/common/include/jfx_frontend.h`) via the headless frontend, so the
CLI and the GUI frontends execute the same code, and `--backend` selects the
real execution path.

Document-based `export-video`, `export DOC.jfx`, `nle export` and `compose export`
use the shared FFmpeg snapshot jobs. Legacy effect-only `render`/`export` write
PPM. `.joltpkg` packaging and signature verification remain unimplemented.

Audio/export controls use editor 1.4 and timeline 1.2. Desktop queues shared
stereo float to SDL2, web uses WebAudio, Android uses AudioTrack and iOS uses
AVAudioEngine. Reset queues/mixer snapshots on successful edits/load, seek and
stop. Step encoded jobs incrementally from the event loop, report errors/progress,
and destroy them on cancel/session cleanup. Host SDK adapters use the same
portable mixer/export wrappers. See `docs/media.md`, media frontend conformance
and real-WASM media tests. Bundled FFmpeg builds native/WASM/Android/iOS.

### Web (WASM)

Use the repository-root Emscripten configure command in
`frontends/web/README.md`, then build `joltfx_web`. Install/build the TypeScript
package and copy the generated `.js`/`.wasm` to `frontends/web/public/`. Serve
`frontends/web/` over HTTP and open `public/index.html`; both `dist/` and `public/`
must be served. The working module is tested with Emscripten 6.0.10.

The module exposes session/color/composition functions through `cwrap`, a virtual
filesystem and a growable heap. Non-pthread modules use the serial bounded
scheduler; shared editor rendering is synchronous CPU evaluation. Preview is
RGBA on a 2D canvas and playback uses `requestAnimationFrame`. `.jfx` documents
and imported resources use the shared editor; the player effect transport is a
JSON envelope. `.joltpkg` decoding/signatures are a separate integration.

Run `npm --prefix frontends/web test` for UI/bridge unit tests. Run
`JFX_WASM_MODULE=/absolute/path/to/joltfx_web.js npm --prefix frontends/web run test:wasm`
against the actual module before changing exports, memory settings or the bridge.

### Mobile (Android)

Requires JDK 17, SDK/build tools 35, NDK 27.2.12479018 and CMake 3.22.1.
The included wrapper pins Gradle 8.9; the project pins AGP 8.7.3 and Kotlin
2.0.21. Set `ANDROID_HOME` or local `sdk.dir`; see `frontends/mobile/README.md`.

```bash
cd frontends/mobile/android
./gradlew assembleDebug

# Install to device
adb install app/build/outputs/apk/debug/app-debug.apk
```

The launch activity uses one JNI editor session for NLE, color and node
composition. `arm64-v8a`/`x86_64` native libraries build through the root CMake
project with flexible-page-size support. Preview uses the shared CPU renderer
and a Bitmap/ImageView; `Choreographer` drives playback. Project/resource paths
must be accessible to the app.

### Mobile (iOS)

Requires macOS/Xcode with the iOS SDK and CMake. Generate the repository-root
Xcode project with `CMAKE_SYSTEM_NAME=iOS`, the appropriate
`CMAKE_OSX_SYSROOT=iphonesimulator`/`iphoneos` and deployment target 14.0, using
the flags in `frontends/mobile/README.md`. Build/run the `jfx_ios` scheme; device
builds use the development team selected in Xcode.

`jfx_ios_ui` links the ARC Objective-C bridge and NLE/color/composition UIKit
controllers; `JFX_MOBILE_IOS_APP=OFF` builds it for embedding. The app delegate
opens the shared NLE controller, with navigation to color and composition.
Preview uses UIImageView/CGImage from shared CPU RGBA; playback uses CADisplayLink.

### Plugins (After Effects, Premiere, DaVinci)

Plugins link against the host application's SDK and the JoltFX core engine.

```bash
# After Effects plugin (requires After Effects SDK)
cmake -DJFX_PLUGIN_AE=ON \
      -DAE_SDK_PATH=/path/to/AfterEffectsSDK \
      ..
cmake --build . --target jfx_ae_plugin

# Install to After Effects plugins directory
cp frontends/plugins/after_effects/jfx_ae_plugin.plugin \
   "/Applications/Adobe After Effects 2024/Plug-ins/"
```

Similar steps for Premiere and DaVinci. Consult each plugin's `README.md` for SDK download instructions and installation paths.

---

## Desktop Frontend Architecture

The implemented desktop is Dear ImGui/SDL2/OpenGL with interface tabs and a
shared preview/toolbar. The Qt6 docking descriptions below are the design target;
see `desktop/README.md` for the shipped controls and tests.

### Main Window

- Menu bar: File, Edit, View, Project, Help
- Toolbar: play/pause, stop, render, export
- Dockable panels: node editor, timeline, properties, asset browser, console
- Central viewport: GPU-accelerated preview of current frame

### Node Editor

Visual graph editor for composing effects. Nodes represent kernels, buffers, textures, and parameters. Edges represent data flow. Clicking a node selects it and updates the properties panel.

- Drag from node output to node input to create edge
- Right-click to open context menu (delete, duplicate, rename)
- Double-click node to open inline parameter editor
- Copy/paste nodes across projects (serialized as JSON)
- Undo/redo for all graph operations

### Timeline

Horizontal timeline with playback controls and keyframe editing. Tracks correspond to animatable parameters. Keyframes are dragged to adjust timing, right-clicked to change interpolation (linear, ease-in, ease-out, bezier).

- Scrub by dragging playhead
- Zoom with scroll wheel
- Pan by middle-click drag
- Snap to frame boundaries (toggle with `S` key)
- Loop region selection (set in/out points with `I`/`O` keys)

### Properties Panel

Reflection-based UI that reads kernel metadata and generates appropriate widgets:
- Sliders for float/int ranges
- Color pickers for color parameters
- Dropdowns for enums
- File pickers for texture/buffer inputs
- Checkboxes for bools

When a parameter changes, the viewport re-renders the current frame. Keyframe button next to each parameter adds a keyframe at the current time.

### Asset Browser

File browser for `.jolt`, `.joltpkg`, images, videos, and audio. Drag assets into the node editor to create input nodes. Thumbnail preview for images and first frame of videos.

### Console

Log output from the engine, Zoltan compiler, and frontend. Errors, warnings, and info messages. Click an error to jump to the corresponding node or line in the source `.jolt` file.

### Export Dialog

- Output path, format (MP4, MOV, PNG sequence, GIF)
- Resolution (preset or custom)
- Frame rate (project default or custom)
- Codec settings (H.264, H.265, ProRes, VP9)
- Quality slider
- Audio export toggle
- Progress bar and cancel button

Export runs on a background thread. Progress callback updates the UI every 10 frames. Clicking cancel gracefully stops the export and cleans up partial output.

---

## CLI Frontend Architecture

The CLI is a single-threaded command-line tool. No GUI or event loop. Commands are executed sequentially and exit when done.

### Compile Command

```bash
jfx_cli compile input.jolt -o output.joltpkg --optimize
```

Invokes Zoltan to compile the `.jolt` source, then packages the result into a signed `.joltpkg` archive. The `--optimize` flag enables dead-code elimination and constant folding.

### Render Command

```bash
jfx_cli render input.joltpkg -o output.mp4 \
        --resolution 1920x1080 \
        --fps 60 \
        --codec h264 \
        --preset medium \
        --start 0.0 \
        --end 10.0
```

Loads the `.joltpkg`, initializes the engine, renders frames from start to end time, and encodes them to video using FFmpeg. A progress bar updates every frame:

```
Rendering: [████████████████████----] 80% (480/600 frames) ETA: 5s
```

### Verify Command

```bash
jfx_cli verify input.joltpkg
```

Checks package signature, validates all kernels, and runs a test render of the first frame. Exits with code 0 if valid, non-zero if invalid. Used in CI pipelines to verify packages before deployment.

### Info Command

```bash
jfx_cli info input.joltpkg
```

Prints package metadata:

```
Package: cool_effect.joltpkg
Version: 1.2.3
Author: user@example.com
Resolution: 1920x1080
Duration: 10.0s
FPS: 60
Kernels:
  - gaussian_blur (v2.0.1)
  - color_curves (v1.5.0)
Dependencies:
  - joltfx_stdlib (v2.0.0)
```

---

## Web Player Architecture

The web player is a TypeScript application that runs the JoltFX engine compiled to WebAssembly. It provides a lightweight embeddable player for `.joltpkg` files.

### Player Initialization

```typescript
import { JoltPlayer } from './player';

const canvas = document.getElementById('jolt-canvas') as HTMLCanvasElement;
const player = new JoltPlayer(canvas);

await player.load('https://example.com/effect.joltpkg');
player.play();
```

The player fetches the `.joltpkg`, decodes it in WASM, and renders frames to the canvas using WebGL or WebGPU (depending on browser support).

### Playback Controls

```typescript
player.play();
player.pause();
player.seek(5.0); // seek to 5 seconds
player.setLoop(true);

player.on('frame', (frameNumber) => {
    console.log(`Frame ${frameNumber}`);
});

player.on('end', () => {
    console.log('Playback finished');
});
```

### Embed Example

```html
<!DOCTYPE html>
<html>
<head>
    <script src="jolt-player.js"></script>
</head>
<body>
    <canvas id="jolt-canvas" width="1920" height="1080"></canvas>
    <div>
        <button onclick="player.play()">Play</button>
        <button onclick="player.pause()">Pause</button>
        <input type="range" min="0" max="10" step="0.01"
               oninput="player.seek(this.value)">
    </div>
    <script>
        const player = new JoltPlayer(document.getElementById('jolt-canvas'));
        player.load('effect.joltpkg').then(() => player.play());
    </script>
</body>
</html>
```

The player bundle (`jolt-player.js`) is ~800 KB gzipped (engine + WASM runtime). It has no dependencies and works in all modern browsers (Chrome 90+, Firefox 88+, Safari 15+, Edge 90+).

---

## Mobile Frontend Architecture

Mobile frontends provide touch editing over the same Core editor as desktop/web.
Android has timeline and composition canvases with generated inspectors; iOS has
NLE lists/frame controls, color panels and a touch composition canvas. Both
support shared bounded history, `.jfx` persistence and PPM export. See the mobile
README and the NLE/color/composition guides for the implemented controls.

### Android

`JoltPlayerActivity` loads `jfx_android_jni`, creates the session on resume,
removes frame callbacks on pause and destroys the session with the activity.
`nativeEdit` commits shared commands; JSON state refreshes timeline/graph
widgets and the color inspectors. JNI pixel arrays become a Bitmap for preview.
Worker ownership remains in the scheduler; UI playback uses Choreographer.
Graph/timeline gestures commit once on release and canvas-local pan/zoom is UI
state. App file defaults use `filesDir`.

### iOS

`JFXAppDelegate` creates one `JFXMobilePlayerBridge` and opens
`JFXNLEViewController`. The NLE controller presents `JFXColorViewController` and
`JFXCompositionViewController` using that bridge. RGBA data becomes CGImage/UIImage;
NLE display-link playback stops on disappearance or inactivity. Composition
previews are independent of output selection and restore the prior active mode
on disappearance. App project/export defaults use `Documents`.

---

## Plugin Architecture

Plugins integrate JoltFX into host applications (After Effects, Premiere, DaVinci).

### After Effects Plugin

The AE plugin is an AEGP (After Effects General Plug-in) that adds import/export menu items and a custom effect.

**Export AE Comp → .jolt:**
1. Read AE composition structure (layers, effects, keyframes)
2. Map AE effects to JoltFX kernels (when possible)
3. Generate `.jolt` source code
4. Invoke Zoltan to compile to `.joltpkg`

**Import .joltpkg → AE Footage:**
1. Decode `.joltpkg`
2. Render all frames using JoltFX engine
3. Write frames to disk as PNG sequence or video
4. Import result as AE footage item

**Custom Effect:**
A native AE effect that wraps a `.joltpkg`. The effect's parameters are populated from the package's reflection metadata. Changing a parameter re-renders the frame in AE's preview window.

### Premiere Plugin

Similar to AE: import/export menu items and a custom effect. Premiere's API is different (Premiere SDK vs. AE SDK), but the integration logic is the same.

### DaVinci Resolve Plugin

DaVinci uses a Fusion-based plugin system. The plugin exposes JoltFX kernels as Fusion tools. Users drag a JoltFX tool onto the timeline, adjust parameters in the inspector, and render in DaVinci's timeline.

---

## Viewport Rendering

All frontends share the same viewport rendering code in `common/src/viewport.c`. The viewport renders the engine's output texture to a platform-specific surface (Qt widget, HTML canvas, Android SurfaceView, iOS Metal layer).

```c
typedef struct jfx_viewport_t {
    jfx_engine_t *engine;
    jfx_texture_t *output_texture;
    uint32_t width;
    uint32_t height;
    jfx_viewport_backend_t backend; /* GL, Vulkan, Metal, WebGPU */
    void *backend_data;
} jfx_viewport_t;

jfx_result_t jfx_viewport_init(jfx_engine_t *engine, const jfx_viewport_desc_t *desc,
                               jfx_viewport_t **out);
void jfx_viewport_shutdown(jfx_viewport_t *viewport);

jfx_result_t jfx_viewport_resize(jfx_viewport_t *viewport, uint32_t width, uint32_t height);
jfx_result_t jfx_viewport_render(jfx_viewport_t *viewport);
```

The viewport blits the engine's output texture to the surface. On desktop, this is a full-screen quad with the texture sampled in a fragment shader. On mobile, the texture is copied to the native surface (Metal drawable on iOS, SurfaceTexture on Android). In the web player, the texture is read back to CPU and drawn to a 2D canvas context (WebGL/WebGPU texture → ImageData).

The viewport supports overlay widgets:
- FPS counter (top-left corner)
- Resolution display (top-right corner)
- Safe zones (action-safe, title-safe guides)
- Grid (rule of thirds, golden ratio)

Overlays are toggled in the View menu (desktop) or settings panel (mobile/web).

---

## Timeline Widget

The timeline widget in `common/src/timeline.c` provides playback control and keyframe editing. It is used by the desktop and mobile frontends (not CLI or web).

```c
typedef struct jfx_timeline_t {
    double duration_sec;
    double current_time_sec;
    double fps;
    bool loop;
    bool playing;
    jfx_keyframe_t *keyframes;
    size_t keyframe_count;
} jfx_timeline_t;

jfx_result_t jfx_timeline_init(const jfx_timeline_desc_t *desc, jfx_timeline_t **out);
void jfx_timeline_shutdown(jfx_timeline_t *timeline);

jfx_result_t jfx_timeline_play(jfx_timeline_t *timeline);
jfx_result_t jfx_timeline_pause(jfx_timeline_t *timeline);
jfx_result_t jfx_timeline_seek(jfx_timeline_t *timeline, double time_sec);
jfx_result_t jfx_timeline_set_loop(jfx_timeline_t *timeline, bool loop);

jfx_result_t jfx_timeline_add_keyframe(jfx_timeline_t *timeline, const jfx_keyframe_t *kf);
jfx_result_t jfx_timeline_remove_keyframe(jfx_timeline_t *timeline, size_t index);
jfx_result_t jfx_timeline_update_keyframe(jfx_timeline_t *timeline, size_t index,
                                          const jfx_keyframe_t *kf);
```

Keyframes store time, value, and interpolation type:

```c
typedef enum jfx_interpolation_t {
    JFX_INTERP_CONSTANT,
    JFX_INTERP_LINEAR,
    JFX_INTERP_EASE_IN,
    JFX_INTERP_EASE_OUT,
    JFX_INTERP_EASE_IN_OUT,
    JFX_INTERP_BEZIER,
} jfx_interpolation_t;

typedef struct jfx_keyframe_t {
    double time_sec;
    jfx_value_t value;
    jfx_interpolation_t interp;
    float bezier_handles[4]; /* used if interp == JFX_INTERP_BEZIER */
} jfx_keyframe_t;
```

When the timeline plays, it evaluates interpolated values for all parameters and pushes them to the engine. The engine re-renders the frame with updated parameters.

---

## Properties Panel

The properties panel in `common/src/properties.c` generates UI widgets from reflection metadata.

```c
typedef struct jfx_properties_t {
    jfx_selection_t *selection; /* currently selected node */
    jfx_param_desc_t *params;   /* reflection metadata */
    size_t param_count;
    void (*on_change)(const char *param_name, const jfx_value_t *value, void *user_data);
    void *user_data;
} jfx_properties_t;

jfx_result_t jfx_properties_init(const jfx_properties_desc_t *desc, jfx_properties_t **out);
void jfx_properties_shutdown(jfx_properties_t *props);

jfx_result_t jfx_properties_set_selection(jfx_properties_t *props, jfx_selection_t *sel);
jfx_result_t jfx_properties_render(jfx_properties_t *props); /* generates UI widgets */
```

For each parameter in the selected node:
1. Read `jfx_param_desc_t` from reflection metadata
2. Generate appropriate widget (slider, color picker, dropdown, etc.)
3. When user changes widget, invoke `on_change` callback
4. Callback updates parameter in engine and re-renders viewport

Example parameter descriptor:

```c
typedef struct jfx_param_desc_t {
    const char *name;
    const char *display_name;
    const char *description;
    jfx_value_type_t type;
    jfx_value_t default_value;
    jfx_value_t min_value;
    jfx_value_t max_value;
    bool animatable;
} jfx_param_desc_t;
```

If `animatable` is true, a keyframe button appears next to the widget. Clicking it adds a keyframe at the current timeline position.

---

## Export

Export is handled by `common/src/export_dialog.c`. The user selects output format, resolution, frame rate, and codec settings. Export runs on a background thread.

```c
typedef struct jfx_export_desc_t {
    const char *output_path;
    jfx_export_format_t format; /* MP4, MOV, PNG_SEQUENCE, GIF */
    uint32_t width;
    uint32_t height;
    double fps;
    jfx_codec_t codec;          /* H264, H265, PRORES, VP9 */
    jfx_preset_t preset;        /* ULTRAFAST, FAST, MEDIUM, SLOW, VERYSLOW */
    int quality;                /* 0-100 */
    bool export_audio;
    double start_time_sec;
    double end_time_sec;
} jfx_export_desc_t;

typedef void (*jfx_export_progress_fn)(size_t current_frame, size_t total_frames,
                                       double elapsed_sec, void *user_data);

jfx_result_t jfx_export_video(jfx_engine_t *engine, const jfx_export_desc_t *desc,
                              jfx_export_progress_fn progress_cb, void *user_data);
```

**Export workflow:**
1. Initialize FFmpeg encoder with codec and format settings
2. For each frame from start_time to end_time:
   a. Render frame with engine
   b. Read back pixel data from GPU
   c. Encode frame with FFmpeg
   d. Invoke progress callback
   e. Check for cancellation
3. Write audio track (if export_audio is true)
4. Finalize and close output file

Progress callback runs on the background thread. The UI updates a progress bar and ETA estimate. Clicking cancel sets a flag that the export thread checks every frame; when set, the thread stops encoding, closes the output file, and cleans up.

---

## Conformance Tests

Frontend conformance tests in `tests/frontend_conformance/` verify that all frontends implement the contract correctly. They cover:

- Project open/save/close
- Playback play/pause/seek/loop
- Viewport rendering and resizing
- Export to all supported formats
- Selection get/set
- Error propagation (invalid project, missing kernel, etc.)

Conformance tests run against the desktop GUI, CLI, and web player. Mobile frontends are tested manually (automated UI testing on mobile is not yet implemented).

**Add or extend a conformance test when fixing a bug that affects multiple frontends.**

---

## UI Testing (Desktop Only)

The desktop frontend has automated UI tests in `tests/ui_tests/` using Qt Test. They simulate user interactions (mouse clicks, keyboard shortcuts, drag-and-drop) and verify UI state.

```cpp
void TestNodeEditor::testCreateNode() {
    NodeEditor editor;
    QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::keyClick(&editor, Qt::Key_A); // open "Add Node" menu
    QTest::keyClick(&editor, Qt::Key_B); // select "Blur" node
    QCOMPARE(editor.nodeCount(), 1);
}

void TestTimeline::testScrub() {
    Timeline timeline;
    timeline.setDuration(10.0);
    QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(200, 50));
    QTest::mouseMove(&timeline, QPoint(400, 50));
    QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(400, 50));
    QCOMPARE(timeline.currentTime(), 5.0);
}
```

Run UI tests before submitting any desktop frontend change:

```bash
ctest --test-dir build -R ui_tests -V
```

UI tests are not required for CLI, web, or mobile frontends (they have no Qt UI).

---

## Performance Guidelines

- Keep the UI responsive during rendering. Run rendering on a background thread; update the viewport only when a frame is ready.
- Avoid blocking the main thread with heavy I/O. Load projects, export videos, and compile `.jolt` files on background threads.
- Minimize redraws. Only repaint the viewport when the current frame changes. Do not redraw on every mouse move or parameter change; debounce updates.
- Use GPU-accelerated rendering for the viewport. CPU-based software rendering is too slow for real-time preview at high resolutions.
- Profile UI responsiveness separately from engine performance. A slow UI makes the engine feel slow even if the engine is fast.

---

## Platform-Specific Notes

### Desktop (Qt)

- Use Qt's docking system for flexible window layouts. Users can drag panels into custom configurations and save layouts as presets.
- Support high-DPI displays. Set `AA_EnableHighDpiScaling` and use Qt's automatic scaling. Icons and fonts should scale with the display's DPI.
- Follow platform conventions for keyboard shortcuts (Cmd on macOS, Ctrl on Windows/Linux).
- Provide native file dialogs (Qt's `QFileDialog::native`).

### CLI

- Use ANSI escape codes for colored output (errors in red, warnings in yellow).
- Detect terminal width and wrap long lines appropriately.
- Provide `--quiet` and `--verbose` flags for minimal and detailed output.
- Exit with code 0 on success, non-zero on error. Follow POSIX conventions.

### Web (WASM)

- Initialize WASM asynchronously (`WebAssembly.instantiateStreaming`).
- Handle browser API differences (WebGL vs. WebGPU, AudioContext prefixes).
- Provide fallback for browsers without WebGPU (use WebGL2 instead).
- Limit memory usage; WASM has a 2 GB or 4 GB limit depending on the browser.
- Support drag-and-drop of `.joltpkg` files into the player.

### Mobile (Android)

- Present shared CPU RGBA frames in the preview Bitmap/ImageView; route taps
  through `performClick` and swipes through the portable gesture API.
- Respect Android lifecycle (pause rendering in `onPause`, resume in `onResume`).
- Request storage permissions before loading `.joltpkg` from external storage.
- Provide a "Share" button to export video to other apps.

### Mobile (iOS)

- Present shared CPU RGBA frames through CGImage/UIImageView; the bridge selects
  the Metal backend adapter for engine operations.
- Respect iOS lifecycle (pause in `applicationDidEnterBackground`, resume in `applicationWillEnterForeground`).
- Support Picture in Picture for video playback (iOS 14+).
- Provide a "Share" button to export video to Photos or other apps.

---

## Adding a New Frontend

1. Create a directory `frontends/<name>/` with `src/`, `include/`, and `tests/`.
2. Implement the `jfx_frontend_t` interface (or a subset, if full support is not needed).
3. Add a `CMakeLists.txt` or equivalent build script.
4. Link against the core engine (`libjfx_engine`) and Zoltan (`libzoltan`).
5. Integrate with `common/viewport.c`, `common/timeline.c`, and `common/properties.c` if applicable.
6. Port the frontend conformance tests; ensure they pass.
7. Add platform-specific build instructions to `frontends/<name>/README.md`.
8. Open a PR with architecture review gate satisfied **before** creating the branch.

---

## Security Considerations

- User projects are untrusted. Validate all input before passing to the engine.
- Do not execute arbitrary code from `.jolt` or `.joltpkg` files without verifying the package signature.
- Sandboxing is handled by the core engine; frontends must not bypass it.
- Limit export resolution and duration to prevent resource exhaustion (e.g., exporting 8K 10-hour video).
- Do not store user credentials in plaintext. Use platform keychains (macOS Keychain, Windows Credential Manager, GNOME Keyring).
- For the web player, enforce same-origin policy when loading `.joltpkg` from a URL. Do not load arbitrary remote files without CORS headers.

---

## PR Checklist

- [ ] Conformance tests pass for the affected frontend
- [ ] UI tests pass (desktop only)
- [ ] No memory leaks detected by Valgrind or ASan
- [ ] Responsive UI (no blocking on main thread)
- [ ] Export works for all supported formats
- [ ] Error messages are user-friendly (no raw engine error codes)
- [ ] Keyboard shortcuts documented in README or Help menu
- [ ] High-DPI displays supported (desktop)
- [ ] Gate requirement met (see Ownership Rules)

---

## Common Mistakes

### Blocking the Main Thread
Rendering, export, and file I/O must run on background threads. Blocking the main thread makes the UI unresponsive.

### Ignoring Platform Conventions
Cmd+C on macOS, Ctrl+C on Windows/Linux. Native file dialogs. Platform-specific keyboard shortcuts.

### Memory Leaks in UI Widgets
Qt widgets must be properly parented or manually deleted. WASM bindings must release references when no longer needed.

### No Error Handling
Every engine call can fail. Check the result code and display a user-friendly error message.

### Exposing Raw Engine Types in the UI
The UI should never directly manipulate `jfx_buffer_t` or `jfx_texture_t`. All engine interaction goes through the frontend interface.

### Hardcoded Paths
Use platform-specific directories for config, cache, and logs (`~/.config/joltfx` on Linux, `~/Library/Application Support/JoltFX` on macOS, `%APPDATA%\JoltFX` on Windows).

### No Viewport Caching
If the current frame has already been rendered, do not re-render it. Cache the last frame and only re-render when parameters change.

### Exporting Without Progress Feedback
Long exports appear frozen without a progress bar and ETA.

---

## Contacts

- **Desktop frontend**: `#joltfx-frontend-desktop`
- **CLI frontend**: `#joltfx-frontend-cli`
- **Web player**: `#joltfx-frontend-web`
- **Mobile (Android)**: `#joltfx-frontend-android`
- **Mobile (iOS)**: `#joltfx-frontend-ios`
- **Plugins**: `#joltfx-plugins`
- **Frontend architecture**: `#joltfx-frontend-core`
- **UI/UX design**: `#joltfx-design`

For questions about the frontend interface or adding a new frontend, post in `#joltfx-frontend-core`.
