# Mobile player core

`jfx_mobile_player` is the tested, platform-neutral playback layer shared by
Android and iOS wrappers. It owns the engine and maps the mobile interaction
contract to playback state:

- tap toggles playback;
- horizontal swipe scrubs the timeline;
- pinch zooms the viewport, clamped to 0.25x–8x;
- rendering advances playback and ticks the engine.

The delivery projects live under `android/` and `ios/`. Android uses
`Choreographer` and JNI lifecycle/gesture calls; iOS uses UIKit controllers,
an Objective-C bridge and display-link playback. Both display the shared CPU
renderer through native image views and respect foreground/background lifecycle
ownership. Platform targets are enabled by their NDK/iOS CMake toolchains; the
portable library also builds and runs its conformance tests on Linux.

## 3D workspace

Android exposes a **3D Modeling & Animation** section with scene/index controls,
primitives, vertex editing, subdivision, transforms, keys, camera and physics
baking. `JFXModeling3DViewController` provides the iOS screen, reached from NLE.
Both expose the expanded primitive catalog, NURBS control points/weights,
metaball centers/radii, cloners and Joltscript transform drivers. Android uses
one-finger quaternion orbit and two-finger pan/pinch/roll. iOS supports pan,
pinch and rotation gestures. Axis-view and gimbal controls are also available.
Both reuse the mobile session's scene document, playback, history and frame
export; 3D playback uses its own clock and does not mix NLE audio. See
[3D usage](../../docs/modeling3d.md) and `jfx_mobile_player_scene3d_state`.

## Build Android

Use JDK 17 and an Android SDK installation. The project pins Gradle 8.9 (included
wrapper), Android Gradle Plugin 8.7.3, Kotlin 2.0.21, SDK/build tools 35, NDK
27.2.12479018 and CMake 3.22.1. Install the tools with Android Studio's SDK
Manager or `sdkmanager`:

```sh
sdkmanager "platforms;android-35" "build-tools;35.0.0" \
  "ndk;27.2.12479018" "cmake;3.22.1" "platform-tools"
export ANDROID_HOME=/path/to/android-sdk
export JAVA_HOME=/path/to/jdk-17
cd frontends/mobile/android
./gradlew assembleDebug
"$ANDROID_HOME/platform-tools/adb" install -r app/build/outputs/apk/debug/app-debug.apk
"$ANDROID_HOME/platform-tools/adb" shell am start -n org.joltfx.mobile/.JoltPlayerActivity
```

Alternatively, set `sdk.dir` in a local `local.properties` file. The application
supports Android 8.0/API 26+ and builds `arm64-v8a` and `x86_64` JNI libraries from
the repository's root CMake project. NDK flexible-page-size support is enabled
for 16-KiB page devices. `JoltPlayerActivity` launches NLE, composition and color
controls over one native editor session. Project/media/LUT paths must be readable
by the app; default project and PPM paths use its private `filesDir`. These builds
include still-image, LUT and miniaudio decoding plus bundled FFmpeg audio/video
decoding and encoded export. Optional OpenColorIO is disabled.

## Build iOS

Use macOS with Xcode, the iOS SDK and CMake 3.20+. From the repository root,
generate an Xcode project for an iOS 14+ simulator application:

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DJFX_FRONTEND_MOBILE=ON -DJFX_MOBILE_IOS_APP=ON \
  -DJFX_FRONTEND_DESKTOP=OFF -DJFX_FRONTEND_CLI=OFF -DJFX_FRONTEND_WEB=OFF \
  -DJFX_PLUGIN_HOST_BRIDGES=OFF \
  -DJFX_BACKEND_METAL=ON -DJFX_BACKEND_VULKAN=OFF \
  -DJFX_BACKEND_D3D12=OFF -DJFX_BACKEND_WEBGPU=OFF \
  -DJFX_EXT_LUA=OFF -DJFX_EXT_MRUBY=OFF \
  -DJFX_COLOR_OCIO=OFF -DJFX_VIDEO_FFMPEG=ON -DJFX_MEDIA_FFMPEG_BUNDLED=ON \
  -DJOLTFX_BUILD_TESTS=OFF -DJOLTFX_BUILD_EXAMPLES=OFF
