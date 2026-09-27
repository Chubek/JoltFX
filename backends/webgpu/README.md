# WebGPU backend

`jfx_backend_webgpu` accepts source and JBC1 workloads and uses the shared
CPU fallback today. It is the cross-platform contract target for the future
wgpu-native device, WGSL translation, buffer upload, dispatch, and readback
implementation. The current capability report is explicit that no GPU work
has been submitted.
