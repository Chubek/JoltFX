# Mobile player core

`jfx_mobile_player` is the tested, platform-neutral playback layer shared by
Android and iOS wrappers. It owns the engine and maps the mobile interaction
contract to playback state:

- tap toggles playback;
- horizontal swipe scrubs the timeline;
- pinch zooms the viewport, clamped to 0.25x–8x;
- rendering advances playback and ticks the engine.

Android and iOS delivery wrappers remain intentionally out of the portable
CMake build because they require the Android NDK and Xcode/Metal toolchains.
The source wrappers now live under `android/` and `ios/`: Android uses
`Choreographer` rather than a raw thread and exposes JNI lifecycle/gesture
calls; iOS provides an Objective-C bridge intended for an `MTKView` controller.
Both respect foreground/background lifecycle ownership in their host app.
