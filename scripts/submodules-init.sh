#!/usr/bin/env bash
#
# JoltFX Third-Party Library Submodule Initialization
#
# This script adds all vendored third-party libraries as git submodules.
# Run from the repository root: ./scripts/submodules-init.sh
#
# Libraries are organized by category and sourced from their official
# upstream repositories. Pinned commits/tags are used for reproducibility.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
THIRD_PARTY_DIR="${REPO_ROOT}/third_party"

mkdir -p "${THIRD_PARTY_DIR}"

# Helper function to add a submodule with a specific commit/tag
add_submodule() {
    local url="$1"
    local path="$2"
    local commit="${3:-}"
    local branch="${4:-}"

    if [[ -z "${url}" || -z "${path}" ]]; then
        echo "error: add_submodule needs a url and a path" >&2
        return 1
    fi

    local full_path="${THIRD_PARTY_DIR}/${path}"

    if [[ -d "${full_path}" ]]; then
        echo "Submodule ${path} already exists, skipping..."
        return 0
    fi

    echo "Adding submodule: ${path} from ${url}"

    if [[ -n "${branch}" ]]; then
        git submodule add -b "${branch}" "${url}" "${full_path}"
    else
        git submodule add "${url}" "${full_path}"
    fi

    if [[ -n "${commit}" ]]; then
        echo "  Pinning to commit: ${commit}"
        (
            cd "${full_path}"
            git checkout "${commit}"
        )
    fi
}

# =============================================================================
# Rendering & Graphics
# =============================================================================

# Dear ImGui - Immediate mode GUI (GUI frontend)
add_submodule "https://github.com/ocornut/imgui.git" \
    "imgui" \
    "docking" \
    "docking"

# Cairo - 2D vector graphics (Cairo backend) - typically system, but can vendor
# add_submodule "https://gitlab.freedesktop.org/cairo/cairo.git" \
#     "cairo" \
#     "1.18.0"

# =============================================================================
# Media & Asset Processing
# =============================================================================

# stb - Single-file public domain libraries (image I/O, etc.)
add_submodule "https://github.com/nothings/stb.git" \
    "stb" \
    "master"

# NanoSVG - SVG parsing and rasterization
add_submodule "https://github.com/memononen/nanosvg.git" \
    "nanosvg" \
    "master"

# =============================================================================
# Extension Language Runtimes
# =============================================================================

# Lua 5.4 - Lua extension runtime
add_submodule "https://github.com/lua/lua.git" \
    "lua" \
    "lua-5.4.7"

# MRuby - Ruby extension runtime
add_submodule "https://github.com/mruby/mruby.git" \
    "mruby" \
    "3.3.0"

# MicroPython - Python extension runtime
add_submodule "https://github.com/micropython/micropython.git" \
    "micropython" \
    "v1.24.0"

# QuickJS - JavaScript extension runtime
add_submodule "https://github.com/bellard/quickjs.git" \
    "quickjs" \
    "master"

# Wasmtime - WASM extension runtime (system-side)
# Note: Wasmtime is typically a Cargo dependency, but can be vendored
# add_submodule "https://github.com/bytecodealliance/wasmtime.git" \
#     "wasmtime" \
#     "main"

# =============================================================================
# Compilation & Code Generation
# =============================================================================

# QBE - Lightweight compiler backend (Tilly QBE target)
add_submodule "git://c9x.me/qbe.git" \
    "qbe" \
    "master"

# SLJIT - JIT compilation backend (Tilly JIT engine)
add_submodule "https://github.com/zherczeg/sljit.git" \
    "sljit" \
    "master"

# spirv-cross - SPIR-V cross-compilation (Metal/D3D12/WebGPU backends)
add_submodule "https://github.com/KhronosGroup/SPIRV-Cross.git" \
    "spirv-cross" \
    "master"

# DXC - DirectX Shader Compiler (D3D12 backend)
add_submodule "https://github.com/microsoft/DirectXShaderCompiler.git" \
    "dxc" \
    "main"

# tree-sitter - Incremental parsing for LSP/IDE tooling
add_submodule "https://github.com/tree-sitter/tree-sitter.git" \
    "tree-sitter" \
    "master"

# tree-sitter grammars for supported languages
add_submodule "https://github.com/tree-sitter/tree-sitter-c.git" \
    "tree-sitter-c" \
    "master"

add_submodule "https://github.com/tree-sitter/tree-sitter-cpp.git" \
    "tree-sitter-cpp" \
    "master"

add_submodule "https://github.com/tree-sitter/tree-sitter-rust.git" \
    "tree-sitter-rust" \
    "master"

