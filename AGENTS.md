# AGENTS.md — JoltFX

**Note to Agents**: Write your progress in `PROGRESS.md`.

## Overview

JoltFX is a professional-grade, extensible graphical suite for the creation and authoring of motion graphics. It is designed around a layered, composable architecture that separates concerns cleanly across its runtime, language, API, and interface tiers.

JoltFX exposes four interaction surfaces: a programmatic API set, a CLI, a TUI, and a GUI built on Dear ImGui. All surfaces operate over the same underlying Core engine, ensuring behavioral parity regardless of entry point.

---

## Architecture

### Layered Model

```
┌────────────────────────────────────────────────────────────┐
│  Interfaces: GUI (Dear ImGui), TUI, CLI, Web (joltvm.js)  │
├────────────────────────────────────────────────────────────┤
│  Extension Languages (sandboxed)                           │
│  Lua 5.4 · MRuby · MicroPython · QuickJS · WASM (Wasmtime) │
├────────────────────────────────────────────────────────────┤
│  Zoltan (Creative Programming Frontend)                    │
├────────────────────────────────────────────────────────────┤
│  Core Engine (Tilly / TillyZ)                              │
├────────────────────────────────────────────────────────────┤
│  Execution Layer                                           │
├────────────────────────────────────────────────────────────┤
│  Glue Layer                                                │
├────────────────────────────────────────────────────────────┤
│  Backends: Vulkan · Metal · D3D12 · WebGPU · Cairo · GTK   │
└────────────────────────────────────────────────────────────┘
```

### Data Flow

```
User Input → Interface API → Event API (XAS) → Core Engine
                                    ↓
                              Execution Layer
                                    ↓
                              Glue Layer (Joltscript → IR → Native)
                                    ↓
                              Hardware Abstraction Layer
                                    ↓
                              GPU/CPU Backends → Output
```

---

## Kernels

JoltFX operates on **kernels**: atomic micro-programs that define a single, well-scoped transformation or computation. Kernels are the fundamental unit of composition within JoltFX. The Core engine is assembled by stitching kernels together at initialization time, at plugin load time, and dynamically at runtime.

Kernels are authored in **Joltscript** (see below). Kernels are stateless by convention; shared state, if required, is managed explicitly through the Core engine's state primitives.

### Kernel Lifecycle

1. **Authoring** — Written in Joltscript (`.jolt` files)
2. **Compilation** — Joltscript → IR → Platform shader/binary (Glue Layer)
3. **Registration** — Kernel metadata and entry points registered with Core
4. **Scheduling** — Execution Layer resolves dependencies, allocates resources
5. **Execution** — CPU SIMD or GPU compute dispatch (Backend)
6. **Completion** — Results marshaled back, resources released

---

## Language Stack

### Joltscript

Joltscript is the primary kernel authoring language. It is a dialect of Lisp, designed for concise, data-oriented expression of transformation logic. Joltscript is implemented in C and can be compiled to the following targets:

- C
- Rust
- Python
- Go

Joltscript is fully interoperable with C and with every API language binding.

**Key characteristics:**
- Statically typed with type inference
- Hygienic macros with compile-time metaprogramming
- Zero-cost abstractions (compiles to native loops)
- Tail-call optimization guaranteed
- SIMD vectorization for image/array operations
- C ABI compatibility via `extern-c` / `export-c`

### Tilly

Tilly is the language in which the Core engine itself is implemented. It is fully interoperable with Joltscript. Tilly is implemented in Go and supports the following execution strategies:

- **VM backend**: Portable, interpreted execution
- **QBE backend**: Lightweight, fast ahead-of-time code generation
- **JIT engine**: Just-in-time compilation for hot paths (via SLJIT)
- **AOT engine**: Full ahead-of-time compilation for deployment targets

Tilly is bootstrapped by **TillyZ**, its self-hosting bootstrap compiler (zero-dependency, ~50KB, no libc required).

### Zoltan

Zoltan is a user-facing **creative programming language**, analogous in spirit to Processing. It is aimed at artists, motion designers, and technical creatives who want to script visual behavior without engaging the full API surface.

Zoltan ships with:

