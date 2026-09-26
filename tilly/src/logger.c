#include "tilly/logger.h"
#include <stdio.h>
#include <stdarg.h>

void tilly_log(tilly_log_level_t level, const char *format, ...) {
    const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    fprintf(stderr, "[%s] ", level_str[level]);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}
