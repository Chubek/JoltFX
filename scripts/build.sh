#!/usr/bin/env bash
set -e

echo "Building JoltFX..."
cmake --preset default
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
echo "Build complete!"
