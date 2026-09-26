#!/usr/bin/env bash
set -euo pipefail

# JoltFX Project Scaffolding Script
# Creates the complete directory structure and placeholder files

PROJECT_ROOT="$(pwd)"
PROJECT_NAME="JoltFX"
PROJECT_VERSION="0.1.0"

echo "🚀 Scaffolding $PROJECT_NAME project structure..."

# ============================================================================
# 1. ROOT-LEVEL FILES
# ============================================================================

echo
echo "📄 Creating root-level files..."

# README.md
cat > README.md << 'EOF'
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
```

## Documentation

See `docs/` for architecture, API references, and guides.

## License

See LICENSE file.
EOF

# CHANGELOG.md
cat > CHANGELOG.md << 'EOF'
# Changelog

All notable changes to JoltFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- Initial project structure
- Core engine API design
- Tilly runtime foundation
- JoltScript language specification

EOF

# ROADMAP.md
cat > ROADMAP.md << 'EOF'
# JoltFX Roadmap

## Phase 1: Foundation (Q1 2027)
- [ ] TillyZ bootstrap implementation
- [ ] Tilly runtime (allocator, logger, module system)
- [ ] Core engine skeleton (init/shutdown/basic API)
- [ ] Basic CMake build system

## Phase 2: Core Functionality (Q2 2027)
- [ ] JoltScript execution and glue layers
- [ ] Vulkan backend implementation
- [ ] Essential kernel library (10+ effects)
- [ ] Zoltan compiler MVP

## Phase 3: Integration (Q3 2027)
- [ ] CLI frontend
- [ ] Unit and integration tests
- [ ] CI/CD pipeline
- [ ] Hello World example

## Phase 4: Expansion (Q4 2027)
- [ ] Additional backends (Metal, D3D12, WebGPU)
- [ ] Desktop GUI frontend
- [ ] Extension language bindings (Lua, mruby)
- [ ] Comprehensive documentation

## Phase 5: Production (2028)
- [ ] Web and mobile frontends
- [ ] Plugin system (After Effects, Premiere, DaVinci)
- [ ] Performance optimization
- [ ] Public beta release

EOF

# .gitignore
cat > .gitignore << 'EOF'
# Build artifacts
build/
cmake-build-*/
*.o
*.a
*.so
*.dylib
*.dll
*.exe

# IDE and editor
.vscode/
.idea/
*.swp
*.swo
*~
.DS_Store

# Rust
target/
Cargo.lock

# Node.js
node_modules/
package-lock.json

# Python
__pycache__/
*.pyc
*.pyo
.pytest_cache/

# Documentation
docs/_build/
*.pdf

# Package artifacts
*.deb
*.rpm
*.tar.gz
*.zip

# Test outputs
test_results/
*.log

EOF

# .clang-format
cat > .clang-format << 'EOF'
---
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
PointerAlignment: Right
AlignConsecutiveAssignments: false
AlignConsecutiveDeclarations: false
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: Never
BreakBeforeBraces: Linux
IndentCaseLabels: false
SortIncludes: true
SpaceAfterCStyleCast: true
EOF

# .clang-tidy
cat > .clang-tidy << 'EOF'
---
Checks: >
  clang-diagnostic-*,
  clang-analyzer-*,
  bugprone-*,
  performance-*,
  portability-*,
  readability-*,
  -readability-identifier-length,
  -readability-magic-numbers
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
AnalyzeTemporaryDtors: false
FormatStyle: file
EOF

# .editorconfig
cat > .editorconfig << 'EOF'
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{c,h,cpp,hpp}]
indent_style = space
indent_size = 4

[*.{rs,toml}]
indent_style = space
indent_size = 4

[*.{js,json,html,css}]
indent_style = space
indent_size = 2

[CMakeLists.txt,*.cmake]
indent_style = space
indent_size = 2

[Makefile]
indent_style = tab
EOF

# version.txt
echo "$PROJECT_VERSION" > version.txt

# Doxyfile
cat > Doxyfile << 'EOF'
PROJECT_NAME           = "JoltFX"
PROJECT_NUMBER         = 0.1.0
OUTPUT_DIRECTORY       = docs/_build
INPUT                  = core/include tilly/include joltscript/layers/execution/include joltscript/layers/glue/include
RECURSIVE              = YES
EXTRACT_ALL            = YES
GENERATE_LATEX         = NO
GENERATE_HTML          = YES
HTML_OUTPUT            = html
EOF

# ============================================================================
# 2. CMAKE CONFIGURATION
# ============================================================================

echo "⚙️  Creating CMake configuration..."

# Root CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.20)
project(JoltFX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

option(JOLTFX_BUILD_TESTS "Build tests" ON)
option(JOLTFX_BUILD_EXAMPLES "Build examples" ON)
option(JOLTFX_BUILD_DOCS "Build documentation" OFF)

include(cmake/modules/CompilerWarnings.cmake)

add_subdirectory(tilly)
add_subdirectory(core)
add_subdirectory(joltscript)
add_subdirectory(backends)
add_subdirectory(extif)
add_subdirectory(kernels)
add_subdirectory(frontends)

if(JOLTFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JOLTFX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(cmake/packaging/install_rules.cmake)
EOF

# CMakePresets.json
cat > CMakePresets.json << 'EOF'
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 20,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "default",
      "displayName": "Default Config",
      "description": "Default build configuration",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "JOLTFX_BUILD_TESTS": "ON",
        "JOLTFX_BUILD_EXAMPLES": "ON"
      }
    },
    {
      "name": "release",
      "inherits": "default",
      "displayName": "Release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "JOLTFX_BUILD_TESTS": "OFF"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "default",
      "configurePreset": "default"
    },
    {
      "name": "release",
      "configurePreset": "release"
    }
  ],
  "testPresets": [
    {
      "name": "default",
      "configurePreset": "default",
      "output": {"outputOnFailure": true}
    }
  ]
}
EOF

mkdir -p cmake/modules cmake/toolchains cmake/packaging

# cmake/modules/CompilerWarnings.cmake
cat > cmake/modules/CompilerWarnings.cmake << 'EOF'
function(set_project_warnings target_name)
    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )

    set(GCC_WARNINGS ${CLANG_WARNINGS})

    set(MSVC_WARNINGS
        /W4
        /w14640
        /permissive-
    )

    if(CMAKE_C_COMPILER_ID MATCHES ".*Clang")
        set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(PROJECT_WARNINGS ${GCC_WARNINGS})
    elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    endif()

    target_compile_options(${target_name} PRIVATE ${PROJECT_WARNINGS})
endfunction()
EOF

# cmake/modules/FindVulkan.cmake
cat > cmake/modules/FindVulkan.cmake << 'EOF'
# Placeholder for Vulkan finder
# Use find_package(Vulkan REQUIRED) in CMake 3.21+
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan SDK not found, skipping Vulkan backend")
endif()
EOF

# cmake/packaging/install_rules.cmake
cat > cmake/packaging/install_rules.cmake << 'EOF'
include(GNUInstallDirs)

install(TARGETS jfx_core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY core/include/jfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
EOF

# ============================================================================
# 3. TILLY RUNTIME
# ============================================================================

echo "🔧 Creating Tilly runtime structure..."

mkdir -p tilly/tillyz/{include/tillyz,src}
mkdir -p tilly/{include/tilly,src}

# tilly/CMakeLists.txt
cat > tilly/CMakeLists.txt << 'EOF'
add_subdirectory(tillyz)

add_library(tilly STATIC
    src/allocator.c
    src/logger.c
    src/module.c
)

target_include_directories(tilly PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tilly PUBLIC tillyz)

set_project_warnings(tilly)
EOF

# tilly/tillyz/include/tillyz/tillyz.h
cat > tilly/tillyz/include/tillyz/tillyz.h << 'EOF'
#ifndef TILLYZ_H
#define TILLYZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bootstrap API - zero dependency runtime foundation
typedef struct tillyz_context tillyz_context_t;

// Initialize the bootstrap runtime
tillyz_context_t *tillyz_init(void);

// Shutdown and cleanup
void tillyz_shutdown(tillyz_context_t *ctx);

// Panic handler
void tillyz_panic(const char *message);

#ifdef __cplusplus
}
#endif

#endif // TILLYZ_H
EOF

# tilly/tillyz/src/bootstrap.c
cat > tilly/tillyz/src/bootstrap.c << 'EOF'
#include "tillyz/tillyz.h"
#include <stdio.h>
#include <stdlib.h>

struct tillyz_context {
    int initialized;
};

tillyz_context_t *tillyz_init(void) {
    tillyz_context_t *ctx = malloc(sizeof(tillyz_context_t));
    if (!ctx) {
        tillyz_panic("Failed to allocate bootstrap context");
    }
    ctx->initialized = 1;
    return ctx;
}

void tillyz_shutdown(tillyz_context_t *ctx) {
    if (ctx) {
        free(ctx);
    }
}

void tillyz_panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message);
    abort();
}
EOF

# tilly/tillyz/CMakeLists.txt
cat > tilly/tillyz/CMakeLists.txt << 'EOF'
add_library(tillyz STATIC
    src/bootstrap.c
)

target_include_directories(tillyz PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_project_warnings(tillyz)
EOF

# tilly/include/tilly/tilly.h
cat > tilly/include/tilly/tilly.h << 'EOF'
#ifndef TILLY_H
#define TILLY_H

#include "tillyz/tillyz.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tilly/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_runtime tilly_runtime_t;

// Initialize the full Tilly runtime
tilly_runtime_t *tilly_init(const tilly_allocator_t *allocator);

// Shutdown the runtime
void tilly_shutdown(tilly_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif // TILLY_H
EOF

# tilly/include/tilly/allocator.h
cat > tilly/include/tilly/allocator.h << 'EOF'
#ifndef TILLY_ALLOCATOR_H
#define TILLY_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_allocator {
    void *(*alloc)(size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);
    void *user_data;
} tilly_allocator_t;

// Get the default system allocator
const tilly_allocator_t *tilly_default_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // TILLY_ALLOCATOR_H
EOF

# tilly/include/tilly/logger.h
cat > tilly/include/tilly/logger.h << 'EOF'
#ifndef TILLY_LOGGER_H
#define TILLY_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TILLY_LOG_DEBUG,
    TILLY_LOG_INFO,
    TILLY_LOG_WARN,
    TILLY_LOG_ERROR
} tilly_log_level_t;

void tilly_log(tilly_log_level_t level, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // TILLY_LOGGER_H
EOF

# tilly/include/tilly/module.h
cat > tilly/include/tilly/module.h << 'EOF'
#ifndef TILLY_MODULE_H
#define TILLY_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_module tilly_module_t;

typedef int (*tilly_module_init_fn)(void);
typedef void (*tilly_module_shutdown_fn)(void);

tilly_module_t *tilly_module_load(const char *name);
void tilly_module_unload(tilly_module_t *module);

#ifdef __cplusplus
}
#endif

#endif // TILLY_MODULE_H
EOF

# tilly/src/allocator.c
cat > tilly/src/allocator.c << 'EOF'
#include "tilly/allocator.h"
#include <stdlib.h>

static void *default_alloc(size_t size, void *user_data) {
    (void)user_data;
    return malloc(size);
}

static void default_free(void *ptr, void *user_data) {
    (void)user_data;
    free(ptr);
}

static tilly_allocator_t default_allocator = {
    .alloc = default_alloc,
    .free = default_free,
    .user_data = NULL
};

const tilly_allocator_t *tilly_default_allocator(void) {
    return &default_allocator;
}
EOF

# tilly/src/logger.c
cat > tilly/src/logger.c << 'EOF'
#include "tilly/logger.h"
#include <stdio.h>
#include <stdarg.h>

void tilly_log(tilly_log_level_t level, const char *format, ...) {
    const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    fprintf(stderr, "[%s] ", level_str[level]);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}
EOF

# tilly/src/module.c
cat > tilly/src/module.c << 'EOF'
#include "tilly/module.h"
#include <stdlib.h>

struct tilly_module {
    char *name;
};

tilly_module_t *tilly_module_load(const char *name) {
    (void)name;
    // TODO: Implement dynamic module loading
    return NULL;
}

void tilly_module_unload(tilly_module_t *module) {
    if (module) {
        free(module);
    }
}
EOF

# ============================================================================
# 4. CORE ENGINE
# ============================================================================

echo "🎨 Creating core engine structure..."

mkdir -p core/{include/jfx,src}

# core/CMakeLists.txt
cat > core/CMakeLists.txt << 'EOF'
add_library(jfx_core STATIC
    src/engine.c
    src/scheduler.c
    src/memory.c
    src/event_bus.c
)

target_include_directories(jfx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_core PUBLIC tilly)

set_project_warnings(jfx_core)
EOF

# core/include/jfx/jfx_engine.h
cat > core/include/jfx/jfx_engine.h << 'EOF'
#ifndef JFX_ENGINE_H
#define JFX_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_engine jfx_engine_t;

typedef enum {
    JFX_SUCCESS = 0,
    JFX_ERROR_INVALID_ARGUMENT = -1,
    JFX_ERROR_OUT_OF_MEMORY = -2,
    JFX_ERROR_NOT_INITIALIZED = -3,
    JFX_ERROR_BACKEND_FAILURE = -4
} jfx_result_t;

typedef struct {
    uint32_t max_buffers;
    uint32_t max_textures;
    uint32_t max_kernels;
    const char *backend_name; // "vulkan", "metal", "d3d12", "webgpu", NULL for auto
} jfx_engine_config_t;

// Initialize the engine
jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine);

// Shutdown the engine
void jfx_engine_shutdown(jfx_engine_t *engine);

// Process one frame
jfx_result_t jfx_engine_tick(jfx_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif // JFX_ENGINE_H
EOF

# core/include/jfx/jfx_buffer.h
cat > core/include/jfx/jfx_buffer.h << 'EOF'
#ifndef JFX_BUFFER_H
#define JFX_BUFFER_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_buffer jfx_buffer_t;

typedef enum {
    JFX_BUFFER_USAGE_TRANSFER_SRC = 1 << 0,
    JFX_BUFFER_USAGE_TRANSFER_DST = 1 << 1,
    JFX_BUFFER_USAGE_STORAGE = 1 << 2
} jfx_buffer_usage_flags_t;

jfx_result_t jfx_buffer_create(
    jfx_engine_t *engine,
    size_t size,
    uint32_t usage_flags,
    jfx_buffer_t **out_buffer
);

void jfx_buffer_destroy(jfx_buffer_t *buffer);

jfx_result_t jfx_buffer_write(jfx_buffer_t *buffer, size_t offset, const void *data, size_t size);

jfx_result_t jfx_buffer_read(jfx_buffer_t *buffer, size_t offset, void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // JFX_BUFFER_H
EOF

# core/include/jfx/jfx_texture.h
cat > core/include/jfx/jfx_texture.h << 'EOF'
#ifndef JFX_TEXTURE_H
#define JFX_TEXTURE_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_texture jfx_texture_t;

typedef enum {
    JFX_FORMAT_R8G8B8A8_UNORM,
    JFX_FORMAT_R32G32B32A32_SFLOAT,
    JFX_FORMAT_R16G16B16A16_SFLOAT
} jfx_format_t;

jfx_result_t jfx_texture_create(
    jfx_engine_t *engine,
    uint32_t width,
    uint32_t height,
    jfx_format_t format,
    jfx_texture_t **out_texture
);

void jfx_texture_destroy(jfx_texture_t *texture);

#ifdef __cplusplus
}
#endif

#endif // JFX_TEXTURE_H
EOF

# core/include/jfx/jfx_kernel.h
cat > core/include/jfx/jfx_kernel.h << 'EOF'
#ifndef JFX_KERNEL_H
#define JFX_KERNEL_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_kernel jfx_kernel_t;

jfx_result_t jfx_kernel_load(
    jfx_engine_t *engine,
    const char *path,
    jfx_kernel_t **out_kernel
);

void jfx_kernel_destroy(jfx_kernel_t *kernel);

jfx_result_t jfx_kernel_execute(jfx_kernel_t *kernel);

#ifdef __cplusplus
}
#endif

#endif // JFX_KERNEL_H
EOF

# core/include/jfx/jfx_result.h
cat > core/include/jfx/jfx_result.h << 'EOF'
#ifndef JFX_RESULT_H
#define JFX_RESULT_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *jfx_result_to_string(jfx_result_t result);

#ifdef __cplusplus
}
#endif

#endif // JFX_RESULT_H
EOF

# core/src/engine.c
cat > core/src/engine.c << 'EOF'
#include "jfx/jfx_engine.h"
#include "tilly/tilly.h"
#include <stdlib.h>
#include <string.h>

struct jfx_engine {
    tilly_runtime_t *runtime;
    jfx_engine_config_t config;
};

jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine) {
    if (!config || !out_engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_engine_t *engine = malloc(sizeof(jfx_engine_t));
    if (!engine) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    engine->runtime = tilly_init(tilly_default_allocator());
    if (!engine->runtime) {
        free(engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    memcpy(&engine->config, config, sizeof(jfx_engine_config_t));

    tilly_log(TILLY_LOG_INFO, "JoltFX engine initialized");

    *out_engine = engine;
    return JFX_SUCCESS;
}

void jfx_engine_shutdown(jfx_engine_t *engine) {
    if (engine) {
        tilly_log(TILLY_LOG_INFO, "JoltFX engine shutting down");
        tilly_shutdown(engine->runtime);
        free(engine);
    }
}

jfx_result_t jfx_engine_tick(jfx_engine_t *engine) {
    if (!engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    // TODO: Implement frame processing
    return JFX_SUCCESS;
}
EOF

# core/src/scheduler.c
cat > core/src/scheduler.c << 'EOF'
#include "tilly/logger.h"

// Placeholder for scheduler implementation
void scheduler_init(void) {
    tilly_log(TILLY_LOG_DEBUG, "Scheduler initialized");
}
EOF

# core/src/memory.c
cat > core/src/memory.c << 'EOF'
#include "tilly/logger.h"

// Placeholder for memory management
void memory_init(void) {
    tilly_log(TILLY_LOG_DEBUG, "Memory subsystem initialized");
}
EOF

# core/src/event_bus.c
cat > core/src/event_bus.c << 'EOF'
#include "tilly/logger.h"

// Placeholder for event bus
void event_bus_init(void) {
    tilly_log(TILLY_LOG_DEBUG, "Event bus initialized");
}
EOF

# ============================================================================
# 5. JOLTSCRIPT
# ============================================================================

echo "📜 Creating JoltScript structure..."

mkdir -p joltscript/layers/{execution/{include/joltscript,src},glue/{include/joltscript,src}}

# joltscript/CMakeLists.txt
cat > joltscript/CMakeLists.txt << 'EOF'
add_subdirectory(layers/execution)
add_subdirectory(layers/glue)
EOF

# joltscript/layers/execution/CMakeLists.txt
cat > joltscript/layers/execution/CMakeLists.txt << 'EOF'
add_library(joltscript_execution STATIC
    src/vm.c
    src/bytecode.c
)

target_include_directories(joltscript_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_execution PUBLIC tilly)

set_project_warnings(joltscript_execution)
EOF

# joltscript/layers/execution/include/joltscript/vm.h
cat > joltscript/layers/execution/include/joltscript/vm.h << 'EOF'
#ifndef JOLTSCRIPT_VM_H
#define JOLTSCRIPT_VM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jolt_vm jolt_vm_t;

jolt_vm_t *jolt_vm_create(void);
void jolt_vm_destroy(jolt_vm_t *vm);

int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *bytecode, size_t size);

#ifdef __cplusplus
}
#endif

#endif // JOLTSCRIPT_VM_H
EOF

# joltscript/layers/execution/src/vm.c
cat > joltscript/layers/execution/src/vm.c << 'EOF'
#include "joltscript/vm.h"
#include <stdlib.h>

struct jolt_vm {
    int dummy;
};

jolt_vm_t *jolt_vm_create(void) {
    return calloc(1, sizeof(jolt_vm_t));
}

void jolt_vm_destroy(jolt_vm_t *vm) {
    free(vm);
}

int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *bytecode, size_t size) {
    (void)vm;
    (void)bytecode;
    (void)size;
    // TODO: Implement bytecode execution
    return 0;
}
EOF

# joltscript/layers/execution/src/bytecode.c
cat > joltscript/layers/execution/src/bytecode.c << 'EOF'
// Placeholder for bytecode utilities
EOF

# joltscript/layers/glue/CMakeLists.txt
cat > joltscript/layers/glue/CMakeLists.txt << 'EOF'
add_library(joltscript_glue STATIC
    src/bindings.c
)

target_include_directories(joltscript_glue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_glue PUBLIC
    joltscript_execution
    jfx_core
)

set_project_warnings(joltscript_glue)
EOF

# joltscript/layers/glue/include/joltscript/bindings.h
cat > joltscript/layers/glue/include/joltscript/bindings.h << 'EOF'
#ifndef JOLTSCRIPT_BINDINGS_H
#define JOLTSCRIPT_BINDINGS_H

#ifdef __cplusplus
extern "C" {
#endif

void jolt_register_core_bindings(void);

#ifdef __cplusplus
}
#endif

#endif // JOLTSCRIPT_BINDINGS_H
EOF

# joltscript/layers/glue/src/bindings.c
cat > joltscript/layers/glue/src/bindings.c << 'EOF'
#include "joltscript/bindings.h"
#include "tilly/logger.h"

void jolt_register_core_bindings(void) {
    tilly_log(TILLY_LOG_INFO, "Registering core JoltScript bindings");
    // TODO: Register buffer, texture, kernel bindings
}
EOF

# ============================================================================
# 6. BACKENDS
# ============================================================================

echo "🎮 Creating backends structure..."

mkdir -p backends/{vulkan/{include,src},metal/{include,src},d3d12/{include,src},webgpu/{include,src}}

# backends/CMakeLists.txt
cat > backends/CMakeLists.txt << 'EOF'
add_subdirectory(vulkan)
# add_subdirectory(metal)
# add_subdirectory(d3d12)
# add_subdirectory(webgpu)
EOF

# backends/vulkan/CMakeLists.txt
cat > backends/vulkan/CMakeLists.txt << 'EOF'
find_package(Vulkan)

if(Vulkan_FOUND)
    add_library(jfx_backend_vulkan STATIC
        src/vulkan_backend.c
    )

    target_include_directories(jfx_backend_vulkan PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(jfx_backend_vulkan PUBLIC
        jfx_core
        Vulkan::Vulkan
    )

    set_project_warnings(jfx_backend_vulkan)
else()
    message(STATUS "Vulkan not found, skipping Vulkan backend")
endif()
EOF

# backends/vulkan/src/vulkan_backend.c
cat > backends/vulkan/src/vulkan_backend.c << 'EOF'
#include "tilly/logger.h"
// #include <vulkan/vulkan.h>

void vulkan_backend_init(void) {
    tilly_log(TILLY_LOG_INFO, "Vulkan backend placeholder");
    // TODO: Initialize Vulkan backend
}
EOF

# ============================================================================
# 7. KERNELS
# ============================================================================

echo "⚡ Creating kernels structure..."

mkdir -p kernels/{transform,color,blur,tests}

# kernels/CMakeLists.txt
cat > kernels/CMakeLists.txt << 'EOF'
# Kernel compilation will be handled by Zoltan

# Custom target to validate kernels (requires Zoltan)
add_custom_target(validate_kernels
    COMMAND echo "Kernel validation requires Zoltan compiler"
    SOURCES ${KERNEL_SOURCES}
)
EOF

# kernels/transform/scale.jolt
cat > kernels/transform/scale.jolt << 'EOF'
// Scale transformation kernel
kernel scale {
    input image: Texture2D;
    output result: Texture2D;
    param scale_x: float;
    param scale_y: float;

    fn main(coord: vec2) -> vec4 {
        let scaled = coord * vec2(scale_x, scale_y);
        return sample(image, scaled);
    }
}
EOF

# kernels/color/brightness.jolt
cat > kernels/color/brightness.jolt << 'EOF'
// Brightness adjustment kernel
kernel brightness {
    input image: Texture2D;
    output result: Texture2D;
    param amount: float;

    fn main(coord: vec2) -> vec4 {
        let color = sample(image, coord);
        return vec4(color.rgb * amount, color.a);
    }
}
EOF

# ============================================================================
# 8. ZOLTAN COMPILER
# ============================================================================

echo "🦀 Creating Zoltan compiler structure..."

mkdir -p zoltan/src/{compiler,cli,preview}

# zoltan/Cargo.toml
cat > zoltan/Cargo.toml << 'EOF'
[package]
name = "zoltan"
version = "0.1.0"
edition = "2021"

[dependencies]
clap = { version = "4.0", features = ["derive"] }
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
toml = "0.8"

[dev-dependencies]
criterion = "0.5"

[[bin]]
name = "zoltan"
path = "src/main.rs"
EOF

# zoltan/src/main.rs
cat > zoltan/src/main.rs << 'EOF'
use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "zoltan")]
#[command(about = "JoltScript compiler and live preview tool")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Compile a JoltScript file
    Compile {
        #[arg(value_name = "FILE")]
        input: String,
        #[arg(short, long, value_name = "FILE")]
        output: Option<String>,
    },
    /// Start live preview server
    Preview {
        #[arg(short, long, default_value = "7072")]
        port: u16,
    },
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Compile { input, output } => {
            println!("Compiling: {}", input);
            if let Some(out) = output {
                println!("Output: {}", out);
            }
            // TODO: Implement compilation
        }
        Commands::Preview { port } => {
            println!("Starting preview server on port {}", port);
            // TODO: Implement live preview
        }
    }
}
EOF

# zoltan/rustfmt.toml
cat > zoltan/rustfmt.toml << 'EOF'
max_width = 100
tab_spaces = 4
edition = "2021"
EOF

# ============================================================================
# 9. EXTENSION INTERFACES
# ============================================================================

echo "🔌 Creating extension interface structure..."

mkdir -p extif/{common/{include,src},lua/{include,src},mruby/{include,src},quickjs/{include,src}}

# extif/CMakeLists.txt
cat > extif/CMakeLists.txt << 'EOF'
add_subdirectory(common)
# add_subdirectory(lua)
# add_subdirectory(mruby)
# add_subdirectory(quickjs)
EOF

# extif/common/CMakeLists.txt
cat > extif/common/CMakeLists.txt << 'EOF'
add_library(jfx_extif_common STATIC
    src/ffi_runtime.c
)

target_include_directories(jfx_extif_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_extif_common PUBLIC jfx_core)

set_project_warnings(jfx_extif_common)
EOF

# extif/common/src/ffi_runtime.c
cat > extif/common/src/ffi_runtime.c << 'EOF'
#include "tilly/logger.h"

void ffi_runtime_init(void) {
    tilly_log(TILLY_LOG_INFO, "FFI runtime common layer initialized");
}
EOF

# ============================================================================
# 10. FRONTENDS
# ============================================================================

echo "🖥️  Creating frontends structure..."

mkdir -p frontends/{common/{include,src},desktop/{src,ui},cli/src,web/{src,public}}

# frontends/CMakeLists.txt
cat > frontends/CMakeLists.txt << 'EOF'
add_subdirectory(common)
add_subdirectory(cli)
# add_subdirectory(desktop)
# add_subdirectory(web)
EOF

# frontends/common/CMakeLists.txt
cat > frontends/common/CMakeLists.txt << 'EOF'
add_library(jfx_frontend_common STATIC
    src/frontend_api.c
)

target_include_directories(jfx_frontend_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_frontend_common PUBLIC jfx_core)

set_project_warnings(jfx_frontend_common)
EOF

# frontends/common/src/frontend_api.c
cat > frontends/common/src/frontend_api.c << 'EOF'
#include "tilly/logger.h"

void frontend_api_init(void) {
    tilly_log(TILLY_LOG_INFO, "Frontend API initialized");
}
EOF

# frontends/cli/CMakeLists.txt
cat > frontends/cli/CMakeLists.txt << 'EOF'
add_executable(joltfx_cli
    src/main.c
    src/commands.c
)

target_link_libraries(joltfx_cli PRIVATE
    jfx_core
    jfx_frontend_common
)

set_target_properties(joltfx_cli PROPERTIES OUTPUT_NAME joltfx)

set_project_warnings(joltfx_cli)
EOF

# frontends/cli/src/main.c
cat > frontends/cli/src/main.c << 'EOF'
#include "jfx/jfx_engine.h"
#include "tilly/logger.h"
#include <stdio.h>
#include <string.h>

static void print_usage(void) {
    printf("JoltFX CLI - High-performance effects engine\n\n");
    printf("Usage: joltfx <command> [options]\n\n");
    printf("Commands:\n");
    printf("  version    Show version information\n");
    printf("  help       Show this help message\n");
    printf("  run        Execute an effect pipeline\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "version") == 0) {
        printf("JoltFX version 0.1.0\n");
        return 0;
    }

    if (strcmp(command, "help") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        jfx_engine_config_t config = {
            .max_buffers = 1024,
            .max_textures = 256,
            .max_kernels = 128,
            .backend_name = NULL
        };

        jfx_engine_t *engine = NULL;
        jfx_result_t result = jfx_engine_init(&config, &engine);

        if (result != JFX_SUCCESS) {
            tilly_log(TILLY_LOG_ERROR, "Failed to initialize engine");
            return 1;
        }

        tilly_log(TILLY_LOG_INFO, "Engine initialized, running...");

        // TODO: Load and execute effect pipeline

        jfx_engine_shutdown(engine);
        return 0;
    }

    printf("Unknown command: %s\n", command);
    print_usage();
    return 1;
}
EOF

# frontends/cli/src/commands.c
cat > frontends/cli/src/commands.c << 'EOF'
// Placeholder for CLI command implementations
EOF

# ============================================================================
# 11. TESTS
# ============================================================================

echo "🧪 Creating tests structure..."

mkdir -p tests/{unit/{core,tilly,joltscript},integration/{pipeline,rendering},conformance/{backends,extensions,frontends},perf/{benchmarks,profiling}}

# tests/CMakeLists.txt
cat > tests/CMakeLists.txt << 'EOF'
add_subdirectory(unit)

add_test(NAME test_tillyz COMMAND test_tillyz)
add_test(NAME test_core COMMAND test_core)
EOF

# tests/unit/CMakeLists.txt
cat > tests/unit/CMakeLists.txt << 'EOF'
add_executable(test_tillyz
    tilly/test_tillyz.c
)

target_link_libraries(test_tillyz PRIVATE tillyz)

add_executable(test_core
    core/test_engine.c
)

target_link_libraries(test_core PRIVATE jfx_core)
EOF

# tests/unit/tilly/test_tillyz.c
cat > tests/unit/tilly/test_tillyz.c << 'EOF'
#include "tillyz/tillyz.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    tillyz_context_t *ctx = tillyz_init();
    assert(ctx != NULL);

    tillyz_shutdown(ctx);

    printf("TillyZ tests passed\n");
    return 0;
}
EOF

# tests/unit/core/test_engine.c
cat > tests/unit/core/test_engine.c << 'EOF'
#include "jfx/jfx_engine.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    jfx_engine_config_t config = {
        .max_buffers = 1024,
        .max_textures = 256,
        .max_kernels = 128,
        .backend_name = NULL
    };

    jfx_engine_t *engine = NULL;
    jfx_result_t result = jfx_engine_init(&config, &engine);

    assert(result == JFX_SUCCESS);
    assert(engine != NULL);

    jfx_engine_shutdown(engine);

    printf("Core engine tests passed\n");
    return 0;
}
EOF

# ============================================================================
# 12. EXAMPLES
# ============================================================================

echo "📚 Creating examples structure..."

mkdir -p examples/{hello_world,particle_system}

# examples/CMakeLists.txt
cat > examples/CMakeLists.txt << 'EOF'
add_subdirectory(hello_world)
EOF

# examples/hello_world/CMakeLists.txt
cat > examples/hello_world/CMakeLists.txt << 'EOF'
add_executable(example_hello_world
    main.c
)

target_link_libraries(example_hello_world PRIVATE jfx_core)
EOF

# examples/hello_world/main.c
cat > examples/hello_world/main.c << 'EOF'
#include "jfx/jfx_engine.h"
#include "tilly/logger.h"

int main(void) {
    tilly_log(TILLY_LOG_INFO, "Hello from JoltFX!");

    jfx_engine_config_t config = {
        .max_buffers = 256,
        .max_textures = 64,
        .max_kernels = 32,
        .backend_name = NULL
    };

    jfx_engine_t *engine = NULL;
    if (jfx_engine_init(&config, &engine) != JFX_SUCCESS) {
        return 1;
    }

    tilly_log(TILLY_LOG_INFO, "Engine initialized successfully");

    jfx_engine_shutdown(engine);

    return 0;
}
EOF

# examples/hello_world/effect.jolt
cat > examples/hello_world/effect.jolt << 'EOF'
// Simple passthrough effect
kernel passthrough {
    input source: Texture2D;
    output result: Texture2D;

    fn main(coord: vec2) -> vec4 {
        return sample(source, coord);
    }
}
EOF

# ============================================================================
# 13. DOCUMENTATION
# ============================================================================

echo "📖 Creating documentation structure..."

mkdir -p docs/{api,guides,examples}

# docs/README.md
cat > docs/README.md << 'EOF'
# JoltFX Documentation

## Getting Started
- [Building from Source](guides/building.md)
- [Quick Start Guide](guides/getting_started.md)
- [Writing Your First Kernel](guides/writing_kernels.md)

## Architecture
- [System Architecture](ARCHITECTURE.md)
- [Tilly Runtime](api/tilly.md)
- [Core Engine](api/core.md)
- [JoltScript Language](api/joltscript.md)

## Contributing
- [How to Contribute](../CONTRIBUTING.md)
- [Code of Conduct](../CODE_OF_CONDUCT.md)

EOF

# docs/ARCHITECTURE.md
cat > docs/ARCHITECTURE.md << 'EOF'
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

EOF

# docs/guides/building.md
cat > docs/guides/building.md << 'EOF'
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

EOF

# docs/CONTRIBUTING.md
cat > CONTRIBUTING.md << 'EOF'
# Contributing to JoltFX

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run tests: `ctest --test-dir build`
5. Format code: `./scripts/format.sh`
6. Commit with clear messages
7. Push and create a pull request

## Code Style

- C code follows the `.clang-format` configuration
- Rust code follows `rustfmt.toml`
- Keep functions focused and well-documented

## Testing

All new features must include tests. Run the full test suite before submitting.

## Documentation

Update relevant documentation in `docs/` when adding or changing features.

EOF

# docs/CODE_OF_CONDUCT.md
cat > CODE_OF_CONDUCT.md << 'EOF'
# Code of Conduct

## Our Pledge

We pledge to make participation in our project a harassment-free experience for everyone.

## Our Standards

- Use welcoming and inclusive language
- Be respectful of differing viewpoints
- Accept constructive criticism gracefully
- Focus on what is best for the community

## Enforcement

Instances of unacceptable behavior may be reported to the project maintainers.

EOF

# ============================================================================
# 14. CI/CD
# ============================================================================

echo "🔄 Creating CI/CD structure..."

mkdir -p .github/{workflows,ISSUE_TEMPLATE}

# .github/workflows/ci.yml
cat > .github/workflows/ci.yml << 'EOF'
name: CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        
    steps:
    - uses: actions/checkout@v3
    
    - name: Configure CMake
      run: cmake --preset default
      
    - name: Build
      run: cmake --build build
      
    - name: Run Tests
      run: ctest --test-dir build --output-on-failure
EOF

# .github/PULL_REQUEST_TEMPLATE.md
cat > .github/PULL_REQUEST_TEMPLATE.md << 'EOF'
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
- [ ] Tests added/updated
- [ ] All tests pass locally

## Checklist
- [ ] Code follows style guidelines
- [ ] Documentation updated
- [ ] No warnings introduced
EOF

# ============================================================================
# 15. SCRIPTS
# ============================================================================

echo "🔧 Creating utility scripts..."

mkdir -p scripts

# scripts/build.sh
cat > scripts/build.sh << 'EOF'
#!/usr/bin/env bash
set -e

echo "Building JoltFX..."
cmake --preset default
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
echo "Build complete!"
EOF

chmod +x scripts/build.sh

# scripts/test.sh
cat > scripts/test.sh << 'EOF'
#!/usr/bin/env bash
set -e

echo "Running tests..."
ctest --test-dir build --output-on-failure
echo "All tests passed!"
EOF

chmod +x scripts/test.sh

# scripts/format.sh
cat > scripts/format.sh << 'EOF'
#!/usr/bin/env bash
set -e

echo "Formatting C/C++ code..."
find . -path ./build -prune -o \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0 \
  | xargs -0 clang-format -i

echo "Formatting Rust code..."
cd zoltan && cargo fmt

echo "Formatting complete!"
EOF

chmod +x scripts/format.sh

# ============================================================================
# 16. ASSETS
# ============================================================================

echo "🎨 Creating assets structure..."

mkdir -p assets/{icons,fonts,shaders/{common,spirv},test_media/{images,videos}}

cat > assets/README.md << 'EOF'
# JoltFX Assets

## Icons
Project logos and branding materials

## Fonts
UI fonts (Inter, etc.)

## Shaders
Common shader code and precompiled SPIR-V

## Test Media
Sample images and videos for testing
EOF

# ============================================================================
# 17. THIRD PARTY
# ============================================================================

echo "📦 Creating third-party structure..."

mkdir -p third_party

cat > third_party/README.md << 'EOF'
# Third-Party Dependencies

## Included Libraries

### stb_image.h
- License: Public Domain
- Purpose: Image loading

### stb_image_write.h
- License: Public Domain
- Purpose: Image writing

See individual library headers for full license information.
EOF

# ============================================================================
# FINAL MESSAGE
# ============================================================================

echo ""
echo "✅ JoltFX project scaffolding complete!"
echo ""
echo "Next steps:"
echo "  1. cd $PROJECT_ROOT"
echo "  2. cmake --preset default"
echo "  3. cmake --build build"
echo "  4. ctest --test-dir build"
echo "  5. ./build/frontends/cli/joltfx help"
echo ""
echo "Directory structure created with:"
tree -L 3 -I 'build|target' 2>/dev/null || find . -type d -maxdepth 3 | head -40
echo ""
echo "Happy coding! 🚀"
