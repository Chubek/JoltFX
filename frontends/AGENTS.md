```markdown
# AGENTS.md — Frontends

## Overview

JoltFX frontends are user-facing applications built on top of the core engine and
Zoltan toolchain. This guide covers contributing to the official desktop GUI,
CLI, web player, and mobile apps, as well as integrating third-party frontends
with the engine.

---

## Repository Layout

```
frontends/
  common/
    include/
      jfx_frontend.h       # abstract frontend interface all UIs implement
      jfx_viewport.h       # shared viewport/preview rendering
      jfx_timeline.h       # timeline widget and playback control
      jfx_properties.h     # property inspector widget
    src/
      viewport.c           # GPU-accelerated preview rendering
      timeline.c           # timeline scrubbing, keyframe editing
      properties.c         # parameter editing and reflection-based UI
      export_dialog.c      # export settings and progress UI
  desktop/
    src/
      main.cpp             # Qt-based desktop application entry point
      mainwindow.cpp       # main window, menu bar, docking system
      node_editor.cpp      # visual node graph editor
      asset_browser.cpp    # asset library and import UI
      settings.cpp         # preferences dialog
    ui/
      mainwindow.ui        # Qt Designer files
      node_editor.ui
      settings.ui
    resources/
      icons/               # application icons and toolbar graphics
      themes/              # light/dark themes
    CMakeLists.txt
    README.md
  cli/
    src/
      main.c               # command-line interface entry point
      commands.c           # compile, render, export, verify commands
      args.c               # argument parsing
      progress.c           # terminal progress bar and status
    tests/
      test_cli.sh          # CLI integration tests
    CMakeLists.txt
    README.md
  web/
    src/
      main.ts              # TypeScript entry point
      player.ts            # WebAssembly player with canvas output
      controls.md
  web/
    src/
      main.ts              # TypeScript entry point
      player.ts            # WebAssembly player with canvas output
      controls.ts          # playback controls, scrubbing, fullasm/
      jfx_wasm.c           # WASM bindings to core engine
      wasm_exports.h       # exported functions for JS interop
    public/
      index.html           # player embed example
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
        res/               # Android resourcessrc/main/
        java/              # Kotlin/Java wrapper around native engine
        cpp/               # JNI bindings
        res/               # Android resources, layouts, icons
      build.gradle
      JoltFX.xcodeproj
    shared/
      jfx_mobile.h         # shared mobile platform interface
      touch_gestures.c     # touch input handling
    README.md
  plugins/
    after_effects/
      src/
        jfx_ae_plugin.cpp  # After Effects plugin entry point
        ae_export.cpp      # export AE comp as .jolt project
        ae_import.cpp      # import .joltpkg as AE footage
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
    frontend_conformance/  # cross-frontend conformance tests
    ui_tests/              # automated UI testing (desktop only)

---

## Frontend Contract

Every frontend implements the interface in `common/include/jfx_frontend.h`:

c
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

The desktop GUI, CLI, web player, and mobile apps all implement subsets of this
interface. The desktop GUI implements the full interface; the CLI omits UI state
and viewport functions; the web player omits save/export (runs read-only).

---

## Ownership Rules

| Area                          | Gate before merge                         |
|-------------------------------|-------------------------------------------|
| `common/jfx_frontend.h`       | Frontends lead + architecture review      |
| `desktop/`                    | Desktop owner + one reviewer              |
| `cli/`                        | CLI owner + one reviewer                  |
| `web/`                        | Web owner + one reviewer                  |
| `mobile/android/`             | Android owner + one reviewer              |
| `mobile/ios/`                 | iOS owner + one reviewer                  |
| `plugins/after_effects/`      | Plugins owner + one reviewer              |
| `plugins/premiere/`           | Plugins owner + one reviewer              |
| `plugins/davinci/`            | Plugins owner + one reviewer              |
| Frontend conformance tests    | QA sign-off                               |

Changes to `jfx_frontend.h` affect every frontend. Coordinate across all owners
before modifying it.

---

## Build and Toolchain

### Desktop (Qt6)

Requires Qt 6.5 or later. On macOS and Windows, download the official Qt installer.
On Linux, install via package manager or build from source.

sh
# Configure with Qt6
cmake -DJFX_FRONTEND_DESKTOP=ON \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.5.0/gcc_64 \
      ..

# Build
cmake --build . --target jfx_desktop

# Run
./frontends/desktop/jfx_desktop

For development, use Qt Creator or any IDE that supports CMake. The desktop
frontend links against the core engine (`libjfx_engine.so`), Zoltan Rust
library (`libzoltan.a`), and Qt6 modules (Widgets, OpenGL, Multimedia).

