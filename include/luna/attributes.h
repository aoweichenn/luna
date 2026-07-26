#ifndef LUNA_ATTRIBUTES_H
#define LUNA_ATTRIBUTES_H

#if defined(__clang__) || defined(__GNUC__)
#define LUNA_PRINTF_LIKE(format_index, first_argument)                         \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define LUNA_PRINTF_LIKE(format_index, first_argument)
#endif

#endif
