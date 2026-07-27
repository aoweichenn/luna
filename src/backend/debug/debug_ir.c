#include "luna/backend/debug/debug_ir.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    LUNA_DEBUG_IR_HEADER_SIZE = 52,
    LUNA_DEBUG_IR_FILE_RECORD_SIZE = 8,
    LUNA_DEBUG_IR_LOCATION_RECORD_SIZE = 24,
    LUNA_DEBUG_IR_FUNCTION_RECORD_SIZE = 40
};

static const char luna_debug_ir_magic[8] = {
    'L', 'U', 'N', 'A', 'D', 'B', 'G', '\0',
};

static bool luna_debug_ir_error(FILE *stream, const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);

static bool luna_debug_ir_error(FILE *stream, const char *format, ...) {
    if (stream != NULL) {
        va_list arguments;
        va_start(arguments, format);
        (void)fputs("Debug IR verification error: ", stream);
        (void)vfprintf(stream, format, arguments);
        (void)fputc('\n', stream);
        va_end(arguments);
    }
    return false;
}

static bool luna_debug_ir_append_u8(LunaStringBuilder *output, uint8_t value) {
    const char byte = (char)value;
    return luna_string_builder_append(output, &byte, 1U);
}

static bool luna_debug_ir_append_u16(LunaStringBuilder *output,
                                     uint16_t value) {
    return luna_debug_ir_append_u8(output, (uint8_t)value) &&
           luna_debug_ir_append_u8(output, (uint8_t)(value >> 8U));
}

static bool luna_debug_ir_append_u32(LunaStringBuilder *output,
                                     uint32_t value) {
    return luna_debug_ir_append_u16(output, (uint16_t)value) &&
           luna_debug_ir_append_u16(output, (uint16_t)(value >> 16U));
}

static bool luna_debug_ir_append_u64(LunaStringBuilder *output,
                                     uint64_t value) {
    return luna_debug_ir_append_u32(output, (uint32_t)value) &&
           luna_debug_ir_append_u32(output, (uint32_t)(value >> 32U));
}

static bool luna_debug_ir_range_valid(uint64_t offset, uint64_t size,
                                      uint64_t total_size) {
    return offset <= total_size && size <= total_size - offset;
}

static bool luna_debug_ir_read_u8(LunaStringView encoded, uint64_t offset,
                                  uint8_t *value) {
    if (value == NULL ||
        !luna_debug_ir_range_valid(offset, 1U, (uint64_t)encoded.length)) {
        return false;
    }
    *value = (uint8_t)(unsigned char)encoded.data[(size_t)offset];
    return true;
}

static bool luna_debug_ir_read_u16(LunaStringView encoded, uint64_t offset,
                                   uint16_t *value) {
    uint8_t first = 0U;
    uint8_t second = 0U;
    if (value == NULL || !luna_debug_ir_read_u8(encoded, offset, &first) ||
        !luna_debug_ir_read_u8(encoded, offset + 1U, &second)) {
        return false;
    }
    *value = (uint16_t)((uint16_t)first | ((uint16_t)second << 8U));
    return true;
}

static bool luna_debug_ir_read_u32(LunaStringView encoded, uint64_t offset,
                                   uint32_t *value) {
    uint16_t first = 0U;
    uint16_t second = 0U;
    if (value == NULL || !luna_debug_ir_read_u16(encoded, offset, &first) ||
        !luna_debug_ir_read_u16(encoded, offset + 2U, &second)) {
        return false;
    }
    *value = (uint32_t)first | ((uint32_t)second << 16U);
    return true;
}

static bool luna_debug_ir_read_u64(LunaStringView encoded, uint64_t offset,
                                   uint64_t *value) {
    uint32_t first = 0U;
    uint32_t second = 0U;
    if (value == NULL || !luna_debug_ir_read_u32(encoded, offset, &first) ||
        !luna_debug_ir_read_u32(encoded, offset + 4U, &second)) {
        return false;
    }
    *value = (uint64_t)first | ((uint64_t)second << 32U);
    return true;
}

