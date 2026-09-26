#!/usr/bin/env bash
set -e

echo "Formatting C/C++ code..."
find . -path ./build -prune -o \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0 \
  | xargs -0 clang-format -i

echo "Formatting Rust code..."
cd zoltan && cargo fmt

echo "Formatting complete!"
