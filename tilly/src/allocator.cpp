#include "tilly/allocator.h"

#include <memtkx/space/bump_pointer.hpp>
#include <memtkx/space/free_list.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TILLY_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define TILLY_ADDRESS_SANITIZER 1
#endif
#ifdef TILLY_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif

namespace {
constexpr size_t guard_bytes = 16;
constexpr size_t page_bytes = 256 * 1024;
constexpr size_t max_bytes = static_cast<size_t>(PTRDIFF_MAX);
thread_local tilly_allocator_t *thread_allocator = nullptr;
thread_local std::weak_ptr<unsigned char> thread_lifetime;
thread_local bool thread_managed = false;

void poison(void *p, size_t n) noexcept {
#ifdef TILLY_ADDRESS_SANITIZER
    __asan_poison_memory_region(p, n);
#else
    (void)p; (void)n;
#endif
}
void unpoison(void *p, size_t n) noexcept {
#ifdef TILLY_ADDRESS_SANITIZER
    __asan_unpoison_memory_region(p, n);
#else
    (void)p; (void)n;
#endif
}

// System allocation is confined to backing storage and allocator metadata.
// All application addresses are placed by MemTKX and tracked out of band:
// invalid free/realloc never dereferences an untrusted allocation header.
struct Region {
    void *data;
    size_t bytes;
    explicit Region(size_t n) : data(std::malloc(n)), bytes(n) {
        if (!data) throw std::bad_alloc();
        poison(data, bytes);
    }
    ~Region() { unpoison(data, bytes); std::free(data); }
    Region(const Region &) = delete;
    Region &operator=(const Region &) = delete;
    MemTKX::Address start() const noexcept { return MemTKX::as_address(data); }
    MemTKX::Address end() const noexcept { return start() + bytes; }
};
struct Segment {
    Region region;
    MemTKX::FreeListAllocator space;
    size_t live = 0;
    explicit Segment(size_t n) : region(n), space(region.start(), region.end()) {}
};
struct Allocation {
    Segment *segment;
    MemTKX::Address raw;
    size_t reserved, size, alignment;
};
struct State {
    bool process;
    std::shared_ptr<unsigned char> lifetime;
    std::mutex lock;
    std::vector<std::unique_ptr<Segment>> segments;
    std::unordered_map<void *, Allocation> objects;
    std::unique_ptr<Region> scratch;
    MemTKX::BumpPointerAllocator bump;
    explicit State(bool is_process = false) : process(is_process),
        lifetime(is_process ? nullptr : std::make_shared<unsigned char>(0)) {}
    ~State() {
        // LSan sees region backing storage, not individual suballocations. The
        // test/debug leak census supplements it after application teardown.
        if (process && !objects.empty() && std::getenv("TILLY_CHECK_LEAKS")) {
            size_t bytes = 0;
            for (const auto &entry : objects) bytes += entry.second.size;
            std::fprintf(stderr, "Tilly MemTKX: %zu live allocations (%zu bytes) at process teardown\n", objects.size(), bytes);
            std::abort();
        }
    }
};

void *managed_alloc(tilly_allocator_t *, size_t, size_t);
void managed_free(tilly_allocator_t *, void *);
void managed_reset(tilly_allocator_t *);
size_t managed_usage(const tilly_allocator_t *);

tilly_allocator_t process_allocator = {
    TILLY_ALLOC_GENERAL, nullptr, 0, 0, 0, 0, 0,
    managed_alloc, managed_free, managed_reset, managed_usage
};
State &state_of(const tilly_allocator_t *alloc) {
    // Constructed before first use; lives through application teardown.
    static State process_state(true);
    return alloc == &process_allocator ? process_state : *static_cast<State *>(alloc->state);
}

bool extent(size_t size, size_t alignment, size_t &head, size_t &total) noexcept {
    head = std::max(guard_bytes, alignment);
    if (size > max_bytes - guard_bytes - 7 || head > max_bytes - size - guard_bytes - 7) return false;
    total = head + ((size + 7) & ~size_t{7}) + guard_bytes;
    return true;
}
void record_usage(tilly_allocator_t *alloc, size_t bytes, size_t replacing) noexcept {
    alloc->used += bytes;
    alloc->peak = std::max(alloc->peak, alloc->used - replacing);
}

void reclaim_idle(State &state, Segment *segment) noexcept {
    if (!segment->live && (segment->region.bytes > page_bytes || state.segments.size() > 1)) {
        auto at = std::find_if(state.segments.begin(), state.segments.end(),
            [&](const auto &item) { return item.get() == segment; });
        state.segments.erase(at);
    }
}

void *heap_alloc_locked(tilly_allocator_t *alloc, State &state, size_t size,
                       size_t alignment, size_t replacing = 0) {
    if (size > max_bytes - (alloc->used - replacing)) return nullptr;
    if (size > SIZE_MAX - alloc->used) return nullptr;
    if (alloc->capacity && size > alloc->capacity - (alloc->used - replacing)) return nullptr;
    size_t head, total;
    if (!extent(size, alignment, head, total)) return nullptr;
    Segment *chosen = nullptr;
    MemTKX::Address raw = 0;
    for (auto &segment : state.segments) {
        auto result = segment->space.allocate(total, std::max(alignment, size_t{8}));
        if (result.is_ok()) { chosen = segment.get(); raw = result.unwrap(); break; }
    }
    if (!chosen) {
        if (total > max_bytes - alignment) return nullptr;
        auto segment = std::make_unique<Segment>(std::max(page_bytes, total + alignment));
        auto result = segment->space.allocate(total, std::max(alignment, size_t{8}));
        if (result.is_err()) return nullptr;
        raw = result.unwrap();
        chosen = segment.get();
        state.segments.push_back(std::move(segment));
    }
    void *pointer = MemTKX::from_address<void>(raw + head);
    try {
        auto inserted = state.objects.emplace(pointer, Allocation{chosen, raw, total, size, alignment});
        if (!inserted.second) {
            if (!chosen->space.free(raw, total)) std::abort();
            reclaim_idle(state, chosen);
            return nullptr;
        }
    } catch (...) {
        if (!chosen->space.free(raw, total)) std::abort();
        reclaim_idle(state, chosen);
        throw;
    }
    ++chosen->live;
    unpoison(pointer, size);
    record_usage(alloc, size, replacing);
    return pointer;
}

void heap_free_locked(tilly_allocator_t *alloc, State &state, void *pointer) noexcept {
    auto found = state.objects.find(pointer);
    if (found == state.objects.end()) return;
    const Allocation object = found->second;
    // The staged free list has reserved metadata: this cannot allocate/throw.
    if (!object.segment->space.free(object.raw, object.reserved)) std::abort();
    poison(MemTKX::from_address<void>(object.raw), object.reserved);
    state.objects.erase(found);
    alloc->used -= object.size;
    --object.segment->live;
    // Retain at most one empty small page; return large/extra idle pages to OS.
    reclaim_idle(state, object.segment);
}

void *managed_alloc(tilly_allocator_t *alloc, size_t size, size_t alignment) {
    try {
        State &state = state_of(alloc);
        std::lock_guard<std::mutex> hold(state.lock);
        void *pointer = nullptr;
        if (alloc->strategy == TILLY_ALLOC_GENERAL) {
            pointer = heap_alloc_locked(alloc, state, size, alignment);
        } else if (alloc->strategy == TILLY_ALLOC_POOL) {
            if (size > 64 || alignment > alignof(max_align_t)) return nullptr;
            // Pools retain 64-byte accounting/slot capacity, but every slot has
            // separate guards and out-of-band ownership, never an in-object link.
            pointer = heap_alloc_locked(alloc, state, 64, alignof(max_align_t));
            if (pointer) poison(static_cast<unsigned char *>(pointer) + size, 64 - size);
        } else {
            if (size > max_bytes - guard_bytes - 7) return nullptr;
            size_t reserved = ((size + 7) & ~size_t{7}) + guard_bytes;
            auto result = state.bump.allocate(reserved, std::max(alignment, size_t{8}));
            if (result.is_ok()) {
                pointer = MemTKX::from_address<void>(result.unwrap());
                unpoison(pointer, size);
                alloc->used += size;
                alloc->peak = std::max(alloc->peak, alloc->used);
            }
        }
        if (pointer) ++alloc->alloc_count;
        return pointer;
    } catch (...) { return nullptr; }
}
void managed_free(tilly_allocator_t *alloc, void *pointer) {
    State &state = state_of(alloc);
    std::lock_guard<std::mutex> hold(state.lock);
    if (alloc->strategy != TILLY_ALLOC_GENERAL && alloc->strategy != TILLY_ALLOC_POOL) return;
    if (state.objects.find(pointer) == state.objects.end()) return;
    heap_free_locked(alloc, state, pointer);
    ++alloc->free_count;
}
void managed_reset(tilly_allocator_t *alloc) {
    State &state = state_of(alloc);
    std::lock_guard<std::mutex> hold(state.lock);
    if (alloc->strategy == TILLY_ALLOC_GENERAL) return;
    if (alloc->strategy == TILLY_ALLOC_POOL) {
        state.objects.clear();
        state.segments.clear();
    } else {
        poison(state.scratch->data, state.scratch->bytes);
        state.bump.reset();
    }
    alloc->used = 0;
}
size_t managed_usage(const tilly_allocator_t *alloc) {
    State &state = state_of(alloc);
    std::lock_guard<std::mutex> hold(state.lock);
    return alloc->used;
}
} // namespace

