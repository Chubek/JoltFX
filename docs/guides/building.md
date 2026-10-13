# Building JoltFX

## Prerequisites

- CMake 3.20+
- C11 compiler (GCC, Clang, MSVC)
- Rust 1.70+ (for Zoltan compiler)
- Python 3 (generates Joltscript's built-in standard-library catalogue)
- Archiver (`ar` or MSVC `lib`, also needed by Zoltan's C runtime bridge)
- Vulkan SDK (optional, for Vulkan backend)

## Build Steps

PCRE2 is built from `third_party/PCRE2` for Joltscript regex support. Zoltan uses
the same vendored runtime and needs CMake, Python and a C11 toolchain as well as
Rust; `cargo build --manifest-path zoltan/Cargo.toml` needs no registry packages.
`CC`, `AR`, `PYTHON` and `CMAKE_TOOLCHAIN_FILE` configure its native dependencies.
See the [library build notes](../../joltscript/docs/stdlib.md#budgets-ownership-and-builds).

```bash
# Configure
cmake --preset default

# Build
cmake --build build

# Run tests
ctest --test-dir build

# Run CLI
./build/frontends/cli/joltfx help
```

## Build Options

- `JOLTFX_BUILD_TESTS`: Build test suite (default: ON)
- `JOLTFX_BUILD_EXAMPLES`: Build examples (default: ON)
- `JOLTFX_BUILD_DOCS`: Build documentation (default: OFF)
