#include "luna/frontend/source/source.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *luna_copy_c_string(const char *text) {
    if (text == NULL) {
        return NULL;
    }

    const size_t length = strlen(text);
    if (length == SIZE_MAX) {
        return NULL;
    }

    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1U);
    return copy;
}

bool luna_source_load(const char *path, LunaSourceFile *source) {
    if (source == NULL) {
        return false;
    }
    *source = (LunaSourceFile){0};

    if (path == NULL) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    size_t length = 0U;
    size_t capacity = 4096U;
    char *text = malloc(capacity + 1U);
    if (text == NULL) {
        (void)fclose(file);
        return false;
    }

    bool success = true;
    while (!feof(file)) {
        if (length == capacity) {
            if (capacity > (SIZE_MAX - 1U) / 2U) {
                success = false;
                break;
            }

            capacity *= 2U;
            char *new_text = realloc(text, capacity + 1U);
            if (new_text == NULL) {
                success = false;
                break;
            }
            text = new_text;
        }

        const size_t read_count =
            fread(text + length, 1U, capacity - length, file);
        length += read_count;

        if (ferror(file) != 0) {
            success = false;
            break;
        }
    }

    if (fclose(file) != 0) {
        success = false;
    }

    char *path_copy = NULL;
    if (success) {
        path_copy = luna_copy_c_string(path);
        success = path_copy != NULL;
    }

    if (!success) {
        free(text);
        free(path_copy);
        return false;
    }

    text[length] = '\0';
    source->path = path_copy;
    source->text = text;
    source->length = length;
    return true;
}

bool luna_source_from_memory(const char *path, const char *text,
                             LunaSourceFile *source) {
    if (text == NULL) {
        return false;
    }
    return luna_source_from_bytes(path, text, strlen(text), source);
}

bool luna_source_from_bytes(const char *path, const char *text, size_t length,
                            LunaSourceFile *source) {
    if (source == NULL) {
        return false;
    }
    *source = (LunaSourceFile){0};

    if (path == NULL || text == NULL || length == SIZE_MAX) {
        return false;
    }

    source->path = luna_copy_c_string(path);
    source->text = malloc(length + 1U);
    if (source->path == NULL || source->text == NULL) {
        luna_source_destroy(source);
        return false;
    }

    if (length > 0U) {
        memcpy(source->text, text, length);
    }
    source->text[length] = '\0';
    source->length = length;
    return true;
}

void luna_source_destroy(LunaSourceFile *source) {
    if (source == NULL) {
        return;
    }

    free(source->path);
    free(source->text);
    *source = (LunaSourceFile){0};
}

LunaStringView luna_source_span_text(LunaSourceSpan span) {
    if (span.source == NULL || span.offset > span.source->length ||
        span.length > span.source->length - span.offset) {
        return luna_string_view(NULL, 0U);
    }

    return luna_string_view(span.source->text + span.offset, span.length);
}
