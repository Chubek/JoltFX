# AGENTS.md — JoltFX Execution Layer

## Overview

The Execution Layer is the runtime substrate of JoltFX. It orchestrates kernel execution, manages the frame pipeline, schedules GPU compute and render passes, handles CPU/GPU memory transfers, performs dependency resolution, and enforces resource budgets. It sits directly above the hardware abstraction layer and below the Glue Layer, receiving dispatch calls and producing rendered frames, processed audio, or computed results.

---

## Repository Layout

```
joltscript/layers/execution/
  src/
    scheduler.c          # Task scheduling and dispatch
    pipeline_manager.c   # Multi-kernel pipeline compilation and caching
    kernel_runner.c      # Individual kernel execution context
    resource_budget.c    # Per-frame memory and time limits
    memory_manager.c     # CPU/GPU buffer allocation and transfer
    frame_graph.c        # Declarative render/compute pass scheduling
    dep_resolver.c       # Task DAG construction and analysis
    command_buffer.c     # Low-level GPU command recording
    batch_compiler.c     # Kernel fusion and optimization
    jolt_execution.c     # Public API entry points
  include/
    jolt_execution.h     # Public C API
    jolt_scheduler.h
    jolt_pipeline.h
    jolt_kernel.h
    jolt_budget.h
    jolt_memory.h
    jolt_frame_graph.h
    jolt_dep_graph.h
    jolt_cmd_buffer.h
    jolt_batch_compiler.h
  tests/
    unit/                # Per-component unit tests
    integration/         # End-to-end execution tests
    conformance/         # Cross-backend execution tests
    perf/                # Scheduler, memory, batch compiler benchmarks
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Glue Layer                             │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│                  Execution Layer                          │
│  ┌──────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │   Scheduler  │ │   Pipeline   │ │  Kernel Runner  │  │
│  │              │ │   Manager    │ │                 │  │
│  └──────────────┘ └──────────────┘ └─────────────────┘  │
│  ┌──────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │   Resource   │ │   Memory     │ │  Frame Graph    │  │
│  │   Budget     │ │   Manager    │ │                 │  │
│  └──────────────┘ └──────────────┘ └─────────────────┘  │
│  ┌──────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │ Dependency   │ │   Command    │ │  Batch Compiler │  │
│  │ Resolver     │ │   Buffer     │ │                 │  │
│  └──────────────┘ └──────────────┘ └─────────────────┘  │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│           Hardware Abstraction Layer (HAL)                │
│            Vulkan │ Metal │ WGPU │ CPU SIMD              │
└──────────────────────────────────────────────────────────┘
```

---

## Core Components

### Implemented CPU image task runner (ABI 0.4)

`include/joltscript/image_task.h` and `src/image_task.c` provide synchronous,
budgeted callback execution for Glue image programs. The callback must write
the entire output frame and honor the remaining scratch budget. Input/output
must be finite; output is published only after success, including aliased calls.
No compiler dependency or worker thread is introduced. Color graph nodes use
this runner; `tests/unit/joltscript/test_image_task.c` covers failure atomicity.

### Implemented stereo audio task (ABI 0.5)

`audio_task.h/.c` dispatch immutable validated 8-input/4-output JBC1 programs over
bounded stereo blocks. Inputs bind source L/R, accumulated L/R, gain, balance and
normalized dual fades; outputs use lanes 0/1. Integer/double clock normalization
stays in the runner; audio math belongs in `kernels/audio_reactive/audio_mix.jolt`.
Use engine allocation and scratch output, preserving caller bytes on numerical
or program failure. No compiler dependency or worker thread is introduced.
`audio_task`, `audio_mixing` and `media_export` cover these contracts.

### 1. Scheduler

The Scheduler determines the order in which kernels execute, respecting data dependencies, resource availability, and user-defined priorities. It operates in three modes: **immediate** (execute-on-submit for interactive tools), **deferred** (batch-optimize for rendering), and **timeline** (timeline playback with frame deadlines).

