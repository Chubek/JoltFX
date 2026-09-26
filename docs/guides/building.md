# Building JoltFX

## Prerequisites

- CMake 3.20+
- C11 compiler (GCC, Clang, MSVC)
- Rust 1.70+ (for Zoltan compiler)
- Vulkan SDK (optional, for Vulkan backend)

## Build Steps

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