static bool luna_debug_ir_string(const LunaDebugIr *debug_ir, uint32_t offset,
                                 uint32_t length, LunaStringView *value) {
    if (debug_ir == NULL || value == NULL ||
        (uint64_t)offset > (uint64_t)debug_ir->strings.length ||
        (uint64_t)length >
            (uint64_t)debug_ir->strings.length - (uint64_t)offset) {
        return false;
    }
    *value = (LunaStringView){
        .data = luna_string_builder_data(&debug_ir->strings) + offset,
        .length = length,
    };
    return true;
}

static bool luna_debug_ir_string_valid(const LunaDebugIr *debug_ir,
                                       uint32_t offset, uint32_t length) {
    LunaStringView value = {0};
    return length > 0U && length <= LUNA_DEBUG_IR_MAX_NAME_BYTES &&
           luna_debug_ir_string(debug_ir, offset, length, &value) &&
           memchr(value.data, '\0', value.length) == NULL;
}

static bool luna_debug_ir_append_string(LunaDebugIr *debug_ir,
                                        LunaStringView value,
                                        uint32_t *offset) {
    if (debug_ir == NULL || offset == NULL || value.data == NULL ||
        value.length == 0U ||
        value.length > (size_t)LUNA_DEBUG_IR_MAX_NAME_BYTES ||
        memchr(value.data, '\0', value.length) != NULL ||
        debug_ir->strings.length > (size_t)UINT32_MAX ||
        value.length > (size_t)UINT32_MAX - debug_ir->strings.length ||
        debug_ir->strings.length + value.length >
            (size_t)LUNA_DEBUG_IR_MAX_STRING_BYTES) {
        return false;
    }
    *offset = (uint32_t)debug_ir->strings.length;
    return luna_string_builder_append_view(&debug_ir->strings, value);
}

void luna_debug_ir_init(LunaDebugIr *debug_ir) {
    if (debug_ir == NULL) {
        return;
    }
    luna_string_builder_init(&debug_ir->strings);
    luna_vector_init(&debug_ir->files, sizeof(LunaDebugIrFile));
    luna_vector_init(&debug_ir->locations, sizeof(LunaDebugIrLocation));
    luna_vector_init(&debug_ir->functions, sizeof(LunaDebugIrFunction));
}

void luna_debug_ir_destroy(LunaDebugIr *debug_ir) {
    if (debug_ir == NULL) {
        return;
    }
    luna_vector_destroy(&debug_ir->functions);
    luna_vector_destroy(&debug_ir->locations);
    luna_vector_destroy(&debug_ir->files);
    luna_string_builder_destroy(&debug_ir->strings);
}

bool luna_debug_ir_file_path(const LunaDebugIr *debug_ir, uint32_t file_id,
                             LunaStringView *path) {
    if (debug_ir == NULL || file_id == 0U ||
        (size_t)file_id > debug_ir->files.length) {
        return false;
    }
    const LunaDebugIrFile *file =
        luna_vector_at_const(&debug_ir->files, (size_t)file_id - 1U);
    return file != NULL && luna_debug_ir_string(debug_ir, file->path_offset,
                                                file->path_length, path);
}

bool luna_debug_ir_function_name(const LunaDebugIr *debug_ir,
                                 const LunaDebugIrFunction *function,
                                 LunaStringView *name) {
    return function != NULL &&
           luna_debug_ir_string(debug_ir, function->name_offset,
                                function->name_length, name);
}

bool luna_debug_ir_function_linkage_name(const LunaDebugIr *debug_ir,
                                         const LunaDebugIrFunction *function,
                                         LunaStringView *name) {
    return function != NULL &&
           luna_debug_ir_string(debug_ir, function->linkage_name_offset,
                                function->linkage_name_length, name);
}

