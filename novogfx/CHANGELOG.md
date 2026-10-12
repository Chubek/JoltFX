# Changelog — novogfx

All notable changes to `novogfx` are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/); versioning follows
[SemVer](https://semver.org/). `novogfx-sys` is versioned independently with
`+libdrm-*` build metadata.

## [0.3.0] — 2026-10-12 (third implementation regiment)

### Changed / migration

- Rust `Event` gains `CrtcSequence(CrtcSequenceEvent)`. Update exhaustive
  matches; these 64-bit vblank-sequence notifications are no longer mistaken
  for page-flip completions. The IDL's C event view maps them to VBLANK, with
  seconds/microseconds timestamps and CRTC id zero.
- `DumbBuffer::unmap()` releases the CPU view; `map()` recreates it without
  replacing the framebuffer. `as_mut_slice()` is empty while unmapped, and
  `fill_solid()` remaps as needed. C unmap now releases the actual mapping;
  consumers must stop using old addresses after unmap.
- C resource handles share identity across overlapping snapshots. They are
  borrowed from their owning resource lists and must not be individually
  destroyed. The last owning list releases the handle.
- ABI version is 300; high-level crate dependencies are pinned to =0.3.0.

### Added / fixed

- Device-local weak handle caches and RAII resource ownership, including
  cleanup on partial enumeration failure. Fresh owned `resource_snapshot()`
  avoids retaining borrow-cache generations on repeated C enumeration.
- Live connector status/routing and framebuffer queries; owned CRTC getter
  snapshots include their modes rather than returning uninitialized state.
- Cross-device request/page-flip rejection and checked C integer narrowing;
  unselected plane fields are ignored. Requests require enabled atomic mode.
- PlaneUpdate validation rejects invalid masks, zero IDs, incomplete bindings,
  empty selected rectangles, and overflowing fullscreen fixed-point dimensions.
- Checked shared DRM event parsing rejects corrupt and short known records;
  readers discard corrupt buffers and clear output views on errors.
- C++ batch stages own copied mode names across source destruction and batch
  copy/move. Runtime C/C++ tests cover resource identity, mapping residency,
  remap data preservation, device association, and stage assembly.

## [0.2.0] — 2026-10-12 (second implementation regiment)

### Changed / migration

- MSRV is now Rust 1.82; the initial 1.76 declaration was incompatible with
  unsafe extern blocks and newer C-string/layout features already in use.
- C `novogfx_mode_t.name` is now `const char *`, as specified by the IDL.
  Rebuild C/C++ consumers; names borrow the owning mode snapshot.
- Rust `PlaneUpdate` adds `update_mask`: FB=1, CRTC=2, source=4,
  destination=8, zpos=16. Helpers initialize the appropriate masks.
- Pointer-taking C exports are unsafe when called from Rust; C signatures
  are unchanged. Callers must honor handle ownership and buffer lifetimes.
- Correct ABI encoding returns 200 for 0.2.0. High-level dependencies are
  pinned to =0.2.0; novogfx-sys retains its independent libdrm version.

### Fixed

- Retired resource borrows, cache invalidation races, dumb-buffer descriptor
  ownership, double cleanup on failure, and mapping geometry validation.
- Atomic C flags, index-zero properties, primary-plane framebuffer routing,
  missing mandatory properties, plane masks/zpos, and atomic capability state.
- C mode snapshots and legacy page-flip cookies; stale Rust event replay.
- C reader descriptor ownership and bounded malformed-event buffering.
- CMake libdrm linkage/installation and C++ request cleanup on exceptions.

### Added

- Runtime C ABI smoke client (`make c-test`), ownership/failure-path
  regressions, IDL format constants/names, and NULL empty resource arrays.
- Isolated libFuzzer targets for EDID and DRM events; explicit vkms feature
  gate on hardware integration checks; complete Apache-2.0 license text.
- Dependency audit policy in `deny.toml` (MIT/Apache-2.0, duplicate-version
  and unknown-source rejection).
- Separate fuzz audit policy with a version-scoped NCSA exception for
  libfuzzer-sys 0.4.13's bundled LLVM code.

## [0.1.0] — 2026-10-12 (initial implementation)

### Added

- Layered workspace: `novogfx-sys` (raw `-ldrm` FFI, no safe abstractions),
  `novogfx-core` (`Device`, `DriverInfo`, `DeviceCaps`, cached
  `ResourceHandles`, typed `Error`), `novogfx-kms` (typed `Connector`/`Crtc`/
  `Encoder`/`Plane`/`Mode`/`Property`/`Edid`), `novogfx-atomic`
  (`AtomicRequest` builder, `CommitFlags`, `AtomicState`), `novogfx-buffer`
  (`DumbBuffer` CREATE→MAP→mmap→ADDFB2 with unwind, `Framebuffer`, PRIME
  import/export), `novogfx-event` (non-blocking `EventReader`, vblank /
  page-flip / hotplug), and the `novogfx` facade (`prelude`, pinned versions).
- C ABI in `capi/novogfx-c` following `NovoGFX-API.idl`: `novogfx.h`,
  DSLtk-based `novogfx.hpp`, `CMakeLists.txt` (Cargo-built staticlib import).
- Examples: `enumerate`, `dumb_buffer`, `atomic_modeset` (test-commit by
  default), `page_flip`.
- Integration tests in `tests/integration/vkms_smoke.rs` (skip without
  `/dev/dri/card0`; no root required for unit tests).
- `Makefile` (`build`/`test`/`test-all`/`lint`/`fmt`/`doc`/`c-headers`),
  `docs/properties.md`, dependency-free (offline) core.
