# QuickJS binding

`JFX_EXT_QUICKJS=ON` builds vendored QuickJS **2026-06-04** into `jfx_quickjs`.
`jfx` / `joltfx` implements the [shared Script API 1.0 services](../../docs/extensions.md).
No QuickJS libc/std/os module loader, network or filesystem functions are linked.
Global eval and Function constructors, including async/generator constructors,
are disabled before user source executes. Loads accept source text only.

```sh
cmake -S . -B build -DJFX_EXT_QUICKJS=ON
cmake --build build --target jfx_quickjs joltfx_cli
build/frontends/cli/joltfx scripts run quickjs extif/examples/grade.js gain 0.75
```

Native INT values map to **BigInt**, preserving int64 precision; Number maps to
FLOAT. Use `1n` when returning an INT and Number arithmetic for FLOAT arguments.
Host indices accept BigInt or integral Number. Vectors are numeric arrays;
resource wrappers are opaque objects whose native access expires after the call.
Callbacks/local batches retain their actual function object.

Tilly supplies the custom allocator, an interrupt handler enforces per-call
execution limits and the C stack is bounded to 256 KiB. GC hooks control cycle
collection; exception text and stack reach the common diagnostics. Promise jobs
are not pumped by the host: the bridge calls synchronous functions, and returned
Promise objects fail typed translation.

Linux is verified with the vendored C toolchain. Other toolchains must support
the upstream QuickJS sources; no external QuickJS dependency is needed.
`ext_perf` reports the built runtime's actual initialization memory/time and
cached-call cost. The runtime resides in the adapter archive.
