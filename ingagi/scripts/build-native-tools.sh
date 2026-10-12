#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=${INGAGI_TOOLS_BUILD:-/tmp/opencode/ingagi-native-tools}
mkdir -p "$build/qbe" "$build/keystone-source"
build=$(cd -- "$build" && pwd)

# QBE's makefile builds in-tree, so use a source copy in the build directory.
cp -R "$root/third_party/qbe/." "$build/qbe/"
make -C "$build/qbe" -j4 CFLAGS='-std=c99 -O2 -fno-strict-aliasing'

cp -R "$root/third_party/keystone/." "$build/keystone-source/"
if [[ ! -d "$build/keystone-source/llvm/lib" || ! -f "$build/keystone-source/llvm/utils/llvm-build/llvm-build" ]]; then
    archive=${INGAGI_KEYSTONE_ARCHIVE:-}
    if [[ -z "$archive" ]]; then
        echo 'Keystone vendor sources are incomplete: llvm/lib and llvm/utils are missing.' >&2
        echo 'Set INGAGI_KEYSTONE_ARCHIVE to the pinned 0.9.2 archive documented in ingagi/README.md.' >&2
        exit 1
    fi
    expected=c9b3a343ed3e05ee168d29daf89820aff9effb2c74c6803c2d9e21d55b5b7c24
    actual=$(sha256sum "$archive")
    if [[ ${actual%% *} != "$expected" ]]; then
        echo 'Keystone source archive SHA-256 mismatch.' >&2
        exit 1
    fi
    mkdir -p "$build/keystone-upstream"
    tar -xzf "$archive" -C "$build/keystone-upstream" --strip-components=1
    # Keep the vendored engine/headers. Supply only absent LLVM implementation
    # and build-tool directories from the version matching those headers.
    if [[ ! -d "$build/keystone-source/llvm/lib" ]]; then
        cp -R "$build/keystone-upstream/llvm/lib" "$build/keystone-source/llvm/"
    fi
    if [[ ! -d "$build/keystone-source/llvm/utils" ]]; then
        cp -R "$build/keystone-upstream/llvm/utils" "$build/keystone-source/llvm/"
    elif [[ ! -f "$build/keystone-source/llvm/utils/llvm-build/llvm-build" ]]; then
        cp -R "$build/keystone-upstream/llvm/utils/llvm-build" "$build/keystone-source/llvm/utils/"
    fi
fi

cmake -S "$build/keystone-source" -B "$build/keystone-build" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=11 -DBUILD_SHARED_LIBS=ON -DBUILD_LIBS_ONLY=ON \
    -DLLVM_TARGETS_TO_BUILD=X86 -DKEYSTONE_BUILD_STATIC_RUNTIME=OFF
cmake --build "$build/keystone-build" --parallel 4

printf 'export INGAGI_QBE=%q\n' "$build/qbe/qbe"
printf 'export INGAGI_KEYSTONE_LIBRARY=%q\n' "$build/keystone-build/llvm/lib/libkeystone.so.0"
