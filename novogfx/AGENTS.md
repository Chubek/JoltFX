# AGENTS.md — novogfx

Guidance for AI agents and contributors working on **novogfx**, a Rust graphics
backend library built on top of `libdrm` for Linux.

NovoGFX is shipped with a C wrapper. The main purpose for NovoGFX is its use in the JoltFX system.

You can find `libdrm` in `third_party`. But if an up-to-date Rust bindings exist, use that instead.

---

## 1. Project Overview

**novogfx** is a safe, idiomatic Rust wrapper around `libdrm` that exposes
Kernel Mode Setting (KMS), atomic modesetting, buffer management, and event
handling as a coherent graphics backend. It is intended to be consumed by
compositors, display servers, kiosk applications, and headless renderers that
need direct DRM access without a full graphics stack.

### Design Philosophy

- **Safety first.** Every unsafe FFI call is confined to a thin, well-audited
  layer. Public APIs use Rust's type system to encode DRM/KMS invariants
  (handle validity, device lifetime, atomic state exclusivity).
- **Idiomatic Rust.** Ownership, lifetimes, `AsFd`, `Drop`, `Result`, and
  builder patterns are preferred over C-style handle juggling.
- **Layered.** Each concern lives in its own crate/module with a narrow public
  surface. Higher layers may depend on lower ones, never the reverse.
- **No hidden global state.** All state is owned by a `Device` or a value
  derived from it. No singletons, no thread-local caches.
- **Minimal dependencies.** Core crates must not pull in `udev`, `gbm`,
  `wayland`, or `egl`. Those belong in optional integration crates.

---

## 2. Repository Layout

```
novogfx/
├── Cargo.toml                  # Workspace manifest
├── AGENTS.md                   # This file
├── README.md
├── LICENSE-MIT
├── LICENSE-APACHE
├── capi
│   ├── novogfx-c/            # the C wrapper
│   │   ├── novogfx.h
│   │   ├── novogfx-c.rs
│   │   ├── CMakeLists.txt           # build system for C wrapper
├── crates/
│   ├── novogfx-sys/            # Raw, unsafe FFI to libdrm
│   │   ├── build.rs            # pkg-config / bindgen
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── drm.rs
│   │       ├── drm_mode.rs
│   │       └── xf86drm.rs
│   ├── novogfx-core/           # Device opening, driver info, resources
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── device.rs
│   │       ├── resources.rs
│   │       └── error.rs
│   ├── novogfx-kms/            # Connectors, CRTCs, encoders, planes, modes
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── connector.rs
│   │       ├── crtc.rs
│   │       ├── encoder.rs
│   │       ├── plane.rs
│   │       ├── mode.rs
│   │       └── property.rs
│   ├── novogfx-atomic/         # Atomic modesetting requests and commits
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── request.rs
│   │       ├── commit.rs
│   │       └── state.rs
│   ├── novogfx-buffer/         # Dumb buffers, framebuffers, PRIME/dma-buf
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── dumb.rs
│   │       ├── framebuffer.rs
│   │       └── prime.rs
│   ├── novogfx-event/          # Vblank, page flip, hotplug
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── reader.rs
│   │       ├── vblank.rs
│   │       ├── page_flip.rs
│   │       └── hotplug.rs
│   └── novogfx/                # User-facing facade crate
│       └── src/lib.rs          # Re-exports + prelude
├── examples/
│   ├── atomic_modeset.rs
│   ├── dumb_buffer.rs
│   ├── page_flip.rs
│   └── enumerate.rs
└── tests/
    └── integration/
        ├── vkms_smoke.rs
        └── common/mod.rs
```

---

## 3. Crate Responsibilities

| Crate             | Responsibility                                                                 | Depends On                |
|-------------------|--------------------------------------------------------------------------------|---------------------------|
| `novogfx-sys`     | Unsafe FFI bindings, ioctl constants, C structs. No safe abstractions.         | `libc`                    |
| `novogfx-core`    | `Device`, opening/closing, driver info, `AsFd`, resource enumeration.          | `novogfx-sys`, `rustix`   |
| `novogfx-kms`     | Typed wrappers for KMS objects, properties, modes, EDID parsing.               | `novogfx-core`            |
| `novogfx-atomic`  | `AtomicRequest` builder, commit/test, atomic state tracking.                   | `novogfx-kms`             |
| `novogfx-buffer`  | Dumb buffers, framebuffers, mmap, PRIME/dma-buf import/export.                 | `novogfx-core`            |
| `novogfx-event`   | Reading and parsing DRM events from the device fd.                             | `novogfx-core`            |
| `novogfx`         | Facade: re-exports, `prelude`, end-to-end examples, version pinning.           | all of the above          |

