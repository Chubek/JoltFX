# Metal backend

`jfx_backend_metal` exposes the stable Metal backend API on every build host.
It currently runs JoltScript source and JBC1 bytecode through the shared CPU
pipeline and reports `software-fallback` in its capability query. Native MTL
device, shader translation, heap, command-buffer, and presentation support
remain to be wired on macOS.
