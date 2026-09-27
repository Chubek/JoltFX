# JoltFX

High-performance, cross-platform effects engine for real-time graphics.

## Overview

JoltFX is a modular effects processing framework built on:
- **Tilly/TillyZ**: Zero-dependency runtime foundation
- **Core Engine**: Unified buffer/texture/kernel execution model
- **JoltScript**: Domain-specific language for effect kernels
- **Zoltan**: JoltScript compiler with live preview
- **Multiple Backends**: Vulkan, Metal, D3D12, WebGPU
- **Multiple Frontends**: Desktop GUI, CLI, web player, plugins

## Quick Start

```bash
# Configure and build
cmake --preset default
cmake --build build

# Run tests
ctest --test-dir build

# Run CLI
./build/frontends/cli/joltfx --help

# Compose one desktop Dear ImGui frame without a display server
./build/frontends/desktop/jfx_desktop --headless-smoke --backend webgpu
```

## Documentation

See `docs/` for architecture, API references, and guides.

## License

See LICENSE file.
