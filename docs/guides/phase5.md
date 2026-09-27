# Phase 5: public beta

The 0.5.0-beta.1 release establishes the production-facing interfaces while
keeping proprietary SDK and platform work explicitly separated from the
portable build.

## Included

- A versioned native plugin host. Native modules export `jfx_plugin_init`,
  register a size-versioned descriptor, declare capabilities, receive a
  host-local ID, and receive an optional `jfx_plugin_shutdown` callback.
  Loading and unloading publish the corresponding XAS plugin events.
- SDK-independent After Effects, Premiere Pro, and DaVinci Resolve bridge
  contracts. Each exposes a stable identifier and advertises import, export,
  and effect support; `requires_host_sdk` correctly remains set until the
  proprietary SDK adapter is supplied.
- A TypeScript web player with an Emscripten bridge to `jfx_web_session`. The
  C bridge selects bundled effects and renders RGBA8 frames to caller-owned
  WASM memory; the player renders them to a canvas, supports playback, seek,
  loop, and drop input, limits packages to 256 MiB, and rejects cross-origin
  URLs.
- A portable mobile player core and Android JNI/iOS Objective-C wrappers. Tap,
  swipe, and pinch map to playback, timeline scrubbing, and viewport scale;
  Android schedules frames through `Choreographer` rather than creating a raw
  rendering thread.
- Engine frame metrics and the `jfx_engine_benchmark` microbenchmark. Pipeline
  bytecode is validated when added, then executed via an internal prevalidated
  path so a pipeline does not rescan immutable bytecode for every pixel.

## Build, test, and package

```sh
cmake --preset default
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tests/perf/jfx_engine_benchmark
(cd frontends/web && npm test)
cpack --config build/CPackConfig.cmake
```

The CPack archive includes the portable runtime libraries, public headers,
CLI, and Phase 5 documentation.

## Beta boundaries

This is a public-beta foundation, not a claim of host or device certification.
The repository contains browser and mobile bridge sources, but this Linux
environment has no Emscripten, Android NDK/SDK, Xcode, Adobe, or Blackmagic
SDK toolchain to compile or certify those targets. Metal, D3D12, and WebGPU
continue to use their documented software fallback paths because the required
native SDKs and WebGPU C headers/libraries are also unavailable here. The
browser bridge deliberately does not execute package code outside the engine
sandbox; its beta JSON envelope must be fed by a production package verifier.
