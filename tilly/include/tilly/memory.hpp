#ifndef TILLY_MEMORY_HPP
#define TILLY_MEMORY_HPP

#include "tilly/allocator.h"
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace tilly {
/* Stateless STL allocator: copies/moves share the process heap and may be
 * destroyed after their originating engine session. C++17-compatible surface. */
template<class T> struct allocator {
    using value_type = T;
    using is_always_equal = std::true_type;
    allocator() noexcept = default;
    template<class U> allocator(const allocator<U> &) noexcept {}
    T *allocate(size_t count) {
        if (count > std::numeric_limits<size_t>::max() / sizeof(T)) throw std::bad_array_new_length();
        void *p = tilly_alloc(const_cast<tilly_allocator_t *>(tilly_default_allocator()), count * sizeof(T), alignof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T *>(p);
    }
    void deallocate(T *p, size_t) noexcept {
        tilly_free(const_cast<tilly_allocator_t *>(tilly_default_allocator()), p);
    }
};
template<class T, class U> bool operator==(const allocator<T> &, const allocator<U> &) noexcept { return true; }
template<class T, class U> bool operator!=(const allocator<T> &, const allocator<U> &) noexcept { return false; }
template<class T> using vector = std::vector<T, allocator<T>>;
using string = std::basic_string<char, std::char_traits<char>, allocator<char>>;

template<class T, class... Args> T *create(Args &&...args) noexcept {
    T *p = nullptr;
    try {
        p = allocator<T>{}.allocate(1);
        return new (p) T(std::forward<Args>(args)...);
    } catch (...) {
        if (p) allocator<T>{}.deallocate(p, 1);
        return nullptr;
    }
}
template<class T> void destroy(T *p) noexcept {
    if (p) { p->~T(); allocator<T>{}.deallocate(p, 1); }
}
template<class T> struct deleter { void operator()(T *p) const noexcept { destroy(p); } };
template<class T> using unique_ptr = std::unique_ptr<T, deleter<T>>;
} // namespace tilly
#endif
