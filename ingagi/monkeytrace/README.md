# MonkeyTrace

MonkeyTrace is an Ingagi-based ray-tracing renderer. Scene geometry and materials
live in Ingagi's ECS. Its compute shader is written in **Ingagi**, in `trace.ing`,
and compiled by `ingagi/shader`. The WebGPU browser host dispatches that shader
and presents its output; the Go CPU backend provides a deterministic reference
renderer and headless PNG output.

## Features

JoltFX's desktop **3D Modeling & Animation** tab integrates the CPU backend via
`native/monkeytrace.h` (ABI 1.0). `JFX_MONKEYTRACE` builds the Go shared library,
independently of `JFX_INGAGI`, and makes MonkeyTrace the default desktop renderer.
The bridge borrows validated triangle/camera descriptors, creates an owned ECS
scene, and publishes RGBA8 only after successful budgeted rendering. Desktop
workers own animated scene copies for preview and PNG/video export; see
[`docs/modeling3d.md`](../../docs/modeling3d.md).

- Spheres and two-sided triangle meshes, perspective cameras.
- Linear-RGB diffuse materials, mirror reflection fractions, emissive surfaces.
- Directional lighting with ray-traced hard shadows and a gradient environment.
- Bounded reflection depth and deterministic grid supersampling.
- Reinhard tone mapping followed by gamma 2.2 display conversion.
- Owned ECS snapshots: entity/material changes appear on the next snapshot.
- Deterministic, stackless BVH acceleration for primary, reflection and shadow rays.
- Real WebGPU compute dispatch, asynchronous readback, canvas display, PNG download,
  repeat rendering, error reporting and resource teardown.
- GLSL, SPIR-V, WGSL and Ingagi's other shader artifact exports.

This is a bounded Whitted-style tracer. It supports up to **4096 primitives**,
**64 samples/pixel**, **8 reflected continuations**, **4096 pixels per axis**,
and **4M total pixels**. It does not yet implement stochastic global illumination,
texture mapping, refraction, or a native GPU
renderer. Materials use one constant albedo per entity. Meshes are triangle entities;
coordinates are world-space, without implicit hierarchy transforms.

## Render a PNG

From `ingagi/`:

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -width 640 -height 360 -samples 16 -bounces 3 -o monkeytrace.png
```

The CPU backend works with `CGO_ENABLED=0`; it needs no shader tools. The CLI
renders the built-in demo or a custom Ingagi scene via `-scene FILE`.
The Go API accepts arbitrary ECS scenes.

## Run the shader on WebGPU

Export requires `glslangValidator` and **Naga 29.0.4**. Ingagi's
`scripts/build-shader-tools.sh` supports `INGAGI_BUILD_NAGA=1`, or install Naga:

```sh
cargo install naga-cli --version 29.0.4 --locked
```

Then, from `ingagi/`:

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -backend webgpu -width 640 -height 360 -samples 4 -o monkeytrace.html
python3 -m http.server 8000
```

Open `http://localhost:8000/monkeytrace.html` in a WebGPU-capable browser. Serve on
localhost or HTTPS so WebGPU has a secure context. The exported HTML includes the
compiled WGSL and scene data; the browser needs no compiler, runtime server, or
external assets. Select Naga with `-naga /path/to/naga` or `INGAGI_NAGA`. Shader
compiler subprocesses and CPU rendering are bounded by `-timeout` (default 1m).
The export timeout does not bound later browser GPU work.