```c
typedef enum JoltSchedMode {
    JOLT_SCHED_IMMEDIATE,     // Execute as soon as submitted
    JOLT_SCHED_DEFERRED,      // Accumulate and optimize batch
    JOLT_SCHED_TIMELINE,      // Timeline playback with frame deadlines
} JoltSchedMode;

typedef struct JoltScheduler {
    JoltSchedMode      mode;
    JoltTaskQueue*     ready_queue;       // Tasks with satisfied dependencies
    JoltTaskQueue*     pending_queue;     // Tasks waiting on dependencies
    JoltDepGraph*      dep_graph;         // Dependency graph
    JoltThreadPool*    thread_pool;
    JoltResourceBudget* budget;
    uint64_t           frame_deadline_ns; // For JOLT_SCHED_TIMELINE
} JoltScheduler;

// Scheduler API
JoltScheduler* jolt_scheduler_create(JoltSchedMode mode, uint32_t num_threads);
void           jolt_scheduler_destroy(JoltScheduler*);

JoltTaskID     jolt_scheduler_submit(
    JoltScheduler*     sched,
    JoltKernelDesc*    kernel,
    JoltTaskID*        dependencies,
    uint32_t           dep_count
);

JoltStatus     jolt_scheduler_execute_frame(JoltScheduler*);
JoltStatus     jolt_scheduler_wait(JoltScheduler*, JoltTaskID);
void           jolt_scheduler_cancel(JoltScheduler*, JoltTaskID);

// Priority adjustment
void           jolt_scheduler_set_priority(JoltScheduler*, JoltTaskID, int32_t priority);
```

#### Task States

```
PENDING → READY → RUNNING → COMPLETED
            ↓        ↓
         CANCELED  FAILED
```

---

### 2. Pipeline Manager

The Pipeline Manager constructs and caches execution pipelines. A pipeline is a compiled sequence of kernels with fixed input/output signatures, suitable for repeated invocation (e.g., per-frame effect chains).

```c
typedef struct JoltPipeline {
    JoltPipelineID    id;
    JoltKernelDesc**  stages;          // Ordered array of kernel descriptors
    uint32_t          stage_count;
    JoltBufferLayout* input_layout;    // Required input buffer shapes
    JoltBufferLayout* output_layout;   // Produced output buffer shapes
    JoltResourceReq*  resource_req;    // Total memory and GPU requirements
    uint64_t          compile_hash;    // Hash of kernel sources and config
    void*             gpu_state;       // Backend-specific compiled pipeline
} JoltPipeline;

// Pipeline API
JoltPipeline* jolt_pipeline_create(
    JoltKernelDesc**  stages,
    uint32_t          stage_count
);

void          jolt_pipeline_destroy(JoltPipeline*);

JoltStatus    jolt_pipeline_execute(
    JoltPipeline*     pipeline,
    JoltBuffer**      inputs,
    uint32_t          input_count,
    JoltBuffer**      outputs,
    uint32_t          output_count
);

// Pipeline caching
JoltStatus    jolt_pipeline_cache_save(JoltPipeline*, const char* path);
JoltPipeline* jolt_pipeline_cache_load(const char* path);
```

#### Pipeline Compilation

The Pipeline Manager calls the **Batch Compiler** to fuse adjacent kernels, eliminate redundant memory transfers, and generate optimized GPU shader code.

---

### 3. Kernel Runner

The Kernel Runner is the execution context for individual Joltscript or XAS kernels. It allocates scratch memory, binds input/output buffers, invokes the kernel entry point, and captures any runtime errors.

```c
typedef struct JoltKernelContext {
    JoltKernelDesc*    desc;
    JoltArena*         scratch;        // Per-invocation scratch arena
    JoltBuffer**       inputs;
    uint32_t           input_count;
    JoltBuffer**       outputs;
    uint32_t           output_count;
    JoltGPUContext*    gpu_ctx;        // GPU device context if GPU kernel
    uint64_t           start_ns;
    uint64_t           end_ns;
} JoltKernelContext;

// Kernel execution
JoltStatus jolt_kernel_run(
    JoltKernelDesc*    kernel,
    JoltBuffer**       inputs,
    uint32_t           input_count,
    JoltBuffer**       outputs,
    uint32_t           output_count,
    JoltKernelContext* ctx
);

// Async execution (returns immediately, result via callback)
JoltTaskID jolt_kernel_run_async(
    JoltKernelDesc*    kernel,
    JoltBuffer**       inputs,
    uint32_t           input_count,
    JoltBuffer**       outputs,
    uint32_t           output_count,
    void             (*callback)(JoltStatus, void* user_data),
    void*              user_data
);
```

