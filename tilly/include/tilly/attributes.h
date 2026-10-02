#ifndef TILLY_ATTRIBUTES_H
#define TILLY_ATTRIBUTES_H

/* Compile-time checking only; these annotations do not change the C ABI. */
#if defined(__GNUC__) || defined(__clang__)
#define TILLY_PRINTF_LIKE(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define TILLY_PRINTF_LIKE(format_index, first_argument)
#endif

#endif
