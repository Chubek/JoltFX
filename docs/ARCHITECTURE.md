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
│         JoltScript Glue Layer           │
├─────────────────────────────────────────┤
│       JoltScript Execution Layer        │
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

### JoltScript Execution
Bytecode VM for executing compiled effects. Handles parameter binding and state management.

### JoltScript Glue
Binds JoltScript VM to core engine resources. Marshals calls between script and native code.

### Backends
Hardware-specific implementations (Vulkan, Metal, D3D12, WebGPU) adhering to a common HAL.

### Extension Interfaces
Language bindings allowing effects to be controlled from Lua, mruby, QuickJS, or Python.

### Frontends
User-facing applications and plugins providing various interfaces to the engine.

