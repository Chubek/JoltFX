# Memory ownership and MemTKX

JoltFX's native application storage is allocated through Tilly's adapter over
`third_party/memtkx`. Existing Core, Glue, Execution, backend and extension
callbacks inherit this implementation. CLI/tool buffers, animation containers,
desktop drawing/history and stb image decoding use the same path. C++17 clients
can use `tilly/memory.hpp`; C11 clients can use `tilly/memory.h`.

## Allocation contract (Tilly allocator API 2.0)

| Strategy | MemTKX implementation | Capacity and release |
|---|---|---|
| General | Stable-address free lists | Capacity bounds live requested bytes; zero is growable. Free exact pointers individually. |
| Pool | Stable-address free lists | 64-byte slots, capacity rounded down to slots with a minimum of one. Requests must fit a slot and fundamental alignment. |
| Arena / stack | Bump pointer | Fixed backing capacity includes alignment, rounding and 16-byte guard overhead per request. Individual free is deferred until reset. |

The struct layout and existing C signatures are retained. **Default-heap storage
is no longer compatible with libc `free` or `realloc`.** Use the matching
`tilly_free` / `tilly_realloc`, or the process-heap helpers:

```c
#include <tilly/memory.h>
char *text = tilly_mem_strdup("JoltFX");
/* Use text only when non-NULL. */
tilly_mem_free(text);
```

```cpp
#include <tilly/memory.hpp>
tilly::vector<float> samples(4096);
tilly::unique_ptr<MyObject> object(tilly::create<MyObject>());
```

Use the named API destructor for API-owned buffers/images/handles. Caller-owned
output buffers remain owned by their caller. Objects holding file handles, GPU
resources or other non-memory resources must be destroyed before their allocator.

- Allocation rejects zero sizes, invalid/non-power-of-two alignments, arithmetic
  overflow, sizes beyond `PTRDIFF_MAX` and exhausted budgets. Over-aligned heap
  allocations preserve their requested alignment through realloc.
- Realloc supports built-in general heaps. On failure, the original pointer,
  contents and live usage remain valid. Custom callbacks and other strategies
  reject non-NULL resize; NULL allocation and zero-size free still dispatch.
- Heap/pool ownership is stored out of band. Foreign, interior and duplicate frees
  are ignored without reading memory adjacent to the supplied pointer. Invalid
  realloc pointers are rejected. Addresses may be reused after release; stale raw
  pointers must never be accessed or reused as handles.
- All built-in operations and usage snapshots are synchronized. Public counters
  must not be read concurrently with mutations. Usage counts live requested bytes
  (whole slots for pools); backing pages, metadata, guard/padding bytes and
  third-party/GPU allocations are additional physical memory costs.
- Heap pages are allocated lazily. Empty large/extra pages are reclaimed, retaining
  at most one 256-KiB idle page per heap. Free/reset do not allocate metadata.
  Destroy releases all backing storage; arena/pool/stack reset preserves peak usage.
- Finish concurrent operations before allocator/context destruction. Built-in TLS
  bindings have weak lifetime tokens and clear after allocator destruction on
  another thread. This does not extend allocator lifetime or permit concurrent
  destruction. Custom allocator bindings require caller-managed lifetime.
- Process-heap allocations can survive an engine session, allowing independent
  documents, buffers and C++ containers to be released after session teardown.
  The process heap itself is reclaimed at process exit.

## Build integration

MemTKX is a required source dependency. Only `tilly/src/allocator.cpp` requires
C++20; application/public headers retain C11/C++17 compatibility. Installed CMake
targets carry the C++ linker dependency but do not expose MemTKX includes or a
C++20 requirement. TillyZ's caller-owned/OS-backed bootstrap arena stays
zero-dependency. Emscripten builds retain C++ exception handling so the C adapter
can translate metadata allocation failures to NULL.

`cmake/MemTKX.cmake` stages the vendor headers into the build directory and applies
checked free-list fixes: overflow-safe extent checks, metadata reservation before
mutation, overlap rejection and allocation-free in-place coalescing. Configuration
fails if those exact upstream contracts change. Review this file when upgrading
MemTKX; the original vendor checkout is not modified. Moving collectors are not
used because the C APIs require stable addresses.

System allocation is confined to backing regions and allocator metadata. Libraries
with allocator hooks (stb, ImGui, miniaudio and extension runtimes) receive Tilly
callbacks. OS handles, GPU drivers, codecs with private allocation and managed
JavaScript/Rust/Java/Swift heaps retain their respective ownership mechanisms.
The native allocation-policy test scans application source; it is an enforcement
check, not a proof of safety in third-party code or all execution paths.

## Browser transfer buffers

`jfx_web_alloc` / `jfx_web_free` own JS-to-native transfer buffers. Do not mix these
with Emscripten's `_malloc` / `_free`. The TypeScript bridge releases buffers in
`finally`, reacquires heap views after native calls that can grow memory and copies
returned frames/audio before release. Disposal is idempotent and subsequent
session calls are rejected before touching native handles. Rebuild both the WASM
module and the TypeScript package when updating this contract.

## Memory-safety verification

MemTKX supplies allocation mechanisms; it does not make raw C/C++ access safe by
itself. ASan builds explicitly poison suballocation padding, guards, freed blocks
and reset scratch regions. This lets ASan catch errors within a backing page.
`TILLY_CHECK_LEAKS=1` aborts at process teardown when process-heap objects remain,
supplementing LSan, which sees backing pages rather than each suballocation.
CTest enables this census for native unit/integration processes. Bulk-owned
context heaps are reclaimed on destroy; they are not counted as process leaks.

```sh
cmake -S . -B build-memory -DCMAKE_BUILD_TYPE=Debug -DJFX_ASAN=ON -DJFX_UBSAN=ON
cmake --build build-memory --parallel
ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-memory --output-on-failure
python3 scripts/check-memory-policy.py
npm --prefix frontends/web test
```

Regression coverage includes alignment, overflow/budgets, exact ownership,
failure-atomic realloc, metadata OOM, freeing while allocation is disabled,
fragmentation/coalescing, concurrent allocation, TLS invalidation, image ownership,
drawing-history OOM and malformed tool/animation input. Sanitizer subprocess tests
deliberately trigger overruns, underruns, use-after-free, use-after-reset and leaks
and require real diagnostic failures. See `PROGRESS.md` for measured validation
and platform coverage of this migration.