bool luna_debug_ir_add_file(LunaDebugIr *debug_ir, LunaStringView path,
                            uint32_t *file_id) {
    if (debug_ir == NULL || file_id == NULL || path.data == NULL ||
        path.length == 0U ||
        debug_ir->files.length >= LUNA_DEBUG_IR_MAX_RECORD_COUNT) {
        return false;
    }
    for (size_t index = 0U; index < debug_ir->files.length; index += 1U) {
        LunaStringView existing = {0};
        if (!luna_debug_ir_file_path(debug_ir, (uint32_t)index + 1U,
                                     &existing)) {
            return false;
        }
        if (luna_string_view_equal(existing, path)) {
            *file_id = (uint32_t)index + 1U;
            return true;
        }
    }

    LunaDebugIrFile file = {0};
    if (!luna_debug_ir_append_string(debug_ir, path, &file.path_offset)) {
        return false;
    }
    file.path_length = (uint32_t)path.length;
    if (!luna_vector_push(&debug_ir->files, &file)) {
        debug_ir->strings.length = file.path_offset;
        if (debug_ir->strings.data != NULL) {
            debug_ir->strings.data[debug_ir->strings.length] = '\0';
        }
        return false;
    }
    *file_id = (uint32_t)debug_ir->files.length;
    return true;
}

bool luna_debug_ir_add_location(LunaDebugIr *debug_ir, uint64_t code_offset,
                                uint32_t file_id, uint32_t line,
                                uint32_t column, bool is_statement) {
    if (debug_ir == NULL || file_id == 0U ||
        (size_t)file_id > debug_ir->files.length || line == 0U ||
        column == 0U) {
        return false;
    }
    LunaDebugIrLocation location = {
        .code_offset = code_offset,
        .file_id = file_id,
        .line = line,
        .column = column,
        .is_statement = is_statement,
    };
    if (debug_ir->locations.length > 0U) {
        LunaDebugIrLocation *previous = luna_vector_at(
            &debug_ir->locations, debug_ir->locations.length - 1U);
        if (previous == NULL || code_offset < previous->code_offset) {
            return false;
        }
        if (code_offset == previous->code_offset) {
            *previous = location;
            return true;
        }
    }
    return debug_ir->locations.length < LUNA_DEBUG_IR_MAX_RECORD_COUNT &&
           luna_vector_push(&debug_ir->locations, &location);
}

bool luna_debug_ir_add_function(LunaDebugIr *debug_ir, uint64_t code_begin,
                                uint64_t code_end, LunaStringView name,
                                LunaStringView linkage_name, bool is_external) {
    if (debug_ir == NULL || code_begin >= code_end ||
        debug_ir->functions.length >= LUNA_DEBUG_IR_MAX_RECORD_COUNT) {
        return false;
    }
    if (debug_ir->functions.length > 0U) {
        const LunaDebugIrFunction *previous = luna_vector_at_const(
            &debug_ir->functions, debug_ir->functions.length - 1U);
        if (previous == NULL || code_begin < previous->code_end) {
            return false;
        }
    }

    const size_t original_length = debug_ir->strings.length;
    LunaDebugIrFunction function = {
        .code_begin = code_begin,
        .code_end = code_end,
        .name_length = (uint32_t)name.length,
        .linkage_name_length = (uint32_t)linkage_name.length,
        .is_external = is_external,
    };
    if (!luna_debug_ir_append_string(debug_ir, name, &function.name_offset) ||
        !luna_debug_ir_append_string(debug_ir, linkage_name,
                                     &function.linkage_name_offset) ||
        !luna_vector_push(&debug_ir->functions, &function)) {
        debug_ir->strings.length = original_length;
        if (debug_ir->strings.data != NULL) {
            debug_ir->strings.data[debug_ir->strings.length] = '\0';
        }
        return false;
    }
    return true;
}