extern "C" {
const tilly_allocator_t *tilly_default_allocator(void) { return &process_allocator; }

tilly_allocator_t *tilly_allocator_create(tilly_alloc_strategy_t strategy, size_t capacity) {
    if (strategy < TILLY_ALLOC_ARENA || strategy > TILLY_ALLOC_STACK || capacity > max_bytes ||
        (!capacity && strategy != TILLY_ALLOC_GENERAL)) return nullptr;
    try {
        auto state = std::make_unique<State>();
        auto alloc = std::make_unique<tilly_allocator_t>();
        alloc->strategy = strategy;
        alloc->capacity = strategy == TILLY_ALLOC_POOL ? std::max(size_t{64}, capacity / 64 * 64) : capacity;
        if (strategy == TILLY_ALLOC_ARENA || strategy == TILLY_ALLOC_STACK) {
            state->scratch = std::make_unique<Region>(capacity);
            state->bump.reset(state->scratch->start(), state->scratch->end());
        }
        alloc->state = state.release();
        alloc->alloc = managed_alloc;
        alloc->free = managed_free;
        alloc->reset = managed_reset;
        alloc->usage = managed_usage;
        return alloc.release();
    } catch (...) { return nullptr; }
}
void tilly_allocator_destroy(tilly_allocator_t *alloc) {
    if (!alloc || alloc == &process_allocator) return;
    if (thread_allocator == alloc) {
        thread_allocator = nullptr; thread_lifetime.reset(); thread_managed = false;
    }
    delete static_cast<State *>(alloc->state);
    delete alloc;
}
void *tilly_alloc(tilly_allocator_t *alloc, size_t size, size_t alignment) {
    if (!alloc || !alloc->alloc || !size || !alignment || (alignment & (alignment - 1)) ||
        size > max_bytes || alignment > max_bytes) return nullptr;
    return alloc->alloc(alloc, size, alignment);
}
void tilly_free(tilly_allocator_t *alloc, void *pointer) {
    if (alloc && alloc->free && pointer) alloc->free(alloc, pointer);
}
void *tilly_realloc(tilly_allocator_t *alloc, void *pointer, size_t size) {
    if (!alloc) return nullptr;
    if (!pointer) return tilly_alloc(alloc, size, alignof(max_align_t));
    if (!size) { tilly_free(alloc, pointer); return nullptr; }
    if (alloc->strategy != TILLY_ALLOC_GENERAL || alloc->alloc != managed_alloc ||
        alloc->free != managed_free || size > max_bytes) return nullptr;
    try {
        State &state = state_of(alloc);
        std::lock_guard<std::mutex> hold(state.lock);
        auto found = state.objects.find(pointer);
        if (found == state.objects.end()) return nullptr;
        const Allocation old = found->second;
        if (size == old.size) return pointer;
        void *next = heap_alloc_locked(alloc, state, size, old.alignment, old.size);
        if (!next) return nullptr;
        std::memcpy(next, pointer, std::min(old.size, size));
        heap_free_locked(alloc, state, pointer);
        // Realloc is one logical allocation; transient overlap is not a peak.
        return next;
    } catch (...) { return nullptr; }
}
void tilly_allocator_reset(tilly_allocator_t *alloc) { if (alloc && alloc->reset) alloc->reset(alloc); }
size_t tilly_allocator_usage(const tilly_allocator_t *alloc) {
    return alloc && alloc->usage ? alloc->usage(alloc) : alloc ? alloc->used : 0;
}
void tilly_thread_set_allocator(tilly_allocator_t *alloc) {
    thread_allocator = alloc;
    thread_managed = alloc && alloc != &process_allocator && alloc->alloc == managed_alloc;
    thread_lifetime = thread_managed ? static_cast<State *>(alloc->state)->lifetime : std::weak_ptr<unsigned char>{};
}
tilly_allocator_t *tilly_thread_get_allocator(void) {
    if (thread_managed && thread_lifetime.expired()) {
        thread_allocator = nullptr; thread_lifetime.reset(); thread_managed = false;
    }
    return thread_allocator;
}
} // extern "C"