#### Kernel Entry Point

A Joltscript kernel compiled to XAS exports a C-compatible entry point:

```c
// Generated by joltc for every kernel
JoltStatus my_kernel_entry(
    JoltKernelContext* ctx,
    void*              params       // Kernel-specific parameter struct
);
```

The Kernel Runner calls this function after setting up the context.

---

### 4. Resource Budget

The Resource Budget enforces per-frame and per-kernel limits on CPU memory, GPU memory, CPU time, and GPU time. When limits are exceeded, the Scheduler delays or rejects tasks.

```c
typedef struct JoltResourceBudget {
    size_t   cpu_mem_limit;       // Bytes
    size_t   cpu_mem_used;
    size_t   gpu_mem_limit;       // Bytes
    size_t   gpu_mem_used;
    uint64_t cpu_time_limit_ns;   // Per-frame CPU budget
    uint64_t cpu_time_used_ns;
    uint64_t gpu_time_limit_ns;   // Per-frame GPU budget
    uint64_t gpu_time_used_ns;
    uint32_t max_concurrent_tasks;
    uint32_t active_task_count;
} JoltResourceBudget;

// Budget API
JoltResourceBudget* jolt_budget_create(void);
void                jolt_budget_destroy(JoltResourceBudget*);

void                jolt_budget_set_cpu_mem(JoltResourceBudget*, size_t bytes);
void                jolt_budget_set_gpu_mem(JoltResourceBudget*, size_t bytes);
void                jolt_budget_set_cpu_time(JoltResourceBudget*, uint64_t ns);
void                jolt_budget_set_gpu_time(JoltResourceBudget*, uint64_t ns);

bool                jolt_budget_can_allocate_cpu(JoltResourceBudget*, size_t bytes);
bool                jolt_budget_can_allocate_gpu(JoltResourceBudget*, size_t bytes);
void                jolt_budget_allocate_cpu(JoltResourceBudget*, size_t bytes);
void                jolt_budget_allocate_gpu(JoltResourceBudget*, size_t bytes);
void                jolt_budget_release_cpu(JoltResourceBudget*, size_t bytes);
void                jolt_budget_release_gpu(JoltResourceBudget*, size_t bytes);

void                jolt_budget_reset_frame(JoltResourceBudget*);  // Called at frame start
```

#### Budget Enforcement

```c
// Before submitting a task
if (!jolt_budget_can_allocate_cpu(budget, kernel_mem_req)) {
    return JOLT_ERR_BUDGET_CPU_MEM;
}
if (!jolt_budget_can_allocate_gpu(budget, kernel_gpu_mem_req)) {
    return JOLT_ERR_BUDGET_GPU_MEM;
}

jolt_budget_allocate_cpu(budget, kernel_mem_req);
jolt_scheduler_submit(sched, kernel, deps, dep_count);

// After task completes
jolt_budget_release_cpu(budget, kernel_mem_req);
```

---

### 5. Memory Manager

The Memory Manager handles allocation, transfer, and synchronization of buffers across CPU and GPU memory spaces. It supports:

- **CPU-side arenas** (scratch allocations for Joltscript kernels)
- **GPU device memory** (Vulkan/Metal/WGPU buffers)
- **Staging buffers** (CPU→GPU and GPU→CPU transfers)
- **Memory pooling** (reuse buffers across frames)

