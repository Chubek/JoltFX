#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build=${INGAGI_SHADER_TOOLS_BUILD:-/tmp/opencode/ingagi-spirv-cross}
cmake -S "$root/third_party/spirv-cross" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF \
  -DSPIRV_CROSS_ENABLE_C_API=OFF
cmake --build "$build" --target spirv-cross --parallel "${INGAGI_BUILD_JOBS:-4}"
printf 'INGAGI_SPIRV_CROSS=%s/spirv-cross\n' "$build"
if [[ ${INGAGI_BUILD_NAGA:-0} == 1 ]]; then
  # Match the Naga version pinned by the vendored wgpu-native Cargo.lock.
  cargo install naga-cli --version 29.0.4 --locked \
    --root "$build/naga" --target-dir "$build/naga-build"
  printf 'INGAGI_NAGA=%s/naga/bin/naga\n' "$build"
fi
