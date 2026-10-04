#!/usr/bin/env bash
#
# Checks that the Zoltan (Rust) compiler and the Glue Layer (C) compiler emit
# byte-identical JBC1 bytecode.
#
# The two are separate implementations of one format, so they can drift. This
# compares them across the whole bundled corpus, which is where a divergence
# would show up first. Run it after changing either compiler, or from CI.
#
# Byte parity only means something for JBC1 sources. The repository also ships
# the bounded CPU image profile, which is interpreted from an AST and emits no
# bytecode, so it has no byte-identical counterpart to compare against. Those
# sources are therefore reported as NOT APPLICABLE and counted explicitly --
# they are deliberately *not* folded into the pass/fail total, and they are not
# silently skipped either. Reporting matters here: an earlier revision of this
# script skipped every non-JBC1 source and still exited 0, so a 14/312 run
# looked like a clean gate. The JBC1 corpus is now pinned to the sources the
# build actually registers as JBC1, and any regression that drops one of them
# out of that set is a hard failure rather than a quiet shrink.
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

# The JBC1 corpus is whatever the build registers as JBC1: the legacy
# kernels/color catalogue, the audio kernel, and any shipped examples. It is
# read from the build definition rather than hardcoded so that adding a JBC1
# source cannot quietly shrink the gate's coverage.
KERNELS_CMAKE="kernels/CMakeLists.txt"
if [[ ! -f "${KERNELS_CMAKE}" ]]; then
  echo "error: ${KERNELS_CMAKE} not found" >&2
  exit 1
fi

shopt -s nullglob
# shellcheck disable=SC2207
mapfile -t effect_names < <(
  sed -n 's/^set(EFFECT_NAMES\(.*\))/\1/p' "${KERNELS_CMAKE}" | tr ' ' '\n' | grep -v '^$'
)
audio_source="$(sed -n 's/^set(audio_source "\${CMAKE_CURRENT_SOURCE_DIR}\/\(.*\)")/\1/p' "${KERNELS_CMAKE}")"

jbc1_expected=()
for name in "${effect_names[@]}"; do
  jbc1_expected+=("kernels/color/${name}.jolt")
done
[[ -n "${audio_source}" ]] && jbc1_expected+=("${audio_source}")
examples=(examples/*/*.jolt)
jbc1_expected+=("${examples[@]}")
shopt -u nullglob

all_sources=(kernels/*/*.jolt examples/*/*.jolt)
if [[ ${#all_sources[@]} -eq 0 ]]; then
  echo "error: no .jolt kernels found" >&2
  exit 1
fi
if [[ ${#jbc1_expected[@]} -eq 0 ]]; then
  echo "error: could not determine the JBC1 corpus from ${KERNELS_CMAKE}" >&2
  exit 1
fi

# Index the JBC1 corpus for the membership test below.
declare -A is_jbc1=()
for path in "${jbc1_expected[@]}"; do
  is_jbc1["${path}"]=1
done

checked=0
failed=0
skipped=0

for kernel in "${all_sources[@]}"; do
  if ! "${CLI}" compile "${kernel}" -o "${WORK}/c.jbc" >/dev/null 2>&1; then
    if [[ -n "${is_jbc1[${kernel}]:-}" ]]; then
      # A registered JBC1 source that the compiler can no longer compile is a
      # lost guarantee, not an inapplicable case.
      echo "FAIL  ${kernel} (registered as JBC1 but the C compiler rejected it)" >&2
      failed=$((failed + 1))
    else
      echo "n/a   ${kernel} (image profile: interpreted from AST, emits no bytecode)"
      skipped=$((skipped + 1))
    fi
    continue
  fi
  if [[ -z "${is_jbc1[${kernel}]:-}" ]]; then
    # JBC1-parseable but not a registered JBC1 source. Still worth comparing,
    # but it is not part of the pinned corpus.
    echo "note  ${kernel} (JBC1-parseable but not a registered JBC1 source)"
  fi
  if ! "${ZOLTAN}" compile "${kernel}" -o "${WORK}/rust.jbc" >/dev/null 2>&1; then
    echo "FAIL  ${kernel} (the Zoltan compiler rejected a kernel the C compiler accepted)" >&2
    failed=$((failed + 1))
    continue
  fi
  if cmp -s "${WORK}/c.jbc" "${WORK}/rust.jbc"; then
    echo "ok    ${kernel} ($(wc -c < "${WORK}/c.jbc" | tr -d ' ') bytes)"
    checked=$((checked + 1))
  else
    echo "FAIL  ${kernel} (bytecode differs)" >&2
    cmp -l "${WORK}/c.jbc" "${WORK}/rust.jbc" | head -5 >&2 || true
    failed=$((failed + 1))
  fi
done

total=${#all_sources[@]}
echo "---"
# Coverage is stated over the pinned JBC1 corpus, because that is the only set
# where byte-identical output is a meaningful claim.
if [[ ${checked} -eq 0 ]]; then
  coverage="0/${#jbc1_expected[@]}"
else
  coverage="${checked}/${#jbc1_expected[@]}"
fi
echo "jbc1 corpus:  ${coverage} sources compared byte-for-byte"
echo "not JBC1:     ${skipped} image-profile sources (byte parity not applicable)"
echo "total corpus: ${total} .jolt sources"
echo "failed:       ${failed}"

if [[ ${checked} -ne ${#jbc1_expected[@]} ]]; then
  echo "error: compared ${checked} of ${#jbc1_expected[@]} registered JBC1 sources" >&2
  exit 1
fi
[[ ${failed} -eq 0 ]]