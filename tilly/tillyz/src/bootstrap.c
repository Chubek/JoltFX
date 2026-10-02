#include "tillyz/tillyz.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32) && (!defined(__wasm__) || defined(__EMSCRIPTEN__))
/* Emscripten implements anonymous mappings in its linear-memory allocator. */
#include <sys/mman.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

static uint64_t get_timestamp_ns(void) {
#if defined(__linux__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#elif defined(_WIN32)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart * 1000000000ULL / (uint64_t)freq.QuadPart;
#else
    return 0;
#endif
}

uint64_t tillyz_time_now_ns(void) {
    return get_timestamp_ns();
}

tillyz_platform_t tillyz_detect_platform(void) {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    return TILLYZ_PLATFORM_WASM;
#elif defined(_WIN32)
    return TILLYZ_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return TILLYZ_PLATFORM_MACOS;
#elif defined(__linux__)
    return TILLYZ_PLATFORM_LINUX;
#else
    return TILLYZ_PLATFORM_UNKNOWN;
#endif
}

const char *tillyz_platform_name(tillyz_platform_t platform) {
    switch (platform) {
        case TILLYZ_PLATFORM_LINUX: return "linux";
        case TILLYZ_PLATFORM_MACOS: return "macos";
        case TILLYZ_PLATFORM_WINDOWS: return "windows";
        case TILLYZ_PLATFORM_WASM: return "wasm";
        case TILLYZ_PLATFORM_BARE: return "bare";
        default: return "unknown";
    }
}

static void *arena_align_ptr(void *ptr, size_t align) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + align - 1) & ~(uintptr_t)(align - 1);
    return (void *)aligned;
}

void *tillyz_arena_alloc(tillyz_arena_t *arena, size_t size, size_t align) {
    if (!arena || !arena->base || !align || (align & (align - 1)) ||
        arena->offset > arena->size || size == 0) {
        return NULL;
    }
    
    void *aligned_ptr = arena_align_ptr(arena->base + arena->offset, align);
    size_t padding = (uintptr_t)aligned_ptr - (uintptr_t)(arena->base + arena->offset);
    
    if (padding > arena->size - arena->offset ||
        size > arena->size - arena->offset - padding) {
        return NULL;
    }
    
    arena->offset += padding + size;
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }
    
    return aligned_ptr;
}

void tillyz_arena_reset(tillyz_arena_t *arena) {
    if (arena) {
        arena->offset = 0;
    }
}

size_t tillyz_arena_usage(const tillyz_arena_t *arena) {
    return arena ? arena->offset : 0;
}

size_t tillyz_arena_remaining(const tillyz_arena_t *arena) {
    return arena && arena->offset <= arena->size ? arena->size - arena->offset : 0;
}

size_t tillyz_arena_peak(const tillyz_arena_t *arena) {
    return arena ? arena->peak : 0;
}

void tillyz_error_push(tillyz_context_t *ctx, tillyz_status_t code, const char *message, const char *file, uint32_t line) {
    if (!ctx || ctx->error_count >= 16) {
        return;
    }
    
    tillyz_error_t *err = &ctx->errors[ctx->error_count++];
    err->code = code;
    err->message = message;
    err->file = file;
    err->line = line;
    err->timestamp_ns = get_timestamp_ns();
}

const tillyz_error_t *tillyz_error_peek(const tillyz_context_t *ctx) {
    if (!ctx || ctx->error_count == 0) {
        return NULL;
    }
    return &ctx->errors[ctx->error_count - 1];
}

void tillyz_error_pop(tillyz_context_t *ctx) {
    if (ctx && ctx->error_count > 0) {
        ctx->error_count--;
    }
}

void tillyz_error_clear(tillyz_context_t *ctx) {
    if (ctx) {
        ctx->error_count = 0;
    }
}

size_t tillyz_strlen(const char *str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int tillyz_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (int)(*a - *b);
}