bool luna_debug_ir_verify(const LunaDebugIr *debug_ir, uint64_t code_size,
                          FILE *diagnostic_stream) {
    if (debug_ir == NULL ||
        debug_ir->files.element_size != sizeof(LunaDebugIrFile) ||
        debug_ir->locations.element_size != sizeof(LunaDebugIrLocation) ||
        debug_ir->functions.element_size != sizeof(LunaDebugIrFunction) ||
        debug_ir->files.length > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        debug_ir->locations.length > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        debug_ir->functions.length > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        debug_ir->strings.length > LUNA_DEBUG_IR_MAX_STRING_BYTES) {
        return luna_debug_ir_error(diagnostic_stream,
                                   "invalid container state or size limit");
    }

    for (size_t index = 0U; index < debug_ir->files.length; index += 1U) {
        const LunaDebugIrFile *file =
            luna_vector_at_const(&debug_ir->files, index);
        if (file == NULL ||
            !luna_debug_ir_string_valid(debug_ir, file->path_offset,
                                        file->path_length)) {
            return luna_debug_ir_error(diagnostic_stream,
                                       "invalid file record %zu", index);
        }
        LunaStringView path = {0};
        if (!luna_debug_ir_file_path(debug_ir, (uint32_t)index + 1U, &path)) {
            return luna_debug_ir_error(diagnostic_stream,
                                       "invalid file path %zu", index);
        }
        for (size_t prior = 0U; prior < index; prior += 1U) {
            LunaStringView prior_path = {0};
            if (!luna_debug_ir_file_path(debug_ir, (uint32_t)prior + 1U,
                                         &prior_path) ||
                luna_string_view_equal(path, prior_path)) {
                return luna_debug_ir_error(
                    diagnostic_stream, "duplicate or invalid file record %zu",
                    index);
            }
        }
    }

    uint64_t previous_offset = 0U;
    for (size_t index = 0U; index < debug_ir->locations.length; index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        if (location == NULL || location->code_offset >= code_size ||
            location->file_id == 0U ||
            (size_t)location->file_id > debug_ir->files.length ||
            location->line == 0U || location->column == 0U ||
            (index > 0U && location->code_offset <= previous_offset)) {
            return luna_debug_ir_error(diagnostic_stream,
                                       "invalid location record %zu", index);
        }
        previous_offset = location->code_offset;
    }

    uint64_t previous_end = 0U;
    for (size_t index = 0U; index < debug_ir->functions.length; index += 1U) {
        const LunaDebugIrFunction *function =
            luna_vector_at_const(&debug_ir->functions, index);
        if (function == NULL || function->code_begin >= function->code_end ||
            function->code_end > code_size ||
            (index > 0U && function->code_begin < previous_end) ||
            !luna_debug_ir_string_valid(debug_ir, function->name_offset,
                                        function->name_length) ||
            !luna_debug_ir_string_valid(debug_ir, function->linkage_name_offset,
                                        function->linkage_name_length)) {
            return luna_debug_ir_error(diagnostic_stream,
                                       "invalid function record %zu", index);
        }
        bool has_location = false;
        for (size_t location_index = 0U;
             location_index < debug_ir->locations.length;
             location_index += 1U) {
            const LunaDebugIrLocation *location =
                luna_vector_at_const(&debug_ir->locations, location_index);
            if (location != NULL &&
                location->code_offset >= function->code_begin &&
                location->code_offset < function->code_end) {
                has_location = true;
                break;
            }
        }
        if (!has_location) {
            return luna_debug_ir_error(diagnostic_stream,
                                       "function record %zu has no location",
                                       index);
        }
        previous_end = function->code_end;
    }

    size_t function_index = 0U;
    for (size_t index = 0U; index < debug_ir->locations.length; index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        const LunaDebugIrFunction *function =
            luna_vector_at_const(&debug_ir->functions, function_index);
        while (function != NULL &&
               location->code_offset >= function->code_end) {
            function_index += 1U;
            function =
                luna_vector_at_const(&debug_ir->functions, function_index);
        }
        if (function == NULL || location->code_offset < function->code_begin) {
            return luna_debug_ir_error(
                diagnostic_stream,
                "location record %zu is outside every function", index);
        }
    }

    if ((debug_ir->locations.length > 0U && debug_ir->files.length == 0U) ||
        (debug_ir->functions.length > 0U && debug_ir->locations.length == 0U)) {
        return luna_debug_ir_error(diagnostic_stream,
                                   "incomplete debug record graph");
    }
    return true;
}

