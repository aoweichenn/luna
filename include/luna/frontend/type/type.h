#ifndef LUNA_FRONTEND_TYPE_H
#define LUNA_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum LunaTypeKind {
    LUNA_TYPE_INVALID,
    LUNA_TYPE_VOID,
    LUNA_TYPE_BOOL,
    LUNA_TYPE_I32
} LunaTypeKind;

#ifdef __cplusplus
extern "C" {
#endif

const char *luna_type_kind_name(LunaTypeKind kind);
bool luna_type_kind_is_integer(LunaTypeKind kind);
uint32_t luna_type_kind_bit_width(LunaTypeKind kind);

#ifdef __cplusplus
}
#endif

#endif
