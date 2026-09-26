#ifndef TILLY_LOGGER_H
#define TILLY_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TILLY_LOG_DEBUG,
    TILLY_LOG_INFO,
    TILLY_LOG_WARN,
    TILLY_LOG_ERROR
} tilly_log_level_t;

void tilly_log(tilly_log_level_t level, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // TILLY_LOGGER_H
