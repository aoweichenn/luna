#include "luna/ast.h"

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
    }

    return "<unknown>";
}
