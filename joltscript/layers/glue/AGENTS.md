# JoltFX Glue Layer Specification

## Overview

The Glue Layer is the binding infrastructure between JoltFX's high-level components — CLI, TUI, GUI, scripting runtimes, and external language APIs — and the low-level Tilly Core engine and JoltVM. It manages ABI boundaries, type marshaling, lifetime synchronization, and capability dispatch across language and process boundaries.

---

## Architecture

┌─────────────────────────────────────────────────┐
│         User-Facing Layer                        │
│   CLI │ TUI │ Dear ImGui GUI │ Web (joltvm.js)  │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────┐
│              Glue Layer                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ ABI      │ │ Marshal  │ │ Capability       │ │
│  │ Registry │ │ Engine   │ │ Dispatcher       │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ Lifetime │ │ Error    │ │ Extension        │ │
│  │ Bridge   │ │ Adapter  │ │ Host             │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────┐
│         Tilly Core / JoltVM                      │
└─────────────────────────────────────────────────┘


---

## Core Components

### 1. ABI Registry

The ABI Registry is the central catalog of all exported and imported function signatures across language boundaries. It is built at link time and queried at runtime.

```c
// ABI entry descriptor
typedef struct JoltABIEntry {
    const char*    symbol;          // Fully qualified symbol name
    JoltABIKind    kind;            // FUNCTION, STRUCT, ENUM, CALLBACK
    JoltTypeDesc*  param_types;     // Array of parameter type descriptors
    uint32_t       param_count;
    JoltTypeDesc*  return_type;
    uint32_t       calling_conv;    // JOLT_CC_C, JOLT_CC_FAST, JOLT_CC_KERNEL
    void*          fn_ptr;          // Resolved function pointer
} JoltABIEntry;

// Registry API
JoltABIRegistry* jolt_abi_registry_create(void);
void             jolt_abi_registry_destroy(JoltABIRegistry*);
JoltStatus       jolt_abi_register(JoltABIRegistry*, JoltABIEntry*);
JoltABIEntry*    jolt_abi_lookup(JoltABIRegistry*, const char* symbol);
JoltStatus       jolt_abi_resolve_all(JoltABIRegistry*);
```

#### Symbol Naming Convention

jolt.<module>.<subsystem>.<name>

Examples:
  jolt.fx.render.submit_frame
  jolt.core.timeline.advance
  jolt.script.kernel.invoke
  jolt.vm.heap.alloc


---

### 2. Marshal Engine

The Marshal Engine handles type conversion between Joltscript's type system, C ABI types, and the type systems of bound foreign languages (Rust, Python, Go).

#### Type Descriptor

```c
typedef enum JoltBaseType {
    JOLT_TYPE_VOID,
    JOLT_TYPE_BOOL,
    JOLT_TYPE_I8, JOLT_TYPE_I16, JOLT_TYPE_I32, JOLT_TYPE_I64,
    JOLT_TYPE_U8, JOLT_TYPE_U16, JOLT_TYPE_U32, JOLT_TYPE_U64,
    JOLT_TYPE_F32, JOLT_TYPE_F64,
    JOLT_TYPE_PTR,
    JOLT_TYPE_SLICE,    // fat pointer: ptr + len
    JOLT_TYPE_STRING,   // UTF-8: ptr + len
    JOLT_TYPE_STRUCT,
    JOLT_TYPE_ENUM,
    JOLT_TYPE_ARRAY,
    JOLT_TYPE_FUNCTION,
    JOLT_TYPE_HANDLE,   // opaque JoltVM handle
} JoltBaseType;

typedef struct JoltTypeDesc {
    JoltBaseType    base;
    uint32_t        flags;          // JOLT_TF_CONST, JOLT_TF_NULLABLE, JOLT_TF_OWNED
    const char*     name;           // For structs/enums
    uint32_t        field_count;    // For structs
    JoltTypeDesc**  fields;
    uint32_t        size;           // In bytes; 0 = dynamic
    uint32_t        align;
} JoltTypeDesc;
```

