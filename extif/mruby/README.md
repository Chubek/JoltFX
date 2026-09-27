# mruby binding

`jfx_mruby` builds the vendored mruby source with no stdio, the compiler gem,
and its debug instruction hook. It allocates through Tilly, provides
`JFX.clamp`, blocks filesystem/process/eval source forms, and stops execution
when the configured instruction budget is exceeded.

The public API loads source, calls a numeric function, and controls GC. It is
a sandboxed host and exposes no raw engine resources or kernel registration.