bool luna_debug_ir_encode(const LunaDebugIr *debug_ir,
                          LunaStringBuilder *output) {
    if (debug_ir == NULL || output == NULL || output->length != 0U ||
        !luna_debug_ir_verify(debug_ir, UINT64_MAX, NULL) ||
        debug_ir->files.length > UINT32_MAX ||
        debug_ir->locations.length > UINT32_MAX ||
        debug_ir->functions.length > UINT32_MAX ||
        debug_ir->strings.length > UINT32_MAX) {
        return false;
    }
    const uint64_t file_offset = LUNA_DEBUG_IR_HEADER_SIZE;
    const uint64_t location_offset =
        file_offset +
        (uint64_t)debug_ir->files.length * LUNA_DEBUG_IR_FILE_RECORD_SIZE;
    const uint64_t function_offset =
        location_offset + (uint64_t)debug_ir->locations.length *
                              LUNA_DEBUG_IR_LOCATION_RECORD_SIZE;
    const uint64_t string_offset =
        function_offset + (uint64_t)debug_ir->functions.length *
                              LUNA_DEBUG_IR_FUNCTION_RECORD_SIZE;
    const uint64_t total_size =
        string_offset + (uint64_t)debug_ir->strings.length;
    if (total_size > UINT32_MAX) {
        return false;
    }

    bool success =
        luna_string_builder_append(output, luna_debug_ir_magic,
                                   sizeof(luna_debug_ir_magic)) &&
        luna_debug_ir_append_u16(output, LUNA_DEBUG_IR_VERSION) &&
        luna_debug_ir_append_u16(output, LUNA_DEBUG_IR_HEADER_SIZE) &&
        luna_debug_ir_append_u32(output, (uint32_t)debug_ir->files.length) &&
        luna_debug_ir_append_u32(output,
                                 (uint32_t)debug_ir->locations.length) &&
        luna_debug_ir_append_u32(output,
                                 (uint32_t)debug_ir->functions.length) &&
        luna_debug_ir_append_u32(output, (uint32_t)debug_ir->strings.length) &&
        luna_debug_ir_append_u32(output, (uint32_t)file_offset) &&
        luna_debug_ir_append_u32(output, (uint32_t)location_offset) &&
        luna_debug_ir_append_u32(output, (uint32_t)function_offset) &&
        luna_debug_ir_append_u32(output, (uint32_t)string_offset) &&
        luna_debug_ir_append_u32(output, (uint32_t)total_size) &&
        luna_debug_ir_append_u32(output, 0U);
    for (size_t index = 0U; success && index < debug_ir->files.length;
         index += 1U) {
        const LunaDebugIrFile *file =
            luna_vector_at_const(&debug_ir->files, index);
        success = file != NULL &&
                  luna_debug_ir_append_u32(output, file->path_offset) &&
                  luna_debug_ir_append_u32(output, file->path_length);
    }
    for (size_t index = 0U; success && index < debug_ir->locations.length;
         index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        success =
            location != NULL &&
            luna_debug_ir_append_u64(output, location->code_offset) &&
            luna_debug_ir_append_u32(output, location->file_id) &&
            luna_debug_ir_append_u32(output, location->line) &&
            luna_debug_ir_append_u32(output, location->column) &&
            luna_debug_ir_append_u8(output, location->is_statement ? 1U : 0U) &&
            luna_debug_ir_append_u8(output, 0U) &&
            luna_debug_ir_append_u16(output, 0U);
    }
    for (size_t index = 0U; success && index < debug_ir->functions.length;
         index += 1U) {
        const LunaDebugIrFunction *function =
            luna_vector_at_const(&debug_ir->functions, index);
        success =
            function != NULL &&
            luna_debug_ir_append_u64(output, function->code_begin) &&
            luna_debug_ir_append_u64(output, function->code_end) &&
            luna_debug_ir_append_u32(output, function->name_offset) &&
            luna_debug_ir_append_u32(output, function->name_length) &&
            luna_debug_ir_append_u32(output, function->linkage_name_offset) &&
            luna_debug_ir_append_u32(output, function->linkage_name_length) &&
            luna_debug_ir_append_u32(output, function->is_external ? 1U : 0U) &&
            luna_debug_ir_append_u32(output, 0U);
    }
    success = success &&
              luna_string_builder_append(
                  output, luna_string_builder_data(&debug_ir->strings),
                  debug_ir->strings.length) &&
              output->length == (size_t)total_size;
    if (!success) {
        output->length = 0U;
        if (output->data != NULL) {
            output->data[0] = '\0';
        }
    }
    return success;
}

