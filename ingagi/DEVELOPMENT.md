# Ingagi implementation status and next milestones

This is an implementation inventory, not a promise that every directory is a
working subsystem. Empty adapters/IR placeholders are not executable backends.

## Completed in this development pass

- Filament surface-material artifacts: `shader(target:"filament")` reuses the
  MonkeyShade parser/body emitter and interface linker to generate `.mat`, then
  bounded `matc` compilation generates `.filamat` for OpenGL/Vulkan/Metal. Supports
  lit/unlit materials, uniform/sampler parameter mapping, optional vertex hooks
  and four smooth vec4 varyings. CLI imports/reflection and the compatibility API
  share the implementation. Real vendored `matc` tests verify packages and reject
  invalid GLSL; no runtime material binding or new execution backend is implied.
- First-class CPU/VM geometry: owned immutable `mesh` and `aabb` kinds, checked
  constructors, structural equality, detached fields, affine transforms, area,
  normals, containment and version-3 snapshots (v1/v2 remain readable). Mesh
  generator/bounds results are typed; legacy map inputs remain accepted.
- First-class linear-algebra checking and operations: vec2/3/4, quat and mat4
  annotations/inference, dimension-aware arithmetic, quaternion multiplication,
  indexed reads, dot/cross/length/normalize, matrix construction, inverse,
  transpose and determinant. Native GLM and libigl triangle-measure bridges
  have Go fallbacks. See `examples/spatial.ing` and `DEPENDENCIES.md`.
- Full-tree Go builds/tests work again: the missing embedded target manifests
  are present, malformed empty Go placeholders no longer poison package discovery,
  and `shader` forwards to the real `monkeyshade` implementation.
- Sixteen target descriptors are validated, sorted, independently owned data.
  **IngagiVM, QBE and WASM** are execution backends. The remaining thirteen are
  planned integrations. `Vulkan` is the canonical ID; the old `Vukkan/` scaffold
  is retained but is not compiled. Shader artifact generation and JoltFX's
  renderer bridges do not make these general Ingagi adapters complete.
- `monkeyshade.Parse` and `CompileSource` provide bounded, shader-only source
  selection over the existing scannerless frontend and GLSL/SPIR-V compiler.
  Ambiguous names, duplicate shaders, imports/modules and host declarations are
  rejected. The original `shader` import path remains compatible.
- `noise` implements immutable seeded improved-Perlin noise, 2D/3D samples,
  normalized bounded fBm and strict JSON profiles. Seed expansion is independent
  of Go's global random state. Profiles reject duplicate/unknown fields and invalid
  parameters. This is gradient noise, **not** a Simplex/Worley implementation.
- `geometry` implements owned triangle meshes, validation, bounds, area-weighted
  vertex normals, cloned vertex transformations, and welded outward-wound cube,
  XZ plane and UV-sphere generators. Invalid indices, nonfinite coordinates,
  degenerate faces and excessive tessellation fail explicitly.
- VM builtins expose these foundations as owned values, with argument and
  collection limits. They do not implicitly allocate engine/GPU resources.
  See `examples/procedural.ing` and the README API tables.
- Shader milestone 2 now has explicit location-based vertex/fragment interfaces,
  bounded std140/std430 declared-resource reflection, direct graphics/compute
  pipeline interface reports, static link diagnostics and compiler capability
  JSON. The CLI exposes `-emit reflect`, `-pipeline` and `shader-capabilities`.
  Real GLSL/SPIR-V and stage-link tests verify the baseline. See
  `examples/shader_interfaces.ing` and the README for limits.

## Remaining work, in dependency order

1. **Engine resource and pipeline execution contract.** Specify owned buffers,
   meshes, material bindings, synchronization, lifetime and error propagation
   between Ingagi ECS, the SDK and JoltFX. Add a headless compute integration
   before implementing general Vulkan/WebGPU adapters. MonkeyTrace's dedicated
   browser host and Jolt Renderer are not that general contract.
2. **Shader interfaces and wider validation (baseline implemented).** Explicit
   vertex/fragment I/O, declared-resource reflection, direct stage-link checks and
   compiler capability reporting are implemented. Remaining: geometry/tessellation
   interface linking, mesh/task stages, full GPU expression/output validation and
   device-feature validation. HLSL/MSL/WGSL artifacts still require external
   tools; DXIL/DXBC/Cg/SLang and device dispatch are not implemented by their
   placeholder files. Do not turn planned manifests into implemented claims.
3. **Advanced geometry and deformation.** Rational NURBS evaluation/tessellation,
   metaball meshing, tangent-space texture/displacement sampling and skeletal
   skinning. Those geometry source files currently document the boundary; they
   deliberately expose no fake successful operations. Production JoltFX materials
   and lighting continue to live in its existing renderer APIs.
4. **Broader native compilation.** QBE/WASM/JIT currently share a typed i64/bool
   subset. Add float and aggregate lowering with VM parity and real object/WASM
   execution tests before extending native signatures or automatic JIT tiering.
5. **Embedding and interchange.** Source-module filesystem resolution in the C
   ABI, aggregate/resource marshalling and stable scene handles require an explicit
   ABI/ownership design. `joltscript_interop` remains reserved: JBC1 and Ingagi
   bytecode cannot safely be translated by copying opcodes. Bundle/linker/rewriter
   and lower-IR files containing only package declarations remain architectural
   scaffolds, not alternate implementations of the working compiler/runtime.
6. **Standard-library modules.** `stdlib/modules.tsv` describes proposed modules,
   not 300+ implemented imports. Procedural operations added here are VM builtins;
   they do not implement the entire proposed `std.math.noise`/`std.geometry` APIs.

## Verification

```sh
cd ingagi
GOROOT=/usr/lib/go go test ./... -timeout 60s
GOROOT=/usr/lib/go CGO_ENABLED=0 go test ./... -timeout 60s
GOROOT=/usr/lib/go go test -race ./... -timeout 90s
GOROOT=/usr/lib/go go vet ./...
GOROOT=/usr/lib/go go run ./cmd check examples/procedural.ing
GOROOT=/usr/lib/go go run ./cmd run examples/procedural.ing
```

From the root, `bash ingagi/scripts/test-native.sh` verifies deterministic grammar
generation and runs the NovoParse-tagged suite. Standalone `ingagi/CMakeLists.txt`
builds/tests the embedding library and available language bindings. Tool-dependent
native/JIT and cross-shader tests must report skips when their tools are absent.
