#ifndef LUNA_BUFFER_H
#define LUNA_BUFFER_H

#include "luna/attributes.h"
#include "luna/string_view.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct LunaVector {
    unsigned char *data;
    size_t length;
    size_t capacity;
    size_t element_size;
} LunaVector;

void luna_vector_init(LunaVector *vector, size_t element_size);
void luna_vector_destroy(LunaVector *vector);
bool luna_vector_reserve(LunaVector *vector, size_t capacity);
bool luna_vector_push(LunaVector *vector, const void *element);
void *luna_vector_at(LunaVector *vector, size_t index);
const void *luna_vector_at_const(const LunaVector *vector, size_t index);

typedef struct LunaStringBuilder {
    char *data;
    size_t length;
    size_t capacity;
} LunaStringBuilder;

void luna_string_builder_init(LunaStringBuilder *builder);
void luna_string_builder_destroy(LunaStringBuilder *builder);
bool luna_string_builder_append(LunaStringBuilder *builder, const char *data,
                                size_t length);
bool luna_string_builder_append_c_string(LunaStringBuilder *builder,
                                         const char *text);
bool luna_string_builder_append_view(LunaStringBuilder *builder,
                                     LunaStringView view);
bool luna_string_builder_append_format(LunaStringBuilder *builder,
                                       const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);
const char *luna_string_builder_data(const LunaStringBuilder *builder);

#endif