- A **TUI IDE** modeled on Processing's GUI IDE: minimal, focused, and extensible
- **LSP plugins** enabling Zoltan authoring in any editor or IDE that supports the Language Server Protocol
- **Package manager** with dependency resolution and signature verification
- **Live preview** with hot-reload via WebSocket server

---

## Execution and Glue Layers

### Glue Layer

The Glue layer is responsible for binding third-party vendored libraries that Joltscript kernels may depend on. It exposes a stable C ABI surface for each vendored library, ensuring that Joltscript's runtime can locate and invoke foreign code without coupling to the library's native API directly.

**Components:**
- **ABI Registry** — Symbol catalog and function pointer resolution
- **Marshal Engine** — Type conversion across all language boundaries
- **Capability Dispatcher** — Pre-call capability enforcement and routing
- **Lifetime Bridge** — Cross-model memory ownership synchronization
- **Error Adapter** — Unified error representation and propagation
- **Extension Host** — Third-party extension load, sandbox, and lifecycle

### Execution Layer

The Execution layer consumes the Glue layer's bindings and constructs the machinery required to schedule, dispatch, and manage kernel execution within the Core engine. It owns:

- **Scheduler** — Task ordering, dependency resolution, thread dispatch
- **Pipeline Manager** — Compile and cache multi-kernel pipelines
- **Kernel Runner** — Execute individual kernels with context and error handling
- **Resource Budget** — Enforce per-frame memory and time limits
- **Memory Manager** — Allocate, transfer, and pool CPU/GPU buffers
- **Frame Graph** — Declarative render/compute pass scheduling
- **Dependency Resolver** — Build and analyze task DAG
- **Command Buffer** — Low-level GPU command recording
- **Batch Compiler** — Fuse and optimize kernel sequences for target backends

---

## Core Engine APIs and ABIs

The Core engine exposes a set of **ABIs** (defined in C) and **APIs** (provided in multiple languages). The ABIs guarantee binary stability across Core engine versions. The APIs are the idiomatic programming surface for each supported language.

### Supported API Languages

- C++
- Rust
- Python
- Go

### API and ABI Inventory

#### Backend API
For implementing JoltFX rendering and output backends. A backend is responsible for translating the Core engine's render graph into concrete output: pixels on screen, frames to disk, or data to another system. The default package ships six backends: **Vulkan**, **Metal**, **D3D12**, **WebGPU**, **Cairo**, and **GTK**.

#### Plugin API
For implementing plugins that extend Core engine functionality. Plugins may register new kernels, expose new data types, attach to the event stream, and extend the asset pipeline.

#### Interface API
For implementing frontend interfaces that present JoltFX to the user. The built-in interfaces (GUI, TUI, CLI, Web) are themselves implemented against this API. Third-party interfaces — browser frontends, remote control surfaces, headless automation drivers — are first-class citizens via the same surface.

#### Render API
For implementing custom render passes and compositing pipelines. The Render API exposes the render graph as a first-class, programmable object: passes can be inserted, reordered, or replaced. It provides access to framebuffer resources, shader management, and GPU synchronization primitives, in a backend-agnostic fashion.

#### Effect API
For implementing visual effects, filters, and compositing operations. Effects are registered as typed kernel wrappers with declared input and output signatures. The Effect API handles parameter binding, GPU resource management, and integration with the timeline's keyframe system.

#### Asset API
For implementing asset importers, exporters, and resource lifecycle managers. The Asset API defines the contract for loading, caching, streaming, and unloading media: images, video, audio, fonts, vector artwork, and user-defined asset types registered by plugins.

#### Timeline API
For implementing and extending the animation sequencing system. The Timeline API exposes sequences, tracks, keyframes, and interpolation curves as programmable objects. It provides hooks for custom curve evaluators, custom track types, and integration with external timecode sources.

#### IO API
For implementing file format parsers, serializers, and project importers and exporters. The IO API manages codec discovery, streaming I/O, and format negotiation. Plugins implementing the IO API can add first-class support for new project file formats and media container formats.

#### Event API
For hooking into JoltFX's **XAS** (eXtensible Animation Stream) event stream. The Event API allows producers to emit typed events and consumers to subscribe to them with filtering, priority, and backpressure semantics. Backends, plugins, and interfaces all communicate through this stream; the Event API makes that communication available to external code.

