# mruby binding

`jfx_mruby` embeds mruby **3.3**, using Tilly allocation and Script API
1.0. `JFX` exposes the [shared host functions](../../docs/extensions.md).
The no-stdio profile includes the compiler, int64, full-precision doubles and
an instruction-fetch hook. Filesystem/process/loading/eval methods and the
script-visible GC module are omitted. Host callable roots are private to the
native GC arena, independent of Ruby's mutable `$_gc_root_` global.

Callbacks and batches accept a Proc or a top-level method-name string:
`JFX.on("frame_begin", "on_frame")`. Opaque resources expire after invocation;
vectors are arrays. Errors include available exception/backtrace details. The
original numeric `jfx/mruby.h` API remains supported, with
`jfx_mruby_runtime_interface` exposing typed access.

```sh
cmake -S . -B build -DJFX_EXT_MRUBY=ON
cmake --build build --target jfx_mruby joltfx_cli
build/frontends/cli/joltfx scripts run mruby extif/examples/grade.rb gain 0.75
```

Builds need Ruby and mruby's `minirake`; each CMake build directory owns its
generated runtime archive and Rake configuration/lockfile. The adapter uses
the matching `<runtime-build>/jfx/include` generated symbol headers. A compatible
`third_party/mruby` checkout is used when available. Otherwise CMake downloads
the SHA-256-verified **3.3.0** release into the build tree on first configuration;
subsequent configurations reuse it. Set
`-DJFX_MRUBY_ROOT=/path/to/mruby-3.3.0` to use an external checkout or build offline.
Explicit external roots must provide `mrb_open_allocf`, which is required for the
sandbox's memory budget; mruby 4.0's allocator interface is incompatible.
Installation exports that archive as
`jfx_mruby_runtime` beside the adapter. Native Linux is verified; cross builds
need a compatible Ruby host compiler and target C toolchain. `ext_perf` reports
the actual memory and initialization/call costs for the current build.