Higher-level integration crates (e.g. `novogfx-gbm`, `novogfx-egl`) are
allowed to live outside this workspace and depend on `novogfx` only.

---

## 4. Build & Test Commands

```sh
# Build the whole workspace
cargo build --workspace

# Run unit tests (no DRM device required)
cargo test --workspace --lib

# Run examples (requires /dev/dri/card0 and permissions)
cargo run --example enumerate
cargo run --example dumb_buffer

# Run integration tests against vkms (requires root or a vkms device)
sudo modprobe vkms
cargo test --workspace --test integration -- --test-threads=1

# Lints and formatting (must pass before merge)
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings

# Documentation
cargo doc --workspace --no-deps --open
```

### Minimum Supported Rust Version (MSRV)

`novogfx` targets **Rust 1.76** or newer (required for `AsFd` ergonomics and
`OnceCell` in `std`). Bumping MSRV requires a note in `CHANGELOG.md`.

---

## 5. Coding Conventions

### General

- Edition 2021. `#![forbid(unsafe_op_in_unsafe_fn)]` at every crate root.
- Every public item must have a doc comment. Missing-docs is a CI error
  (`#![warn(missing_docs)]`, promoted to `-D warnings`).
- No `unwrap()` / `expect()` in library code except where an invariant is
  provably upheld; prefer `debug_assert!` plus explicit error returns.
- No `panic!` for recoverable conditions. Errors are typed (`novogfx::Error`).

### Unsafe Code

- All unsafe FFI lives in `novogfx-sys` **or** in a small, clearly marked
  block inside `novogfx-core` / `novogfx-buffer` where a syscall or mmap is
  unavoidable.
- Every `unsafe` block must be preceded by a `// SAFETY:` comment explaining
  why the invariants hold.
- The `novogfx-sys` crate exposes raw pointers and integer handles only.
  Never leak a raw pointer into a higher-level public API.

### Error Handling

- Use `thiserror` for error enums, `anyhow` only in examples and tests.
- `errno` values returned by `libdrm` are converted to `std::io::Error` via
  `std::io::Error::from_raw_os_error(-ret)` and wrapped in `novogfx::Error::Io`.
- Never lose the original error context when layering.

### Naming

- KMS object wrappers are nouns: `Connector`, `Crtc`, `Encoder`, `Plane`.
- IDs and handles are suffixed: `ConnectorId`, `CrtcId`, `FramebufferId`.
- Builder methods use `set_*` and `with_*` (chained) interchangeably; prefer
  `with_*` for consuming builders and `set_*` for `&mut self`.
- Flags use `bitflags` and are named `<Domain>Flags` (e.g. `CommitFlags`).

### Lifetimes

- KMS wrappers borrow the `Device` (`Connector<'a>`) so they cannot outlive
  the fd they were obtained from.
- Where a self-referential structure would be needed, use raw handles plus a
  compile-time marker tied to the device's lifetime.

### Concurrency

- `Device: Send + Sync`.
- KMS wrappers are `Send` but not `Sync` unless they hold no mutable state.
- `AtomicRequest` holds a `&mut Device` (or an atomicity token) so only one
  commit is in flight per device.
- `DumbBuffer` is `Send` but not `Sync` (mmap is mutable).

---

## 6. Module Contracts

### `novogfx-core`

```rust
pub struct Device { /* OwnedFd + cached resources + caps */ }

impl Device {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, Error>;
    pub fn open_control(path: impl AsRef<Path>) -> Result<Self, Error>;
    pub fn driver(&self) -> Result<DriverInfo, Error>;
    pub fn capabilities(&self) -> DeviceCaps;
    pub fn resources(&self) -> Result<&ResourceHandles, Error>;
    pub fn enable_atomic(&self) -> Result<(), Error>;
}

impl AsFd for Device { /* ... */ }
```

- `open` must reject non-DRM files and probe for `DRIVER_GET_CAPS`.
- `resources()` caches the result of `DRM_IOCTL_MODE_GETRESOURCES`.
- The `Device` must invalidate cached resources when a hotplug event arrives
  (this invalidation is driven by `novogfx-event`).

### `novogfx-kms`

- `Connector::modes()` returns `&[Mode]` parsed once at construction.
- `Connector::status()` maps `DRM_MODE_CONNECTED`, `DISCONNECTED`, `UNKNOWN`.
- `Connector::edid()` returns an optional `Edid` newtype; parsing lives in
  `novogfx-kms::edid` and must be fuzzable in isolation.
- Properties are resolved by name lazily and cached per object.

### `novogfx-atomic`

- `AtomicRequest::commit(flags)` performs `DRM_IOCTL_MODE_ATOMIC` with the
  correct `DRM_MODE_ATOMIC_*` bitmask.
