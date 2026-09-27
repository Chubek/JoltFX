#!/usr/bin/env bash
# Formats the project's own C/C++ and Rust sources.
#
# Only files tracked by git are touched, so vendored submodules under
# third_party/ and generated build trees are left alone by construction.
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format not found on PATH" >&2
  exit 1
fi

echo "Formatting C/C++ code (tracked files only)..."
# `git ls-files` covers submodules as a single gitlink entry, so nothing inside
# third_party/ can ever be selected.
git ls-files -z -- \
    '*.c' '*.h' '*.cpp' '*.hpp' \
  | xargs -0 --no-run-if-empty clang-format -i

echo "Formatting Rust code..."
(cd zoltan && cargo fmt)

echo "Formatting complete!"
