# AGENTS.md — JoltFX

## Overview

JoltFX is a professional-grade, extensible graphical suite for the creation and
authoring of motion graphics. It is designed around a layered, composable
architecture that separates concerns cleanly across its runtime, language, API,
and interface tiers.

JoltFX exposes four interaction surfaces: a programmatic API set, a CLI, a TUI,
and a GUI built on Dear ImGui. All surfaces operate over the same underlying
Core engine, ensuring behavioral parity regardless of entry point.

---

## Architecture

### Layered Model

```
┌──────────────────────────────────────────┐
│  Interfaces: GUI (Dear ImGui), TUI, CLI  │
├──────────────────────────────────────────┤
│  Extension Languages (sandboxed)         │
│  Lua · MRuby · MicroPython · QuickJS ·   │
│  WASM (Wasmtime)                         │
├──────────────────────────────────────────┤
│  Zoltan (Creative Programming Frontend)  │
├──────────────────────────────────────────┤
│  Core Engine (Tilly / TillyZ)            │
├──────────────────────────────────────────┤
│  Execution Layer                         │
├──────────────────────────────────────────┤
│  Glue Layer                              │
├──────────────────────────────────────────┤
│  Backends: Vulkan · Cairo · GTK · Metal  │
└──────────────────────────────────────────┘
```
---

## Kernels

JoltFX operates on **kernels**: atomic micro-programs that define a single,
well-scoped transformation or computation. Kernels are the fundamental unit of
composition within JoltFX. The Core engine is assembled by stitching kernels
together at initialization time, at plugin load time, and dynamically at
runtime.

Kernels are authored in **Joltscript** (see below). Kernels are stateless by
convention; shared state, if required, is managed explicitly through the Core
engine's state primitives.

---

## Language Stack

### Joltscript

Joltscript is the primary kernel authoring language. It is a dialect of Lisp,
designed for concise, data-oriented expression of transformation logic.
Joltscript is implemented in C and can be compiled to the following targets:

- C
- Rust
- Python
- Go

Joltscript is fully interoperable with C and with every API language binding.

### Tilly

Tilly is the language in which the Core engine itself is implemented. It is
fully interoperable with Joltscript. Tilly is implemented in Go and supports
the following execution strategies:

- **VM backend**: portable, interpreted execution.
- **QBE backend**: lightweight, fast ahead-of-time code generation.
- **JIT engine**: just-in-time compilation for hot paths.
- **AOT engine**: full ahead-of-time compilation for deployment targets.

Tilly is bootstrapped by **TillyZ**, its self-hosting bootstrap compiler.

### Zoltan

Zoltan is a user-facing **creative programming language**, analogous in spirit
to Processing. It is aimed at artists, motion designers, and technical
creatives who want to script visual behavior without engaging the full API
surface.

Zoltan ships with:

- A **TUI IDE** modeled on Processing's GUI IDE: minimal, focused, and
  extensible.
- **LSP plugins** enabling Zoltan authoring in any editor or IDE that
  supports the Language Server Protocol.

---

## Execution and Glue Layers

### Glue Layer

The Glue layer is responsible for binding third-party vendored libraries that
Joltscript kernels may depend on. It exposes a stable C ABI surface for each
vendored library, ensuring that Joltscript's runtime can locate and invoke
foreign code without coupling to the library's native API directly.

### Execution Layer

The Execution layer consumes the Glue layer's bindings and constructs the
machinery required to schedule, dispatch, and manage kernel execution within
the Core engine. It owns:

- Kernel registration and lifecycle management.
- Dependency resolution between kernels.
- Execution ordering and pipeline scheduling.
- Error propagation and isolation boundaries.

---

## Core Engine APIs and ABIs

The Core engine exposes a set of **ABIs** (defined in C) and **APIs** (provided
in multiple languages). The ABIs guarantee binary stability across Core engine
versions. The APIs are the idiomatic programming surface for each supported
language.

### Supported API Languages

- C++
- Rust
- Python
- Go

### API and ABI Inventory

#### Backend API
For implementing JoltFX rendering and output backends. A backend is responsible
for translating the Core engine's render graph into concrete output: pixels on
screen, frames to disk, or data to another system. The default package ships
four backends: **Vulkan**, **Cairo**, **GTK**, and **Metal**.

#### Plugin API
For implementing plugins that extend Core engine functionality. Plugins may
register new kernels, expose new data types, attach to the event stream, and
extend the asset pipeline.

#### Interface API
For implementing frontend interfaces that present JoltFX to the user. The
built-in interfaces (GUI, TUI, CLI) are themselves implemented against this
API. Third-party interfaces — browser frontends, remote control surfaces,
headless automation drivers — are first-class citizens via the same surface.

