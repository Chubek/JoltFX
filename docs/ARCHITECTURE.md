# JoltFX Architecture

## Overview

JoltFX is structured in layers, from low-level runtime to high-level frontends.

## Layer Diagram


┌─────────────────────────────────────────┐
│         Frontends                       │
│  (Desktop, CLI, Web, Mobile, Plugins)   │
├─────────────────────────────────────────┤
│         Extension Interfaces            │
│      (Lua, mruby, QuickJS, Python)      │
├─────────────────────────────────────────┤
│         Joltscript Glue Layer           │
├─────────────────────────────────────────┤
│       Joltscript Execution Layer        │
├─────────────────────────────────────────┤
│         Core Engine                     │
│  (Scheduler, Memory, Event Bus)         │
├─────────────────────────────────────────┤
│         Backend Abstraction             │
│  (Vulkan, Metal, D3D12, WebGPU)         │
├─────────────────────────────────────────┤
│         Tilly Runtime                   │
│  (Allocator, Logger, Module System)     │
├─────────────────────────────────────────┤
│         TillyZ Bootstrap                │
│      (Zero-Dependency Foundation)       │
└─────────────────────────────────────────┘

## Component Responsibilities

### TillyZ
Minimal bootstrap layer with zero dependencies. Provides panic handler and basic context management.

### Tilly
Full runtime services: custom allocators, structured logging, dynamic module loading.

### Core Engine
Unified API for buffers, textures, and kernel execution. Manages resource lifecycle and scheduling.

### Joltscript Execution
Execution ABI 0.9 supports the legacy f32 stack VM (JBC1 version 1) and a typed,
budgeted tree VM (version 2). Typed programs support exact primitives, loops,
functions/closures, immutable collections and six built-in library modules with
262 exports. The runtime owns invocation arenas, callback/regex step budgets
and failure-atomic output publication. Tagged string/collection views are
VM-owned; pixel APIs consume numeric results. See the
[language](../joltscript/docs/language.md) and [library](../joltscript/docs/stdlib.md)
references for limits and ownership.

### Joltscript Glue
Compiles source into validated JBC1 and binds execution to engine resources.
C and Zoltan independently parse/serialize identical bytecode; Zoltan shares the
portable C typed checker/VM through its allocator bridge. Built-in imports
resolve against a catalogue generated from `joltscript/stdlib/*.jolt`.
The CPU image profile used by image effects is a separate bounded AST interpreter,
with its own sampling/resource contract and no standard-library imports.

### Backends
Vulkan provides version-1 device dispatch; version-2 programs use CPU fallback
and report `used_gpu=false`. Metal, D3D12, and WebGPU currently share a
validated CPU fallback adapter so every selected backend preserves the JBC1
pixel contract while native dispatch work proceeds.

### Extension Interfaces
Lua and mruby are embedded sandboxed hosts with Tilly allocation, bounded
execution, explicit GC controls, and a small `jfx` namespace. They expose no
filesystem, process, package loading, raw buffer, or kernel-registration API.

### Frontends
User-facing applications and plugins providing various interfaces to the engine.
The desktop target uses Dear ImGui to compose its editor frame; platform window
creation and draw-data presentation remain separate backend integration work.