void tillyz_strcpy(char *dst, const char *src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return;
    
    size_t i = 0;
    while (i < dst_size - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

char *tillyz_strdup(tillyz_arena_t *arena, const char *src) {
    if (!arena || !src) return NULL;
    
    size_t len = tillyz_strlen(src);
    char *dst = (char *)tillyz_arena_alloc(arena, len + 1, 1);
    if (!dst) return NULL;
    
    for (size_t i = 0; i <= len; i++) {
        dst[i] = src[i];
    }
    return dst;
}

int tillyz_snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (!buf || size == 0 || !fmt) return 0;
    
    va_list args;
    va_start(args, fmt);
    
    int written = 0;
    size_t i = 0;
    
    while (*fmt && i < size - 1) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(args, const char *);
                if (s) {
                    while (*s && i < size - 1) {
                        buf[i++] = *s++;
                    }
                }
            } else if (*fmt == 'd' || *fmt == 'i') {
                int d = va_arg(args, int);
                char tmp[32];
                int len = 0;
                if (d == 0) {
                    tmp[len++] = '0';
                } else {
                    if (d < 0) {
                        if (i < size - 1) buf[i++] = '-';
                        d = -d;
                    }
                    while (d > 0 && len < 31) {
                        tmp[len++] = (char)('0' + (d % 10));
                        d /= 10;
                    }
                    for (int j = len - 1; j >= 0 && i < size - 1; j--) {
                        buf[i++] = tmp[j];
                    }
                }
            } else if (*fmt == 'u') {
                unsigned int u = va_arg(args, unsigned int);
                char tmp[32];
                int len = 0;
                if (u == 0) {
                    tmp[len++] = '0';
                } else {
                    while (u > 0 && len < 31) {
                        tmp[len++] = (char)('0' + (u % 10));
                        u /= 10;
                    }
                    for (int j = len - 1; j >= 0 && i < size - 1; j--) {
                        buf[i++] = tmp[j];
                    }
                }
            } else if (*fmt == 'x') {
                unsigned int x = va_arg(args, unsigned int);
                char tmp[32];
                int len = 0;
                if (x == 0) {
                    tmp[len++] = '0';
                } else {
                    while (x > 0 && len < 31) {
                        int digit = x & 0xF;
                        tmp[len++] = (char)((digit < 10) ? '0' + digit : 'a' + (digit - 10));
                        x >>= 4;
                    }
                    for (int j = len - 1; j >= 0 && i < size - 1; j--) {
                        buf[i++] = tmp[j];
                    }
                }
            } else if (*fmt == 'p') {
                void *p = va_arg(args, void *);
                uintptr_t addr = (uintptr_t)p;
                if (i + 2 < size - 1) {
                    buf[i++] = '0';
                    buf[i++] = 'x';
                }
                char tmp[32];
                int len = 0;
                if (addr == 0) {
                    tmp[len++] = '0';
                } else {
                    while (addr > 0 && len < 31) {
                        int digit = addr & 0xF;
                        tmp[len++] = (char)((digit < 10) ? '0' + digit : 'a' + (digit - 10));
                        addr >>= 4;
                    }
                    for (int j = len - 1; j >= 0 && i < size - 1; j--) {
                        buf[i++] = tmp[j];
                    }
                }
            } else if (*fmt == 'c') {
                int c = va_arg(args, int);
                if (i < size - 1) buf[i++] = (char)c;
            } else if (*fmt == '%') {
                if (i < size - 1) buf[i++] = '%';
            } else {
                if (i < size - 1) buf[i++] = *fmt;
            }
        } else {
            buf[i++] = *fmt;
        }
        fmt++;
    }
    
    buf[i] = '\0';
    written = (int)i;
    
    va_end(args);
    return written;
}

void tillyz_default_panic(const char *message, const char *file, uint32_t line) {
    if (file) {
        fprintf(stderr, "PANIC at %s:%u: %s\n", file, line, message ? message : "unknown");
    } else {
        fprintf(stderr, "PANIC: %s\n", message ? message : "unknown");
    }
    abort();
}

void tillyz_panic(tillyz_context_t *ctx, const char *message, const char *file, uint32_t line) {
    if (ctx && ctx->panic_handler) {
        ctx->panic_handler(message, file, line);
    } else {
        tillyz_default_panic(message, file, line);
    }
}

tillyz_context_t *tillyz_init(const tillyz_config_t *config) {
    tillyz_config_t default_config = {
        .arena_size = 1024 * 1024,
        .arena_buffer = NULL,
        .panic_handler = tillyz_default_panic,
        .platform = TILLYZ_PLATFORM_AUTO,
    };
    
    if (config) {
        default_config = *config;
    }
    
    // Allocate context from arena
    size_t arena_size = default_config.arena_size;
    uint8_t *arena_base = default_config.arena_buffer;
    int owns_arena = !arena_base;
    
    if (!arena_base && arena_size >= sizeof(tillyz_context_t)) {
#if defined(_WIN32)
        arena_base = VirtualAlloc(NULL, arena_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif !defined(__wasm__) || defined(__EMSCRIPTEN__)
        arena_base = mmap(NULL, arena_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena_base == MAP_FAILED) arena_base = NULL;
#endif

    }
    
    if (!arena_base) return NULL;
    if (arena_size < sizeof(tillyz_context_t) ||
        ((uintptr_t)arena_base % _Alignof(tillyz_context_t)) != 0) {
#if defined(_WIN32)
        if (owns_arena) VirtualFree(arena_base, 0, MEM_RELEASE);
#elif !defined(__wasm__) || defined(__EMSCRIPTEN__)
        if (owns_arena) munmap(arena_base, arena_size);
#endif
        return NULL;
    }
    
    // Place context at start of arena
    // Alignment has been checked above, including caller-owned storage.
    tillyz_context_t *ctx = (tillyz_context_t *)(void *)arena_base;
    
    ctx->owns_arena = owns_arena;
    ctx->arena.base = arena_base;
    ctx->arena.size = arena_size;
    ctx->arena.offset = sizeof(tillyz_context_t);
    ctx->arena.peak = sizeof(tillyz_context_t);
    ctx->error_count = 0;
    ctx->platform = (default_config.platform == TILLYZ_PLATFORM_AUTO) ? 
                    tillyz_detect_platform() : default_config.platform;
    ctx->panic_handler = default_config.panic_handler;
    ctx->init_timestamp_ns = get_timestamp_ns();
    
    return ctx;
}

void tillyz_shutdown(tillyz_context_t *ctx) {
    if (!ctx) return;
    
#if !defined(_WIN32) && (!defined(__wasm__) || defined(__EMSCRIPTEN__))
    // If we allocated the arena, free it
    if (ctx->owns_arena && ctx->arena.base) {
        munmap(ctx->arena.base, ctx->arena.size);
    }
#elif defined(_WIN32)
    if (ctx->owns_arena && ctx->arena.base) {
        VirtualFree(ctx->arena.base, 0, MEM_RELEASE);
    }
#endif
}
