# CPU performance

`JFX_CPU_SIMD=ON` (default) uses vendored `third_party/xsimd` for RGBA exposure,
unautomated audio-track accumulation, recording finite-value validation and the
optimized-build 3D rasterizer. The private C bridge is `src/src/cpu_numeric.h`.
Unaligned buffers and scalar tails are supported. Targets without an xsimd
architecture fall back to scalar code. No host-native ISA flags or fast-math are
required. Set `JFX_CPU_SIMD=OFF` for comparison or a scalar-only build.

Byte-to-float conversion retains its compiler-vectorizable loop: the generic
xsimd converting load measured slower on baseline SSE2. Audio automation retains
its sample-accurate evaluator; constant track gains use the vector kernel.

The 3D renderer caches immutable local normals until geometry/shading changes.
Camera gestures validate only camera state rather than copying and validating
every mesh. Optimized builds batch triangle coverage, depth and lighting through
xsimd; Debug builds retain scalar rasterization because unoptimized SIMD wrapper
calls were slower. Both retain four-sample antialiasing and near-plane clipping.

For interactive use, build with `CMAKE_BUILD_TYPE=Release` or `RelWithDebInfo`.
Debug builds intentionally retain expensive checks and unoptimized code.

## Repeatable measurements

Build `jfx_modeling_benchmark`, `jfx_numeric_benchmark_simd` and
`jfx_numeric_benchmark_scalar` under `tests/perf`. Numeric benchmarks compile both
variants with identical `-O3 -ffp-contract=off` on GCC/Clang, leaving compiler
auto-vectorization enabled in the scalar reference. Run on an idle machine.

Measurements on the development Linux host (baseline four-float SIMD lanes):

| Workload | Reference | Optimized | Result |
|---|---:|---:|---|
| 640x360 eight-sphere scene, Debug | 187.35 ms/frame | 168.35 ms/frame | 10% less time from normal caching |
| Camera gesture update, Debug | 1.053 ms | 0.002 ms | Avoids mesh copies/validation |
| Same scene, renderer compiled at `-O3` | 23.13 ms scalar | 15.69 ms SIMD | 1.47x throughput |
| 1080p RGBA exposure, `-O3` | 5.97 ms scalar | 3.17 ms SIMD | 1.88x throughput |

These are workload-specific measurements, not whole-application speedups. Audio
accumulation was memory-bandwidth limited (roughly 3 ms for 8.3 million samples)
in both variants. The optimized-renderer comparison used separately compiled
renderer objects linked against the same dependencies; both passed the 3D tool
tests. Debug and optimized-build timings are not directly comparable.

`cpu_numeric` and `cpu_numeric_scalar` cover unaligned/tail lengths, in-place
exposure, NaN/infinity validation, clamping, alpha preservation and scalar
references. `modeling3d_tools` checks cache invalidation against fresh scene loads.
