#ifndef TILLY_LOGGER_H
#define TILLY_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include "tilly/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TILLY_LOG_TRACE = 0,
    TILLY_LOG_DEBUG,
    TILLY_LOG_INFO,
    TILLY_LOG_WARN,
    TILLY_LOG_ERROR,
    TILLY_LOG_FATAL,
} tilly_log_level_t;

typedef struct {
    tilly_log_level_t level;
    const char *message;
    const char *file;
    uint32_t line;
    uint64_t timestamp_ns;
    uint32_t thread_id;
    const char *module;
} tilly_log_entry_t;

typedef void (*tilly_log_sink_fn)(const tilly_log_entry_t *entry, void *user_data);

// Initialize logging system
void tilly_log_init(void);

// Shutdown logging system
void tilly_log_shutdown(void);

// Log a message (full API with module, file, line)
void tilly_log(
    tilly_log_level_t level,
    const char *module,
    const char *file,
    uint32_t line,
    const char *fmt,
    ...
) TILLY_PRINTF_LIKE(5, 6);

// Log a message (simple API for backward compatibility)
void tilly_log_simple(
    tilly_log_level_t level,
    const char *fmt,
    ...
) TILLY_PRINTF_LIKE(2, 3);

// Convenience macros
#define tilly_log_trace(module, ...) \
    tilly_log(TILLY_LOG_TRACE, module, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_debug(module, ...) \
    tilly_log(TILLY_LOG_DEBUG, module, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_info(module, ...) \
    tilly_log(TILLY_LOG_INFO, module, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_warn(module, ...) \
    tilly_log(TILLY_LOG_WARN, module, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_error(module, ...) \
    tilly_log(TILLY_LOG_ERROR, module, __FILE__, __LINE__, __VA_ARGS__)

#define tilly_log_fatal(module, ...) \
    tilly_log(TILLY_LOG_FATAL, module, __FILE__, __LINE__, __VA_ARGS__)

// Simple convenience macros (backward compatible)
#define tilly_log_trace_simple(...) \
    tilly_log_simple(TILLY_LOG_TRACE, __VA_ARGS__)

#define tilly_log_debug_simple(...) \
    tilly_log_simple(TILLY_LOG_DEBUG, __VA_ARGS__)

#define tilly_log_info_simple(...) \
    tilly_log_simple(TILLY_LOG_INFO, __VA_ARGS__)

#define tilly_log_warn_simple(...) \
    tilly_log_simple(TILLY_LOG_WARN, __VA_ARGS__)

#define tilly_log_error_simple(...) \
    tilly_log_simple(TILLY_LOG_ERROR, __VA_ARGS__)

#define tilly_log_fatal_simple(...) \
    tilly_log_simple(TILLY_LOG_FATAL, __VA_ARGS__)

// Register custom log sink
void tilly_log_add_sink(tilly_log_sink_fn sink, void *user_data);

// Remove log sink
void tilly_log_remove_sink(tilly_log_sink_fn sink);

// Get log level name
const char *tilly_log_level_name(tilly_log_level_t level);

// Set global log level filter
void tilly_log_set_level(tilly_log_level_t level);

// Get current global log level
tilly_log_level_t tilly_log_get_level(void);

#ifdef __cplusplus
}
#endif

#endif // TILLY_LOGGER_H
