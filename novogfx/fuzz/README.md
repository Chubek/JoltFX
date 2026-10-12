# Parser fuzzing

Install `cargo-fuzz` and a nightly toolchain, then from this directory:

```sh
cargo +nightly fuzz run edid
cargo +nightly fuzz run events
cargo deny check
```

For bounded CI smoke runs:

```sh
cargo +nightly fuzz run edid -- -max_total_time=30 -max_len=4096
cargo +nightly fuzz run events -- -max_total_time=30 -max_len=4096
```

Seed EDID runs with checksum-valid 128-byte blocks and event runs with complete
and partial vblank/page-flip records to reach valid-input parsing branches.

These targets need no DRM device or elevated privileges. Their isolated
workspace requires downloading `libfuzzer-sys`; production builds remain
offline-capable. Deterministic parser corpora run in the regular unit suite.
