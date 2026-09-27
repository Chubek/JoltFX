#include "tilly/containers.h"
#include <assert.h>
/* These tests are plain assert() programs, and several rely on assert() for
 * the side effect of its expression, not only for checking. A build that
 * defined NDEBUG would delete those calls and report success while testing
 * nothing. CMake passes -UNDEBUG to every test target; this turns a regression
 * into a build failure rather than a silently empty test run. */
#ifdef NDEBUG
#error "test targets must be compiled with assertions enabled (see tests/unit/TestHelpers.cmake)"
#endif
/* khash's initialiser dereferences a bucket pointer it has just memset, which
 * -Wnull-dereference flags once the macro is inlined at -O2. Upstream is
 * vendored, so the diagnostic is suppressed here rather than in the project
 * warning set, where it would hide real null dereferences. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
KHASH_MAP_INIT_STR(test, int)
int main(void) {
    kvec_t(int) v; kv_init(v);
    for (int i = 0; i < 4096; ++i) {
        assert(tilly_vec_reserve(&v, (size_t)i + 1));
        v.a[v.n++] = i;
    }
    for (int i = 4095; i >= 0; --i) assert(kv_pop(v) == i);
    size_t capacity = v.m;
    assert(!tilly_vec_reserve(&v, SIZE_MAX));
    assert(v.m == capacity && v.a);
    tilly_vec_destroy(v);
    khash_t(test) *h = kh_init(test);
    assert(h);
    int ret; khiter_t k = kh_put(test, h, "hello", &ret);
    assert(ret > 0); kh_value(h, k) = 42;
    assert(kh_value(h, kh_get(test, h, "hello")) == 42);
    k = kh_put(test, h, "hello", &ret); assert(ret == 0);
    kh_del(test, h, k); assert(kh_get(test, h, "hello") == kh_end(h));
    kh_destroy(test, h);
    return 0;
}