#### Diagnostics API
For structured logging, distributed tracing, and profiling integration. The Diagnostics API provides a stable sink interface into which all JoltFX subsystems emit structured telemetry. Agents, plugins, and backends may attach their own sinks. The API supports span-based tracing and a profiler attachment point for tools such as Tracy and Perfetto.

---

## Extension Languages

JoltFX embeds five sandboxed extension runtimes. Extensions have access only to Core engine functionality through their respective JoltFX library. They have no direct access to system resources.

| Runtime    | Library         | Notes                                      |
|------------|-----------------|---------------------------------------------|
| Lua 5.4    | `ljoltfx`       | C ABI interop via standard Lua C API        |
| MRuby      | `joltfx-rb`     | C ABI interop via MRuby's C extension API   |
| MicroPython| `pyjoltfx`      | C ABI interop via MicroPython's C API       |
| QuickJS    | `joltfx.js`     | C ABI interop via QuickJS C API             |
| WASM       | `joltwasm`      | Hosted by Wasmtime; interop via WASI and component model bindings |

All five libraries share a fixed ABI defined in C. C interoperability is the mechanism by which each extension language library is implemented.

JoltFX uses **Wasmtime** as its WASM runtime.

### Extension Language Limitations vs. APIs

Extension languages are deliberately more constrained than the full API surface:

- No kernel registration
- No access to the Backend or Interface API
- No direct GPU resource management
- Execution is sandboxed; system calls are mediated by library functions only

For use cases that require these capabilities, a full plugin via the Plugin API is the appropriate path.

---

## JoltVM

JoltVM is JoltFX's included runtime and the default backend. It operates on JoltFX's **XAS** (eXtensible Animation Stream) event stream: a typed, ordered stream of animation events produced by the Core engine.

### Browser Target

JoltVM can run in the browser via the **`joltvm.js`** library, which implements the JoltVM backend as a mix of JavaScript, WASM, and WebGPU. This is distinct from the system-side QuickJS and Wasmtime runtimes, which run on the host process. `joltvm.js` is a browser-side runtime implementation.

---

## Tooling and Language Support

All languages in the JoltFX ecosystem — Joltscript, Tilly, Zoltan, and the extension languages — ship with:

- **LSP servers** for editor and IDE integration
- **Editor plugins** for major editors (VS Code, Neovim, Emacs, Zed, Sublime Text)
- **Formatters** (`joltfmt`, `tillyfmt`, `zoltanfmt`)
- **Documentation generators** (`joltdoc`, `tillydoc`, `zoltandoc`)
- **REPLs** (`jolti`, `tillyrepl`, `zoltani`)
- **Debugger adapters** (DAP-compatible)

---

## Third-Party Libraries

The following vendored or linked libraries are used across JoltFX's subsystems. All are managed through the Glue layer unless noted otherwise.

### Rendering and Graphics

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| Vulkan SDK           | Low-level GPU API (Vulkan backend)                    |
| Metal (system)       | GPU API on Apple platforms (Metal backend)            |
| D3D12 (system)       | GPU API on Windows (D3D12 backend)                    |
| WebGPU / wgpu        | GPU API for WebGPU backend and browser target         |
| Cairo                | 2D vector graphics (Cairo backend)                    |
| GTK 4                | Native widget toolkit (GTK backend)                   |
| Dear ImGui           | Immediate-mode GUI for the GUI interface              |

### Media and Asset Processing

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| FFmpeg               | Video and audio decoding, encoding, and muxing        |
| FreeType             | Font rasterisation                                    |
| HarfBuzz             | Text shaping and OpenType layout                      |
| libpng               | PNG image decode/encode                               |
| libjpeg-turbo        | JPEG image decode/encode                              |
| stb_image / stb_image_write | Lightweight auxiliary image format support   |
| NanoSVG              | SVG parsing and rasterisation                         |

### Extension Language Runtimes

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| Lua 5.4              | Lua extension runtime                                 |
| MRuby                | Ruby extension runtime                                |
| MicroPython          | Python extension runtime                              |
| QuickJS              | JavaScript extension runtime                          |
| Wasmtime             | WASM extension runtime (system-side)                  |