```c
typedef enum JoltMemoryDomain {
    JOLT_MEM_CPU,          // Host memory
    JOLT_MEM_GPU_DEVICE,   // GPU-only, fastest
    JOLT_MEM_GPU_SHARED,   // CPU-visible GPU memory (slower)
    JOLT_MEM_GPU_STAGING,  // Temporary transfer buffer
} JoltMemoryDomain;

typedef struct JoltBuffer {
    JoltBufferID       id;
    JoltMemoryDomain   domain;
    void*              data;           // CPU pointer if applicable
    uint64_t           gpu_handle;     // Backend-specific GPU buffer handle
    size_t             size;
    size_t             stride;         // Element size for typed buffers
    JoltBufferUsage    usage;          // READ_ONLY, WRITE_ONLY, READ_WRITE
    uint32_t           ref_count;      // For pooling
} JoltBuffer;

// Memory Manager API
JoltMemoryMgr* jolt_memory_create(size_t cpu_pool_size, size_t gpu_pool_size);
void           jolt_memory_destroy(JoltMemoryMgr*);

JoltBuffer*    jolt_memory_alloc(
    JoltMemoryMgr*     mgr,
    JoltMemoryDomain   domain,
    size_t             size,
    size_t             alignment
);

void           jolt_memory_free(JoltMemoryMgr*, JoltBuffer*);

// Transfer operations
JoltStatus     jolt_memory_upload(
    JoltMemoryMgr*     mgr,
    JoltBuffer*        dst,        // Must be GPU buffer
    const void*        src,        // CPU pointer
    size_t             size
);

JoltStatus     jolt_memory_download(
    JoltMemoryMgr*     mgr,
    void*              dst,        // CPU pointer
    JoltBuffer*        src,        // GPU buffer
    size_t             size
);

JoltStatus     jolt_memory_copy_gpu(
    JoltMemoryMgr*     mgr,
    JoltBuffer*        dst,
    JoltBuffer*        src,
    size_t             size
);

// Synchronization
void           jolt_memory_barrier(JoltMemoryMgr*);
void           jolt_memory_flush(JoltMemoryMgr*, JoltBuffer*);
```

#### Memory Pooling

To reduce allocation overhead, the Memory Manager maintains per-domain free lists:

```c
typedef struct JoltMemoryPool {
    JoltBuffer**   free_list;
    uint32_t       free_count;
    uint32_t       capacity;
    size_t         block_size;     // Fixed size for this pool
} JoltMemoryPool;

// Pool management
JoltMemoryPool* jolt_memory_pool_create(size_t block_size, uint32_t capacity);
void            jolt_memory_pool_destroy(JoltMemoryPool*);

JoltBuffer*     jolt_memory_pool_acquire(JoltMemoryPool*);
void            jolt_memory_pool_release(JoltMemoryPool*, JoltBuffer*);
```

---

### 6. Frame Graph

The Frame Graph is a declarative description of all resources (textures, buffers) and passes (render, compute, transfer) required to produce one output frame. The Execution Layer compiles the Frame Graph into a sequence of GPU commands, automatically inserting barriers and managing transient resources.

```c
typedef enum JoltPassType {
    JOLT_PASS_RENDER,
    JOLT_PASS_COMPUTE,
    JOLT_PASS_TRANSFER,
} JoltPassType;

typedef struct JoltFrameGraphPass {
    JoltPassID        id;
    JoltPassType      type;
    const char*       name;
    JoltResourceID*   inputs;       // Array of resource IDs this pass reads
    uint32_t          input_count;
    JoltResourceID*   outputs;      // Array of resource IDs this pass writes
    uint32_t          output_count;
    void            (*execute)(JoltCommandBuffer*, void* user_data);
    void*             user_data;
} JoltFrameGraphPass;

typedef struct JoltFrameGraphResource {
    JoltResourceID    id;
    JoltResourceType  type;         // TEXTURE, BUFFER, RENDER_TARGET
    JoltResourceDesc  desc;         // Size, format, usage flags
    bool              is_transient; // Auto-allocated and freed within frame
} JoltFrameGraphResource;

typedef struct JoltFrameGraph {
    JoltFrameGraphPass**     passes;
    uint32_t                 pass_count;
    JoltFrameGraphResource** resources;
    uint32_t                 resource_count;
    JoltDepGraph*            dep_graph;    // Pass dependencies
} JoltFrameGraph;

// Frame Graph API
JoltFrameGraph* jolt_frame_graph_create(void);
void            jolt_frame_graph_destroy(JoltFrameGraph*);

JoltResourceID  jolt_frame_graph_add_resource(
    JoltFrameGraph*        fg,
    JoltResourceType       type,
    JoltResourceDesc*      desc,
    bool                   is_transient
);

JoltPassID      jolt_frame_graph_add_pass(
    JoltFrameGraph*        fg,
    JoltPassType           type,
    const char*            name,
    JoltResourceID*        inputs,
    uint32_t               input_count,
    JoltResourceID*        outputs,
    uint32_t               output_count,
    void                 (*execute)(JoltCommandBuffer*, void*),
    void*                  user_data
);

// Compile and execute
JoltStatus      jolt_frame_graph_compile(JoltFrameGraph*);
JoltStatus      jolt_frame_graph_execute(JoltFrameGraph*, JoltCommandBuffer*);
```