To inspect the actual shader:

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -backend shader -format glsl -o trace.comp
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -backend shader -format spirv -o trace.spv
spirv-val --target-env vulkan1.1 trace.spv
```

## Author a scene in Ingagi

`LoadScene(source)` runs IngagiVM in a fresh ECS world. Scene initializers and
`main` each use a 100,000-instruction/64-frame budget. Source is import-free and
limited to 1 MiB. It provides these synchronous host functions:

```ingagi
fn main() {
    trace_camera(vec3(0.0, 1.0, 5.0), vec3(0.0, 0.0, -1.0), vec3(0.0, 1.0, 0.0), 45.0);
    trace_sky(vec3(0.6, 0.7, 0.9), vec3(0.1, 0.2, 0.5));
    trace_light(vec3(-1.0, 3.0, 2.0), 1.8);
    let ball = trace_sphere(vec3(0.0, 0.0, -2.0), 1.0, vec3(0.8, 0.2, 0.1), 0.25, 0.0);
    // ball is a real ECS entity handle; despawn(ball) uses Ingagi's ECS builtin.
}
```

`trace_sphere(center, radius, albedo, reflectivity, emission)` and
`trace_triangle(a, b, c, albedo, reflectivity, emission)` return entity handles.
Camera arguments are eye/target/up/FOV; sky arguments are low/high colors; light
arguments are direction/intensity. Use Ingagi functions, loops and expressions
to generate geometry. The scene is validated after `main` completes. Printed
script output is discarded by this scene-loading API.

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -scene monkeytrace/demo.ing -o scripted.png
```

The same `-scene` works with `-backend webgpu`. `demo.ing` is an executable
Ingagi scene matching the Go demo; tests compare their rendered pixels.
`many_spheres.ing` generates a 32×32 sphere grid and a triangle ground plane
(1026 primitives) within the same VM budget:

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -scene monkeytrace/many_spheres.ing -width 640 -height 480 -o many-spheres.png
```

## Build a scene in Go

```go
scene := monkeytrace.Scene{
    World:          ecs.NewWorld(),
    Camera:         monkeytrace.DefaultCamera(),
    SkyLow:         monkeytrace.Vec3{X: 0.6, Y: 0.7, Z: 0.9},
    SkyHigh:        monkeytrace.Vec3{X: 0.1, Y: 0.2, Z: 0.5},
    LightDirection: monkeytrace.Vec3{X: -1, Y: 3, Z: 2},
    LightIntensity: 1.8,
}
id := scene.AddSphere(
    monkeytrace.Sphere{Center: monkeytrace.Vec3{Z: -2}, Radius: 1},
    monkeytrace.Material{
        Albedo: monkeytrace.Vec3{X: 0.8, Y: 0.2, Z: 0.1},
        Reflectivity: 0.25,
    },
)
options := monkeytrace.Options{Width: 320, Height: 180, Samples: 4, Bounces: 3}
img, err := monkeytrace.Render(ctx, scene, options, monkeytrace.CPUBackend{})
// Check err, then encode img with image/png.
scene.World.Despawn(id) // The next render sees the removal.
```

Components are named `monkeytrace.Sphere`, `monkeytrace.Triangle` and
`monkeytrace.Material`. Each component's `value` field holds its corresponding
Go struct. Geometry must have a valid material; malformed payloads, ambiguous
geometry, degenerate camera/triangles and nonfinite inputs are errors.
Serialize world changes with snapshot creation. Snapshots own primitive copies
and may then be rendered independently of world mutations. Context cancellation
is checked during CPU rendering.

`Snapshot` builds a `Frame`; `Backend.Render` consumes it. `Frame.Options`,
`GeometryData`, `ParameterData` and `BVHData` expose validated settings and fresh packed
buffers for additional GPU hosts. `WriteWebGPU` exports a browser-renderable frame.

## Acceleration

Each snapshot builds a deterministic, balanced median-split BVH along the longest
bounding-box axis. Primitive storage remains in entity-ID order; tied hits select
the lowest primitive index. The tree contains one leaf per primitive and at most
8191 nodes. Conservative bounds include both CPU double-precision geometry and
its float32 GPU representation, with outward rounding and padding for planar
triangles.

Nodes are stored in preorder with a skip link to the first node after their
subtree. Traversal enters an internal node's first child at the next slot, or
jumps past an entire subtree on a bounds miss. This requires no recursion or
per-ray traversal stack on either backend. Closest-hit traversal tightens the
distance bound; shadow traversal exits at its first valid hit. Parallel slab
directions are handled explicitly to avoid zero/infinity arithmetic.
Secondary-ray origins use a shared scale-aware normal offset, so the shadow and
reflection bias remains representable in float32 at large world coordinates.

Run the reproducible query microbenchmark from `ingagi/`:

```sh
GOROOT=/usr/lib/go go test ./monkeytrace -run '^$' \
  -bench BenchmarkBVHClosestHit -benchmem
