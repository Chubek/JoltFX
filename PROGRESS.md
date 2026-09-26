# Phase 1 foundation progress

Updated September 27, 2026. Phase 1 is **partially implemented**. This document replaces the earlier completion claim with results reproduced from the current source tree. The roadmap dates Phase 1 to Q1 2027.

## Implemented and tightened

- TillyZ: platform bootstrap and arena allocation, including caller-owned arena storage. Initialization now rejects undersized or misaligned buffers; shutdown does not unmap caller-owned memory. Arena allocations reject invalid alignment and overflow.
- Tilly: arena, fixed 64-byte pool, general and stack allocators; logger and dynamic module registry. Allocations reject invalid alignment; unsafe realloc copies for non-general allocators have been removed. Runtime shutdown unloads every module even if it was loaded repeatedly.
- Core: engine init/shutdown, frame boundaries, persistent and frame memory, CPU worker scheduler, synchronous event bus, result strings. Engine init checks and unwinds failed subsystem startup. Tick waits for submitted CPU tasks. Scheduler tracks executing tasks as well as queued tasks, drains accepted work at shutdown, and selects the highest priority queued task. Full queues reject submissions. Event dispatch snapshots subscribers and invokes callbacks outside the registry lock; the registry resets between engine lifetimes.
- CMake: library, executable, and test targets build. C++ specific warning flags are now applied to C++ only.

## Verification on Linux

- `cmake -S . -B /tmp/joltfx-phase1 -DCMAKE_BUILD_TYPE=Debug` and `cmake --build /tmp/joltfx-phase1 -j 4`: pass.
- `ctest --test-dir /tmp/joltfx-phase1 --output-on-failure`: 2/2 pass. Tests exercise invalid inputs, engine restart, event unsubscribe inside callback, scheduler task completion, frame arena reset, resource pool usage, and caller-owned TillyZ memory.
- AddressSanitizer and UndefinedBehaviorSanitizer build: 2/2 tests pass with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer cannot run in this environment (`LeakSanitizer does not work under ptrace`); leak freedom has not been verified.

## Remaining Phase 1 work and limitations

- TillyZ still links libc and OS runtime facilities; the zero-libc/~50KB bootstrap claim is not implemented or verified. The bootstrap formatter supports only a subset of printf and does not yet match standard snprintf semantics.
- Core memory, event registry, and scheduler are process-global. Concurrent independent engine instances and concurrent shutdown/API calls are unsupported. The engine has no render/frame-graph execution yet; a tick supplies frame events, arena reset, and a CPU task barrier.
- The pool has fixed 64-byte blocks rather than the previously claimed 4KB blocks. General allocator accounting is incomplete; a comprehensive tracked heap, robust pointer validation, and portable high-alignment allocations remain open.
- Dynamic module loading depends on POSIX `dlopen`; Windows, WASM, and bare-metal builds and runtime behavior have not been verified. A production module lifecycle needs callback reentrancy and concurrency design.
- The build still emits warnings in other, unfinished subsystems. CI, cross-platform builds, full leak checks, and a broader concurrency stress suite remain outstanding.

Phase 2 components listed in `ROADMAP.md` are outside this foundation verification.