#### Example: Simple Render Graph

```c
JoltFrameGraph* fg = jolt_frame_graph_create();

// Declare resources
JoltResourceID color_target = jolt_frame_graph_add_resource(
    fg, JOLT_RES_TEXTURE,
    &(JoltResourceDesc){ .width = 1920, .height = 1080, .format = JOLT_FMT_RGBA8 },
    false  // Not transient; output
);

JoltResourceID depth_buffer = jolt_frame_graph_add_resource(
    fg, JOLT_RES_TEXTURE,
    &(JoltResourceDesc){ .width = 1920, .height = 1080, .format = JOLT_FMT_D32F },
    true   // Transient; only needed during frame
);

// Declare passes
JoltPassID render_pass = jolt_frame_graph_add_pass(
    fg, JOLT_PASS_RENDER, "MainRender",
    NULL, 0,                             // No inputs
    (JoltResourceID[]){ color_target, depth_buffer }, 2,
    my_render_callback, user_data
);

JoltPassID post_pass = jolt_frame_graph_add_pass(
    fg, JOLT_PASS_COMPUTE, "PostProcess",
    (JoltResourceID[]){ color_target }, 1,
    (JoltResourceID[]){ color_target }, 1,
    my_post_callback, user_data
);

// Compile and execute
jolt_frame_graph_compile(fg);
jolt_frame_graph_execute(fg, cmd_buf);
```

---

### 7. Dependency Resolver

The Dependency Resolver builds and analyzes the directed acyclic graph (DAG) of task dependencies. It detects cycles, computes execution order, and identifies opportunities for parallelism.

```c
typedef struct JoltDepGraph {
    JoltTaskID*   nodes;
    uint32_t      node_count;
    JoltDepEdge*  edges;
    uint32_t      edge_count;
    uint32_t*     in_degree;        // For topological sort
    uint32_t*     topo_order;       // Computed execution order
} JoltDepGraph;

// Dependency Graph API
JoltDepGraph* jolt_dep_graph_create(void);
void          jolt_dep_graph_destroy(JoltDepGraph*);

void          jolt_dep_graph_add_node(JoltDepGraph*, JoltTaskID);
void          jolt_dep_graph_add_edge(JoltDepGraph*, JoltTaskID from, JoltTaskID to);

JoltStatus    jolt_dep_graph_build(JoltDepGraph*);  // Compute topo order
bool          jolt_dep_graph_has_cycle(JoltDepGraph*);

// Query
uint32_t      jolt_dep_graph_get_order(JoltDepGraph*, JoltTaskID);
JoltTaskID*   jolt_dep_graph_get_ready(JoltDepGraph*, uint32_t* out_count);
```

#### Cycle Detection

If a cycle is detected during `jolt_dep_graph_build`, the function returns `JOLT_ERR_CYCLE` and populates an error with the offending task IDs.

---

### 8. Command Buffer

The Command Buffer is a low-level recording of GPU commands. It abstracts the backend (Vulkan, Metal, WGPU) into a unified interface.

```c
typedef struct JoltCommandBuffer {
    void*            backend_handle;   // VkCommandBuffer, MTLCommandBuffer, etc.
    JoltCmdState     state;            // RECORDING, EXECUTABLE, PENDING, COMPLETED
    uint32_t         command_count;
} JoltCommandBuffer;

// Command Buffer API
JoltCommandBuffer* jolt_cmd_create(void);
void               jolt_cmd_destroy(JoltCommandBuffer*);

void               jolt_cmd_begin(JoltCommandBuffer*);
void               jolt_cmd_end(JoltCommandBuffer*);

// Render commands
void               jolt_cmd_begin_render_pass(
    JoltCommandBuffer*     cmd,
    JoltRenderPassDesc*    desc
);

void               jolt_cmd_end_render_pass(JoltCommandBuffer*);

void               jolt_cmd_bind_pipeline(JoltCommandBuffer*, JoltPipeline*);
void               jolt_cmd_bind_buffer(JoltCommandBuffer*, uint32_t binding, JoltBuffer*);
void               jolt_cmd_draw(JoltCommandBuffer*, uint32_t vertex_count, uint32_t instance_count);

// Compute commands
void               jolt_cmd_dispatch(
    JoltCommandBuffer*     cmd,
    uint32_t               group_x,
    uint32_t               group_y,
    uint32_t               group_z
);

// Transfer commands
void               jolt_cmd_copy_buffer(
    JoltCommandBuffer*     cmd,
    JoltBuffer*            src,
    JoltBuffer*            dst,
    size_t                 size
);

void               jolt_cmd_barrier(
    JoltCommandBuffer*     cmd,
    JoltPipelineStage      src_stage,
    JoltPipelineStage      dst_stage
);

// Submit to GPU
JoltStatus         jolt_cmd_submit(JoltCommandBuffer*);
void               jolt_cmd_wait(JoltCommandBuffer*);
```