```

It compares accelerated and brute-force queries on the same 4096-sphere scene;
timings describe that query workload, rather than a whole-frame speedup.

## GPU contract

The shader uses descriptor set/group 0 and an **8×8×1** workgroup:

| Binding | Resource | Layout |
|---|---|---|
| 0 | Read-only geometry storage buffer | 16384 `vec4`s, std430, 256 KiB |
| 1 | Camera/light/options uniform buffer | 9 `vec4`s, std140, 144 bytes |
| 2 | Write-only output storage texture | `rgba32float` |
| 3 | Read-only BVH storage buffer | 16382 `vec4`s, std430, 262112 bytes |

Four geometry vectors encode each primitive: A/radius (radius 0 indicates a
triangle), B/reflectivity, C/reserved, albedo/emission. Parameter vectors encode
eye, forward/FOV scale, right/aspect ratio, up, low sky, high sky,
light direction/intensity, width/height/primitive count/bounces,
samples/BVH node count/reserved/reserved. Each BVH node uses two vectors:
minimum bounds/skip index, maximum bounds/primitive index (−1 for internal nodes).
Empty scenes have zero nodes. Both buffers have fixed capacity with zeroed unused
slots. GPU hosts must provide all four bindings when using this shader.
The browser rounds dispatch dimensions up and the shader guards edge invocations.
Texture readback rows are padded to WebGPU's 256-byte alignment.

This implements the dedicated MonkeyTrace WebGPU host. Ingagi's general pipeline
binding/dispatch and desktop GPU linkage are still separate development work.
Shader artifacts for other targets do not by themselves provide those renderers.

## Verification

```sh
# From ingagi/
GOROOT=/usr/lib/go go test ./monkeytrace ./shader -timeout 30s
GOROOT=/usr/lib/go CGO_ENABLED=0 go test ./monkeytrace ./shader -timeout 30s
```

Tests cover geometric hits, inside-sphere roots, shadows, reflection, emission,
owned snapshots, ECS removal, VM scene authoring/handle interoperability and
budget exhaustion, invalid inputs, cancellation, real SPIR-V
validation, and WGSL/SPIR-V round trips when the tools are available.
BVH coverage includes randomized closest-hit and shadow comparisons against
brute force, coincident-hit ordering, inside/parallel/planar rays, full-capacity
trees, skip-link integrity, owned snapshots and primitive-test pruning.

Optional real browser integration requires Playwright and Chromium. Export a
17×9 frame to exercise both padded rows and partial workgroups:

```sh
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -width 17 -height 9 -samples 4 -o /tmp/opencode/monkeytrace-cpu.png
GOROOT=/usr/lib/go go run ./monkeytrace/cmd/monkeytrace \
  -backend webgpu -width 17 -height 9 -samples 4 -o /tmp/opencode/monkeytrace.html
node monkeytrace/scripts/test-webgpu.mjs \
  /tmp/opencode/monkeytrace.html /tmp/opencode/monkeytrace-cpu.png
```

`MONKEYTRACE_PLAYWRIGHT` can select an absolute Playwright module directory;
`MONKEYTRACE_CHROMIUM` can select an existing Chromium executable. The
test serves its own localhost page, checks shader dispatch against CPU pixels
with float32 rounding tolerance, and verifies a second dispatch/readback.
The same browser runner is integrated into the Go suite behind an explicit opt-in:

```sh
MONKEYTRACE_WEBGPU_TEST=1 GOROOT=/usr/lib/go go test ./monkeytrace \
  -run TestWebGPUTraversal -v -timeout 30s
```

Configure Naga and Playwright as above. This covers empty, demo, 1026-primitive
procedural and full 4096-primitive scenes, including large world coordinates.
