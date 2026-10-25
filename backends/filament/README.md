# Optional Filament scene renderer

`JFX_FILAMENT=ON` adds **Filament (GPU rasterizer)** to the desktop 3D Modeling &
Animation renderer selector. It consumes the same sampled scene, cloners,
procedural tessellations, smooth normals and quaternion camera as the other 3D
renderers. PNG, image-sequence and encoded video export capture this renderer selection.
The third desktop **Rendering** sub-tab selects Filament on first entry when
available and writes numbered PNG sequences through vendored `third_party/stb`.
Set a filename prefix in an existing directory, dimensions and source-frame
range; output uses the scene FPS. Scheduler workers report progress and support
cancellation after the current frame, retaining completed PNGs.

This is a scene-rendering adapter, with lit PBR surfaces (nonmetallic, roughness
0.6), directional shadows, diffuse ambient illumination, Filament tone mapping
and FXAA. It uses **OpenGL ES/EGL on Linux** and **Metal on macOS**. It accepts at
most 4,096 visible instances and 6,291,456 expanded vertices (2,097,152 triangles).
Ray-tracing quality fields apply to MonkeyTrace only. Runtime GPU initialization
failure is reported; desktop previews show a raster fallback status.

## Build

```sh
cmake -S . -B build -DJFX_FILAMENT=ON
cmake --build build --target jfx_desktop
```

Native Linux/macOS builds require Clang 17+, CMake 3.22+ and Python 3. Linux needs
EGL/OpenGL ES runtime libraries and a compatible driver. The build compiles the
vendored `third_party/filament` in an isolated C++20 project. JoltFX's C/C++17
public API and compiler settings remain independent. Materials are compiled by
the vendored `matc` at build time and embedded; no runtime shader tool is needed.
`JFX_FILAMENT_BUILD_DIR` overrides the isolated build directory. The option is
OFF by default; cross builds retain the unavailable-renderer path.

The private C descriptor borrows object-space position/normal arrays, transform
matrices, colors and allocator callbacks. Filament owns its GPU resources. Bridge
upload and readback storage uses the engine callbacks, stays live through GPU
completion and is released after SDK teardown. Each synchronous call owns an
independent Filament engine; scheduler preview/PNG/video jobs own independent
scene snapshots. Filament's job pool is configured single-threaded, with the
SDK's own graphics-driver thread handling its command stream.
The optional parallel shader-compiler pool is disabled for these frame-scoped
engines; shader compilation and context teardown stay on the SDK driver thread.

## Checks

```sh
ctest --test-dir build -R 'filament_scene|filament_ui_tests' --output-on-failure
```

Enabled tests exercise actual GPU rendering, empty and animated scenes, cloners,
camera transforms, RGBA orientation, PNG pixels, renderer controls and lossless
video snapshot pixels. On headless Linux, the tests select `EGL_PLATFORM=surfaceless`;
the same setting can be used for headless application runs on Mesa. Disabled
tests verify unavailable selection and failure-atomic output. ASAN/UBSan options
instrument the JoltFX bridge; upstream Filament uses its own build configuration.
