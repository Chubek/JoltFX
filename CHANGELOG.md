# Changelog

All notable changes to JoltFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- **The desktop frontend no longer exits on launch.** It composed a single
  ImGui frame, discarded the draw data and returned. It now opens a real
  window (SDL2 + OpenGL 3.3) and runs a frame loop with input, present, and
  quit handling, driven by the vendored Dear ImGui SDL2 and OpenGL3 backends.
  The viewport image is produced by rendering the selected effect through the
  engine's backend and uploaded as a texture, the timeline advances the engine
  clock while playing, the properties panel edits the live effect, and the
  console shows captured engine events and log output. `--headless-smoke`
  remains the display-server-free path, and `--frames` / `--duration` bound a
  run so the loop cannot hang CI.
- **The engine now has a render path.** It created a backend and never used it.
  `jfx_engine_execute_bytecode` and `jfx_engine_execute_source` dispatch JBC1
  programs through the selected backend, and the CLI, web, mobile and desktop
  frontends all render through the engine rather than through private CPU
  paths. `joltfx render --backend` now selects the real execution path and
  reports the device the frame actually ran on.
- `jfx_vk_query_caps` no longer reports `cpu_fallback` unconditionally; a
  backend with a device says so, and the new `used_gpu` field reports whether
  the last dispatch really reached the device.
- The CPU-path backends name the backend in their fallback `device_name`, so a
  capability report from three adapters is distinguishable.
- `tilly_realloc` no longer returns `NULL` silently for arena, pool and stack
  allocators; it logs that an in-place grow is impossible and names the
  strategy.
- The Tilly logger no longer deadlocks when a sink logs, registers a sink or
  removes a sink, and no longer indexes its level-name table out of bounds.
  `tilly_log_init` is idempotent instead of an empty no-op, the default sink
  no longer prints `unknown:0` for calls without a source location, and a full
  sink registry reports itself rather than dropping the sink.
- mruby's allocator now enforces the script memory budget it was storing. It
  carries a per-block size header so the accounting is exact and returns to its
  starting figure after a collection.
- The Lua allocator's budget test can no longer underflow and silently disable
  the limit.
- `memory_init` reports that the memory subsystem is already owned by another
  engine instead of failing with an unrelated out-of-memory.
- The ImGui font atlas is no longer built before the renderer backend exists,
  which tripped an assertion on the first windowed frame; and the OpenGL3
  backend is shut down exactly once.
- `jfx_desktop` no longer reads a string from the frontend after destroying it
  (found by AddressSanitizer).
- `scripts/submodules-init.sh` no longer aborts partway through on a malformed
  `add_submodule` call, and its PCRE2 and wgpu-native URLs and paths now match
  `.gitmodules`.
- `CompilerWarnings.cmake` selects flags per language, so a toolchain with
  different C and C++ compilers no longer leaves C++ targets warning-free, and
  MSVC no longer receives Clang-only flags.
- CTest tests carry labels (`unit`, `conformance`, `frontend`, `integration`)
  and timeouts, so a hung worker thread or a blocked display server fails the
  run instead of stalling it.
- **Test executables are no longer compiled with `NDEBUG` in RelWithDebInfo and
  Release.** The tests are `assert()` programs and several rely on `assert()` for
  the side effect of its expression, so `NDEBUG` deleted the calls and left the
  suite asserting almost nothing. All test targets are created through
  `jfx_test_target`, which always disables `NDEBUG`, and `test_containers` has
  an `#error` guard so a regression is a build failure.
- The CLI integration script resolves its binary to an absolute path, so passing
  a relative path no longer breaks the checks that run the CLI from a temporary
  directory, and its failure messages report the real exit status.

### Added

- A backend HAL (`jfx/backend_interface.h`): one operations table that every
  backend implements, so the engine selects and drives backends without knowing
  their concrete types, and the four duplicated per-backend caps and config
  structs are now aliases of one definition.
- `jfx_engine_get_caps` and `jfx_engine_used_gpu` for capability reporting,
  and `jfx_engine_caps_t` with a size guard for ABI versioning.
- `jfx_buffer`, `jfx_texture` and `jfx_kernel`: the public API declared these
  nine functions but defined none of them, so any caller failed to link. They
  are now real engine-owned objects, and the `max_buffers`, `max_textures` and
  `max_kernels` configuration fields are enforced instead of ignored. Textures
  support RGBA8/f32/f16 storage with conversion, and kernels compile and
  dispatch through the engine's backend.
- `jfx_engine_config_t::max_worker_threads`. `max_buffers` was silently being
  used as the worker-thread count; worker sizing now has its own field and
  defaults to the hardware concurrency.
- The shared frontend contract (`frontends/common/include/jfx_frontend.h`):
  a vtable, a capability bitmask derived from that vtable, and safe dispatchers
  that turn a missing operation into `JFX_ERROR_NOT_IMPLEMENTED`. The CLI's
  `render` and the new `export` command are implemented against it by a
  headless frontend, so the CLI and the GUI frontends run the same code.