add_submodule "https://github.com/tree-sitter/tree-sitter-python.git" \
    "tree-sitter-python" \
    "master"

add_submodule "https://github.com/tree-sitter/tree-sitter-go.git" \
    "tree-sitter-go" \
    "master"

add_submodule "https://github.com/tjdevries/tree-sitter-lua" \
    "tree-sitter-lua" \
    "master"

# =============================================================================
# Serialization & Data
# =============================================================================

# yyjson - Fast JSON parsing and serialization
add_submodule "https://github.com/ibireme/yyjson.git" \
    "yyjson" \
    "master"

# MessagePack (cmp) - Binary serialization
add_submodule "https://github.com/camgunz/cmp.git" \
    "cmp" \
    "master"

# zstd - General-purpose compression
add_submodule "https://github.com/facebook/zstd.git" \
    "zstd" \
    "v1.5.6"

# lz4 - Low-latency compression for streaming assets
add_submodule "https://github.com/lz4/lz4.git" \
    "lz4" \
    "v1.10.0"

# =============================================================================
# Networking & IPC
# =============================================================================

# libuv - Cross-platform async I/O (CLI and daemon mode)
add_submodule "https://github.com/libuv/libuv.git" \
    "libuv" \
    "v1.48.0"

# nng - Lightweight messaging for inter-process communication
add_submodule "https://github.com/nanomsg/nng.git" \
    "nng" \
    "v1.8.0"

# =============================================================================
# Diagnostics & Profiling
# =============================================================================

# Tracy - Frame profiler attachment point (Diagnostics API)
add_submodule "https://github.com/wolfpld/tracy.git" \
    "tracy" \
    "master"

# Perfetto - System-level tracing sink (typically system, but can vendor)
# add_submodule "https://android.googlesource.com/platform/external/perfetto" \
#     "perfetto" \
#     "master"

# =============================================================================
# Utilities
# =============================================================================

# mimalloc - High-performance memory allocator
add_submodule "https://github.com/microsoft/mimalloc.git" \
    "mimalloc" \
    "v2.1.7"

# xxHash - Fast non-cryptographic hashing
add_submodule "https://github.com/Cyan4973/xxHash.git" \
    "xxhash" \
    "v0.8.3"

# utf8.h - UTF-8 string utilities (single header)
add_submodule "https://github.com/sheredom/utf8.h.git" \
    "utf8.h" \
    "master"

# PCRE2 - Regular expression engine
add_submodule "https://github.com/PCRE2Project/PCRE2.git" \
    "PCRE2" \
    "pcre2-10.44"

# Klib - Multi-purpose simple lightweight libraries
add_submodule "https://github.com/attractivechaos/klib.git" \
    "klib" \
    "master"

# =============================================================================
# GPU Compute & Backend Support
# =============================================================================

# wgpu-native - WebGPU native implementation (WebGPU backend)
add_submodule "https://github.com/gfx-rs/webgpu-native" \
    "wgpu-native" \
    "master"

# VMA - Vulkan Memory Allocator (Vulkan backend)
add_submodule "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" \
    "vma" \
    "master"

# ===============================================================================
# Color Grading Libraries
# ===============================================================================

# OpenColorIO
add_submodule "https://github.com/AcademySoftwareFoundation/OpenColorIO" \
	"opencolorio" \
	"main"

# Color Transformation Language
add_submodule "https://github.com/aces-aswf/CTL" \
	"ctl" \
	"master"

# libraw
add_submodule "https://github.com/LibRaw/LibRaw" \
	"libraw" \
	"master"

# OpenEXR
add_submodule "https://github.com/AcademySoftwareFoundation/openexr" \
	"openexr" \
	"main"

# =============================================================================
# Numerical Libraries
# =============================================================================

# xsimd
add_submodule "https://github.com/xtensor-stack/xsimd" \
	"xsimd" \
	"master"

# EIGEN
add_submodule "https://github.com/PX4/eigen" \
	"eigen" \
	"master"

# Simdette
add_submodule "https://github.com/Chubek/simdette" \
	"simdette" \
	"master"


# =============================================================================
# Initialize and update all submodules
# =============================================================================

echo ""
echo "Initializing all submodules..."
git submodule update --init --recursive

echo ""
echo "=========================================="
echo "Submodule initialization complete!"
echo "=========================================="
echo ""
echo "Third-party libraries are now available in: ${THIRD_PARTY_DIR}"
echo ""
echo "To update all submodules to their pinned commits later, run:"
echo "  git submodule update --remote --recursive"
echo ""
echo "To add a new submodule, use the add_submodule function pattern above."
