#ifndef LUNA_STRING_VIEW_H
#define LUNA_STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>

typedef struct LunaStringView {
    const char *data;
    size_t length;
} LunaStringView;

LunaStringView luna_string_view(const char *data, size_t length);
LunaStringView luna_string_view_from_c_string(const char *text);
bool luna_string_view_equal(LunaStringView left, LunaStringView right);
bool luna_string_view_equal_c_string(LunaStringView view, const char *text);

#endif