- `joltfx export` and `joltfx capabilities`. `export` renders a frame range to
  a PPM sequence; `capabilities` reports what this build actually implements
  and refuses to claim what it does not.
- `joltfx info` reads compiled JBC1 artifacts, not just source.
- `zoltan compile -o` now emits real JBC1 bytecode instead of copying its input
  back with a comment header. It is byte-identical to the Glue Layer compiler's
  output, enforced over every bundled kernel by
  `scripts/check-bytecode-parity.sh` and a CI job.
- `jfx_mobile_player_set_effect` and `jfx_mobile_player_render_rgba8`, so the
  Android and iOS surfaces have a real render to bind instead of a clock tick.
- Build options that the subsystem guides document but that did not exist:
  `JFX_BACKEND_*`, `JFX_EXT_*`, `JFX_FRONTEND_*`, `JFX_PLUGIN_HOST_BRIDGES`
  and `JFX_DESKTOP_WINDOW`. A backend that is switched off is neither built nor
  offered by the engine, and a request for it is rejected by name.
- `sanitizers` and `minimal` CMake presets, plus `unit` and `conformance` test
  presets.
- Emscripten and Android toolchain files, which `cmake/toolchains/` referenced
  as an empty directory.
- `jfx_diagnostic_format`, so the `file:line:column: message` form is defined
  once instead of per call site.
- Integration tests for the desktop frontend: `desktop_headless` and, where a
  windowing backend is available, `desktop_window`. The windowed test skips
  rather than fails on a host with no display server.

### Changed

- The Glue layer no longer depends on `jfx_core`. The compiler and the ABI
  registry sit below the engine, which is what lets the engine depend on them
  to run kernels; the previous edge was unused and blocked a real render path.
- The desktop frontend's ImGui layout persists in the platform's per-user
  config directory (`~/.config/joltfx`, `%APPDATA%\JoltFX`,
  `~/Library/Application Support/JoltFX`) instead of writing `imgui.ini` into
  the source tree.
- The host-application plugin bridges advertise no capabilities. They
  previously claimed import, export and effect registration, none of which
  exists in the shipped SDK-independent stubs.
- mruby's sandbox is capability removal (an unopened stdio/filesystem build,
  undefined process-control methods) instead of a substring blocklist on the
  script text, which both missed indirect access and rejected innocent
  identifiers containing a blocked word.
- `kernels`' effect catalog is a flat array with a linear scan instead of a
  hash map. It holds 12 immutable entries built once, so the map was indirection
  without benefit.
- Install rules ship the engine, the Joltscript layers, the backends and the
  `jfx_desktop` binary. The previous rules installed the frontend and bridge
  libraries but omitted the runtime the engine dispatches through, so a package
  could not run a kernel.
- `JOLTFX_BUILD_DOCS` builds a `docs` target instead of being a dead option.
- CI has job timeouts, a concurrency group, a build matrix that actually
  exercises the option combinations, cargo and ccache-style caching, and leaks
  are checked rather than disabled. The release artifact is now tested before
  it is published.

### Removed

- `scripts/scaffold.sh`. With `--force` it overwrote the whole repository,
  including the working engine, backend and CLI sources, with 1951 lines of
  stale stubs. There was no safe way to run it.
- `scripts/format.sh` no longer walks `third_party/`; it formats only
  git-tracked files, so vendored submodules cannot be rewritten.

### Known limitations

- Metal, D3D12 and WebGPU still execute through the shared validated CPU
  pipeline. They are portable, SDK-independent and honest about it, and each is
  a single function away from native dispatch, but they do not reach a GPU.
- The engine's memory, scheduler and event subsystems are process-global, so
  only one engine may be alive at a time. Concurrent independent engines and
  concurrent shutdown remain unsupported.
- The host-application bridges still need their proprietary SDKs.
- The web player still uses a JSON effect envelope rather than a signed
  `.joltpkg` container.
- The QuickJS and MicroPython extension runtimes are still absent;
  `JFX_EXT_QUICKJS` and `JFX_EXT_PYTHON` are a configure error rather than a
  silent no-op.

## [0.5.0-beta.1] - 2026-09-27

### Added
- Versioned native plugin host with lifecycle events and host adapter contracts
  for After Effects, Premiere Pro, and DaVinci Resolve.
- Dependency-free web player runtime and portable C WASM session bridge.
- Portable mobile playback core with touch gesture semantics.
- Frame timing metrics, a repeatable engine microbenchmark, beta install rules,
  and public-beta documentation.

### Changed
- Pipeline execution now skips redundant bytecode validation for immutable,
  already-validated pipeline stages while preserving validation at public API
  boundaries.

### Known limitations
- Host application SDK adapters, Emscripten export glue, Android/iOS wrappers,
  and native GPU implementations for Metal/D3D12/WebGPU remain release-blocking
  work for a general-availability release.