---

### 9. Batch Compiler

The Batch Compiler takes a sequence of Joltscript kernels and fuses them into optimized GPU shader code (SPIR-V, MSL, WGSL) or vectorized CPU code. It performs:

- **Kernel fusion**: merge adjacent map/reduce operations
- **Dead code elimination**: remove unused intermediate values
- **Memory coalescing**: rewrite memory access patterns for cache/bandwidth efficiency
- **Constant propagation**: fold constants across kernel boundaries

```c
typedef struct JoltBatchCompiler {
    JoltKernelDesc**   kernels;
    uint32_t           kernel_count;
    JoltCompileTarget  target;        // GPU_SPIRV, GPU_MSL, GPU_WGSL, CPU_SIMD
    JoltOptLevel       opt_level;     // NONE, BASIC, AGGRESSIVE
} JoltBatchCompiler;

// Batch Compiler API
JoltBatchCompiler* jolt_batch_compiler_create(JoltCompileTarget target);
void               jolt_batch_compiler_destroy(JoltBatchCompiler*);

void               jolt_batch_compiler_add_kernel(JoltBatchCompiler*, JoltKernelDesc*);

JoltStatus         jolt_batch_compiler_compile(
    JoltBatchCompiler*     bc,
    JoltCompiledKernel**   out_kernel
);

// Save compiled artifact
JoltStatus         jolt_batch_compiler_save(
    JoltCompiledKernel*    kernel,
    const char*            path
);
```

#### Compilation Targets

| Target          | Output Format     | Backend |
|-----------------|-------------------|---------|
| `GPU_SPIRV`     | SPIR-V binary     | Vulkan, WGPU |
| `GPU_MSL`       | Metal Shading Language | Metal |
| `GPU_WGSL`      | WebGPU Shading Language | WGPU, Browser |
| `CPU_SIMD`      | LLVM IR → native binary | CPU thread pool |

---

## Execution Modes

### Immediate Mode

In immediate mode, each kernel executes as soon as it is submitted. This is used by interactive tools (TUI, GUI) where low latency is critical.

```c
JoltScheduler* sched = jolt_scheduler_create(JOLT_SCHED_IMMEDIATE, 4);

JoltTaskID task = jolt_scheduler_submit(sched, kernel, NULL, 0);
jolt_scheduler_wait(sched, task);  // Blocks until kernel completes
```

### Deferred Mode

In deferred mode, kernels accumulate in the pending queue. The application calls `jolt_scheduler_execute_frame()` to batch-compile and execute all pending work.

```c
JoltScheduler* sched = jolt_scheduler_create(JOLT_SCHED_DEFERRED, 8);

// Submit multiple kernels
jolt_scheduler_submit(sched, kernel_a, NULL, 0);
jolt_scheduler_submit(sched, kernel_b, NULL, 0);
jolt_scheduler_submit(sched, kernel_c, deps, 2);

// Execute all at once
jolt_scheduler_execute_frame(sched);
```

### Timeline Mode

In timeline mode, the Scheduler respects a per-frame deadline. If the budget is exhausted before all tasks complete, lower-priority tasks are deferred to the next frame.

```c
JoltScheduler* sched = jolt_scheduler_create(JOLT_SCHED_TIMELINE, 8);
sched->frame_deadline_ns = 16666667;  // 60 FPS (16.67ms)

while (playing) {
    jolt_scheduler_execute_frame(sched);  // Runs until deadline or completion
}
```

---

## Error Handling

All Execution Layer APIs return `JoltStatus`. On error, the thread-local error stack is populated with diagnostic information.

