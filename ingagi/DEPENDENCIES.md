# Native-tool source supplements

This file records the source supplement introduced for Ingagi's native tool
build. Existing vendored libraries retain their own source and license files.

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
