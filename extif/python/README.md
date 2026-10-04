# MicroPython binding

`JFX_EXT_PYTHON=ON` builds vendored **MicroPython 1.24** into `jfx_python`.
The library provides `import jfx` and `import pyjoltfx` for the
[shared Script API 1.0 services](../../docs/extensions.md). This is MicroPython,
with its embedded standard-library subset; it is not a CPython/PyPy embedding.

```sh
cmake -S . -B build -DJFX_EXT_PYTHON=ON
cmake --build build --target jfx_python joltfx_cli
build/frontends/cli/joltfx scripts run python extif/examples/grade.py gain 0.75
```

Python 3 and Make generate the vendored port's qstr/module/root headers. The
port disables external imports, persistent bytecode, eval/exec, file/process I/O,
sys/gc/micropython access and threads. Builtin print routes to Tilly diagnostics.
Integers preserve int64 at the boundary using MPZ, floats use double, vectors
use lists/tuples and resources use opaque `Value` objects. Tracebacks reach the
common error sink. Loop/return hooks enforce execution limits; C recursion has
a 128-KiB stack bound.

Each runtime owns a fixed Tilly-allocated heap and saved interpreter state.
All runtimes run on one serialized owner thread; different runtimes cannot be
entered concurrently or nested. Explicit host references are GC roots.
Automatic collection is disabled during execution; collection happens at host
boundaries or explicit host requests, honoring GC pause. Three quarters of the
available creation budget reserves the interpreter heap; the remainder supports
result strings/shared-host marshaling. `memory_used` counts reserved heap bytes,
not the live objects inside it. Collection reclaims interpreter space for reuse.

No external Python runtime is linked; the C interpreter is part of the adapter
archive. Native Linux is verified. Cross compilation needs a host Python/Make
and a supported target C toolchain; platform frontends do not load this adapter
unless explicitly enabled. `ext_perf` measures the selected build's footprint
and initialization/cached-call costs, including boundary collection.
