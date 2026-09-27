# Lua binding

`jfx_lua` embeds vendored Lua with Tilly allocation. Its sandbox exposes base,
table, string, math, coroutine, UTF-8, and `jfx.clamp`. It excludes OS, I/O,
package loading, debug, `dofile`, `loadfile`, and runtime `load`.

The public API loads source, calls a numeric function, and controls GC. Each
evaluation has memory and instruction limits supplied through
`jfx_script_config_t`.