```c
JoltStatus result = jolt_scheduler_submit(sched, kernel, deps, dep_count);
if (result != JOLT_OK) {
    JoltError* err = jolt_error_get();
    fprintf(stderr, "Execution error [%s:%u]: %s\n",
            err->source_file, err->source_line, err->message);
    
    // Check for specific error types
    if (result == JOLT_ERR_BUDGET_CPU_MEM) {
        // Reduce memory usage or increase budget
    } else if (result == JOLT_ERR_CYCLE) {
        // Fix dependency graph
    }
}
```

---

## Performance Monitoring

The Execution Layer exposes real-time performance counters via the diagnostics API.

```c
typedef struct JoltExecStats {
    uint64_t frame_count;
    uint64_t total_tasks_submitted;
    uint64_t total_tasks_completed;
    uint64_t total_tasks_failed;
    
    uint64_t cpu_time_ns;
    uint64_t gpu_time_ns;
    
    size_t   cpu_mem_peak;
    size_t   gpu_mem_peak;
    
    uint32_t avg_tasks_per_frame;
    uint64_t avg_frame_time_ns;
} JoltExecStats;

JoltExecStats jolt_exec_get_stats(JoltScheduler*);
void          jolt_exec_reset_stats(JoltScheduler*);
```

---

## Thread Model

The Execution Layer is **multi-threaded by default**. The Scheduler owns a thread pool for CPU kernel execution. GPU command submission happens on a dedicated GPU thread to avoid stalls.

```
Main Thread
  │
  ├─ Submit tasks to Scheduler
  │
  ▼
Scheduler Thread
  │
  ├─ Topological sort
  ├─ Dispatch to thread pool (CPU kernels)
  ├─ Dispatch to GPU thread (GPU kernels)
  │
  ▼
Worker Threads (CPU)         GPU Thread
  │                            │
  ├─ Execute CPU kernels       ├─ Record command buffers
  │                            ├─ Submit to GPU queue
  │                            ├─ Wait for fences
  │                            │
  ▼                            ▼
Callback on completion       Callback on completion
```

### Synchronization

The Execution Layer uses lock-free queues for task submission and completion callbacks. GPU synchronization is handled via fences and semaphores provided by the backend.

---

## Backend Abstraction (HAL)

The Execution Layer does not directly call Vulkan, Metal, or WGPU. Instead, it calls a thin Hardware Abstraction Layer (HAL) with a unified interface.

```c
typedef struct JoltHAL {
    JoltHALBackend backend;  // VULKAN, METAL, WGPU, CPU

    // Device management
    JoltStatus (*init)(JoltHALConfig* cfg);
    void       (*shutdown)(void);

    // Buffer operations
    JoltBuffer* (*buffer_create)(size_t size, JoltBufferUsage usage);
    void        (*buffer_destroy)(JoltBuffer*);
    void        (*buffer_upload)(JoltBuffer*, const void* data, size_t size);
    void        (*buffer_download)(JoltBuffer*, void* data, size_t size);

    // Command buffer operations
    JoltCommandBuffer* (*cmd_create)(void);
    void               (*cmd_destroy)(JoltCommandBuffer*);
    void               (*cmd_begin)(JoltCommandBuffer*);
    void               (*cmd_end)(JoltCommandBuffer*);
    JoltStatus         (*cmd_submit)(JoltCommandBuffer*);
    void               (*cmd_wait)(JoltCommandBuffer*);

    // Pipeline operations
    JoltPipeline* (*pipeline_create)(JoltPipelineDesc* desc);
    void          (*pipeline_destroy)(JoltPipeline*);

    // Synchronization
    void          (*barrier)(JoltPipelineStage src, JoltPipelineStage dst);
    void          (*fence_wait)(JoltFence*);
} JoltHAL;

// HAL selection at runtime
JoltHAL* jolt_hal_create(JoltHALBackend backend);
void     jolt_hal_destroy(JoltHAL*);
```

---

## Kernel Invocation Flow

A complete kernel execution from submission to completion:

1. **User submits kernel via Glue Layer**
   `jolt.call("jolt.fx.kernels.wave_distortion", inputs)`

2. **Glue Layer dispatches to Execution Layer**
   `jolt_scheduler_submit(sched, kernel_desc, deps, dep_count)`

3. **Scheduler adds task to pending queue**

4. **Dependency Resolver checks if dependencies are satisfied**

