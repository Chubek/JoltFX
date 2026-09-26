# Tilly Core Engine Specification

## Overview

**Tilly** is the foundational runtime engine of JoltFX. It provides the primitive systems that all higher layers depend on: memory management, error handling, logging, reflection, module loading, configuration, and platform abstraction. Tilly is written in C11 and designed for embedding, minimal dependencies, and deterministic resource usage.

**TillyZ** is the zero-dependency bootstrap subset of Tilly. It is a self-contained, statically-linkable core (~50KB compiled) that initializes the Tilly runtime without external libraries, making it suitable for embedded systems, WASM targets, and cold-start scenarios.

---

## Architecture

┌─────────────────────────────────────────────────────────────┐
│                      JoltFX Application                      │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                        Glue Layer                            │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                     Execution Layer                          │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                       Tilly Core                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  TillyZ (Bootstrap Core)                             │  │
│  │  • Arena allocator    • Minimal libc stubs           │  │
│  │  • Error stack        • Platform detection           │  │
│  │  │  String utilities   • Panic handler               │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────┐  │
│  │   Memory     │ │   Logging    │ │   Reflection     │  │
│  │   Manager    │ │              │ │                  │  │
│  └──────────────┘ └──────────────┘ └──────────────────┘  │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────┐  │
│  │   Module     │ │   Config     │ │   Platform       │  │
│  │   System     │ │   Parser     │ │   Abstraction    │  │
│  └──────────────┘ └──────────────┘ └──────────────────┘  │
└─────────────────────────────────────────────────────────────┘


---

## Core Principles

1. **Zero-allocation initialization**: TillyZ can bootstrap without calling `malloc`.
2. **Deterministic teardown**: All resources are explicitly released; no hidden global state.
3. **Error propagation**: All fallible operations return `TillyStatus` and populate an error stack.
4. **Portable**: Runs on Linux, macOS, Windows, WASM, and bare-metal (with minimal libc).
5. **Introspectable**: Reflection API exposes types, modules, and symbols at runtime.
6. **Thread-safe**: Core primitives are thread-safe by default; higher-level systems opt in.

---

## TillyZ: Bootstrap Core

TillyZ is the minimal subset of Tilly required to initialize the engine. It has **no external dependencies** (not even standard libc) and can be compiled as a single translation unit.

### Bootstrap Sequence

```c
// 1. Initialize TillyZ bootstrap core
TillyZConfig z_cfg = {
    .arena_size = 1024 * 1024,  // 1MB bootstrap arena
    .panic_handler = my_panic_handler,
    .platform = TILLYZ_PLATFORM_AUTO,  // Auto-detect OS
};

TillyZContext* z_ctx = tillyz_init(&z_cfg);

// 2. Bootstrap Tilly full runtime
TillyConfig cfg = {
    .bootstrap_ctx = z_ctx,
    .heap_size = 64 * 1024 * 1024,  // 64MB
    .enable_logging = true,
    .log_level = TILLY_LOG_INFO,
};

TillyContext* ctx = tilly_init(&cfg);

// 3. Use Tilly subsystems
tilly_log_info(ctx, "Tilly initialized");

// 4. Shutdown in reverse order
tilly_shutdown(ctx);
tillyz_shutdown(z_ctx);
```

### TillyZ Components

#### 1. Arena Allocator

TillyZ uses a bump allocator (arena) for all bootstrap allocations. No fragmentation, no free() calls—just reset the arena when done.

```c
typedef struct TillyZArena {
    uint8_t*  base;
    size_t    size;
    size_t    offset;
    size_t    peak;
} TillyZArena;

// Arena API (internal to TillyZ)
void*  tillyz_arena_alloc(TillyZArena* arena, size_t size, size_t align);
void   tillyz_arena_reset(TillyZArena* arena);
size_t tillyz_arena_usage(TillyZArena* arena);
```

The arena is used to allocate:
- Error strings
- Platform detection buffers
- Initial module table
- Bootstrap configuration

#### 2. Error Stack

TillyZ maintains a fixed-size error stack (default 16 entries) for bootstrap errors.

