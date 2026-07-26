#ifndef LUNA_SOURCE_H
#define LUNA_SOURCE_H

#include "luna/string_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct LunaSourceFile {
    char *path;
    char *text;
    size_t length;
} LunaSourceFile;

typedef struct LunaSourceSpan {
    const LunaSourceFile *source;
    size_t offset;
    size_t length;
    uint32_t line;
    uint32_t column;
} LunaSourceSpan;

bool luna_source_load(const char *path, LunaSourceFile *source);
bool luna_source_from_memory(const char *path, const char *text,
                             LunaSourceFile *source);
bool luna_source_from_bytes(const char *path, const char *text, size_t length,
                            LunaSourceFile *source);
void luna_source_destroy(LunaSourceFile *source);
LunaStringView luna_source_span_text(LunaSourceSpan span);

#endif
