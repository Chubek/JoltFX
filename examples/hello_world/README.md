# Hello World example

Minimal end-to-end use of the Core engine with a Joltscript kernel.

- `effect.jolt` — identity passthrough kernel in the MVP `defkernel` syntax.
  It verifies with both the C compiler (`joltfx verify`) and the Zoltan MVP
  (`zoltan verify`).
- `main.c` — initializes the engine, compiles the kernel to JBC1, runs a 2x2
  RGBA gradient through a single-stage pipeline, prints per-channel
  input/output values, ticks the engine once, and shuts down.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target example_hello_world
./build/examples/hello_world/example_hello_world examples/hello_world/effect.jolt
```

With no argument the example uses an embedded copy of the same passthrough
source. Expected output ends with:

```
Passthrough verified: output matches input
```

The same kernel can be exercised through the CLI:

```bash
./build/frontends/cli/joltfx verify examples/hello_world/effect.jolt
./build/frontends/cli/joltfx render --effect invert --width 16 --height 16 \
    -o /tmp/hello.ppm
```
