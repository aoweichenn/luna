#include "luna/frontend/type/type.h"

#include <stddef.h>

const char *luna_type_kind_name(LunaTypeKind kind) {
    switch (kind) {
    case LUNA_TYPE_INVALID:
        return "<invalid>";
    case LUNA_TYPE_VOID:
        return "void";
    case LUNA_TYPE_BOOL:
        return "bool";
    case LUNA_TYPE_I8:
        return "i8";
    case LUNA_TYPE_I16:
        return "i16";
    case LUNA_TYPE_I32:
        return "i32";
    case LUNA_TYPE_I64:
        return "i64";
    case LUNA_TYPE_ISIZE:
        return "isize";
    case LUNA_TYPE_U8:
        return "u8";
    case LUNA_TYPE_U16:
        return "u16";
    case LUNA_TYPE_U32:
        return "u32";
    case LUNA_TYPE_U64:
        return "u64";
    case LUNA_TYPE_USIZE:
        return "usize";
    case LUNA_TYPE_F32:
        return "f32";
    case LUNA_TYPE_F64:
        return "f64";
    case LUNA_TYPE_POINTER:
        return "pointer";
    case LUNA_TYPE_ARRAY:
        return "array";
    case LUNA_TYPE_NAMED:
        return "named type";
    case LUNA_TYPE_STRUCT:
        return "struct";
    case LUNA_TYPE_UNION:
        return "union";
    case LUNA_TYPE_ENUM:
        return "enum";
    }

    return "<unknown>";
}

bool luna_type_kind_is_integer(LunaTypeKind kind) {
    return luna_type_kind_is_signed_integer(kind) ||
           luna_type_kind_is_unsigned_integer(kind);
}

bool luna_type_kind_is_signed_integer(LunaTypeKind kind) {
    return kind == LUNA_TYPE_I8 || kind == LUNA_TYPE_I16 ||
           kind == LUNA_TYPE_I32 || kind == LUNA_TYPE_I64 ||
           kind == LUNA_TYPE_ISIZE;
}

bool luna_type_kind_is_unsigned_integer(LunaTypeKind kind) {
    return kind == LUNA_TYPE_U8 || kind == LUNA_TYPE_U16 ||
           kind == LUNA_TYPE_U32 || kind == LUNA_TYPE_U64 ||
           kind == LUNA_TYPE_USIZE;
}

bool luna_type_kind_is_float(LunaTypeKind kind) {
    return kind == LUNA_TYPE_F32 || kind == LUNA_TYPE_F64;
}

uint32_t luna_type_kind_bit_width(LunaTypeKind kind,
                                  const LunaDataLayout *data_layout) {
    switch (kind) {
    case LUNA_TYPE_BOOL:
        return 1U;
    case LUNA_TYPE_I8:
    case LUNA_TYPE_U8:
        return 8U;
    case LUNA_TYPE_I16:
    case LUNA_TYPE_U16:
        return 16U;
    case LUNA_TYPE_I32:
    case LUNA_TYPE_U32:
        return 32U;
    case LUNA_TYPE_I64:
    case LUNA_TYPE_U64:
        return 64U;
    case LUNA_TYPE_ISIZE:
    case LUNA_TYPE_USIZE:
        return data_layout == NULL ? 0U : data_layout->pointer.size_bits;
    case LUNA_TYPE_F32:
        return 32U;
    case LUNA_TYPE_F64:
        return 64U;
    case LUNA_TYPE_INVALID:
    case LUNA_TYPE_VOID:
    case LUNA_TYPE_POINTER:
    case LUNA_TYPE_ARRAY:
    case LUNA_TYPE_NAMED:
    case LUNA_TYPE_STRUCT:
    case LUNA_TYPE_UNION:
    case LUNA_TYPE_ENUM:
        return 0U;
    }

    return 0U;
}