### CLI

The CLI has zero GUI dependencies and builds on all platforms.

sh
cmake -DJFX_FRONTEND_CLI=ON ..
cmake --build . --target jfx_cli
./frontends/cli/jfx_cli --help

CLI commands:
- `jfx_cli compile <input.jolt> -o <output.joltpkg>`
- `jfx_cli render <input.joltpkg> -o <output.mp4> --resolution 1920x1080 --fps 60`
- `jfx_cli verify <input.joltpkg>`
- `jfx_cli info <input.joltpkg>` (print metadata,<output.mp4> --resolution 1920x1080 --fps 60`
- `jfx_cli verify <input.joltpkg>`
- `jfx_cli info <input.joltpkg>` (print metadata, kernels, dependencies)

### Web (WASM +DJFX_TARGET_WASM=ON ..
emmake make

# Build TypeScript player
cd ..
npm install
npm run build

# Serve locally
npm run serve
# Open http://localhost:8080


The WASM module exposes a C API to JavaScript via `cwrap`. The TypeScript player
loads `.joltpkg` files, decodes them with the WASM engine, and renders frames to
a canvas element. Playback runs at the project's target FPS using
`requestAnimationFrame`.

### Mobile (Android)

Requires Android Studio 2023.1+, NDK r26+, and Gradle 8.2+.

```sh
cd frontends/mobile/android
./gradlew assembleDebug

# Install to device
adb install app/build/outputs/apk/debug/app-debug.apk
```

The Android app uses JNI to call the native engine. Touch gestures map to
timeline scrubbing, pinch-to-zoom on the viewport, and parameter adjustment.

### Mobile (iOS)

Requires Xcode 15+ and macOS 13+.

```sh
cd frontends/mobile/ios
open JoltFX.xcodeproj
# Build and run in Xcode simulator or device
```

The iOS app uses Swift/Objective-C bindings to the native engine. Metal rendering
is used for viewport output.

### Plugins (After Effects, Premiere, DaVinci)

Plugins link against the host application's SDK and the JoltFX core engine.

```sh
# After Effects plugin (requires After Effects SDK)
cmake -DJFX_PLUGIN_AE=ON \
      -DAE_SDK_PATH=/path/to/AfterEffectsSDK \
      ..
cmake --build . --target jfx_ae_plugin

# Install to After Effects plugins directory
cp frontends/plugins/after_effects/jfx_ae_plugin.plugin \
   "/Applications/Adobe After Effects 2024/Plug-ins/"
```

Similar steps for Premiere and DaVinci. Consult each plugin's `README.md` for
SDK download instructions and installation paths.

---

## Desktop Frontend Architecture

The desktop frontend is a Qt6-based application with a multi-window docking
interface. Main components:

### Main Window

- Menu bar: File, Edit, View, Project, Help
- Toolbar: play/pause, stop, render, export
- Dockable panels: node editor, timeline, properties, asset browser, console
- Central viewport: GPU-accelerated preview of current frame

### Node Editor

Visual graph editor for composing effects. Nodes represent kernels, buffers,
textures, and parameters. Edges represent data flow. Clicking a node selects
it and updates the properties panel.

- Drag from node output to node input to create edge
- Right-click to open context menu (delete, duplicate, rename)
- Double-click node to open inline parameter editor
- Copy/paste nodes across projects (serialized as JSON)
- Undo/redo for all graph operations

### Timeline

Horizontal timeline with playback controls and keyframe editing. Tracks
correspond to animatable parameters. Keyframes are dragged to adjust timing,
right-clicked to change interpolation (linear, ease-in, ease-out, bezier).

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

When a parameter changes, the viewport re-renders the current frame. Keyframe
button next to each parameter adds a keyframe at the current time.

### Asset Browser

File browser for `.jolt`, `.joltpkg`, images, videos, and audio. Drag assets
into the node editor to create input nodes. Thumbnail preview for images and
first frame of videos.

### Console

Log output from the engine, Zoltan compiler, and frontend. Errors, warnings,
and info messages. Click an error to jump to the corresponding node or line in
the source `.jolt` file.

### Export Dialog

- Output path, format (MP4, MOV, PNG sequence, GIF)
- Resolution (preset or custom)
- Frame rate (project default or custom)
- Codec settings (H.264, H.265, ProRes, VP9)
- Quality slider
- Audio export toggle
- Progress bar and cancel button

Export runs on a background thread. Progress callback updates the UI every 10
frames. Clicking cancel gracefully stops the export and cleans up partial output.

---

## CLI Frontend Architecture

The CLI is a single-threaded command-line tool. No GUI or event loop. Commands
are executed sequentially and exit when done.

### Compile Command

```sh
jfx_cli compile input.jolt -o output.joltpkg --optimize
```

Invokes Zoltan to compile the `.jolt` source, then packages the result into a
signed `.joltpkg` archive. The `--optimize` flag enables dead-code elimination
and constant folding.

### Render Command

```sh
jfx_cli render input.joltpkg -o output.mp4 \
        --resolution 1920x1080 \
        --fps 60 \
        --codec h264 \
        --preset medium \
        --start 0.0 \
        --end 10.0
```

Loads the `.joltpkg`, initializes the engine, renders frames from start to end
time, and encodes them to video using FFmpeg. A progress bar updates every
frame:

Rendering: [████████████████████----] 80% (480/600 frames) ETA: 5s


### Verify Command

```sh
jfx_cli verify input.joltpkg
```

Checks package signature, validates all kernels, and runs a test render of the
first frame. Exits with code 0 if valid, non-zero if invalid. Used in CI
pipelines to verify packages before deployment.

### Info Command

```sh
jfx_cli info input.joltpkg
```

Prints package metadata:

Package: cool_effect.joltpkg
Version: 1.2 CI
pipelines to verify packages before deployment.

### Info Command

sh
jfx_cli info input.joltpkg

Prints package metadata:


Package: cool_effect.joltpkg
Version: 1.2.3
Author: user@example.com
Resolution: 1920x1080
Duration1.0)
  - ...
Dependencies:
  - joltfx_stdlib (v2.0.0)

---

## Web Player Architecture

The web player is a TypeScript application that runs the JoltFX engine compiled
to WebAssembly. It provides a lightweight embeddable player for `.joltpkg` files.

### Player Initialization

typescript
import { JoltPlayer } from './player';

const canvas = document.getElementById('jolt-canvas') as HTMLCanvasElement;
const player = new JoltPlayer(canvas);

await player.load('https://example.com/effect.joltpkg');
player.play();

The player fetches the `.joltpkg`, decodes it in WASM, and renders frames to the
canvas using WebGL or WebGPU (depending on browser support).

### Playback Controls

typescript
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

### Embed Example

html
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

The player bundle (`jolt-player.js`) is ~800 KB gzipped (engine + WASM runtime).
It has no dependencies and works in all modern browsers (Chrome 90+, Firefox 88+,
Safari 15+, Edge 90+).

---

## Mobile Frontend Architecture

Mobile frontends provide touch-based interaction with the engine. The UI is
simplified compared to desktop: no node editor, no complex timeline editing,
just playback and basic parameter adjustment.

### Android

The Android app uses a `SurfaceView` for GPU-accelerated rendering and JNI to
call the native engine.

kotlin
class JoltPlayerActivity : AppCompatActivity() {
    private external fun nativeInit(): Long
    private external fun nativeLoadPackage(handle: Long, path: String): Int
    private external fun nativeRenderFrame(handle: Long, surface: Surface): Int
    private external fun nativeShutdown(handle: Long)

    private var engineHandle: Long = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_player)

        System.loadLibrary("jfx_engine")
        engineHandle = nativeInit()

        val surfaceView = findViewById<SurfaceView>(R.id.surface_view)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                nativeLoadPackage(engineHandle, "/sdcard/effect.joltpkg")
                startPlayback()
            }
            // ...
        })
    }

    private fun startPlayback() {
        thread {
            while (isPlaying) {
                nativeRenderFrame(engineHandle, surfaceView.holder.surface)
                Thread.sleep(16) // ~60 FPS
            }
        }
    }
}

Touch gestures:
- Single tap: play/pause
- Horizontal swipe: scrub timeline
- Pinch: zoom viewport
- Long press on parameter: open adjustment slider

### iOS

The iOS app uses Metal for rendering and Swift bindings to the native engine.

swift
import MetalKit

class JoltPlayerViewController: UIViewController, MTKViewDelegate {
    var device: MTLDevice!
    var metalView: MTKView!
    var engineHandle: OpaquePointer?

    override func viewDidLoad() {
        super.viewDidLoad()

        device = MTLCreateSystemDefaultDevice()
        metalView = MTKView(frame: view.bounds, device: device)
        metalView.delegate = self
        view.addSubview(metalView)

        engineHandle = jfx_init(nil)
        jfx_load_package(engineHandle, "/path/to/effect.joltpkg")
    }

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable else { return }
        jfx_render_frame(engineHandle, drawable.texture)
        drawable.present()
    }

    // Touch handling
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // play/pause, scrub, zoom
    }
}

Touch gestures match Android conventions.

---

## Plugin Architecture

Plugins integrate JoltFX into host applications (After Effects, Premiere, DaVinci).

### After Effects Plugin

The AE plugin is an AEGP (After Effects General Plug-in) that adds import/export
menu items and a custom effect.

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

A native AE effect that wraps a `.joltpkg`. The effect's parameters are populated
from the package's reflection metadata. Changing a parameter re-renders the frame
in AE's preview window.

### Premiere Plugin

Similar to AE: import/export menu items and a custom effect. Premiere's API is
different (Premiere SDK vs. AE SDK), but the integration logic is the same.

### DaVinci Resolve Plugin

DaVinci uses a Fusion-based plugin system. The plugin exposes JoltFX kernels as
Fusion tools. Users drag a JoltFX tool onto the timeline, adjust parameters in
the inspector, and render in DaVinci's timeline.

---

## Viewport Rendering

All frontends share the same viewport rendering code in `common/src/viewport.c`.
The viewport renders the engine's output texture to a platform-specific surface
(Qt widget, HTML canvas, Android SurfaceView, iOS Metal layer).

c
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

The viewport blits the engine's output texture to the surface. On desktop, this
is a full-screen quad with the texture sampled in a fragment shader. On mobile,
the texture is copied to the native surface (Metal drawable on iOS, SurfaceTexture
on Android). In the web player, the texture is read back to CPU and drawn to a
2D canvas context (WebGL/WebGPU texture → ImageData).

The viewport supports overlay widgets:
- FPS counter (top-left corner)
- Resolution display (top-right corner)
- Safe zones (action-safe, title-safe guides)
- Grid (rule of thirds, golden ratio)

Overlays are toggled in the View menu (desktop) or settings panel (mobile/web).

---

## Timeline Widget

The timeline widget in `common/src/timeline.c` provides playback control and
keyframe editing. It is used by the desktop and mobile frontends (not CLI or web).

c
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

Keyframes store time, value, and interpolation type:

c
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

When the timeline plays, it evaluates interpolated values for all parameters and
pushes them to the engine. The engine re-renders the frame with updated parameters.

---

## Properties Panel

The properties panel in `common/src/properties.c` generates UI widgets from
reflection metadata.

c
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

For each parameter in the selected node:
1. Read `jfx_param_desc_t` from reflection metadata
2. Generate appropriate widget (slider, color picker, dropdown, etc.)
3. When user changes widget, invoke `on_change` callback
4. Callback updates parameter in engine and re-renders viewport

Example parameter descriptor:

c
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

If `animatable` is true, a keyframe button appears next to the widget. Clicking
it adds a keyframe at the current timeline position.

---

## Export

Export is handled by `common/src/export_dialog.c`. The user selects output format,
resolution, frame rate, and codec settings. Export runs on a background thread.

c
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

Export workflow:
1. Initialize FFmpeg encoder with codec and format settings
2. For each frame from start_time to end_time:
   a. Render frame with engine
   b. Read back pixel data from GPU
   c. Encode frame with FFmpeg
   d. Invoke progress callback
   e. Check for cancellation
3. Write audio track (if export_audio is true)
4. Finalize and close output file

Progress callback runs on the background thread. The UI updates a progress bar
and ETA estimate. Clicking cancel sets a flag that the export thread checks
every frame; when set, the thread stops encoding, closes the output file, and
cleans up.

---

## Conformance Tests

Frontend conformance tests in `tests/frontend_conformance/` verify that all
frontends implement the contract correctly. They cover:

- Project open/save/close
- Playback play/pause/seek/loop
- Viewport rendering and resizing
- Export to all supported formats
- Selection get/set
- Error propagation (invalid project, missing kernel, etc.)

Conformance tests run against the desktop GUI, CLI, and web player. Mobile
frontends are tested manually (automated UI testing on mobile is not yet implemented).

Add or extend a conformance test when fixing a bug that affects multiple frontends.

---

## UI Testing (Desktop Only)

The desktop frontend has automated UI tests in `tests/ui_tests/` using Qt Test.
They simulate user interactions (mouse clicks, keyboard shortcuts, drag-and-drop)
and verify UI state.

cpp
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

Run UI tests before submitting any desktop frontend change:

sh
ctest --test-dir build -R ui_tests -V

UI tests are not required for CLI, web, or mobile frontends (they have no Qt UI).

---

## Performance Guidelines

- Keep the UI responsive during rendering. Run rendering on a background thread;
  update the viewport only when a frame is ready.
- Avoid blocking the main thread with heavy I/O. Load projects, export videos,
  and compile `.jolt` files on background threads.
- Minimize redraws. Only repaint the viewport when the current frame changes.
  Do not redraw on every mouse move or parameter change; debounce updates.
- Use GPU-accelerated rendering for the viewport. CPU-based software rendering
  is too slow for real-time preview at high resolutions.
- Profile UI responsiveness separately from engine performance. A slow UI makes
  the engine feel slow even if the engine is fast.

---

## Platform-Specific Notes

### Desktop (Qt)

- Use Qt's docking system for flexible window layouts. Users can drag panels
  into custom configurations and save layouts as presets.
- Support high-DPI displays. Set `AA_EnableHighDpiScaling` and use Qt's automatic
  scaling. Icons and fonts should scale with the display's DPI.
- Follow platform conventions for keyboard shortcuts (Cmd on macOS, Ctrl on
  Windows/Linux).
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

- Use hardware-accelerated `SurfaceView` for rendering.
- Respect Android lifecycle (pause rendering in `onPause`, resume in `onResume`).
- Request storage permissions before loading `.joltpkg` from external storage.
- Provide a "Share" button to export video to other apps.

### Mobile (iOS)

- Use Metal for rendering (fastest path on iOS).
- Respect iOS lifecycle (pause in `applicationDidEnterBackground`, resume in
  `applicationWillEnterForeground`).
- Support Picture in Picture for video playback (iOS 14+).
- Provide a "Share" button to export video to Photos or other apps.

---

## Adding a New Frontend

1. Create a directory `frontends/<name>/` with `src/`, `include/`, and `tests/`.
2. Implement the `jfx_frontend_t` interface (or a subset, if full support is not
   needed).
3. Add a `CMakeLists.txt` or equivalent build script.
4. Link against the core engine (`libjfx_engine`) and Zoltan (`libzoltan`).
5. Integrate with `common/viewport.c`, `common/timeline.c`, and
   `common/properties.c` if applicable.
6. Port the frontend conformance tests; ensure they pass.
7. Add platform-specific build instructions to `frontends/<name>/README.md`.
8. Open a PR with architecture review gate satisfied before creating the branch.

---

## Security Considerations

- User projects are untrusted. Validate all input before passing to the engine.
- Do not execute arbitrary code from `.jolt` or `.joltpkg` files without
  verifying the package signature.
- Sandboxing is handled by the core engine; frontends must not bypass it.
- Limit export resolution and duration to prevent resource exhaustion (e.g.,
  exporting 8K 10-hour video).
- Do not store user credentials in plaintext. Use platform keychains (macOS
  Keychain, Windows Credential Manager, GNOME Keyring).
- For the web player, enforce same-origin policy when loading `.joltpkg` from
  a URL. Do not load arbitrary remote files without CORS headers.

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

**Blocking the main thread.** Rendering, export, and file I/O must run on
background threads. Blocking the main thread makes the UI unresponsive.

**Ignoring platform conventions.** Cmd+C on macOS, Ctrl+C on Windows/Linux.
Native file dialogs. Platform-specific keyboard shortcuts.

**Memory leaks in UI widgets.** Qt widgets must be properly parented or manually
deleted. WASM bindings must release references when no longer needed.

**No error handling.** Every engine call can fail. Check the result code and
display a user-friendly error message.

**Exposing raw engine types in the UI.** The UI should never directly manipulate
`jfx_buffer_t` or `jfx_texture_t`. All engine interaction goes through the
frontend interface.

**Hardcoded paths.** Use platform-specific directories for config, cache, and
logs (`~/.config/joltfx` on Linux, `~/Library/Application Support/JoltFX` on
macOS, `%APPDATA%\JoltFX` on Windows).

**No viewport caching.** If the current frame has already been rendered, do not
re-render it. Cache the last frame and only re-render when parameters change.

**Exporting without progress feedback.** Long exports appear frozen without a
progress bar and ETA.

---

## Contacts

- Desktop frontend: `#joltfx-frontend-desktop`
- CLI frontend: `#joltfx-frontend-cli`
- Web player: `#joltfx-frontend-web`
- Mobile (Android): `#joltfx-frontend-android`
- Mobile (iOS): `#joltfx-frontend-ios`
- Plugins: `#joltfx-plugins`
- Frontend architecture: `#joltfx-frontend-core`
- UI/UX design: `#joltfx-design`

For questions about the frontend interface or adding a new frontend, post in
`#joltfx-frontend-core`.