#### Render API
For implementing custom render passes and compositing pipelines. The Render API
exposes the render graph as a first-class, programmable object: passes can be
inserted, reordered, or replaced. It provides access to framebuffer resources,
shader management, and GPU synchronization primitives, in a backend-agnostic
fashion.

#### Effect API
For implementing visual effects, filters, and compositing operations. Effects
are registered as typed kernel wrappers with declared input and output
signatures. The Effect API handles parameter binding, GPU resource management,
and integration with the timeline's keyframe system.

#### Asset API
For implementing asset importers, exporters, and resource lifecycle managers.
The Asset API defines the contract for loading, caching, streaming, and
unloading media: images, video, audio, fonts, vector artwork, and
user-defined asset types registered by plugins.

#### Timeline API
For implementing and extending the animation sequencing system. The Timeline
API exposes sequences, tracks, keyframes, and interpolation curves as
programmable objects. It provides hooks for custom curve evaluators, custom
track types, and integration with external timecode sources.

#### IO API
For implementing file format parsers, serializers, and project importers and
exporters. The IO API manages codec discovery, streaming I/O, and format
negotiation. Plugins implementing the IO API can add first-class support for
new project file formats and media container formats.

#### Event API
For hooking into JoltFX's **XAS** event stream. The Event API allows producers
to emit typed events and consumers to subscribe to them with filtering,
priority, and backpressure semantics. Backends, plugins, and interfaces all
communicate through this stream; the Event API makes that communication
available to external code.

#### Diagnostics API
For structured logging, distributed tracing, and profiling integration. The
Diagnostics API provides a stable sink interface into which all JoltFX
subsystems emit structured telJoltFX
subsystems emit structured telemetry. Agents, plugins, and backends may attach
their own sinks. The API supports span-based ts and a profiler attachment point for tools such as Tracy.

---

## Extension Languages

JoltFX embeds five sandboxed extension runtimes. Extensions have access only to
Core engine functionality through their respective JoltFX library. They have no
direct access to system resources.

| Runtime    | Library         | Notes                                      |
|------------|-----------------|---------------------------------------------|
| Lua 5.4    | `ljoltfx`       | C ABI interop via standard Lua C API        |
| MRuby      | `joltfx-rb`     | C ABI interop via MRuby's C extension API   |
| MicroPython| `pyjoltfx`      | C ABI interop via MicroPython's C API       |
| QuickJS    | `joltfx.js`     | C ABI interop via QuickJS C API             |
| WASM       | `joltwasm`      | Hosted by Wasmtime; interop via WASI and    |
|            |                 | component model bindings                    |

All five libraries share a fixed ABI defined in C. C interoperability is the
mechanism by which each extension language library is implemented.

JoltFX uses **Wasmtime** as its WASM runtime.

### Extension Language Limitations vs. APIs

Extension languages are deliberately more constrained than the full API surface:

- No kernel registration.
- No access to the Backend or Interface API.
- No direct GPU resource management.
- Execution is sandboxed; system calls are mediated by library functions only.

For use cases that require these capabilities, a full plugin via the Plugin API
is the appropriate path.

---

## JoltVM

JoltVM is JoltFX's included runtime and the default backend. It operates on
JoltFX's **XAS** (eXtensible Animation Stream) event stream: a typed,
ordered stream of animation events produced by the Core engine.

### Browser Target

JoltVM can run in the browser via the **`joltvm.js`** library, which implements
the JoltVM backend as a mix of JavaScript, WASM, and WebGPU. This is distinct
from the system-side QuickJS and Wasmtime runtimes, which run on the host
process. `joltvm.js` is a browser-side runtime implementation.

---

## Tooling and Language Support

All languages in the JoltFX ecosystem — Joltscript, Tilly, Zoltan, and the
extension languages — ship with:

- **LSP servers** for editor and IDE integration.
- **Editor plugins** for major edit
The previous response was cut off and had a garbled Diagnostics API section. Here is the complete, corrected document:

## Third-Party Libraries

The following vendored or linked libraries are used across JoltFX's subsystems.
All are managed through the Glue layer unless noted otherwise.

### Rendering and Graphics

| Library              | Purpose                                               |
|----------------------|-------------------------------------------------------|
| Vulkan SDK           | Low-level GPU API (Vulkan backend)                    |
| Cairo                | 2D vector graphics (Cairo backend)                    |
| GTK 4                | Native widget toolkit (GTK backend)                   |
| Metal (system)       | GPU API on Apple platforms (Metal backend)            |
| WebGPU / wgpu        | GPU API for the browser target (`joltvm.js`)          |
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
| Sljit                | JIT compilation backend (Tilly JIT engine)            |

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
