#include "tilly/allocator.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

template<class Allocate, class Release>
static double measure(Allocate allocate, Release release) {
    constexpr size_t rounds = 500, slots = 256;
    void *objects[slots];
    auto start = std::chrono::steady_clock::now();
    for (size_t round = 0; round < rounds; ++round) {
        for (size_t i = 0; i < slots; ++i) {
            size_t size = 32 + i * 7;
            objects[i] = allocate(size); assert(objects[i]);
            std::memset(objects[i], 0, size);
        }
        for (size_t i = 0; i < slots; i += 2) release(objects[i]);
        for (size_t i = 1; i < slots; i += 2) release(objects[i]);
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now()-start).count() / (rounds*slots);
}

int main() {
    auto *heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 0); assert(heap);
    double system = measure([](size_t n) { return std::malloc(n); }, [](void *p) { std::free(p); });
    double managed = measure([&](size_t n) { return tilly_alloc(heap, n, 64); }, [&](void *p) { tilly_free(heap, p); });
    assert(tilly_allocator_usage(heap) == 0);
    std::printf("fragmented_256_slots system_ns=%.1f memtkx_ns=%.1f payload_peak=%zu\n", system, managed, heap->peak);
    tilly_allocator_destroy(heap);
}
