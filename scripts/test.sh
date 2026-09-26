#!/usr/bin/env bash
set -e

echo "Running tests..."
ctest --test-dir build --output-on-failure
echo "All tests passed!"
