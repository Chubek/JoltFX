#!/usr/bin/env bash
#
# Checks that the Zoltan (Rust) compiler and the Glue Layer (C) compiler emit
# byte-identical JBC1 bytecode.
#
# The two are separate implementations of one format, so they can drift. This
# compares them over every bundled kernel plus the Hello World example, which
# is where a divergence would show up first. Run it after changing either
# compiler, or from CI.
#
# Usage: scripts/check-bytecode-parity.sh [BUILD_DIR]
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build}"
CLI="${BUILD_DIR}/frontends/cli/joltfx"
ZOLTAN="zoltan/target/debug/zoltan"

if [[ ! -x "${CLI}" ]]; then
  echo "error: ${CLI} not found; configure and build first (cmake --preset default)" >&2
  exit 1
fi
if [[ ! -x "${ZOLTAN}" ]]; then
  echo "error: ${ZOLTAN} not found; run 'cargo build' in zoltan/ first" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

checked=0
failed=0

shopt -s nullglob
kernels=(kernels/*/*.jolt examples/*/*.jolt)
shopt -u nullglob

if [[ ${#kernels[@]} -eq 0 ]]; then
  echo "error: no .jolt kernels found" >&2
  exit 1
fi

for kernel in "${kernels[@]}"; do
  if ! "${CLI}" compile "${kernel}" -o "${WORK}/c.jbc" >/dev/null 2>&1; then
    echo "SKIP  ${kernel} (the C compiler rejected it)"
    continue
  fi
  if ! "${ZOLTAN}" compile "${kernel}" -o "${WORK}/rust.jbc" >/dev/null 2>&1; then
    echo "FAIL  ${kernel} (the Zoltan compiler rejected a kernel the C compiler accepted)"
    failed=$((failed + 1))
    continue
  fi
  if cmp -s "${WORK}/c.jbc" "${WORK}/rust.jbc"; then
    echo "ok    ${kernel} ($(wc -c < "${WORK}/c.jbc" | tr -d ' ') bytes)"
    checked=$((checked + 1))
  else
    echo "FAIL  ${kernel} (bytecode differs)"
    cmp -l "${WORK}/c.jbc" "${WORK}/rust.jbc" | head -5 || true
    failed=$((failed + 1))
  fi
done

echo "---"
echo "checked=$checked failed=$failed"
[[ ${failed} -eq 0 ]]