#### Marshal Operations

```c
// Core marshal/unmarshal
JoltStatus jolt_marshal_to_c(
    JoltMarshalCtx* ctx,
    JoltValue*      src,            // Joltscript-side value
    JoltTypeDesc*   target_type,
    void*           out_buf,
    size_t          out_buf_size
);

JoltStatus jolt_unmarshal_from_c(
    JoltMarshalCtx* ctx,
    void*           src,
    JoltTypeDesc*   src_type,
    JoltValue**     out_value       // Heap-allocated JoltValue
);

// Language-specific adapters
JoltStatus jolt_marshal_to_python(JoltMarshalCtx*, JoltValue*, PyObject**);
JoltStatus jolt_unmarshal_from_python(JoltMarshalCtx*, PyObject*, JoltValue**);

JoltStatus jolt_marshal_to_rust(JoltMarshalCtx*, JoltValue*, JoltRustVal*);
JoltStatus jolt_unmarshal_from_rust(JoltMarshalCtx*, JoltRustVal*, JoltValue**);

JoltStatus jolt_marshal_to_go(JoltMarshalCtx*, JoltValue*, JoltGoVal*);
JoltStatus jolt_unmarshal_from_go(JoltMarshalCtx*, JoltGoVal*, JoltValue**);
```

#### String Handling

All strings crossing the Glue Layer boundary are normalized to UTF-8. The Marshal Engine performs conversion and owns the resulting allocation until the call frame is released.

```c
// String views — zero-copy when source is already UTF-8
typedef struct JoltStringView {
    const uint8_t* data;
    size_t         len;
    uint32_t       flags;   // JOLT_SV_OWNED | JOLT_SV_STATIC | JOLT_SV_INTERNED
} JoltStringView;

JoltStringView jolt_string_borrow(const char* c_str);
JoltStringView jolt_string_own(char* c_str, size_t len);
void           jolt_string_release(JoltStringView);
```

---

### 3. Capability Dispatcher

The Capability Dispatcher routes calls from foreign language bindings and scripting runtimes to the correct Tilly Core subsystem, applying capability checks before dispatch.

```c
typedef enum JoltCapability {
    JOLT_CAP_RENDER          = 1 << 0,
    JOLT_CAP_AUDIO           = 1 << 1,
    JOLT_CAP_FILE_READ       = 1 << 2,
    JOLT_CAP_FILE_WRITE      = 1 << 3,
    JOLT_CAP_NETWORK         = 1 << 4,
    JOLT_CAP_GPU_COMPUTE     = 1 << 5,
    JOLT_CAP_KERNEL_AUTHOR   = 1 << 6,
    JOLT_CAP_EXTENSION_HOST  = 1 << 7,
    JOLT_CAP_TIMELINE_WRITE  = 1 << 8,
    JOLT_CAP_ASSET_WRITE     = 1 << 9,
} JoltCapability;

typedef struct JoltDispatchCtx {
    uint64_t        capabilities;   // Bitmask of granted JoltCapability
    JoltABIEntry*   target;
    JoltMarshalCtx* marshal;
    JoltArena*      arena;          // Per-call scratch arena
    uint32_t        timeout_ms;     // 0 = no limit
} JoltDispatchCtx;

JoltStatus jolt_dispatch(
    JoltDispatchCtx* ctx,
    JoltValue**      args,
    uint32_t         arg_count,
    JoltValue**      out_result
);
```

#### Capability Grant Model

Capabilities are assigned per-call-site at the binding layer, not per-module. A Python extension calling into the render subsystem must have `JOLT_CAP_RENDER` in its dispatch context. The Glue Layer enforces this before any C A Python extension calling into the render subsystem must have `JOLT_CAP_RENDER` in its dispatch context. The Glue Layer enforces this before any C s);
void          jolt_cap_grant_destroy(JoltCapGrant*);