```c
typedef struct TillyZError {
    TillyStatus   code;
    const char*   message;      // Points into arena
    const char*   file;
    uint32_t      line;
    uint64_t      timestamp_ns;
} TillyZError;

typedef struct TillyZErrorStack {
    TillyZError   entries[TILLYZ_MAX_ERRORS];
    uint32_t      count;
} TillyZErrorStack;

// Error API
void         tillyz_error_push(
    TillyZContext*   ctx,
    TillyStatus      code,
    const char*      message,
    const char*      file,
    uint32_t         line
);

TillyZError* tillyz_error_peek(TillyZContext* ctx);
void         tillyz_error_pop(TillyZContext* ctx);
void         tillyz_error_clear(TillyZContext* ctx);

// Convenience macro
#define TILLYZ_ERROR(ctx, code, msg) \
    tillyz_error_push(ctx, code, msg, __FILE__, __LINE__)
```

#### 3. String Utilities

TillyZ provides minimal string functions (since we don't link libc):

```c
size_t tillyz_strlen(const char* str);
int    tillyz_strcmp(const char* a, const char* b);
void   tillyz_strcpy(char* dst, const char* src, size_t dst_size);
char*  tillyz_strdup(TillyZArena* arena, const char* src);
int    tillyz_snprintf(char* buf, size_t size, const char* fmt, ...);
```

These are implemented in `tillyz_string.c` with no external dependencies.

#### 4. Platform Detection

TillyZ detects the host platform at runtime:

```c
typedef enum TillyZPlatform {
    TILLYZ_PLATFORM_UNKNOWN,
    TILLYZ_PLATFORM_LINUX,
    TILLYZ_PLATFORM_MACOS,
    TILLYZ_PLATFORM_WINDOWS,
    TILLYZ_PLATFORM_WASM,
    TILLYZ_PLATFORM_BARE,
    TILLYZ_PLATFORM_AUTO,  // Auto-detect
} TillyZPlatform;

TillyZPlatform tillyz_detect_platform(void);
const char*    tillyz_platform_name(TillyZPlatform platform);
```

Detection uses compiler-defined macros:

```c
TillyZPlatform tillyz_detect_platform(void) {
#if defined(__wasm__) || defined(__EMSCRIP    tillyz_platform_name(TillyZPlatform platform);
```

Detection uses compiler-defined macros:

```c
TillyZPlatform tillyz_detect_platform(void) {
#if defined(__wasm__) || defined(__EMSCRIP
```

#### 5. Panic Handler

When a fatal error occurs during bootstrap, TillyZ calls the panic handler.

```c
typedef void (*TillyZPanicHandler)(
    TillyZContext*   ctx,
    const char*      message,
    const char*      file,
    uint32_t         line
);

// Default handler (prints to stderr and aborts)
void tillyz_default_panic(
    TillyZContext*   ctx,
    const char*      message,
    const char*      file,
    uint32_t         line
);

// Panic macro
#define TILLYZ_PANIC(ctx, msg) \
    ctx->panic_handler(ctx, msg, __FILE__, __LINE__)
```

Custom panic handlers can flush logs, dump stack traces, or send telemetry before terminating.

#### 6. TillyZ Context

The bootstrap context holds all TillyZ state:

```c
typedef struct TillyZContext {
    TillyZArena        arena;
    TillyZErrorStack   error_stack;
    TillyZPlatform     platform;
    TillyZPanicHandler panic_handler;
    uint64_t           init_timestamp_ns;
} TillyZContext;

// Initialization
TillyZContext* tillyz_init(TillyZConfig* cfg);
void           tillyz_shutdown(TillyZContext* ctx);
```

---

## Tilly: Full Runtime

Once TillyZ has bootstrapped, Tilly extends it with production-grade subsystems.

### 1. Memory Manager

Tilly's memory manager provides multiple allocation strategies:

```c
typedef enum TillyAllocStrategy {
    TILLY_ALLOC_ARENA,      // Bump allocator (no free)
    TILLY_ALLOC_POOL,       // Fixed-size block pool
    TILLY_ALLOC_GENERAL,    // General-purpose heap (dlmalloc-style)
    TILLY_ALLOC_STACK,      // Thread-local stack allocator
} TillyAllocStrategy;

typedef struct TillyAllocator {
    TillyAllocStrategy strategy;
    void*              state;       // Allocator-specific state
    size_t             capacity;
    size_t             used;
    size_t             peak;
    uint32_t           alloc_count;
    uint32_t           free_count;
} TillyAllocator;

// Allocator API
TillyAllocator* tilly_allocator_create(
    TillyContext*        ctx,
    TillyAllocStrategy   strategy,
    size_t               capacity
);

void            tilly_allocator_destroy(TillyAllocator* alloc);

void*           tilly_alloc(TillyAllocator* alloc, size_t size, size_t align);
void            tilly_free(TillyAllocator* alloc, void* ptr);
void*           tilly_realloc(TillyAllocator* alloc, void* ptr, size_t new_size);

void            tilly_allocator_reset(TillyAllocator* alloc);  // For arenas
size_t          tilly_allocator_usage(TillyAllocator* alloc);
```

#### Pool Allocator

For high-frequency, fixed-size allocations (e.g., task structs):

```c
TillyAllocator* task_pool = tilly_allocator_create(
    ctx,
    TILLY_ALLOC_POOL,
    sizeof(Task) * 1024  // Pool of 1024 tasks
);

Task* task = tilly_alloc(task_pool, sizeof(Task), alignof(Task));
// ... use task ...
tilly_free(task_pool, task);
```

---

### 2. Logging System

Tilly provides structured logging with multiple sinks (stdout, file, network).

```c
typedef enum TillyLogLevel {
    TILLY_LOG_TRACE,
    TILLY_LOG_DEBUG,
    TILLY_LOG_INFO,
    TILLY_LOG_WARN,
    TILLY_LOG_ERROR,
    TILLY_LOG_FATAL,
} TillyLogLevel;

typedef struct TillyLogEntry {
    TillyLogLevel  level;
    const char*    message;
    const char*    file;
    uint32_t       line;
    uint64_t       timestamp_ns;
    uint32_t       thread_id;
    const char*    module;     // Optional module name
} TillyLogEntry;

typedef void (*TillyLogSink)(TillyLogEntry* entry, void* user_data);

// Logging API
void tilly_log(
    TillyContext*    ctx,
    TillyLogLevel    level,
    const char*      module,
    const char*      file,
    uint32_t         line,
    const char*      fmt,
    ...
);

// Convenience macros
#define tilly_log_trace(ctx, ...) \
    tilly_log(ctx, TILLY_LOG_TRACE, NULL, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_info(ctx, ...) \
    tilly_log(ctx, TILLY_LOG_INFO, NULL, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_error(ctx, ...) \
    tilly_log(ctx, TILLY_LOG_ERROR, NULL, __FILE__, __LINE__, __VA_ARGS__)

// Register custom log sink
void tilly_log_add_sink(TillyContext* ctx, TillyLogSink sink, void* user_data);
```

#### Example: File Sink

```c
void file_log_sink(TillyLogEntry* entry, void* user_data) {
    FILE* f = (FILE*)user_data;
    fprintf(f, "[%s] %s:%u: %s\n",
            tilly_log_level_name(entry->level),
            entry->file, entry->line, entry->message);
    fflush(f);
}

FILE* log_file = fopen("joltfx.log", "w");
tilly_log_add_sink(ctx, file_log_sink, log_file);
```

---

### 3. Reflection System

Tilly provides runtime introspection of types, functions, and modules.

```c
typedef enum TillyTypeKind {
    TILLY_TYPE_VOID,
    TILLY_TYPE_BOOL,
    TILLY_TYPE_INT,
    TILLY_TYPE_FLOAT,
    TILLY_TYPE_POINTER,
    TILLY_TYPE_STRUCT,
    TILLY_TYPE_ENUM,
    TILLY_TYPE_FUNCTION,
} TillyTypeKind;

typedef struct TillyTypeInfo {
    TillyTypeKind   kind;
    const char*     name;
    size_t          size;
    size_t          align;
    uint32_t        field_count;   // For structs
    TillyFieldInfo* fields;
} TillyTypeInfo;

typedef struct TillyFieldInfo {
    const char*     name;
    TillyTypeInfo*  type;
    size_t          offset;
} TillyFieldInfo;

// Reflection API
TillyTypeInfo*  tilly_type_register(
    TillyContext*    ctx,
    const char*      name,
    TillyTypeKind    kind,
    size_t           size,
    size_t           align
);

TillyTypeInfo*  tilly_type_find(TillyContext* ctx, const char* name);

void            tilly_type_add_field(
    TillyTypeInfo*   type,
    const char*      name,
    TillyTypeInfo*   field_type,
    size_t           offset
);

// Example: Register a struct
TillyTypeInfo* vec3_type = tilly_type_register(
    ctx, "Vec3", TILLY_TYPE_STRUCT, sizeof(Vec3), alignof(Vec3)
);
tilly_type_add_field(vec3_type, "x", float_type, offsetof(Vec3, x));
tilly_type_add_field(vec3_type, "y", float_type, offsetof(Vec3, y));
tilly_type_add_field(vec3_type, "z", float_type, offsetof(Vec3, z));
```

#### Function Registration

```c
typedef struct TillyFunctionInfo {
    const char*     name;
    TillyTypeInfo*  return_type;
    TillyTypeInfo** param_types;
    uint32_t        param_count;
    void*           function_ptr;
} TillyFunctionInfo;

TillyFunctionInfo* tilly_function_register(
    TillyContext*     ctx,
    const char*       name,
    TillyTypeInfo*    return_type,
    TillyTypeInfo**   param_types,
    uint32_t          param_count,
    void*             function_ptr
);

TillyFunctionInfo* tilly_function_find(TillyContext* ctx, const char* name);
```

---

### 4. Module System

Tilly modules are dynamically loadable shared libraries (`.so`, `.dylib`, `.dll`) or statically linked units. Each module exports a registration function.

```c
typedef struct TillyModule {
    const char*     name;
    const char*     version;
    void*           handle;        // dlopen handle (NULL if static)
    TillyModuleAPI* api;           // Exported API
    uint32_t        ref_count;
} TillyModule;

typedef struct TillyModuleAPI {
    TillyStatus (*init)(TillyContext* ctx);
    void        (*shutdown)(TillyContext* ctx);
    const char* (*get_name)(void);
    const char* (*get_version)(void);
} TillyModuleAPI;

// Module API
TillyModule* tilly_module_load(TillyContext* ctx, const char* path);
void         tilly_module_unload(TillyContext* ctx, TillyModule* mod);

TillyModule* tilly_module_find(TillyContext* ctx, const char* name);

void*        tilly_module_get_symbol(TillyModule* mod, const char* symbol);
```

#### Module Entry Point

Every module must export:

```c
// In my_module.c
TILLY_MODULE_EXPORT TillyModuleAPI* tilly_module_register(void) {
    static TillyModuleAPI api = {
        .init = my_module_init,
        .shutdown = my_module_shutdown,
        .get_name = my_module_get_name,
        .get_version = my_module_get_version,
    };
    return &api;
}
```

---

### 5. Configuration System

Tilly uses a simple key-value configuration format (similar to TOML).

```ini
[tilly]
heap_size = 67108864          # 64MB
log_level = "info"
enable_profiling = true

[execution]
num_threads = 8
backend = "vulkan"

[glue]
max_extensions = 16
```

```c
typedef struct TillyConfig {
    TillyZContext*   bootstrap_ctx;
    size_t           heap_size;
    bool             enable_logging;
    TillyLogLevel    log_level;
    bool             enable_profiling;
} TillyConfig;

// Config API
TillyConfig* tilly_config_load(TillyContext* ctx, const char* path);
void         tilly_config_free(TillyConfig* cfg);

const char*  tilly_config_get_string(TillyConfig* cfg, const char* section, const char* key);
int64_t      tilly_config_get_int(TillyConfig* cfg, const char* section, const char* key);
bool         tilly_config_get_bool(TillyConfig* cfg, const char* section, const char* key);
```

---

### 6. Platform Abstraction

Tilly abstracts OS-specific functionality:

```c
// File I/O
TillyFile*  tilly_file_open(const char* path, const char* mode);
void        tilly_file_close(TillyFile* f);
size_t      tilly_file_read(TillyFile* f, void* buf, size_t size);
size_t      tilly_file_write(TillyFile* f, const void* buf, size_t size);

// Time
uint64_t    tilly_time_now_ns(void);
void        tilly_sleep_ms(uint32_t ms);

// Threading
TillyThread* tilly_thread_create(void (*func)(void*), void* arg);
void         tilly_thread_join(TillyThread* t);
TillyMutex*  tilly_mutex_create(void);
void         tilly_mutex_lock(TillyMutex* m);
void         tilly_mutex_unlock(TillyMutex* m);

// Dynamic library loading
void*        tilly_dlopen(const char* path);
void*        tilly_dlsym(void* handle, const char* symbol);
void         tilly_dlclose(void* handle);
```

These are implemented per-platform in `tilly_platform_{linux,macos,windows,wasm}.c`.

---

## Tilly Context

The main Tilly context holds all runtime state:

```c
typedef struct TillyContext {
    TillyZContext*      bootstrap_ctx;
    TillyAllocator*     heap;
    TillyLogSink*       log_sinks;
    uint32_t            log_sink_count;
    TillyModule**       modules;
    uint32_t            module_count;
    TillyTypeInfo**     types;
    uint32_t            type_count;
    TillyFunctionInfo** functions;
    uint32_t            function_count;
    TillyMutex*         global_lock;
    uint64_t            init_timestamp_ns;
} TillyContext;

// Initialization
TillyContext* tilly_init(TillyConfig* cfg);
void          tilly_shutdown(TillyContext* ctx);
```

---

## Bootstrap Process in Detail

### Phase 1: TillyZ Initialization

```c
TillyZConfig z_cfg = {
    .arena_size = 1024 * 1024,
    .panic_handler = tillyz_default_panic,
    .platform = TILLYZ_PLATFORM_AUTO,
};

TillyZContext* z_ctx = tillyz_init(&z_cfg);
```

**What happens:**
1. Detect platform via `tillyz_detect_platform()`.
2. Allocate bootstrap arena (1MB) using `mmap` (POSIX) or `VirtualAlloc` (Windows).
3. Initialize error stack.
4. Set panic handler.
5. Record initialization timestamp.

**Memory layout after Phase 1:**

┌─────────────────────────────┐ ← arena.base
│  TillyZContext (struct)     │
├─────────────────────────────┤
│  Error stack entries        │
├─────────────────────────────┤
│  Platform detection strings │
├─────────────────────────────┤
│  (free space)               │
└─────────────────────────────┘ ← arena.base + arena.size


### Phase 2: Tilly Initialization

```c
TillyConfig cfg = {
    .bootstrap_ctx = z_ctx,
    .heap_size = 64 * 1024 * 1024,
    .enable_logging = true,
    .log_level = TILLY_LOG_INFO,
};

TillyContext* ctx = tilly_init(&cfg);
```

**What happens:**
1. Create general-purpose heap allocator (64MB).
2. Initialize logging system with default stdout sink.
3. Initialize reflection system (type and function registries).
4. Initialize module system (empty module table).
5. Create global mutex for thread safety.
6. Register core types (`int32_t`, `float`, `void*`, etc.).

**Memory layout after Phase 2:**

TillyZ Arena (1MB)
┌─────────────────────────────┐
│  TillyZContext              │
│  Bootstrap state            │
└─────────────────────────────┘

Tilly Heap (64MB)
┌─────────────────────────────┐
│  TillyContext (struct)      │
├─────────────────────────────┤
│  Log sink array             │
├─────────────────────────────┤
│  Module table               │
├─────────────────────────────┤
│  Type registry              │
├─────────────────────────────┤
│  Function registry          │
├─────────────────────────────┤
│  (free space)               │
└─────────────────────────────┘


### Phase 3: Load Core Modules

```c
tilly_module_load(ctx, "libjolt_execution.so");
tilly_module_load(ctx, "libjolt_glue.so");
```

Each module's `tilly_module_register()` is called, and its `init()` function is invoked.

### Phase 4: Application Initialization

The application can now use all Tilly subsystems:

```c
tilly_log_info(ctx, "Application starting");

TillyAllocator* frame_arena = tilly_allocator_create(
    ctx, TILLY_ALLOC_ARENA, 1024 * 1024
);

// Main loop
while (running) {
    // Use frame_arena for per-frame allocations
    tilly_allocator_reset(frame_arena);
}
```

### Phase 5: Shutdown

```c
tilly_shutdown(ctx);        // Shuts down all modules, frees heap
tillyz_shutdown(z_ctx);     // Releases bootstrap arena
```

**Shutdown order:**
1. Call `shutdown()` on all loaded modules (in reverse load order).
2. Flush log sinks.
3. Free all heap allocations.
4. Destroy global mutex.
5. Free TillyContext.
6. Release TillyZ arena.
7. Close platform handles.

---

## Error Handling Model

Tilly uses result codes with error context:

```c
typedef enum TillyStatus {
    TILLY_OK = 0,
    TILLY_ERR_NOMEM,
    TILLY_ERR_INVALID_ARG,
    TILLY_ERR_NOT_FOUND,
    TILLY_ERR_IO,
    TILLY_ERR_MODULE_LOAD,
    TILLY_ERR_INIT_FAILED,
} TillyStatus;

typedef struct TillyError {
    TillyStatus  code;
    char         message[256];
    const char*  file;
    uint32_t     line;
    uint64_t     timestamp_ns;
} TillyError;

// Thread-local error (for non-bootstrap code)
TillyError* tilly_error_get(void);
void        tilly_error_set(TillyStatus code, const char* fmt, ...);
void        tilly_error_clear(void);

// Example
TillyStatus load_config(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        tilly_error_set(TILLY_ERR_IO, "Cannot open config file: %s", path);
        return TILLY_ERR_IO;
    }
    // ...
    return TILLY_OK;
}
```

---

## Thread Safety

- **TillyZ**: Not thread-safe (bootstrap is single-threaded).
- **Tilly Core**: Thread-safe by default. The global mutex protects:
  - Module loading/unloading
  - Type/function registration
  - Logging sinks

- **Allocators**: Each `TillyAllocator` has its own lock. Prefer per-thread allocators for performance.

---

## Platform-Specific Notes

### WASM

- Uses Emscripten's `sbrk()` for arena allocation.
- File I/O is virtual (memory-backed or async fetch).
- Threading uses Web Workers (limited).

### Bare Metal

- Requires a custom panic handler (no `abort()`).
- Provide your own `mmap` equivalent or use a static buffer:

```c
static uint8_t tilly_heap[64 * 1024 * 1024];

TillyZConfig z_cfg = {
    .arena_size = sizeof(tilly_heap),
    .arena_buffer = tilly_heap,  // Use static buffer
    .platform = TILLYZ_PLATFORM_BARE,
};
```

---

## Build System

Tilly uses a minimal build system (single `Makefile` or `build.sh` script).

### Building TillyZ

```bash
# Build TillyZ as a static library
cc -std=c11 -O2 -c tillyz_arena.c tillyz_error.c tillyz_string.c tillyz_platform.c
ar rcs libtillyz.a tillyz_arena.o tillyz_error.o tillyz_string.o tillyz_platform.o
```

### Building Tilly

```bash
# Build Tilly (requires TillyZ)
cc -std=c11 -O2 -c tilly_memory.c tilly_log.c tilly_reflect.c tilly_module.c tilly_config.c
ar rcs libtilly.a tilly_memory.o tilly_log.o tilly_reflect.o tilly_module.o tilly_config.o

# Link application
cc -o joltfx main.o -L. -ltilly -ltillyz -ldl -lpthread -lm
```

### Cross-Compilation

```bash
# For WASM
emcc -std=c11 -O2 -s WASM=1 -o tilly.js \
    tillyz_*.c tilly_*.c tilly_platform_wasm.c

# For Windows (from Linux)
x86_64-w64-mingw32-gcc -std=c11 -O2 -c tilly*.c
x86_64-w64-mingw32-ar rcs libtilly.a tilly*.o
```

---

## Configuration File Format

Tilly uses a simple INI-like format:

```ini
# JoltFX Configuration

[tilly]
heap_size = 67108864       # 64 MB
log_level = "debug"
enable_profiling = true

[execution]
backend = "vulkan"
num_threads = 8
cpu_mem_limit = 536870912  # 512 MB
gpu_mem_limit = 2147483648 # 2 GB

[glue]
max_extensions = 16
sandbox_memory = 16777216  # 16 MB per extension

[modules]
autoload = ["jolt.fx.kernels", "jolt.audio", "jolt.io"]
```

Parsed via `tilly_config_load()`:

```c
TillyConfig* cfg = tilly_config_load(ctx, "joltfx.conf");
size_t heap_size = tilly_config_get_int(cfg, "tilly", "heap_size");
const char* backend = tilly_config_get_string(cfg, "execution", "backend");
```

---

## Summary

| Component         | Responsibility |
|-------------------|----------------|
| **TillyZ**        | Zero-dependency bootstrap: arena, error stack, platform detection |
| **Memory Manager**| Arenas, pools, general heap, thread-local stacks |
| **Logging**       | Structured logging with custom sinks |
| **Reflection**    | Runtime type and function introspection |
| **Module System** | Dynamic/static module loading and registration |
| **Config Parser** | Key-value configuration files |
| **Platform Layer**| OS abstraction (file I/O, threading, time, dlopen) |

Tilly is the bedrock of JoltFX. Everything else—Execution Layer, Glue Layer, Joltscript runtime, extension sandboxes—builds on Tilly's primitives. TillyZ ensures cold-start determinism, making JoltFX embeddable in any environment from WASM to bare metal.