cmake --build build-ios --target jfx_ios --config Debug
open build-ios/JoltFX.xcodeproj
```

Select the `jfx_ios` scheme and an available simulator, then run. For a device,
configure with `-DCMAKE_OSX_SYSROOT=iphoneos` and select a development team for
signing in Xcode. The app delegate creates the shared bridge and opens
`JFXNLEViewController`, which links to both color and node composition editors.
Projects and PPM exports default to the app's `Documents` directory.

For embedding, set `JFX_MOBILE_IOS_APP=OFF` and link `jfx_ios_ui`; this ARC static
library includes the bridge and all three controllers and links UIKit,
Foundation, CoreGraphics, QuartzCore and AVFoundation. Initialize the controllers with one
shared bridge, keeping only one owning session alive at a time. UIKit application
build/runtime checks require Xcode; portable conformance tests exercise the same
editor state, rendering and persistence on Linux.

The Android activity now has dedicated Color Calibration and Color Grading
sections, backed by native kernel metadata and the shared editing commands.
The iOS sources include `JFXColorViewController` (UIKit, iOS 14+, ARC); add the
controller and bridge sources to the host application, initialize it with the
existing bridge, then set `track`/`clip` and call `refresh` after selection changes.
Both support parameters, LUT file paths, bypass, reset, removal and reordering.
See [color editing](../../docs/editor.md) for path, execution and parameter rules.

## Non-linear editor

The Android activity supplies `NLETimelineView`: touch the ruler to seek, select
and drag a clip to move it between tracks, or drag an edge to trim. Numeric
controls expose split/trim/move/duplicate/slip, ripple delete/gap insertion,
track order/mute/solo/names, undo/redo, new raster/rational FPS, `.jfx` file paths
and PPM frame export. Color controls are invalidated after edits/selection
changes. Playback uses `Choreographer` and pauses rendering in `onPause`.

iOS provides `JFXNLEViewController` (UIKit, iOS 14+, ARC). Link `jfx_ios_ui` or add
the controller/bridge sources to your host application, then present the NLE
with the existing shared bridge:

```objc
JFXNLEViewController *editor = [[JFXNLEViewController alloc] initWithPlayer:bridge];
[self presentViewController:editor animated:YES completion:nil];
```

It shows track/clip lists, preview/playback, frame scrubbing, editing/history,
project paths, sequence setup and PPM export. **Color Calibration / Color
Grading** opens the color controller with the selected clip. Display-link
playback stops when the controller disappears or the application becomes inactive.

Portable wrappers expose `jfx_mobile_player_edit`, `sequence_state`, `seek` and
`write_frame` over the same session. Successful load/new resets playback to zero.
Native NLE conformance verifies shared state, pixels and export; platform UI
builds require NDK/Xcode. See [the NLE guide](../../docs/nle.md).

## Node-based composition

Android includes `CompositionGraphView`: drag node bodies and typed output-to-input
ports, pan the background, pinch to zoom and use Fit. A native-descriptor library
and inspector expose labels, parameters, paths and compatible connection choices,
with duplication/reset/removal, shared undo/redo, output/interior preview, raster
setup, explicit preview seconds, project paths and PPM export.

iOS includes `JFXCompositionViewController` (UIKit, iOS 14+, ARC), opened from the
NLE's **Node Composition** button or initialized directly with the existing
bridge. It is included in `jfx_ios_ui` and the standalone `jfx_ios` application.
It provides a touch graph canvas and native-generated menus/inspectors, history,
project paths, output/interior preview and composition export.

Portable `jfx_mobile_player_graph_state`, `render_graph` and `write_graph` operate
over the same editor as NLE/color. The bridge also exposes `nodeKinds`,
`graphState`, `previewGraphNode:seconds:` and `writeCompositionAtSeconds:path:`.
Native conformance checks graph state, pixels and export; platform UI builds
require the NDK/Xcode toolchains. See [the composition guide](../../docs/composition.md).

## Audio mixing and encoded export

Both apps expose audio-only clips, clip/track gain, stereo pan and fades through
the shared editor/history. Android streams PCM float to AudioTrack; iOS pumps
the shared mix to AVAudioEngine from its display clock. Edits/seeks/stop reset
snapshots and queued audio.

Sequence and composition export controls create immutable encoded jobs, step
one frame per display callback, display progress and support cancellation.
Application pause/view disappearance releases playback and cancels active
exports. Android paths default to `filesDir`; iOS uses `Documents`. The portable
`jfx_mobile_player_audio_mixer` / `export_begin` wrappers provide the same APIs
for embedding. See [media codecs, builds and timing](../../docs/media.md).

Bundled iOS FFmpeg requires one architecture per build directory. Use `arm64`
for Apple Silicon simulators/devices or `x86_64` for Intel simulators, with GNU
make installed. Compilation/runtime testing of the UIKit app requires Xcode;
Android on-device playback needs a device/emulator.