// Attach to a scripting runtime context
JoltStatus jolt_cap_attach(JoltScriptCtx*, JoltCapGrant*);

// Check before dispatch (called internally)
bool jolt_cap_check(JoltDispatchCtx*, JoltCapability);

---

### 4. Lifetime Bridge

The Lifetime Bridge synchronizes object lifetimes across the ARC (Joltscript), stack/arena (C kernels), and garbage-collected (Python) or ownership-tracked (Rust) memory models.

c
typedef enum JoltLifetimeKind {
    JOLT_LT_ARC,        // Joltscript automatic reference counting
    JOLT_LT_ARENA,      // Kernel-scoped arena; freed at kernel end
    JOLT_LT_STACK,      // C stack allocation; not heap-managed
    JOLT_LT_GC,         // Python or Go GC-managed
    JOLT_LT_RUST,       // Rust ownership; drop called on release
    JOLT_LT_STATIC,     // Static/const; never freed
} JoltLifetimeKind;

typedef struct JoltLifetimeHandle {
    void*            ptr;
    JoltLifetimeKind kind;
    uint32_t         ref_count;     // Used for JOLT_LT_ARC
    void           (*destructor)(void*);
} JoltLifetimeHandle;

JoltLifetimeHandle* jolt_lt_acquire(void* ptr, JoltLifetimeKind, void (*dtor)(void*));
void                jolt_lt_retain(JoltLifetimeHandle*);
void                jolt_lt_release(JoltLifetimeHandle*);
bool                jolt_lt_is_live(JoltLifetimeHandle*);

#### Cross-boundary Ownership Rules

| Source → Target         | Rule |
|-------------------------|------|
| Joltscript → C          | Glue retains ARC handle for call duration; releases after return |
| C → Joltscript          | Glue wraps raw pointer in `JOLT_LT_STACK`; copy-on-escape if stored |
| Python → Joltscript     | Glue borrows via `JOLT_LT_GC`; Python GC notified via callback |
| Joltscript → Python     | Glue creates a Python capsule holding an ARC retain |
| Rust → Joltscript       | Glue moves ownership into `JOLT_LT_RUST`; Rust drop is the destructor |
| Joltscript → Rust       | Glue clones the value; Rust owns the clone |

---

### 5. Error Adapter

The Error Adapter normalizes error representations from all language contexts into a unified `JoltError` type and propagates them across boundaries without loss of detail.

c
typedef enum JoltErrorDomain {
    JOLT_EDOM_CORE,
    JOLT_EDOM_SCRIPT,
    JOLT_EDOM_MARSHAL,
    JOLT_EDOM_DISPATCH,
    JOLT_EDOM_PYTHON,
    JOLT_EDOM_RUST,
    JOLT_EDOM_GO,
    JOLT_EDOM_IO,
    JOLT_EDOM_GPU,
} JoltErrorDomain;

