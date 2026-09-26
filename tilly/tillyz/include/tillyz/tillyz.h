#ifndef TILLYZ_H
#define TILLYZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform detection
typedef enum {
    TILLYZ_PLATFORM_UNKNOWN = 0,
    TILLYZ_PLATFORM_LINUX,
    TILLYZ_PLATFORM_MACOS,
    TILLYZ_PLATFORM_WINDOWS,
    TILLYZ_PLATFORM_WASM,
    TILLYZ_PLATFORM_BARE,
    TILLYZ_PLATFORM_AUTO,
} tillyz_platform_t;

// Error codes
typedef enum {
    TILLYZ_OK = 0,
    TILLYZ_ERR_NOMEM = -1,
    TILLYZ_ERR_INVALID_ARG = -2,
    TILLYZ_ERR_NOT_FOUND = -3,
    TILLYZ_ERR_INIT_FAILED = -4,
} tillyz_status_t;

// Error entry
typedef struct {
    tillyz_status_t code;
    const char *message;
    const char *file;
    uint32_t line;
    uint64_t timestamp_ns;
} tillyz_error_t;

// Arena allocator
typedef struct {
    uint8_t *base;
    size_t size;
    size_t offset;
    size_t peak;
} tillyz_arena_t;

// Configuration for initialization
typedef struct {
    size_t arena_size;
    void *arena_buffer;  // Optional pre-allocated buffer
    void (*panic_handler)(const char *message, const char *file, uint32_t line);
    tillyz_platform_t platform;
} tillyz_config_t;

// Bootstrap context
typedef struct tillyz_context {
    tillyz_arena_t arena;
    tillyz_error_t errors[16];
    uint32_t error_count;
    tillyz_platform_t platform;
    void (*panic_handler)(const char *message, const char *file, uint32_t line);
    uint64_t init_timestamp_ns;
} tillyz_context_t;

// Initialize the bootstrap runtime
tillyz_context_t *tillyz_init(const tillyz_config_t *config);

// Shutdown and cleanup
void tillyz_shutdown(tillyz_context_t *ctx);

// Platform detection
tillyz_platform_t tillyz_detect_platform(void);
const char *tillyz_platform_name(tillyz_platform_t platform);

// Arena allocator API
void *tillyz_arena_alloc(tillyz_arena_t *arena, size_t size, size_t align);
void tillyz_arena_reset(tillyz_arena_t *arena);
size_t tillyz_arena_usage(const tillyz_arena_t *arena);
size_t tillyz_arena_remaining(const tillyz_arena_t *arena);
size_t tillyz_arena_peak(const tillyz_arena_t *arena);

// Error stack API
void tillyz_error_push(tillyz_context_t *ctx, tillyz_status_t code, const char *message, const char *file, uint32_t line);
const tillyz_error_t *tillyz_error_peek(const tillyz_context_t *ctx);
void tillyz_error_pop(tillyz_context_t *ctx);
void tillyz_error_clear(tillyz_context_t *ctx);

// String utilities (no libc dependency)
size_t tillyz_strlen(const char *str);
int tillyz_strcmp(const char *a, const char *b);
void tillyz_strcpy(char *dst, const char *src, size_t dst_size);
char *tillyz_strdup(tillyz_arena_t *arena, const char *src);
int tillyz_snprintf(char *buf, size_t size, const char *fmt, ...);

// Panic handler
void tillyz_panic(tillyz_context_t *ctx, const char *message, const char *file, uint32_t line);
void tillyz_default_panic(const char *message, const char *file, uint32_t line);

// Timestamp
uint64_t tillyz_time_now_ns(void);

#ifdef __cplusplus
}
#endif

#endif // TILLYZ_H