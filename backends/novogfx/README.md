# novogfx — safe, idiomatic DRM/KMS backend in Rust

`novogfx` is a layered Rust wrapper around `libdrm` for Linux, exposing Kernel
Mode Setting (KMS), atomic modesetting, dumb buffers, and DRM events as a
coherent graphics backend. It is the display backend for [JoltFX](../README.md).

- **Safety first.** Unsafe FFI is confined to `novogfx-sys` plus small audited
  `mmap`/`read`/`fcntl` shims. Public APIs use ownership, lifetimes, `AsFd`,
  `Result`, and builder patterns.
- **Layered.** `sys → core → kms/atomic/buffer/event → facade`. Higher layers
  provide typed KMS object IDs; some builder and resource APIs still accept raw IDs.
- **Dependency-free core.** The workspace builds offline with `std` only (no
  `libc`/`bitflags`/`thiserror`/`rustix` fetches); flag and error types mirror
  a subset of those crates' APIs. See `AGENTS.md §5`.
- **C ABI.** `capi/novogfx-c` implements the surface in `NovoGFX-API.idl`
  (`novogfx.h` + DSLtk-based `novogfx.hpp`).

## Layout

See `AGENTS.md §2` for the full tree. Crates live in `crates/`, the C wrapper
in `capi/novogfx-c`, runnable examples in `examples/`, and vkms integration
tests in `tests/integration/`.

## Build & test

```sh
cargo build --workspace
cargo test --workspace --lib
cargo run -p novogfx --example enumerate -- /dev/dri/card0
sudo modprobe vkms
cargo test -p novogfx --features vkms --test vkms_smoke -- --test-threads=1
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo doc --workspace --no-deps
cargo deny check
make c-test
make cpp-test
```

MSRV: Rust 1.82+. `novogfx-sys` links `-ldrm` (system libdrm 2.4.x; constants
verified against `third_party/libdrm`).

## C API

```sh
# Headers: capi/novogfx-c/novogfx.h, novogfx.hpp (needs ../DSLtk.hpp)
# CMake: capi/novogfx-c/CMakeLists.txt (builds the Rust staticlib via Cargo)
```

`make c-test` and `make cpp-test` run clients against the shared library. Set
`NOVOGFX_TEST_DEVICE=/dev/dri/card1` to add hardware enumeration, resource
identity checks, and dumb-buffer unmap/remap checks. The C++ client assembles
a modeset request with TEST_ONLY; it never applies a modeset or page flip.
Parser fuzz targets are in `fuzz/` (separate nightly/libFuzzer workspace).

### Current conformance limits

Resource snapshots share C handles while their owning lists overlap. Destroying
one list releases its ownership; another live list keeps shared handles valid.
Mode views remain snapshots tied to those handles; status/routing/framebuffer
getters query live state. `novogfx_buffer_free` is reserved: no API function
currently returns an owning generic buffer. Vulkan/GBM integration, actual
vkms modeset/flip conformance, and full ABI conformance remain separate work.

## License

MIT OR Apache-2.0. See `LICENSE-MIT` / `LICENSE-APACHE`.
