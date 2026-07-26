#ifndef LUNA_FRONTEND_TYPE_H
#define LUNA_FRONTEND_TYPE_H

#include "luna/target/target.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum LunaTypeKind {
    LUNA_TYPE_INVALID,
    LUNA_TYPE_VOID,
    LUNA_TYPE_BOOL,
    LUNA_TYPE_I8,
    LUNA_TYPE_I16,
    LUNA_TYPE_I32,
    LUNA_TYPE_I64,
    LUNA_TYPE_ISIZE,
    LUNA_TYPE_U8,
    LUNA_TYPE_U16,
    LUNA_TYPE_U32,
    LUNA_TYPE_U64,
    LUNA_TYPE_USIZE
} LunaTypeKind;

#ifdef __cplusplus
extern "C" {
#endif

const char *luna_type_kind_name(LunaTypeKind kind);
bool luna_type_kind_is_integer(LunaTypeKind kind);
bool luna_type_kind_is_signed_integer(LunaTypeKind kind);
bool luna_type_kind_is_unsigned_integer(LunaTypeKind kind);
uint32_t luna_type_kind_bit_width(LunaTypeKind kind,
                                  const LunaDataLayout *data_layout);

#ifdef __cplusplus
}
#endif

#endif
