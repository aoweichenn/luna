#include "luna/frontend/type/type.h"

const char *luna_type_kind_name(LunaTypeKind kind) {
    switch (kind) {
    case LUNA_TYPE_INVALID:
        return "<invalid>";
    case LUNA_TYPE_VOID:
        return "void";
    case LUNA_TYPE_BOOL:
        return "bool";
    case LUNA_TYPE_I32:
        return "i32";
    case LUNA_TYPE_I64:
        return "i64";
    }

    return "<unknown>";
}

bool luna_type_kind_is_integer(LunaTypeKind kind) {
    return kind == LUNA_TYPE_I32 || kind == LUNA_TYPE_I64;
}

uint32_t luna_type_kind_bit_width(LunaTypeKind kind) {
    switch (kind) {
    case LUNA_TYPE_BOOL:
        return 1U;
    case LUNA_TYPE_I32:
        return 32U;
    case LUNA_TYPE_I64:
        return 64U;
    case LUNA_TYPE_INVALID:
    case LUNA_TYPE_VOID:
        return 0U;
    }

    return 0U;
}
