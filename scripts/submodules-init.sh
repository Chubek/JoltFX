#!/usr/bin/env bash
#
# JoltFX Third-Party Repository Cloning
#
# Clone all listed repositories into third_party or an optional directory:
#   ./scripts/submodules-init.sh [DIRECTORY]
#
# Libraries are organized by category and sourced from their official
# upstream repositories. Repositories are cloned at whatever revision their
# upstream default branch points at; no branches, tags or commits are
# requested or checked out. Each repository's nested dependencies are
# initialized afterwards.

set -euo pipefail

usage() {
    echo "Usage: ${0##*/} [DIRECTORY]"
    echo ""
    echo "Clone JoltFX's third-party repositories into DIRECTORY."
    echo "Default: <repository-root>/third_party (relative to this script)."
    echo "Existing Git checkouts are skipped."
}

if [[ $# -gt 1 ]]; then
    usage >&2
    exit 1
fi

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
    -*)
        echo "error: unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
esac

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
THIRD_PARTY_DIR="${1:-${REPO_ROOT}/third_party}"

mkdir -p -- "${THIRD_PARTY_DIR}"
THIRD_PARTY_DIR="$(cd -- "${THIRD_PARTY_DIR}" && pwd)"

# Clone a repository at its upstream default revision.
clone_repository() {
    local url="${1:-}"
    local path="${2:-}"

    if [[ -z "${url}" || -z "${path}" ]]; then
        echo "error: clone_repository needs a url and a path" >&2
        return 1
    fi

    local full_path="${THIRD_PARTY_DIR}/${path}"

    if [[ -d "${full_path}/.git" || -f "${full_path}/.git" ]]; then
        echo "Repository ${path} already exists, skipping..."
        return 0
    fi

    echo "Cloning repository: ${path} from ${url}"

    git clone -- "${url}" "${full_path}"
}

# =============================================================================
# Rendering & Graphics
# =============================================================================

# Dear ImGui - Immediate mode GUI (GUI frontend)
clone_repository "https://github.com/ocornut/imgui.git" \
    "imgui"

# Cairo - 2D vector graphics (Cairo backend) - typically system, but can vendor
# clone_repository "https://gitlab.freedesktop.org/cairo/cairo.git" \
#     "cairo"

# =============================================================================
# Media & Asset Processing
# =============================================================================

# stb - Single-file public domain libraries (image I/O, etc.)
clone_repository "https://github.com/nothings/stb.git" \
    "stb"

# NanoSVG - SVG parsing and rasterization
clone_repository "https://github.com/memononen/nanosvg.git" \
    "nanosvg"

# =============================================================================
# Extension Language Runtimes
# =============================================================================

# Lua 5.4 - Lua extension runtime
clone_repository "https://github.com/lua/lua.git" \
    "lua"

# MRuby - Ruby extension runtime
clone_repository "https://github.com/mruby/mruby.git" \
    "mruby"

# MicroPython - Python extension runtime
clone_repository "https://github.com/micropython/micropython.git" \
    "micropython"

# QuickJS - JavaScript extension runtime
clone_repository "https://github.com/bellard/quickjs.git" \
    "quickjs"

# WASM runtime
clone_repository "https://github.com/wasm-micro-runtime/wasm-micro-runtime" \
     "wasm-micro-runtime"

# =============================================================================
# Compilation & Code Generation
# =============================================================================

# QBE - Lightweight compiler backend (Tilly QBE target)
clone_repository "git://c9x.me/qbe.git" \
    "qbe"

# SLJIT - JIT compilation backend (Tilly JIT engine)
clone_repository "https://github.com/zherczeg/sljit.git" \
    "sljit"

# spirv-cross - SPIR-V cross-compilation (Metal/D3D12/WebGPU backends)
clone_repository "https://github.com/KhronosGroup/SPIRV-Cross.git" \
    "spirv-cross"

# DXC - DirectX Shader Compiler (D3D12 backend)
clone_repository "https://github.com/microsoft/DirectXShaderCompiler.git" \
    "dxc"

# tree-sitter - Incremental parsing for LSP/IDE tooling
clone_repository "https://github.com/tree-sitter/tree-sitter.git" \
    "tree-sitter"

# tree-sitter grammars for supported languages
clone_repository "https://github.com/tree-sitter/tree-sitter-c.git" \
    "tree-sitter-c"

clone_repository "https://github.com/tree-sitter/tree-sitter-cpp.git" \
    "tree-sitter-cpp"

clone_repository "https://github.com/tree-sitter/tree-sitter-rust.git" \
    "tree-sitter-rust"

clone_repository "https://github.com/tree-sitter/tree-sitter-python.git" \
    "tree-sitter-python"

clone_repository "https://github.com/tree-sitter/tree-sitter-go.git" \
    "tree-sitter-go"

clone_repository "https://github.com/tjdevries/tree-sitter-lua" \
    "tree-sitter-lua"

# =============================================================================
# Serialization & Data
# =============================================================================

# yyjson - Fast JSON parsing and serialization
clone_repository "https://github.com/ibireme/yyjson.git" \
    "yyjson"

# MessagePack (cmp) - Binary serialization
clone_repository "https://github.com/camgunz/cmp.git" \
    "cmp"

# zstd - General-purpose compression
clone_repository "https://github.com/facebook/zstd.git" \
    "zstd"

# lz4 - Low-latency compression for streaming assets
clone_repository "https://github.com/lz4/lz4.git" \
    "lz4"

# =============================================================================
# Networking & IPC
# =============================================================================

# libuv - Cross-platform async I/O (CLI and daemon mode)
clone_repository "https://github.com/libuv/libuv.git" \
    "libuv"

# nng - Lightweight messaging for inter-process communication
clone_repository "https://github.com/nanomsg/nng.git" \
    "nng"

# =============================================================================
# Diagnostics & Profiling
# =============================================================================

# Tracy - Frame profiler attachment point (Diagnostics API)
clone_repository "https://github.com/wolfpld/tracy.git" \
    "tracy"

# Perfetto - System-level tracing sink (typically system, but can vendor)
# clone_repository "https://android.googlesource.com/platform/external/perfetto" \
#     "perfetto"

# =============================================================================
# Utilities
# =============================================================================

# libglr parser
clone_repository "https://github.com/Chubek/libglr.git" \
    "libglr"

# mimalloc - High-performance memory allocator
clone_repository "https://github.com/microsoft/mimalloc.git" \
    "mimalloc"

# xxHash - Fast non-cryptographic hashing
clone_repository "https://github.com/Cyan4973/xxHash.git" \
    "xxhash"

# utf8.h - UTF-8 string utilities (single header)
clone_repository "https://github.com/sheredom/utf8.h.git" \
    "utf8.h"

# PCRE2 - Regular expression engine
clone_repository "https://github.com/PCRE2Project/PCRE2.git" \
    "PCRE2"

# Klib - Multi-purpose simple lightweight libraries
clone_repository "https://github.com/attractivechaos/klib.git" \
    "klib"

# Unittesting
clone_repository "https://github.com/catchorg/Catch2.git" \
    "catch2"

# OpenFX support
clone_repository "https://github.com/AcademySoftwareFoundation/openfx.git"  \
    "openfx"

# Spdlog
clone_repository "https://github.com/gabime/spdlog" \
    "spdlog"

# Formatting
clone_repository "https://github.com/fmtlib/fmt" \
    "fmt"

# Perfect Hashing
clone_repository "https://github.com/Chubek/nuperf.git" \
    "nuperf"

# =============================================================================
# GPU Compute & Backend Support
# =============================================================================

# wgpu-native - WebGPU native implementation (WebGPU backend)
clone_repository "https://github.com/gfx-rs/wgpu-native" \
    "wgpu-native"

# VMA - Vulkan Memory Allocator (Vulkan backend)
clone_repository "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" \
    "vma"

# ===============================================================================
# Color Grading Libraries
# ===============================================================================

# OpenColorIO
clone_repository "https://github.com/AcademySoftwareFoundation/OpenColorIO" \
    "opencolorio"

# Color Transformation Language
clone_repository "https://github.com/aces-aswf/CTL" \
    "ctl"

# libraw
clone_repository "https://github.com/LibRaw/LibRaw" \
    "libraw"

# OpenEXR
clone_repository "https://github.com/AcademySoftwareFoundation/openexr" \
    "openexr"

# =============================================================================
# Numerical Libraries
# =============================================================================

# xsimd
clone_repository "https://github.com/xtensor-stack/xsimd" \
    "xsimd"

# EIGEN
clone_repository "https://github.com/PX4/eigen" \
    "eigen"

# Simdette
clone_repository "https://github.com/Chubek/simdette" \
    "simdette"

# GLM
clone_repository "https://github.com/icaven/glm" \
    "glm"

# ===============================================================================
# Animation Libraries
# ===============================================================================

# SDL
clone_repository "https://github.com/libsdl-org/SDL" \
    "sdl"

# bgfx
clone_repository "https://github.com/bkaradzic/bgfx.git" \
    "bgfx"

# FreeType
clone_repository "https://github.com/freetype/freetype" \
    "freetype"

# HarfBuzz
clone_repository "https://github.com/harfbuzz/harfbuzz" \
    "harfbuzz"

# Blend2D
clone_repository "https://github.com/blend2d/blend2d" \
    "blend2d"

# LIEF
clone_repository "https://github.com/lief-project/LIEF" \
    "lief"

# FlatBuffers
clone_repository "https://github.com/google/flatbuffers" \
    "flatbuffers"


# =============================================================================
# Completion
# =============================================================================

echo ""
echo "=========================================="
echo "Third-party repository cloning complete!"
echo "=========================================="
echo ""
echo "Third-party libraries are now available in: ${THIRD_PARTY_DIR}"
echo ""
echo "To add a new repository, use the clone_repository function pattern above."