typedef struct JoltError {
    JoltErrorDomain  domain;
    int32_t          code;
    const char*      message;       // UTF-8, Glue-owned
    const char*      source_file;
    uint32_t         source_line;
    struct JoltError* cauef struct JoltError {
    JoltErrorDomain  domain;
    int32_t          code;
    const char*      message;       // UTF-8, Glue-owned
    const char*      source_file;
    uint32_t         source_line;
    struct JoltError* caurDomain, int32_t code, const char* msg);
void       jolt_error_chain(JoltError* cause);
JoltError* jolt_error_pop(void);

// Status codes
typedef enum JoltStatus {
    JOLT_OK            =  0,
    JOLT_ERR_GENERIC   = -1,
    JOLT_ERR_CAP       = -2,   // Capability denied
    JOLT_ERR_TYPE      = -3,   // Marshal type mismatch
    JOLT_ERR_NULL      = -4,
    JOLT_ERR_OVERFLOW  = -5,
    JOLT_ERR_TIMEOUT   = -6,
    JOLT_ERR_SANDBOX   = -7,   // Sandbox violation
    JOLT_ERR_SCRIPT    = -8,   // Script runtime error
    JOLT_ERR_ABI       = -9,   // ABI resolution failure
} JoltStatus;

---

### 6. Extension Host

The Extension Host is the part of the Glue Layer responsible for loading, sandboxing, and communicating with third-party extensions written in any supported language.

c
typedef struct JoltExtManifest {
    const char*   name;
    const char*   version;         // SemVer string
    const char*   entry_symbol;    // Symbol for extension init fn
    uint64_t      required_caps;   // ue ABI version this wst char*   abi_version;     // Glue ABI version this was compiled against
} JoltExtManifest;

typedef struct JoltExtensionAPI {
    // Provided to the extension on load
    JoltABIRegistry* registry;
    JoltDispatchCtx* dispatch;
    JoltMarshalCtx*  marshal;
    JoltCapGrant*    grants;
    void*            user_data;
} JoltExtensionAPI;

// Extension lifecycle
JoltExtHandle jolt_ext_load(const char* path, JoltExtManifest*);
JoltStatus    jolt_ext_init(JoltExtHandle, JoltExtensionAPI*);
JoltStatus    jolt_ext_unload(JoltExtHandle);

// Extension ABI version check
bool jolt_ext_abi_compatible(const char* ext_abi_ver);

---

## Language Binding Adapters

The Glue Layer ships adapters for each supported host language. Each adapter wraps the C ABI into idiomatic patterns for that language.

### C Adapter (Native)

Direct C ABI — no wrapping overhead. Headers generated by `joltc --gen-header`.

### Rust Adapter

rust
// jolt-glue crate generated from ABI registry
use jolt_glue::{Dispatch, Marshal, CapGrant};

let ctx = Dispatch::new()
    .with_caps(CapGrant::RENDER | CapGrant::FILE_READ)
    .build()?;

let result = ctx.call("jolt.fx.render.submit_frame", &[frame.into()])?;

### Python Adapter

python
# jolt_glue module, installed via pip or bundled
import jolt_glue as jolt

ctx = jolt.DispatchContext(caps=["render", "file_read"])
result = ctx.call("jolt.fx.render.submit_frame", frame)

### Go Adapter

go
import "github.com/joltfx/glue"

ctx := glue.NewDispatch(glue.CapRender | glue.CapFileRead)
result, err := ctx.Call("jolt.fx.render.submit_frame", frame)

---

## Call Flow

A complete cross-boundary call from a Python extension into a Joltscript kernel:


Python extension
  │ calls jolt_glue.call("jolt.fx.kernels.wave_distortion", args)
  ▼
Python Adapter
  │ unmarshal Python args → JoltValue[]
  ▼
Capability Dispatcher
  │ check JOLT_CAP_KERNEL_AUTHOR in dispatch ctx
  ▼
ABI Registry
  │ lookup "jolt.fx.kernels.wave_distortion" → JoltABIEntry
  ▼
Marshal Engine
  │ convert JoltValue[] → C parameter layout per JoltTypeDesc
  │ allocate per-call arena
  ▼
Lifetime Bridge
  │ retain all ARC handles for call duration
  ▼
Tilly Core / JoltVM
  │ execute kernel
  ▼
Return path:
  │ unmarshal C return value → JoltValue
  │ release ARC retains
  │ free per-call arena
  │ marshal JoltValue → PyObject
  ▼
Python extension receives result

---

## Thread Safety

| Component            | Thread Model |
|----------------------|--------------|
| ABI Registry         | Read-many/write-once after init; no lock needed at runtime |
| Marshal Engine       | One `JoltMarshalCtx` per thread; contexts are not shared |
| Capability Dispatcher| One `JoltDispatchCtx` per call; thread-local error stack |
| Lifetime Bridge      | ARC is atomic; cross-thread retain/release is safe |
| Extension Host       | Extensions are loaded on the main thread; dispatch is multi-thread |

---

## Versioning and Stability

The Glue Layer exposes a versioned ABI. Extensions compiled against a given ABI version are forward-compatible within the same major version.

c
#define JOLT_GLUE_ABI_MAJOR  1
#define JOLT_GLUE_ABI_MINOR  0
#define JOLT_GLUE_ABI_PATCH  0

const char* jolt_glue_abi_version(void);
bool        jolt_glue_abi_compatible(uint32_t major, uint32_t minor);

Breaking changes (new required fields in structs, removed symbols, changed calling conventions) increment `MAJOR`. Additive changes increment `MINOR`. Bug fixes increment `PATCH`.

---

## Header and Build Artifacts

| Artifact                  | Description |
|---------------------------|-------------|
| `jolt_glue.h`             | Main C header; include this |
| `jolt_glue_types.h`       | Type descriptors only; for codegen tools |
| `jolt_glue_marshal.h`     | Marshal engine internals; for adapter authors |
| `libjolt_glue.a`          | Static library |
| `libjolt_glue.so` / `.dll`| Dynamic library |
| `jolt_glue.pyi`           | Python type stubs |
| `jolt_glue.go`            | Go binding source |
| `jolt_glue` (crate)       | Rust crate, generated |

---

## Security Considerations

The Glue Layer enforces sandboxing at every boundary crossing. An extension or script cannot bypass capability checks by calling C ABI functions directly — all entry points are routed through the Capability Dispatcher.

### Sandbox Enforcement Points


1. ABI Registration   — symbols not in the registry cannot be dispatched
2. Capability Check   — dispatch fails before the call if caps are absent
3. Marshal Validation — malformed or oversized inputs are rejected pre-call
4. Arena Limits       — per-call arenas have a configurable byte ceiling
5. Timeout            — dispatch context carries a hard timeout in ms

### Capability Denial Behavior

c
// When a capability check fails, dispatch returns JOLT_ERR_CAP.
// The error stack is populated with domain, code, and a message
// naming the missing capability.

JoltStatus result = jolt_dispatch(&ctx, args, arg_count, &out);
if (result == JOLT_ERR_CAP) {
    JoltError* err = jolt_error_get();
    // err->message: "Missing capability: JOLT_CAP_RENDER"
}

---

## Initialization Sequence

The Glue Layer must be initialized before any binding or dispatch call is made.

c
int main(void) {
    // 1. Initialize the Glue Layer
    JoltGlueConfig cfg = {
        .arena_limit_bytes  = 64 * 1024 * 1024,   // 64 MB per-call arena
        .dispatch_timeout   = 5000,                 // 5 seconds
        .abi_strict_mode    = true,                 // reject unresolved symbols
        .sandbox_enabled    = true,
    };
    JoltStatus s = jolt_glue_init(&cfg);
    assert(s == JOLT_OK);

    // 2. Create and populate the ABI registry
    JoltABIRegistry* reg = jolt_abi_registry_create();
    jolt_abi_register(reg, &my_entry);
    jolt_abi_resolve_all(reg);

    // 3. Load extensions
    JoltExtHandle ext = jolt_ext_load("./extensions/my_ext.so", &manifest);
    jolt_ext_init(ext, &api);

    // ... run application ...

    // 4. Teardown
    jolt_ext_unload(ext);
    jolt_abi_registry_destroy(reg);
    jolt_glue_shutdown();
}

---

## Summary

| Component            | Responsibility |
|----------------------|----------------|
| ABI Registry         | Symbol catalog and function pointer resolution |
| Marshal Engine       | Type conversion across all language boundaries |
| Capability Dispatcher| Pre-call capability enforcement and routing |
| Lifetime Bridge      | Cross-model memory ownership synchronization |
| Error Adapter        | Unified error representation and propagation |
| Extension Host       | Third-party extension load, sandbox, and lifecycle |