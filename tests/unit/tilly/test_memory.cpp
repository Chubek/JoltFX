#include "tilly/memory.h"
#include "tilly/memory.hpp"
#include "../../../frontends/desktop/src/animation_drawing.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

// Fault the adapter's native metadata allocator. Application allocations still
// use MemTKX; free/reset must succeed even when no more metadata can be created.
static int fail_after = -1;
void *operator new(size_t n) {
    if (fail_after == 0) throw std::bad_alloc();
    if (fail_after > 0) --fail_after;
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

struct alignas(256) Aligned { unsigned char bytes[256]; };
struct Throws { Throws() { throw 1; } };

int main(int argc, char **argv) {
    if (argc == 2) {
        auto *heap = const_cast<tilly_allocator_t *>(tilly_default_allocator());
        if (!std::strcmp(argv[1], "reset")) heap = tilly_allocator_create(TILLY_ALLOC_ARENA, 128);
        volatile unsigned char *p = static_cast<unsigned char *>(tilly_alloc(heap, 13, 8));
        assert(p);
        if (!std::strcmp(argv[1], "overflow")) p[13] = 1;
        else if (!std::strcmp(argv[1], "underflow")) p[-1] = 1;
        else if (!std::strcmp(argv[1], "freed")) { tilly_free(heap, const_cast<unsigned char *>(p)); p[0] = 1; }
        else if (!std::strcmp(argv[1], "reset")) { tilly_allocator_reset(heap); p[0] = 1; tilly_allocator_destroy(heap); }
        else if (!std::strcmp(argv[1], "leak")) return 0;
        else return 2;
        return 0;
    }
    auto *process = const_cast<tilly_allocator_t *>(tilly_default_allocator());
    size_t baseline = tilly_allocator_usage(process);
    {
        tilly::vector<Aligned> v(3);
        assert(reinterpret_cast<uintptr_t>(v.data()) % 256 == 0);
        v[0].bytes[0] = 12;
        v.resize(50);
        assert(v[0].bytes[0] == 12);
        tilly::string name(1000, 'x');
        assert(tilly_allocator_usage(process) >= baseline + name.size());
    }
    assert(tilly_allocator_usage(process) == baseline);
    assert(!tilly::create<Throws>() && tilly_allocator_usage(process) == baseline);
    assert(!tilly_mem_calloc(SIZE_MAX, 2));
    char *copy = tilly_mem_strndup("short", SIZE_MAX);
    assert(copy && !std::strcmp(copy, "short"));
    tilly_mem_free(copy);

    for (int failure = 0; failure < 8; ++failure) {
        auto *heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 4096);
        assert(heap);
        auto *p = static_cast<unsigned char *>(tilly_alloc(heap, 128, 32));
        assert(p); p[0] = 42;
        fail_after = failure;
        auto *next = static_cast<unsigned char *>(tilly_realloc(heap, p, 512));
        fail_after = -1;
        assert(tilly_allocator_usage(heap) == (next ? 512u : 128u));
        assert((next ? next : p)[0] == 42);
        fail_after = 0;
        tilly_free(heap, next ? next : p);
        fail_after = -1;
        assert(!tilly_allocator_usage(heap));
        assert(tilly_alloc(heap, 4096, 8));
        assert(!tilly_alloc(heap, 1, 8));
        tilly_allocator_destroy(heap);
    }
    // Fragmentation, splitting and coalescing with free-time allocation disabled.
    auto *heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 0);
    assert(heap);
    void *blocks[300];
    for (size_t i = 0; i < 300; ++i) { blocks[i] = tilly_alloc(heap, i + 1, 64); assert(blocks[i]); }
    fail_after = 0;
    for (size_t i = 0; i < 300; i += 2) tilly_free(heap, blocks[i]);
    for (size_t i = 1; i < 300; i += 2) tilly_free(heap, blocks[i]);
    fail_after = -1;
    assert(!tilly_allocator_usage(heap));
    // Randomized mixed alignment/size churn verifies split/coalesce ownership
    // and data preservation when allocations and frees interleave.
    unsigned char *live[64]{};
    size_t sizes[64]{}, used = 0;
    uint32_t random = 123;
    for (size_t step = 0; step < 5000; ++step) {
        random = random * 1664525u + 1013904223u;
        size_t slot = (random >> 16) % 64;
        if (live[slot]) {
            for (size_t j = 0; j < sizes[slot]; ++j) assert(live[slot][j] == slot);
            fail_after = 0; tilly_free(heap, live[slot]); fail_after = -1;
            used -= sizes[slot]; live[slot] = nullptr;
        } else {
            sizes[slot] = random % 1024 + 1;
            size_t alignment = size_t{1} << (random % 9);
            live[slot] = static_cast<unsigned char *>(tilly_alloc(heap, sizes[slot], alignment));
            assert(live[slot] && reinterpret_cast<uintptr_t>(live[slot]) % alignment == 0);
            std::memset(live[slot], static_cast<int>(slot), sizes[slot]); used += sizes[slot];
        }
        assert(tilly_allocator_usage(heap) == used);
    }
    for (auto *p : live) tilly_free(heap, p);
    assert(!tilly_allocator_usage(heap));
    tilly_allocator_destroy(heap);
    // UI mutations must recover before changing artwork/history, and must not
    // propagate allocation exceptions through the C frontend/ImGui scopes.
    {
        jfx_drawing::Editor e;
        assert(e.begin_shape({1, 2})); e.commit_shape();
        assert(e.cel().shapes.size() == 1 && e.past.size() == 1);
        fail_after = 0;
        e.add_layer(); e.frame = 2; e.key(false); e.undo();
        assert(!e.checkpoint());
        fail_after = -1;
        assert(e.allocation_failed && e.doc.layers.size() == 1);
        assert(e.cel().shapes.size() == 1 && e.past.size() == 1 && e.future.empty());
        e.undo(); assert(e.cel().shapes.empty());
        fail_after = 0; e.redo(); fail_after = -1;
        assert(e.cel().shapes.empty() && e.future.size() == 1);
        e.redo(); assert(e.cel().shapes.size() == 1);
    }
    assert(tilly_allocator_usage(process) == baseline);
}
