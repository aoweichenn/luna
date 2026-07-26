#include "luna/buffer.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool luna_multiply_size(size_t left, size_t right, size_t *result) {
    if (left != 0U && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

void luna_vector_init(LunaVector *vector, size_t element_size) {
    vector->data = NULL;
    vector->length = 0U;
    vector->capacity = 0U;
    vector->element_size = element_size;
}

void luna_vector_destroy(LunaVector *vector) {
    free(vector->data);
    vector->data = NULL;
    vector->length = 0U;
    vector->capacity = 0U;
}

bool luna_vector_reserve(LunaVector *vector, size_t capacity) {
    if (capacity <= vector->capacity) {
        return true;
    }

    size_t byte_count = 0U;
    if (vector->element_size == 0U ||
        !luna_multiply_size(capacity, vector->element_size, &byte_count)) {
        return false;
    }

    void *new_data = realloc(vector->data, byte_count);
    if (new_data == NULL) {
        return false;
    }

    vector->data = new_data;
    vector->capacity = capacity;
    return true;
}

bool luna_vector_push(LunaVector *vector, const void *element) {
    if (vector->length == vector->capacity) {
        size_t new_capacity =
            vector->capacity == 0U ? 8U : vector->capacity * 2U;

        if (new_capacity < vector->capacity ||
            !luna_vector_reserve(vector, new_capacity)) {
            return false;
        }
    }

    unsigned char *destination =
        vector->data + (vector->length * vector->element_size);
    memcpy(destination, element, vector->element_size);
    vector->length += 1U;
    return true;
}

void *luna_vector_at(LunaVector *vector, size_t index) {
    if (index >= vector->length) {
        return NULL;
    }

    return vector->data + (index * vector->element_size);
}

const void *luna_vector_at_const(const LunaVector *vector, size_t index) {
    if (index >= vector->length) {
        return NULL;
    }

    return vector->data + (index * vector->element_size);
}

static bool luna_string_builder_reserve(LunaStringBuilder *builder,
                                        size_t required) {
    if (required <= builder->capacity) {
        return true;
    }

    size_t capacity = builder->capacity == 0U ? 128U : builder->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    char *new_data = realloc(builder->data, capacity);
    if (new_data == NULL) {
        return false;
    }

    builder->data = new_data;
    builder->capacity = capacity;
    return true;
}

void luna_string_builder_init(LunaStringBuilder *builder) {
    builder->data = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
}

void luna_string_builder_destroy(LunaStringBuilder *builder) {
    free(builder->data);
    builder->data = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
}

bool luna_string_builder_append(LunaStringBuilder *builder, const char *data,
                                size_t length) {
    if (length > SIZE_MAX - builder->length - 1U) {
        return false;
    }

    const size_t required = builder->length + length + 1U;
    if (!luna_string_builder_reserve(builder, required)) {
        return false;
    }

    if (length > 0U) {
        memcpy(builder->data + builder->length, data, length);
    }
    builder->length += length;
    builder->data[builder->length] = '\0';
    return true;
}

bool luna_string_builder_append_c_string(LunaStringBuilder *builder,
                                         const char *text) {
    return luna_string_builder_append(builder, text, strlen(text));
}

bool luna_string_builder_append_view(LunaStringBuilder *builder,
                                     LunaStringView view) {
    return luna_string_builder_append(builder, view.data, view.length);
}

bool luna_string_builder_append_format(LunaStringBuilder *builder,
                                       const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    va_list arguments_copy;
    va_copy(arguments_copy, arguments);
    const int required_count = vsnprintf(NULL, 0U, format, arguments_copy);
    va_end(arguments_copy);

    if (required_count < 0) {
        va_end(arguments);
        return false;
    }

    const size_t required_length = (size_t)required_count;
    if (required_length > SIZE_MAX - builder->length - 1U) {
        va_end(arguments);
        return false;
    }

    const size_t required_capacity = builder->length + required_length + 1U;
    if (!luna_string_builder_reserve(builder, required_capacity)) {
        va_end(arguments);
        return false;
    }

    const int written = vsnprintf(builder->data + builder->length,
                                  required_length + 1U, format, arguments);
    va_end(arguments);

    if (written != required_count) {
        return false;
    }

    builder->length += required_length;
    return true;
}

const char *luna_string_builder_data(const LunaStringBuilder *builder) {
    return builder->data == NULL ? "" : builder->data;
}