### Compilation and Code Generation

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| QBE                  | Lightweight compiler backend (Tilly QBE target)       |
| SLJIT                | JIT compilation backend (Tilly JIT engine)            |

### Parsing and Syntax

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| tree-sitter          | Incremental parsing for LSP and IDE tooling           |

### Serialisation and Data

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| yyjson               | Fast JSON parsing and serialisation                   |
| MessagePack (cmp)    | Binary serialisation for inter-process messaging      |
| zstd                 | General-purpose compression                           |
| lz4                  | Low-latency compression for streaming assets          |

### Networking and IPC

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| libuv                | Cross-platform async I/O (CLI and daemon mode)        |
| nanomsg / nng        | Lightweight messaging for inter-process communication |

### Diagnostics and Profiling

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| Tracy                | Frame profiler attachment point (Diagnostics API)     |
| Perfetto             | System-level tracing sink (Diagnostics API)           |

### Utilities

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| mimalloc             | High-performance memory allocator                     |
| xxHash               | Fast non-cryptographic hashing                        |
| utf8.h               | UTF-8 string utilities                                |
| PCRE2                | Regular expression engine                             |
| Klib                 | Multi-purpose simple lightweight libraries            |

---

## Repository Structure

```
joltfx/
├── AGENTS.md                 # This file
├── core/                     # Core engine (scheduler, memory, event, plugin, registry)
├── joltscript/               # Joltscript compiler, runtime, stdlib
│   ├── layers/
│   │   ├── glue/             # Glue layer (ABI, marshal, capability, lifetime, error, extension)
│   │   └── execution/        # Execution layer (scheduler, pipeline, kernel runner, budget, memory, frame graph, deps, cmd buffer, batch compiler)
├── tilly/                    # Tilly core runtime (TillyZ bootstrap + full runtime)
├── zoltan/                   # Zoltan CLI and toolchain (Rust)
├── kernels/                  # Built-in Joltscript kernels by category
├── backends/                 # GPU backends (Vulkan, Metal, D3D12, WebGPU)
├── extif/                    # Extension language bindings (Lua, MRuby, QuickJS, Python)
├── frontends/                # User-facing applications (Desktop, CLI, Web, Mobile, Plugins)
└── docs/                     # Generated and manual documentation
```

---

## Contribution Guidelines

### General Principles

1. **Read the relevant AGENTS.md** for the subsystem you're working on before starting
2. **Follow ownership rules** — each area has designated gatekeepers
3. **Write tests first** — new functionality requires test coverage
4. **No direct malloc/free** — use the engine's allocators (Tilly/arena/pool)
5. **No raw threads** — the scheduler owns all worker threads
6. **ABI stability** — public symbols require API review and version bumps
7. **Cross-platform** — core code must compile on Linux, macOS, Windows, WASM

### PR Requirements

- [ ] Builds clean with ASAN/UBSan enabled
- [ ] All existing tests pass
- [ ] New tests added for changed functionality
- [ ] Documentation updated (code comments + relevant AGENTS.md)
- [ ] Gate requirements met for affected areas
- [ ] No compiler warnings (Clang 15+, GCC 12+, MSVC 19.34+)

---

## Contacts

- **Engine architecture & scheduling**: `#joltfx-engine`
- **Glue Layer / compiler**: `#joltfx-compiler`
- **GPU backends**: `#joltfx-backend-core`
- **Public API changes**: Tag platform lead in PR
- **Kernel authoring**: `#joltfx-kernels`
- **Tooling & Zoltan**: `#joltfx-tooling`
- **Extensions / FFI**: `#joltfx-ext-core`
- **Frontends**: `#joltfx-frontend-core`
- **UI/UX design**: `#joltfx-design`
- **QA / conformance**: `#joltfx-qa`

---

## Versioning

JoltFX uses **semantic versioning** (MAJOR.MINOR.PATCH) for all public APIs, ABIs, and user-facing tools.

- **MAJOR**: Breaking ABI/API changes, removal of public symbols
- **MINOR**: New functionality, new public symbols, additive changes
- **PATCH**: Bug fixes, performance improvements, internal refactoring

The Glue Layer ABI version is tracked separately via `JOLT_GLUE_ABI_MAJOR/MINOR/PATCH`.