- `test()` must not mutate hardware state and must not emit events.
- On failure, the request must leave no dangling blob or property references.
- Blob properties (e.g. `IN_FORMATS`, `MODE_ID`) are created and destroyed
  within a single commit call; never leak blobs across commits.

### `novogfx-buffer`

- `DumbBuffer::new` performs `CREATE_DUMB` → `MAP_DUMB` → `mmap` →
  `ADDFB2` in that order, and unwinds each step on failure.
- `Drop` for `DumbBuffer` reverses the order: `RMFB` → `munmap` →
  `DESTROY_DUMB`.
- `DumbBuffer::as_mut_slice()` returns the full mapped region; callers are
  responsible for respecting `pitch` when writing.

### `novogfx-event`

- `EventReader::read_events` performs a single non-blocking `read(2)` on the
  device fd, then parses zero or more `drm_event` structs from the buffer.
- Partial reads must be buffered and completed on the next call.
- The reader must never block; callers use `poll`/`epoll` on `Device::as_fd`.

---

## 7. Testing Requirements

- **Unit tests** for every public function that does not require a DRM device.
  This includes: mode parsing, EDID parsing, property name resolution,
  errno → error mapping, `bitflags` conversions.
- **Integration tests** run against `vkms` (Virtual KMS). They are gated
  behind the `vkms` feature and skipped if `/dev/dri/card0` is absent.
- **Fuzz targets** (in a separate `fuzz/` directory, not part of the
  workspace default build) for `Edid::parse` and any blob parser.
- **No test may require root** except the `vkms` suite, which documents its
  requirements in a top-of-file comment.

---

## 8. CI Checklist

Before merging any change, all of the following must pass:

- [ ] `cargo fmt --all -- --check`
- [ ] `cargo clippy --workspace --all-targets -- -D warnings`
- [ ] `cargo test --workspace --lib`
- [ ] `cargo build --workspace --no-default-features`
- [ ] `cargo doc --workspace --no-deps`
- [ ] `cargo deny check` (licenses, advisories, bans)
- [ ] No new `unsafe` block without a `// SAFETY:` comment
- [ ] Public API changes documented in `CHANGELOG.md`
- [ ] New public items have doc comments and, where relevant, examples

---

## 9. Common Agent Tasks

### Adding a new KMS property

1. Add the property name to `novogfx-kms::property::well_known`.
2. Add a typed accessor on the relevant wrapper (e.g. `Plane::alpha()`).
3. Test with a property mock in `novogfx-kms::property::tests`.
4. Update `docs/properties.md`.

### Adding a new atomic flag

1. Extend `CommitFlags` in `novogfx-atomic::commit` with the new bit.
2. Map it in the `commit()` translation to `DRM_MODE_ATOMIC_*`.
3. Document the kernel-side semantics in the flag's doc comment.

### Adding a new event type

1. Define a struct in `novogfx-event::<event>.rs` with a `from_raw` constructor.
2. Extend `EventReader::read_events` to dispatch on `drm_event.type_`.
3. Add the variant to `Event`.
4. Add a fuzz target that feeds random bytes to the parser.

### Fixing an unsafe block

1. Identify the invariant that makes the block sound.
2. Add or update the `// SAFETY:` comment to explain it.
3. If the invariant is not local, refactor so it is (e.g. add a newtype or
   a lifetime).

---

## 10. Anti-Patterns to Avoid

- **Do not** add a `Clone` impl to `Device`. Multiple fds to the same card
  cause independent state and are almost never what the caller wants.
- **Do not** expose raw `u32` handles in the public API. Wrap them in
  newtypes (`ConnectorId`, `CrtcId`, …).
- **Do not** cache state that the kernel can change underneath us (e.g.
  connector status, current CRTC assignment) for longer than one event
  cycle.
- **Do not** call `DRM_IOCTL_MODE_ATOMIC` outside of `novogfx-atomic`.
- **Do not** depend on `udev`, `libgbm`, or `libEGL` from core crates.
- **Do not** use `println!` for diagnostics in library code; use `tracing`.

---

## 11. References

- Linux kernel DRM/KMS documentation: `Documentation/gpu/drm-kms.rst`
- `libdrm` source: `https://gitlab.freedesktop.org/mesa/drm`
- Prior art: `drm-rs`, `nucleid`, `drmkit`, `smithay`, `linux-drm`
- Rust FFI guidelines: `https://rust-lang.github.io/unsafe-code-guidelines/`

---

## 12. Versioning

`novogfx` follows **SemVer**. The `novogfx-sys` crate is versioned
independently and tracks the `libdrm` ABI it was generated against (e.g.
`0.1.0+libdrm-2.4.120`). The facade crate `novogfx` pins exact versions of
its internal dependencies so downstream users get reproducible builds.

Breaking changes to any public API require a minor version bump and a
migration note in `CHANGELOG.md`.