5. **Task moves to ready queue**

6. **Resource Budget checks CPU/GPU memory availability**

7. **Memory Manager allocates input/output buffers**

8. **Kernel Runner creates execution context**

9. **Backend (HAL) executes kernel**
   - CPU: calls compiled native function
   - GPU: records commands to command buffer, submits to GPU queue

10. **On completion, callback invoked**

11. **Memory Manager releases buffers (if not pooled)**

12. **Resource Budget updated**

13. **Result marshaled back to Glue Layer**

14. **User receives result**

---

## Configuration

The Execution Layer is configured at initialization.

```c
typedef struct JoltExecConfig {
    JoltSchedMode      sched_mode;
    uint32_t           num_cpu_threads;
    size_t             cpu_mem_limit;
    size_t             gpu_mem_limit;
    uint64_t           frame_deadline_ns;
    JoltHALBackend     backend;
    JoltOptLevel       opt_level;
    bool               enable_profiling;
    bool               enable_validation;  // GPU API validation layers
} JoltExecConfig;

JoltStatus jolt_exec_init(JoltExecConfig* cfg);
void       jolt_exec_shutdown(void);
```

---

## Summary

| Component          | Responsibility |
|--------------------|----------------|
| Scheduler          | Task ordering, dependency resolution, thread dispatch |
| Pipeline Manager   | Compile and cache multi-kernel pipelines |
| Kernel Runner      | Execute individual kernels with context and error handling |
| Resource Budget    | Enforce per-frame memory and time limits |
| Memory Manager     | Allocate, transfer, and pool CPU/GPU buffers |
| Frame Graph        | Declarative render/compute pass scheduling |
| Dependency Resolver| Build and analyze task DAG |
| Command Buffer     | Low-level GPU command recording |
| Batch Compiler     | Fuse and optimize kernel sequences for target backends |

The Execution Layer is the performance-critical core of JoltFX, responsible for turning high-level kernel descriptions into real-time rendered frames and computed results.

---

## Contribution Guidelines

### Adding a New Scheduler Mode

1. Add enum value to `JoltSchedMode`.
2. Implement scheduling logic in `scheduler.c`.
3. Add tests for the new mode in `tests/unit/scheduler/`.
4. Benchmark against existing modes.

### Modifying the Frame Graph

1. Changes to `JoltFrameGraphPass` or `JoltFrameGraphResource` require version bump.
2. Update all backend frame graph compilers.
3. Run conformance tests across all backends.

### Optimizing the Batch Compiler

1. Add new optimization pass in `batch_compiler.c`.
2. Verify correctness with `jolt verify --opt-level=AGGRESSIVE`.
3. Benchmark on representative kernel chains.
4. Ensure all four targets produce valid output.

---

## PR Checklist

- [ ] Builds clean with ASAN/UBSan
- [ ] All unit tests pass
- [ ] Integration tests pass (all execution modes)
- [ ] Conformance tests pass (all backends)
- [ ] No memory leaks (Valgrind clean)
- [ ] Thread safety verified (TSan clean)
- [ ] Benchmarks run for scheduler/memory/batch compiler changes
- [ ] Documentation updated for API changes
- [ ] Gate requirements met (see core/AGENTS.md Ownership Rules)

---

## Common Mistakes

**Submitting tasks without checking budget.** Always call `jolt_budget_can_allocate_*` before `jolt_scheduler_submit`.

**Not releasing budget after task completion.** Every `allocate` must have a matching `release`.

**Creating frame graph cycles.** The dependency resolver will catch this, but it's better to design acyclic graphs from the start.

**Forgetting to reset frame budget.** Call `jolt_budget_reset_frame` at the start of each frame in timeline mode.

**Blocking on GPU in scheduler thread.** GPU submission is async; use fences and callbacks, not `jolt_cmd_wait` in the hot path.

**Leaking command buffers.** Every `jolt_cmd_create` must have a matching `jolt_cmd_destroy`.

---

## Contacts

- **Scheduler**: `#joltfx-scheduler`
- **Pipeline Manager**: `#joltfx-pipeline`
- **Memory/Budget**: `#joltfx-memory`
- **Frame Graph**: `#joltfx-render`
- **Batch Compiler**: `#joltfx-compiler`
- **HAL / Backend integration**: `#joltfx-backend-core`