bool luna_debug_ir_decode(LunaStringView encoded, LunaDebugIr *debug_ir,
                          FILE *diagnostic_stream) {
    uint16_t version = 0U;
    uint16_t header_size = 0U;
    uint32_t file_count = 0U;
    uint32_t location_count = 0U;
    uint32_t function_count = 0U;
    uint32_t string_size = 0U;
    uint32_t file_offset = 0U;
    uint32_t location_offset = 0U;
    uint32_t function_offset = 0U;
    uint32_t string_offset = 0U;
    uint32_t total_size = 0U;
    uint32_t reserved = 0U;
    if (debug_ir == NULL || encoded.data == NULL ||
        encoded.length < LUNA_DEBUG_IR_HEADER_SIZE ||
        memcmp(encoded.data, luna_debug_ir_magic,
               sizeof(luna_debug_ir_magic)) != 0 ||
        !luna_debug_ir_read_u16(encoded, 8U, &version) ||
        !luna_debug_ir_read_u16(encoded, 10U, &header_size) ||
        !luna_debug_ir_read_u32(encoded, 12U, &file_count) ||
        !luna_debug_ir_read_u32(encoded, 16U, &location_count) ||
        !luna_debug_ir_read_u32(encoded, 20U, &function_count) ||
        !luna_debug_ir_read_u32(encoded, 24U, &string_size) ||
        !luna_debug_ir_read_u32(encoded, 28U, &file_offset) ||
        !luna_debug_ir_read_u32(encoded, 32U, &location_offset) ||
        !luna_debug_ir_read_u32(encoded, 36U, &function_offset) ||
        !luna_debug_ir_read_u32(encoded, 40U, &string_offset) ||
        !luna_debug_ir_read_u32(encoded, 44U, &total_size) ||
        !luna_debug_ir_read_u32(encoded, 48U, &reserved)) {
        return luna_debug_ir_error(diagnostic_stream,
                                   "truncated or invalid encoded header");
    }
    if (version != LUNA_DEBUG_IR_VERSION ||
        header_size != LUNA_DEBUG_IR_HEADER_SIZE || reserved != 0U ||
        total_size != encoded.length ||
        file_count > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        location_count > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        function_count > LUNA_DEBUG_IR_MAX_RECORD_COUNT ||
        string_size > LUNA_DEBUG_IR_MAX_STRING_BYTES ||
        file_offset != LUNA_DEBUG_IR_HEADER_SIZE ||
        location_offset !=
            file_offset +
                file_count * (uint32_t)LUNA_DEBUG_IR_FILE_RECORD_SIZE ||
        function_offset !=
            location_offset +
                location_count * (uint32_t)LUNA_DEBUG_IR_LOCATION_RECORD_SIZE ||
        string_offset !=
            function_offset +
                function_count * (uint32_t)LUNA_DEBUG_IR_FUNCTION_RECORD_SIZE ||
        total_size != string_offset + string_size) {
        return luna_debug_ir_error(diagnostic_stream,
                                   "inconsistent encoded layout");
    }

    luna_debug_ir_init(debug_ir);
    bool success =
        luna_vector_reserve(&debug_ir->files, file_count) &&
        luna_vector_reserve(&debug_ir->locations, location_count) &&
        luna_vector_reserve(&debug_ir->functions, function_count) &&
        luna_string_builder_append(&debug_ir->strings,
                                   encoded.data + string_offset, string_size);
    for (uint32_t index = 0U; success && index < file_count; index += 1U) {
        const uint64_t offset =
            (uint64_t)file_offset +
            (uint64_t)index * LUNA_DEBUG_IR_FILE_RECORD_SIZE;
        LunaDebugIrFile file = {0};
        success =
            luna_debug_ir_read_u32(encoded, offset, &file.path_offset) &&
            luna_debug_ir_read_u32(encoded, offset + 4U, &file.path_length) &&
            luna_vector_push(&debug_ir->files, &file);
    }
    for (uint32_t index = 0U; success && index < location_count; index += 1U) {
        const uint64_t offset =
            (uint64_t)location_offset +
            (uint64_t)index * LUNA_DEBUG_IR_LOCATION_RECORD_SIZE;
        LunaDebugIrLocation location = {0};
        uint8_t is_statement = 0U;
        uint8_t reserved8 = 0U;
        uint16_t reserved16 = 0U;
        success =
            luna_debug_ir_read_u64(encoded, offset, &location.code_offset) &&
            luna_debug_ir_read_u32(encoded, offset + 8U, &location.file_id) &&
            luna_debug_ir_read_u32(encoded, offset + 12U, &location.line) &&
            luna_debug_ir_read_u32(encoded, offset + 16U, &location.column) &&
            luna_debug_ir_read_u8(encoded, offset + 20U, &is_statement) &&
            luna_debug_ir_read_u8(encoded, offset + 21U, &reserved8) &&
            luna_debug_ir_read_u16(encoded, offset + 22U, &reserved16) &&
            is_statement <= 1U && reserved8 == 0U && reserved16 == 0U;
        location.is_statement = is_statement != 0U;
        success = success && luna_vector_push(&debug_ir->locations, &location);
    }
    for (uint32_t index = 0U; success && index < function_count; index += 1U) {
        const uint64_t offset =
            (uint64_t)function_offset +
            (uint64_t)index * LUNA_DEBUG_IR_FUNCTION_RECORD_SIZE;
        LunaDebugIrFunction function = {0};
        uint32_t is_external = 0U;
        uint32_t function_reserved = 0U;
        success =
            luna_debug_ir_read_u64(encoded, offset, &function.code_begin) &&
            luna_debug_ir_read_u64(encoded, offset + 8U, &function.code_end) &&
            luna_debug_ir_read_u32(encoded, offset + 16U,
                                   &function.name_offset) &&
            luna_debug_ir_read_u32(encoded, offset + 20U,
                                   &function.name_length) &&
            luna_debug_ir_read_u32(encoded, offset + 24U,
                                   &function.linkage_name_offset) &&
            luna_debug_ir_read_u32(encoded, offset + 28U,
                                   &function.linkage_name_length) &&
            luna_debug_ir_read_u32(encoded, offset + 32U, &is_external) &&
            luna_debug_ir_read_u32(encoded, offset + 36U, &function_reserved) &&
            is_external <= 1U && function_reserved == 0U;
        function.is_external = is_external != 0U;
        success = success && luna_vector_push(&debug_ir->functions, &function);
    }
    success = success &&
              luna_debug_ir_verify(debug_ir, UINT64_MAX, diagnostic_stream);
    if (!success) {
        luna_debug_ir_destroy(debug_ir);
        return luna_debug_ir_error(diagnostic_stream,
                                   "invalid encoded record graph");
    }
    return true;
}
