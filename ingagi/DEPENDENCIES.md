# Native-tool source supplements

## Default math and geometry libraries

- Linear algebra: header-only `third_party/glm`; the private synchronous bridge
  in `runtime/math_bridge.cc` uses double-precision GLM matrices. Existing batch
  addition/scaling also uses `third_party/simdette` scalar double vectors.
- Geometry: header-only `third_party/libigl/include/igl/doublearea.h`, using
  `igl::doublearea_single` on three projections for each triangle's oriented
  area vector. Go assembles surface areas and area-weighted vertex normals.
- libigl's Eigen dependency comes from
  `third_party/bullet3/examples/ThirdPartyLibs/Eigen` (**3.4.0**). The root
  `third_party/eigen` is an older 3.3.90 development snapshot incompatible with
  this libigl revision (index placeholders and a transposition template error).
  No downloads or vendor modifications are needed. Include roots are explicit;
  system Eigen is not selected implicitly.
- Licenses remain with the vendored sources: GLM's `copying.txt`, libigl's
  `LICENSE.MPL2`, and MPL-2.0 notices in Bullet's vendored Eigen headers.
- Both bridges use Go-owned buffers synchronously and keep no C pointers. The
  geometry bridge only instantiates fixed-size Eigen operations; no library
  worker threads or dynamic Eigen storage are used. `CGO_ENABLED=0` uses Go
  implementations with matching value/ownership/error contracts.

This file records the source supplement introduced for Ingagi's native tool
build. Existing vendored libraries retain their own source and license files.

## Filament material compiler

- `.mat` source emission is pure Go in `monkeyshade/filament.go`, reusing the
  existing Ingagi shader AST and GLSL body emitter. No additional module is used.
- `.filamat` compilation invokes `matc` from **vendored `third_party/filament`**.
  `INGAGI_MATC` selects the executable; `ToolOptions.Matc` / CLI `-matc` override it.
- JoltFX's `JFX_FILAMENT=ON` build produces `build/filament/vendor/tools/matc/matc`.
  `INGAGI_BUILD_FILAMENT=1 scripts/build-shader-tools.sh` can also build it using
  the existing isolated CMake configuration (Clang/CMake/Python required).
- No vendor modifications or linked SDK/CGO dependency are introduced. Match the
  `matc` revision to the SDK that loads the versioned package. See the vendored
  Filament license and `backends/filament/README.md` for SDK requirements.

## Keystone 0.9.2 LLVM source directories

- Consumer: `ingagi/scripts/build-native-tools.sh`.
- Existing vendor location: `third_party/keystone`; its API header reports 0.9.2.
- The current checkout lacks `llvm/lib/` and `llvm/utils/llvm-build/llvm-build`.
- Supplement: upstream Keystone release **0.9.2**, only the missing `llvm/lib`
  and `llvm/utils` directories, copied into an external build-tree overlay.
- URL: <https://codeload.github.com/keystone-engine/keystone/tar.gz/refs/tags/0.9.2>
- SHA-256: `c9b3a343ed3e05ee168d29daf89820aff9effb2c74c6803c2d9e21d55b5b7c24`.
- The build script verifies the hash before extraction and performs no network
  download. Existing vendored engine sources and headers are kept in the overlay.
- Licenses: see `third_party/keystone/COPYING`,
  `third_party/keystone/EXCEPTIONS-CLIENT`, and
  `third_party/keystone/llvm/LICENSE.TXT`; the archive includes its upstream license files.

QBE is built directly from a copy of `third_party/qbe`. Ingagi invokes that QBE
executable for code generation and the Keystone C ABI for assembly. It does not
substitute GNU `as` or a C compiler for assembly of generated Ingagi code.
