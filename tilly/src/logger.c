/* Tilly diagnostics sink.
 *
 * Sinks are invoked from whichever thread logged, synchronously, on a snapshot
 * of the registry taken under the lock. The lock is released *before* any sink
 * runs, so a sink may itself log, add a sink or remove a sink without
 * deadlocking — the pattern the Core threading rules call out. */

#include "tilly/logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TILLY_LOG_MAX_SINKS 16
#define TILLY_LOG_MESSAGE_MAX 1024

typedef struct {
    tilly_log_sink_fn sink;
    void *user_data;
} sink_entry_t;

static sink_entry_t sinks[TILLY_LOG_MAX_SINKS];
static uint32_t sink_count;
static atomic_int log_level = TILLY_LOG_INFO;
static atomic_bool initialized;
static pthread_mutex_t sinks_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
#else
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
#endif
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint32_t get_thread_id(void) {
    return (uint32_t)(uintptr_t)pthread_self();
}

static void default_stderr_sink(const tilly_log_entry_t *entry, void *user_data) {
    (void)user_data;
    if (entry->file && entry->file[0] && strcmp(entry->file, "-") != 0) {
        fprintf(stderr, "[%s] %s:%u [%s]: %s\n", tilly_log_level_name(entry->level),
            entry->file, entry->line, entry->module, entry->message);
    } else {
        fprintf(stderr, "[%s] [%s]: %s\n", tilly_log_level_name(entry->level),
            entry->module, entry->message);
    }
}

void tilly_log_init(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&initialized, &expected, true)) {
        return; /* already initialized; init is idempotent */
    }
    pthread_mutex_lock(&sinks_mutex);
    sink_count = 0;
    sinks[0].sink = default_stderr_sink;
    sinks[0].user_data = NULL;
    sink_count = 1;
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log_shutdown(void) {
    pthread_mutex_lock(&sinks_mutex);
    sink_count = 0;
    pthread_mutex_unlock(&sinks_mutex);
    atomic_store(&initialized, false);
}

/* Formats and dispatches one entry. The caller has already filtered by level. */
static void emit(tilly_log_level_t level, const char *module, const char *file,
    uint32_t line, const char *fmt, va_list args) {
    char message[TILLY_LOG_MESSAGE_MAX];
    vsnprintf(message, sizeof(message), fmt, args);

    tilly_log_entry_t entry = {
        .level = level,
        .message = message,
        .file = file ? file : "-",
        .line = line,
        .timestamp_ns = get_timestamp_ns(),
        .thread_id = get_thread_id(),
        .module = (module && module[0]) ? module : "tilly",
    };

    /* Snapshot under the lock, then dispatch with it released. */
    sink_entry_t snapshot[TILLY_LOG_MAX_SINKS];
    uint32_t count = 0;
    pthread_mutex_lock(&sinks_mutex);
    count = sink_count > TILLY_LOG_MAX_SINKS ? TILLY_LOG_MAX_SINKS : sink_count;
    memcpy(snapshot, sinks, count * sizeof(snapshot[0]));
    pthread_mutex_unlock(&sinks_mutex);

    for (uint32_t i = 0; i < count; ++i) {
        if (snapshot[i].sink) {
            snapshot[i].sink(&entry, snapshot[i].user_data);
        }
    }
}

void tilly_log(tilly_log_level_t level, const char *module, const char *file,
    uint32_t line, const char *fmt, ...) {
    if (!fmt || level < (tilly_log_level_t)atomic_load(&log_level)) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    emit(level, module, file, line, fmt, args);
    va_end(args);
}

void tilly_log_simple(tilly_log_level_t level, const char *fmt, ...) {
    if (!fmt || level < (tilly_log_level_t)atomic_load(&log_level)) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    /* "-" marks "no source location", which the default sink renders without
     * a bogus file:line prefix. */
    emit(level, NULL, "-", 0, fmt, args);
    va_end(args);
}

void tilly_log_add_sink(tilly_log_sink_fn sink, void *user_data) {
    if (!sink) {
        return;
    }
    pthread_mutex_lock(&sinks_mutex);
    if (sink_count >= TILLY_LOG_MAX_SINKS) {
        pthread_mutex_unlock(&sinks_mutex);
        fprintf(stderr, "[ERROR] [tilly/logger]: sink registry full (%d); sink not registered\n",
            TILLY_LOG_MAX_SINKS);
        return;
    }
    sinks[sink_count].sink = sink;
    sinks[sink_count].user_data = user_data;
    sink_count++;
    pthread_mutex_unlock(&sinks_mutex);
}

void tilly_log_remove_sink(tilly_log_sink_fn sink) {
    pthread_mutex_lock(&sinks_mutex);
    for (uint32_t i = 0; i < sink_count; ++i) {
        if (sinks[i].sink == sink) {
            sinks[i] = sinks[sink_count - 1u];
            sink_count--;
            break;
        }
    }
    pthread_mutex_unlock(&sinks_mutex);
}

const char *tilly_log_level_name(tilly_log_level_t level) {
    static const char *const names[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    if (level < TILLY_LOG_TRACE || level > TILLY_LOG_FATAL) {
        return "UNKNOWN";
    }
    return names[level];
}

void tilly_log_set_level(tilly_log_level_t level) {
    atomic_store(&log_level, level);
}

tilly_log_level_t tilly_log_get_level(void) {
    return (tilly_log_level_t)atomic_load(&log_level);
}
