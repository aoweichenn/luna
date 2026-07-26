#include "luna/frontend/support/string_view.h"

#include <string.h>

LunaStringView luna_string_view(const char *data, size_t length) {
    return (LunaStringView){
        .data = data,
        .length = length,
    };
}

LunaStringView luna_string_view_from_c_string(const char *text) {
    return luna_string_view(text, strlen(text));
}

bool luna_string_view_equal(LunaStringView left, LunaStringView right) {
    if (left.length != right.length) {
        return false;
    }

    if (left.length == 0U) {
        return true;
    }

    return memcmp(left.data, right.data, left.length) == 0;
}

bool luna_string_view_equal_c_string(LunaStringView view, const char *text) {
    return luna_string_view_equal(view, luna_string_view_from_c_string(text));
}
