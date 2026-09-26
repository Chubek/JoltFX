#include "tilly/logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define MAX_SINKS 16

typedef struct {
    tilly_log_sink_fn sink;
    void *user_data;
} sink_entry_t;

static sink_entry_t sinks[MAX_SINKS];
static uint32_t sink_count = 0;
static tilly_log_level_t global_level = TILLY_LOG_INFO;
static pthread_mutex_t sinks_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint32_t get_thread_id(void) {
    return (uint32_t)(uintptr_t)pthread_self();
}

void tilly_log_init(void) {
    // Already initialized via static init
}

void tilly_log_shutdown(void) {
    pthread_mutex_lock(&sinks_mutex);
    sink_count = 0;
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log(
    tilly_log_level_t level,
    const char *module,
    const char *file,
    uint32_t line,
    const char *fmt,
    ...
) {
    if (level < global_level) return;
    
    // Format message
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    // Create log entry
    tilly_log_entry_t entry = {
        .level = level,
        .message = message,
        .file = file,
        .line = line,
        .timestamp_ns = get_timestamp_ns(),
        .thread_id = get_thread_id(),
        .module = module ? module : "core",
    };
    
    // Dispatch to sinks
    pthread_mutex_lock(&sinks_mutex);
    for (uint32_t i = 0; i < sink_count; i++) {
        if (sinks[i].sink) {
            sinks[i].sink(&entry, sinks[i].user_data);
        }
    }
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log_simple(
    tilly_log_level_t level,
    const char *fmt,
    ...
) {
    if (level < global_level) return;
    
    // Format message
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    // Create log entry
    tilly_log_entry_t entry = {
        .level = level,
        .message = message,
        .file = "unknown",
        .line = 0,
        .timestamp_ns = get_timestamp_ns(),
        .thread_id = get_thread_id(),
        .module = "legacy",
    };
    
    // Dispatch to sinks
    pthread_mutex_lock(&sinks_mutex);
    for (uint32_t i = 0; i < sink_count; i++) {
        if (sinks[i].sink) {
            sinks[i].sink(&entry, sinks[i].user_data);
        }
    }
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log_add_sink(tilly_log_sink_fn sink, void *user_data) {
    pthread_mutex_lock(&sinks_mutex);
    if (sink_count < MAX_SINKS) {
        sinks[sink_count++] = (sink_entry_t){sink, user_data};
    }
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log_remove_sink(tilly_log_sink_fn sink) {
    pthread_mutex_lock(&sinks_mutex);
    for (uint32_t i = 0; i < sink_count; i++) {
        if (sinks[i].sink == sink) {
            sinks[i] = sinks[--sink_count];
            break;
        }
    }
    pthread_mutex_unlock(&sinks_mutex);
}

const char *tilly_log_level_name(tilly_log_level_t level) {
    static const char *names[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    if (level <= TILLY_LOG_FATAL) {
        return names[level];
    }
    return "UNKNOWN";
}

void tilly_log_set_level(tilly_log_level_t level) {
    global_level = level;
}

tilly_log_level_t tilly_log_get_level(void) {
    return global_level;
}

// Default stderr sink
static void default_stderr_sink(const tilly_log_entry_t *entry, void *user_data) {
    (void)user_data;
    fprintf(stderr, "[%s] %s:%u [%s]: %s\n",
            tilly_log_level_name(entry->level),
            entry->file, entry->line, entry->module, entry->message);
}

// Auto-register default sink
__attribute__((constructor))
static void tilly_log_auto_init(void) {
    tilly_log_add_sink(default_stderr_sink, NULL);
}