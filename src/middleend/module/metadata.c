#include "luna/middleend/module/metadata.h"

#include "luna/frontend/support/string_view.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t luna_metadata_magic[8] = {'L', 'U', 'N', 'A',
                                               'L', 'M', 'I', '\0'};
static const uint16_t luna_metadata_format_major = 1U;
static const uint16_t luna_metadata_format_minor = 0U;
static const uint32_t luna_metadata_language_abi = 1U;
static const size_t luna_metadata_header_size = 32U;
static const size_t luna_metadata_max_file_size = (size_t)16U * 1024U * 1024U;
static const uint32_t luna_metadata_max_record_count = UINT32_C(1048576);
static const uint32_t luna_metadata_max_string_length = UINT32_C(1048576);
static const uint32_t luna_metadata_max_type_depth = 64U;
static const uint64_t luna_metadata_hash_offset =
    UINT64_C(14695981039346656037);
static const uint64_t luna_metadata_hash_prime = UINT64_C(1099511628211);

typedef enum LunaMetadataTypeTag {
    LUNA_METADATA_TYPE_INVALID = 0U,
    LUNA_METADATA_TYPE_VOID = 1U,
    LUNA_METADATA_TYPE_BOOL = 2U,
    LUNA_METADATA_TYPE_I8 = 3U,
    LUNA_METADATA_TYPE_I16 = 4U,
    LUNA_METADATA_TYPE_I32 = 5U,
    LUNA_METADATA_TYPE_I64 = 6U,
    LUNA_METADATA_TYPE_ISIZE = 7U,
    LUNA_METADATA_TYPE_U8 = 8U,
    LUNA_METADATA_TYPE_U16 = 9U,
    LUNA_METADATA_TYPE_U32 = 10U,
    LUNA_METADATA_TYPE_U64 = 11U,
    LUNA_METADATA_TYPE_USIZE = 12U,
    LUNA_METADATA_TYPE_F32 = 13U,
    LUNA_METADATA_TYPE_F64 = 14U,
    LUNA_METADATA_TYPE_NAMED = 15U,
    LUNA_METADATA_TYPE_POINTER = 16U,
    LUNA_METADATA_TYPE_ARRAY = 17U
} LunaMetadataTypeTag;

typedef enum LunaMetadataDeclarationTag {
    LUNA_METADATA_DECLARATION_STRUCT = 1U,
    LUNA_METADATA_DECLARATION_UNION = 2U,
    LUNA_METADATA_DECLARATION_ENUM = 3U
} LunaMetadataDeclarationTag;

typedef struct LunaMetadataWriter {
    LunaStringBuilder *output;
    const char *failure_reason;
} LunaMetadataWriter;

typedef struct LunaMetadataReader {
    const uint8_t *data;
    size_t length;
    size_t offset;
    LunaArena *arena;
    LunaSourceSpan span;
    const char *failure_reason;
} LunaMetadataReader;

static uint64_t luna_metadata_hash(const uint8_t *data, size_t length) {
    uint64_t hash = luna_metadata_hash_offset;
    for (size_t index = 0U; index < sizeof(luna_metadata_magic); index += 1U) {
        hash ^= (uint64_t)luna_metadata_magic[index];
        hash *= luna_metadata_hash_prime;
    }
    for (uint32_t index = 0U; index < 4U; index += 1U) {
        const uint8_t byte =
            (uint8_t)((luna_metadata_language_abi >> (index * 8U)) &
                      UINT32_C(0xff));
        hash ^= (uint64_t)byte;
        hash *= luna_metadata_hash_prime;
    }
    for (size_t index = 0U; index < length; index += 1U) {
        hash ^= (uint64_t)data[index];
        hash *= luna_metadata_hash_prime;
    }
    return hash;
}

static bool luna_metadata_writer_fail(LunaMetadataWriter *writer,
                                      const char *reason) {
    if (writer->failure_reason == NULL) {
        writer->failure_reason = reason;
    }
    return false;
}

static bool luna_metadata_write_bytes(LunaMetadataWriter *writer,
                                      const void *data, size_t length) {
    if (writer->failure_reason != NULL) {
        return false;
    }
    if (length > 0U && data == NULL) {
        return luna_metadata_writer_fail(writer,
                                         "metadata contains invalid data");
    }
    if (!luna_string_builder_append(writer->output, data, length)) {
        return luna_metadata_writer_fail(
            writer, "out of memory while encoding module metadata");
    }
    return true;
}

static bool luna_metadata_write_u8(LunaMetadataWriter *writer, uint8_t value) {
    return luna_metadata_write_bytes(writer, &value, sizeof(value));
}

static bool luna_metadata_write_u16(LunaMetadataWriter *writer,
                                    uint16_t value) {
    const uint8_t bytes[2] = {
        (uint8_t)(value & UINT16_C(0xff)),
        (uint8_t)((value >> 8U) & UINT16_C(0xff)),
    };
    return luna_metadata_write_bytes(writer, bytes, sizeof(bytes));
}

static bool luna_metadata_write_u32(LunaMetadataWriter *writer,
                                    uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value & UINT32_C(0xff)),
        (uint8_t)((value >> 8U) & UINT32_C(0xff)),
        (uint8_t)((value >> 16U) & UINT32_C(0xff)),
        (uint8_t)((value >> 24U) & UINT32_C(0xff)),
    };
    return luna_metadata_write_bytes(writer, bytes, sizeof(bytes));
}

static bool luna_metadata_write_u64(LunaMetadataWriter *writer,
                                    uint64_t value) {
    uint8_t bytes[8] = {0};
    for (uint32_t index = 0U; index < 8U; index += 1U) {
        bytes[index] = (uint8_t)((value >> (index * 8U)) & UINT64_C(0xff));
    }
    return luna_metadata_write_bytes(writer, bytes, sizeof(bytes));
}

static bool luna_metadata_write_string(LunaMetadataWriter *writer,
                                       LunaStringView value) {
    if (value.length == 0U || value.length > luna_metadata_max_string_length ||
        value.length > UINT32_MAX || value.data == NULL) {
        return luna_metadata_writer_fail(
            writer, "metadata contains an invalid or oversized name");
    }
    return luna_metadata_write_u32(writer, (uint32_t)value.length) &&
           luna_metadata_write_bytes(writer, value.data, value.length);
}

static bool luna_metadata_count_imports(const LunaImport *first,
                                        uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaImport *item = first; item != NULL; item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static bool
luna_metadata_count_type_declarations(const LunaTypeDeclaration *first,
                                      uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaTypeDeclaration *item = first; item != NULL;
         item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static bool luna_metadata_count_functions(const LunaFunction *first,
                                          uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaFunction *item = first; item != NULL; item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static bool luna_metadata_count_fields(const LunaField *first,
                                       uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaField *item = first; item != NULL; item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static bool luna_metadata_count_enum_members(const LunaEnumMember *first,
                                             uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaEnumMember *item = first; item != NULL; item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static bool luna_metadata_count_parameters(const LunaParameter *first,
                                           uint32_t *count) {
    uint32_t result = 0U;
    for (const LunaParameter *item = first; item != NULL; item = item->next) {
        if (result == luna_metadata_max_record_count) {
            return false;
        }
        result += 1U;
    }
    *count = result;
    return true;
}

static LunaMetadataTypeTag luna_metadata_type_tag(LunaTypeKind kind) {
    switch (kind) {
    case LUNA_TYPE_VOID:
        return LUNA_METADATA_TYPE_VOID;
    case LUNA_TYPE_BOOL:
        return LUNA_METADATA_TYPE_BOOL;
    case LUNA_TYPE_I8:
        return LUNA_METADATA_TYPE_I8;
    case LUNA_TYPE_I16:
        return LUNA_METADATA_TYPE_I16;
    case LUNA_TYPE_I32:
        return LUNA_METADATA_TYPE_I32;
    case LUNA_TYPE_I64:
        return LUNA_METADATA_TYPE_I64;
    case LUNA_TYPE_ISIZE:
        return LUNA_METADATA_TYPE_ISIZE;
    case LUNA_TYPE_U8:
        return LUNA_METADATA_TYPE_U8;
    case LUNA_TYPE_U16:
        return LUNA_METADATA_TYPE_U16;
    case LUNA_TYPE_U32:
        return LUNA_METADATA_TYPE_U32;
    case LUNA_TYPE_U64:
        return LUNA_METADATA_TYPE_U64;
    case LUNA_TYPE_USIZE:
        return LUNA_METADATA_TYPE_USIZE;
    case LUNA_TYPE_F32:
        return LUNA_METADATA_TYPE_F32;
    case LUNA_TYPE_F64:
        return LUNA_METADATA_TYPE_F64;
    case LUNA_TYPE_NAMED:
        return LUNA_METADATA_TYPE_NAMED;
    case LUNA_TYPE_POINTER:
        return LUNA_METADATA_TYPE_POINTER;
    case LUNA_TYPE_ARRAY:
        return LUNA_METADATA_TYPE_ARRAY;
    default:
        return LUNA_METADATA_TYPE_INVALID;
    }
}

static bool luna_metadata_write_type(LunaMetadataWriter *writer,
                                     const LunaTypeRef *type, uint32_t depth) {
    if (type == NULL || depth > luna_metadata_max_type_depth) {
        return luna_metadata_writer_fail(
            writer, "module metadata type nesting exceeds the format limit");
    }

    const LunaMetadataTypeTag tag = luna_metadata_type_tag(type->kind);
    if (tag == LUNA_METADATA_TYPE_INVALID ||
        !luna_metadata_write_u8(writer, (uint8_t)tag)) {
        return luna_metadata_writer_fail(
            writer, "module metadata contains an unsupported type");
    }

    switch (type->kind) {
    case LUNA_TYPE_NAMED:
        return luna_metadata_write_string(writer, type->as.name);
    case LUNA_TYPE_POINTER:
        if (type->as.pointer.pointee == NULL) {
            return luna_metadata_writer_fail(
                writer, "module metadata contains an invalid pointer type");
        }
        return luna_metadata_write_u8(
                   writer,
                   (uint8_t)(type->as.pointer.is_read_only ? 1U : 0U)) &&
               luna_metadata_write_type(writer, type->as.pointer.pointee,
                                        depth + 1U);
    case LUNA_TYPE_ARRAY:
        if (type->as.array.element == NULL || type->as.array.count == 0U) {
            return luna_metadata_writer_fail(
                writer, "module metadata contains an invalid array type");
        }
        return luna_metadata_write_u64(writer, type->as.array.count) &&
               luna_metadata_write_type(writer, type->as.array.element,
                                        depth + 1U);
    default:
        return true;
    }
}

static bool luna_metadata_enum_initializer(const LunaExpression *expression,
                                           bool *is_negative,
                                           uint64_t *magnitude) {
    if (expression == NULL || is_negative == NULL || magnitude == NULL) {
        return false;
    }

    const LunaExpression *literal = expression;
    *is_negative = false;
    if (expression->kind == LUNA_EXPRESSION_UNARY) {
        if (expression->as.unary.operator_kind != LUNA_TOKEN_PLUS &&
            expression->as.unary.operator_kind != LUNA_TOKEN_MINUS) {
            return false;
        }
        *is_negative = expression->as.unary.operator_kind == LUNA_TOKEN_MINUS;
        literal = expression->as.unary.operand;
    }
    if (literal == NULL || literal->kind != LUNA_EXPRESSION_INTEGER) {
        return false;
    }
    *magnitude = literal->as.integer;
    return true;
}

static bool
luna_metadata_write_type_declaration(LunaMetadataWriter *writer,
                                     const LunaTypeDeclaration *declaration) {
    uint8_t tag = 0U;
    switch (declaration->kind) {
    case LUNA_TYPE_STRUCT:
        tag = LUNA_METADATA_DECLARATION_STRUCT;
        break;
    case LUNA_TYPE_UNION:
        tag = LUNA_METADATA_DECLARATION_UNION;
        break;
    case LUNA_TYPE_ENUM:
        tag = LUNA_METADATA_DECLARATION_ENUM;
        break;
    default:
        return luna_metadata_writer_fail(
            writer, "module metadata contains an unsupported declaration");
    }

    if (!luna_metadata_write_u8(writer, tag) ||
        !luna_metadata_write_u8(
            writer, (uint8_t)(declaration->is_exported ? 1U : 0U)) ||
        !luna_metadata_write_string(writer, declaration->name)) {
        return false;
    }

    if (declaration->kind == LUNA_TYPE_ENUM) {
        uint32_t member_count = 0U;
        if (!luna_metadata_count_enum_members(
                declaration->as.enumeration.first_member, &member_count) ||
            member_count != declaration->as.enumeration.member_count ||
            !luna_metadata_write_type(
                writer, &declaration->as.enumeration.underlying_type, 0U) ||
            !luna_metadata_write_u32(writer, member_count)) {
            return luna_metadata_writer_fail(
                writer, "module metadata contains an invalid enum");
        }
        for (const LunaEnumMember *member =
                 declaration->as.enumeration.first_member;
             member != NULL; member = member->next) {
            if (!luna_metadata_write_string(writer, member->name) ||
                !luna_metadata_write_u8(
                    writer, (uint8_t)(member->initializer == NULL ? 0U : 1U))) {
                return false;
            }
            if (member->initializer != NULL) {
                bool is_negative = false;
                uint64_t magnitude = 0U;
                if (!luna_metadata_enum_initializer(member->initializer,
                                                    &is_negative, &magnitude) ||
                    !luna_metadata_write_u8(writer,
                                            (uint8_t)(is_negative ? 1U : 0U)) ||
                    !luna_metadata_write_u64(writer, magnitude)) {
                    return luna_metadata_writer_fail(
                        writer,
                        "module metadata contains an invalid enum value");
                }
            }
        }
        return true;
    }

    uint32_t field_count = 0U;
    if (!luna_metadata_count_fields(declaration->as.aggregate.first_field,
                                    &field_count) ||
        field_count != declaration->as.aggregate.field_count ||
        !luna_metadata_write_u32(writer, field_count)) {
        return luna_metadata_writer_fail(
            writer, "module metadata contains an invalid aggregate");
    }
    for (const LunaField *field = declaration->as.aggregate.first_field;
         field != NULL; field = field->next) {
        if (!luna_metadata_write_string(writer, field->name) ||
            !luna_metadata_write_type(writer, &field->type, 0U)) {
            return false;
        }
    }
    return true;
}

static bool luna_metadata_write_function(LunaMetadataWriter *writer,
                                         const LunaFunction *function) {
    if (!function->is_declaration || function->body != NULL) {
        return luna_metadata_writer_fail(
            writer, "module metadata can encode only function declarations");
    }
    const uint8_t flags = (uint8_t)((function->is_exported ? 1U : 0U) |
                                    (function->is_external ? 2U : 0U));
    uint32_t parameter_count = 0U;
    if (!luna_metadata_count_parameters(function->first_parameter,
                                        &parameter_count) ||
        parameter_count != function->parameter_count ||
        !luna_metadata_write_u8(writer, flags) ||
        !luna_metadata_write_string(writer, function->name) ||
        !luna_metadata_write_u32(writer, parameter_count)) {
        return luna_metadata_writer_fail(
            writer, "module metadata contains an invalid function");
    }

    for (const LunaParameter *parameter = function->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        if (!luna_metadata_write_string(writer, parameter->name) ||
            !luna_metadata_write_type(writer, &parameter->type, 0U)) {
            return false;
        }
    }
    return luna_metadata_write_type(writer, &function->return_type, 0U);
}

static bool
luna_metadata_write_payload(LunaMetadataWriter *writer,
                            const LunaProgram *interface_unit,
                            const LunaTargetInfo *target,
                            const LunaModuleMetadataDependency *dependencies,
                            uint32_t dependency_count) {
    const LunaStringView target_triple =
        luna_string_view_from_c_string(target->triple);
    if (!luna_metadata_write_string(writer, target_triple) ||
        !luna_metadata_write_string(writer, interface_unit->module_name)) {
        return false;
    }

    uint32_t import_count = 0U;
    if (!luna_metadata_count_imports(interface_unit->first_import,
                                     &import_count) ||
        import_count != dependency_count ||
        (dependency_count > 0U && dependencies == NULL) ||
        !luna_metadata_write_u32(writer, import_count)) {
        return luna_metadata_writer_fail(
            writer,
            "module metadata dependency fingerprints do not match imports");
    }
    uint32_t dependency_index = 0U;
    for (const LunaImport *import = interface_unit->first_import;
         import != NULL; import = import->next) {
        const LunaModuleMetadataDependency *dependency =
            &dependencies[dependency_index];
        if (!luna_string_view_equal(import->module_name,
                                    dependency->module_name) ||
            !luna_metadata_write_string(writer, import->module_name) ||
            !luna_metadata_write_u64(writer, dependency->content_hash)) {
            (void)luna_metadata_writer_fail(
                writer, "module metadata dependency fingerprint is invalid");
            return false;
        }
        dependency_index += 1U;
    }

    uint32_t type_count = 0U;
    if (!luna_metadata_count_type_declarations(
            interface_unit->first_type_declaration, &type_count) ||
        !luna_metadata_write_u32(writer, type_count)) {
        return luna_metadata_writer_fail(
            writer, "module metadata contains too many type declarations");
    }
    for (const LunaTypeDeclaration *declaration =
             interface_unit->first_type_declaration;
         declaration != NULL; declaration = declaration->next) {
        if (!luna_metadata_write_type_declaration(writer, declaration)) {
            return false;
        }
    }

    uint32_t function_count = 0U;
    if (!luna_metadata_count_functions(interface_unit->first_function,
                                       &function_count) ||
        !luna_metadata_write_u32(writer, function_count)) {
        return luna_metadata_writer_fail(
            writer, "module metadata contains too many functions");
    }
    for (const LunaFunction *function = interface_unit->first_function;
         function != NULL; function = function->next) {
        if (!luna_metadata_write_function(writer, function)) {
            return false;
        }
    }
    return true;
}

bool luna_module_metadata_encode(
    const LunaProgram *interface_unit, const LunaTargetInfo *target,
    const LunaModuleMetadataDependency *dependencies, uint32_t dependency_count,
    LunaDiagnosticEngine *diagnostics, LunaStringBuilder *output) {
    if (interface_unit == NULL || !interface_unit->is_interface) {
        luna_diagnostic_error_plain(
            diagnostics, "module metadata requires a validated interface unit");
        return false;
    }
    if (!luna_target_info_is_supported(target)) {
        luna_diagnostic_error_plain(
            diagnostics, "module metadata requires a supported target");
        return false;
    }
    if (output == NULL || output->length != 0U) {
        luna_diagnostic_error_plain(diagnostics,
                                    "module metadata output must be empty");
        return false;
    }

    LunaStringBuilder payload;
    luna_string_builder_init(&payload);
    LunaMetadataWriter payload_writer = {
        .output = &payload,
    };
    bool success =
        luna_metadata_write_payload(&payload_writer, interface_unit, target,
                                    dependencies, dependency_count);
    if (success && payload.length > luna_metadata_max_file_size -
                                        luna_metadata_header_size) {
        success = luna_metadata_writer_fail(
            &payload_writer, "module metadata exceeds the file-size limit");
    }

    if (success) {
        LunaMetadataWriter output_writer = {
            .output = output,
        };
        const uint64_t payload_hash = luna_metadata_hash(
            (const uint8_t *)luna_string_builder_data(&payload),
            payload.length);
        success =
            luna_metadata_write_bytes(&output_writer, luna_metadata_magic,
                                      sizeof(luna_metadata_magic)) &&
            luna_metadata_write_u16(&output_writer,
                                    luna_metadata_format_major) &&
            luna_metadata_write_u16(&output_writer,
                                    luna_metadata_format_minor) &&
            luna_metadata_write_u32(&output_writer,
                                    luna_metadata_language_abi) &&
            luna_metadata_write_u64(&output_writer, (uint64_t)payload.length) &&
            luna_metadata_write_u64(&output_writer, payload_hash) &&
            luna_metadata_write_bytes(&output_writer,
                                      luna_string_builder_data(&payload),
                                      payload.length);
        if (!success && payload_writer.failure_reason == NULL) {
            payload_writer.failure_reason = output_writer.failure_reason;
        }
    }

    if (!success) {
        luna_diagnostic_error_plain(diagnostics, "%s",
                                    payload_writer.failure_reason == NULL
                                        ? "failed to encode module metadata"
                                        : payload_writer.failure_reason);
    }
    luna_string_builder_destroy(&payload);
    return success;
}

static bool luna_metadata_reader_fail(LunaMetadataReader *reader,
                                      const char *reason) {
    if (reader->failure_reason == NULL) {
        reader->failure_reason = reason;
    }
    return false;
}

static bool luna_metadata_read_bytes(LunaMetadataReader *reader,
                                     const uint8_t **data, size_t length) {
    if (reader->failure_reason != NULL) {
        return false;
    }
    if (reader->offset > reader->length ||
        length > reader->length - reader->offset) {
        (void)luna_metadata_reader_fail(reader, "truncated metadata payload");
        return false;
    }
    *data = reader->data + reader->offset;
    reader->offset += length;
    return true;
}

static bool luna_metadata_read_u8(LunaMetadataReader *reader, uint8_t *value) {
    const uint8_t *bytes = NULL;
    if (!luna_metadata_read_bytes(reader, &bytes, 1U)) {
        return false;
    }
    *value = bytes[0];
    return true;
}

static bool luna_metadata_read_u32(LunaMetadataReader *reader,
                                   uint32_t *value) {
    const uint8_t *bytes = NULL;
    if (!luna_metadata_read_bytes(reader, &bytes, 4U)) {
        return false;
    }
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
             ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    return true;
}

static bool luna_metadata_read_u64(LunaMetadataReader *reader,
                                   uint64_t *value) {
    const uint8_t *bytes = NULL;
    if (!luna_metadata_read_bytes(reader, &bytes, 8U)) {
        return false;
    }
    uint64_t result = 0U;
    for (uint32_t index = 0U; index < 8U; index += 1U) {
        result |= (uint64_t)bytes[index] << (index * 8U);
    }
    *value = result;
    return true;
}

static bool luna_metadata_is_identifier(LunaStringView value) {
    if (value.length == 0U || value.data == NULL) {
        return false;
    }
    const unsigned char first = (unsigned char)value.data[0];
    if (!((first >= (unsigned char)'a' && first <= (unsigned char)'z') ||
          (first >= (unsigned char)'A' && first <= (unsigned char)'Z') ||
          first == (unsigned char)'_')) {
        return false;
    }
    for (size_t index = 1U; index < value.length; index += 1U) {
        const unsigned char character = (unsigned char)value.data[index];
        if (!((character >= (unsigned char)'a' &&
               character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' &&
               character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' &&
               character <= (unsigned char)'9') ||
              character == (unsigned char)'_')) {
            return false;
        }
    }
    return true;
}

static bool luna_metadata_is_module_name(LunaStringView value) {
    if (value.data == NULL || value.length == 0U) {
        return false;
    }
    size_t segment_start = 0U;
    for (size_t index = 0U; index <= value.length; index += 1U) {
        if (index != value.length && value.data[index] != '.') {
            continue;
        }
        const LunaStringView segment =
            luna_string_view(value.data + segment_start, index - segment_start);
        if (!luna_metadata_is_identifier(segment)) {
            return false;
        }
        segment_start = index + 1U;
    }
    return value.length > 0U;
}

static bool luna_metadata_read_raw_string(LunaMetadataReader *reader,
                                          LunaStringView *value) {
    uint32_t length = 0U;
    if (!luna_metadata_read_u32(reader, &length) || length == 0U ||
        length > luna_metadata_max_string_length) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an invalid string length");
    }
    const uint8_t *bytes = NULL;
    if (!luna_metadata_read_bytes(reader, &bytes, (size_t)length)) {
        return false;
    }
    *value = luna_string_view((const char *)bytes, (size_t)length);
    return true;
}

static bool luna_metadata_copy_string(LunaMetadataReader *reader,
                                      LunaStringView raw,
                                      LunaStringView *value) {
    char *copy = luna_arena_copy_string(reader->arena, raw);
    if (copy == NULL) {
        return luna_metadata_reader_fail(
            reader, "out of memory while decoding module metadata");
    }
    *value = luna_string_view(copy, raw.length);
    return true;
}

static bool luna_metadata_read_identifier(LunaMetadataReader *reader,
                                          LunaStringView *value) {
    LunaStringView raw = {0};
    if (!luna_metadata_read_raw_string(reader, &raw) ||
        !luna_metadata_is_identifier(raw)) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an invalid identifier");
    }
    return luna_metadata_copy_string(reader, raw, value);
}

static bool luna_metadata_read_module_name(LunaMetadataReader *reader,
                                           LunaStringView *value) {
    LunaStringView raw = {0};
    if (!luna_metadata_read_raw_string(reader, &raw) ||
        !luna_metadata_is_module_name(raw)) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an invalid module name");
    }
    return luna_metadata_copy_string(reader, raw, value);
}

static void *luna_metadata_allocate(LunaMetadataReader *reader, size_t size,
                                    size_t alignment) {
    void *value = luna_arena_allocate_zero(reader->arena, size, alignment);
    if (value == NULL) {
        (void)luna_metadata_reader_fail(
            reader, "out of memory while decoding module metadata");
    }
    return value;
}

static LunaTypeKind luna_metadata_type_kind(uint8_t tag) {
    switch ((LunaMetadataTypeTag)tag) {
    case LUNA_METADATA_TYPE_VOID:
        return LUNA_TYPE_VOID;
    case LUNA_METADATA_TYPE_BOOL:
        return LUNA_TYPE_BOOL;
    case LUNA_METADATA_TYPE_I8:
        return LUNA_TYPE_I8;
    case LUNA_METADATA_TYPE_I16:
        return LUNA_TYPE_I16;
    case LUNA_METADATA_TYPE_I32:
        return LUNA_TYPE_I32;
    case LUNA_METADATA_TYPE_I64:
        return LUNA_TYPE_I64;
    case LUNA_METADATA_TYPE_ISIZE:
        return LUNA_TYPE_ISIZE;
    case LUNA_METADATA_TYPE_U8:
        return LUNA_TYPE_U8;
    case LUNA_METADATA_TYPE_U16:
        return LUNA_TYPE_U16;
    case LUNA_METADATA_TYPE_U32:
        return LUNA_TYPE_U32;
    case LUNA_METADATA_TYPE_U64:
        return LUNA_TYPE_U64;
    case LUNA_METADATA_TYPE_USIZE:
        return LUNA_TYPE_USIZE;
    case LUNA_METADATA_TYPE_F32:
        return LUNA_TYPE_F32;
    case LUNA_METADATA_TYPE_F64:
        return LUNA_TYPE_F64;
    case LUNA_METADATA_TYPE_NAMED:
        return LUNA_TYPE_NAMED;
    case LUNA_METADATA_TYPE_POINTER:
        return LUNA_TYPE_POINTER;
    case LUNA_METADATA_TYPE_ARRAY:
        return LUNA_TYPE_ARRAY;
    default:
        return LUNA_TYPE_INVALID;
    }
}

static bool luna_metadata_read_type(LunaMetadataReader *reader,
                                    LunaTypeRef *type, uint32_t depth) {
    if (depth > luna_metadata_max_type_depth) {
        return luna_metadata_reader_fail(
            reader, "metadata type nesting exceeds the format limit");
    }
    uint8_t tag = 0U;
    if (!luna_metadata_read_u8(reader, &tag)) {
        return false;
    }
    const LunaTypeKind kind = luna_metadata_type_kind(tag);
    if (kind == LUNA_TYPE_INVALID) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an unknown type tag");
    }
    *type = (LunaTypeRef){
        .kind = kind,
        .span = reader->span,
    };

    if (kind == LUNA_TYPE_NAMED) {
        return luna_metadata_read_identifier(reader, &type->as.name);
    }
    if (kind == LUNA_TYPE_POINTER) {
        uint8_t is_read_only = 0U;
        if (!luna_metadata_read_u8(reader, &is_read_only) ||
            is_read_only > 1U) {
            return luna_metadata_reader_fail(
                reader, "metadata contains invalid pointer qualifiers");
        }
        type->as.pointer.is_read_only = is_read_only != 0U;
        type->as.pointer.pointee = luna_metadata_allocate(
            reader, sizeof(LunaTypeRef), _Alignof(LunaTypeRef));
        return type->as.pointer.pointee != NULL &&
               luna_metadata_read_type(reader, type->as.pointer.pointee,
                                       depth + 1U);
    }
    if (kind == LUNA_TYPE_ARRAY) {
        if (!luna_metadata_read_u64(reader, &type->as.array.count) ||
            type->as.array.count == 0U) {
            return luna_metadata_reader_fail(
                reader, "metadata contains an invalid array length");
        }
        type->as.array.element = luna_metadata_allocate(
            reader, sizeof(LunaTypeRef), _Alignof(LunaTypeRef));
        return type->as.array.element != NULL &&
               luna_metadata_read_type(reader, type->as.array.element,
                                       depth + 1U);
    }
    return true;
}

static bool luna_metadata_read_count(LunaMetadataReader *reader,
                                     uint32_t *count) {
    if (!luna_metadata_read_u32(reader, count) ||
        *count > luna_metadata_max_record_count) {
        return luna_metadata_reader_fail(
            reader, "metadata record count exceeds the format limit");
    }
    return true;
}

static bool luna_metadata_read_enum_initializer(LunaMetadataReader *reader,
                                                LunaExpression **initializer) {
    uint8_t has_initializer = 0U;
    if (!luna_metadata_read_u8(reader, &has_initializer) ||
        has_initializer > 1U) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an invalid enum initializer flag");
    }
    if (has_initializer == 0U) {
        *initializer = NULL;
        return true;
    }

    uint8_t is_negative = 0U;
    uint64_t magnitude = 0U;
    if (!luna_metadata_read_u8(reader, &is_negative) || is_negative > 1U ||
        !luna_metadata_read_u64(reader, &magnitude)) {
        return luna_metadata_reader_fail(
            reader, "metadata contains an invalid enum initializer");
    }

    LunaExpression *literal = luna_metadata_allocate(
        reader, sizeof(LunaExpression), _Alignof(LunaExpression));
    if (literal == NULL) {
        return false;
    }
    literal->kind = LUNA_EXPRESSION_INTEGER;
    literal->span = reader->span;
    literal->as.integer = magnitude;
    if (is_negative == 0U) {
        *initializer = literal;
        return true;
    }

    LunaExpression *unary = luna_metadata_allocate(
        reader, sizeof(LunaExpression), _Alignof(LunaExpression));
    if (unary == NULL) {
        return false;
    }
    unary->kind = LUNA_EXPRESSION_UNARY;
    unary->span = reader->span;
    unary->as.unary.operator_kind = LUNA_TOKEN_MINUS;
    unary->as.unary.operand = literal;
    *initializer = unary;
    return true;
}

static bool luna_metadata_read_aggregate(LunaMetadataReader *reader,
                                         LunaTypeDeclaration *declaration) {
    uint32_t field_count = 0U;
    if (!luna_metadata_read_count(reader, &field_count)) {
        return false;
    }
    declaration->as.aggregate.field_count = field_count;
    LunaField **next = &declaration->as.aggregate.first_field;
    for (uint32_t index = 0U; index < field_count; index += 1U) {
        LunaField *field = luna_metadata_allocate(reader, sizeof(LunaField),
                                                  _Alignof(LunaField));
        if (field == NULL ||
            !luna_metadata_read_identifier(reader, &field->name) ||
            !luna_metadata_read_type(reader, &field->type, 0U)) {
            return false;
        }
        field->span = reader->span;
        *next = field;
        next = &field->next;
    }
    return true;
}

static bool luna_metadata_read_enumeration(LunaMetadataReader *reader,
                                           LunaTypeDeclaration *declaration) {
    if (!luna_metadata_read_type(
            reader, &declaration->as.enumeration.underlying_type, 0U)) {
        return false;
    }
    uint32_t member_count = 0U;
    if (!luna_metadata_read_count(reader, &member_count)) {
        return false;
    }
    declaration->as.enumeration.member_count = member_count;
    LunaEnumMember **next = &declaration->as.enumeration.first_member;
    for (uint32_t index = 0U; index < member_count; index += 1U) {
        LunaEnumMember *member = luna_metadata_allocate(
            reader, sizeof(LunaEnumMember), _Alignof(LunaEnumMember));
        if (member == NULL ||
            !luna_metadata_read_identifier(reader, &member->name) ||
            !luna_metadata_read_enum_initializer(reader,
                                                 &member->initializer)) {
            return false;
        }
        member->span = reader->span;
        *next = member;
        next = &member->next;
    }
    return true;
}

static bool luna_metadata_read_type_declarations(LunaMetadataReader *reader,
                                                 LunaProgram *program) {
    uint32_t declaration_count = 0U;
    if (!luna_metadata_read_count(reader, &declaration_count)) {
        return false;
    }

    LunaTypeDeclaration **next = &program->first_type_declaration;
    for (uint32_t index = 0U; index < declaration_count; index += 1U) {
        uint8_t tag = 0U;
        uint8_t flags = 0U;
        if (!luna_metadata_read_u8(reader, &tag) ||
            !luna_metadata_read_u8(reader, &flags) || flags > 1U) {
            return luna_metadata_reader_fail(
                reader, "metadata contains invalid declaration flags");
        }

        LunaTypeKind kind = LUNA_TYPE_INVALID;
        if (tag == LUNA_METADATA_DECLARATION_STRUCT) {
            kind = LUNA_TYPE_STRUCT;
        } else if (tag == LUNA_METADATA_DECLARATION_UNION) {
            kind = LUNA_TYPE_UNION;
        } else if (tag == LUNA_METADATA_DECLARATION_ENUM) {
            kind = LUNA_TYPE_ENUM;
        } else {
            return luna_metadata_reader_fail(
                reader, "metadata contains an unknown declaration tag");
        }

        LunaTypeDeclaration *declaration = luna_metadata_allocate(
            reader, sizeof(LunaTypeDeclaration), _Alignof(LunaTypeDeclaration));
        if (declaration == NULL) {
            return false;
        }
        declaration->kind = kind;
        declaration->span = reader->span;
        declaration->is_exported = flags != 0U;
        if (!luna_metadata_read_identifier(reader, &declaration->name)) {
            return false;
        }

        const bool decoded =
            kind == LUNA_TYPE_ENUM
                ? luna_metadata_read_enumeration(reader, declaration)
                : luna_metadata_read_aggregate(reader, declaration);
        if (!decoded) {
            return false;
        }
        *next = declaration;
        next = &declaration->next;
    }
    return true;
}

static bool luna_metadata_read_parameters(LunaMetadataReader *reader,
                                          LunaFunction *function) {
    uint32_t parameter_count = 0U;
    if (!luna_metadata_read_count(reader, &parameter_count)) {
        return false;
    }
    function->parameter_count = parameter_count;
    LunaParameter **next = &function->first_parameter;
    for (uint32_t index = 0U; index < parameter_count; index += 1U) {
        LunaParameter *parameter = luna_metadata_allocate(
            reader, sizeof(LunaParameter), _Alignof(LunaParameter));
        if (parameter == NULL ||
            !luna_metadata_read_identifier(reader, &parameter->name) ||
            !luna_metadata_read_type(reader, &parameter->type, 0U)) {
            return false;
        }
        parameter->span = reader->span;
        *next = parameter;
        next = &parameter->next;
    }
    return true;
}

static bool luna_metadata_read_functions(LunaMetadataReader *reader,
                                         LunaProgram *program) {
    uint32_t function_count = 0U;
    if (!luna_metadata_read_count(reader, &function_count)) {
        return false;
    }

    LunaFunction **next = &program->first_function;
    for (uint32_t index = 0U; index < function_count; index += 1U) {
        uint8_t flags = 0U;
        if (!luna_metadata_read_u8(reader, &flags) ||
            (flags & (uint8_t)~UINT8_C(3)) != 0U) {
            return luna_metadata_reader_fail(
                reader, "metadata contains invalid function flags");
        }
        LunaFunction *function = luna_metadata_allocate(
            reader, sizeof(LunaFunction), _Alignof(LunaFunction));
        if (function == NULL) {
            return false;
        }
        function->span = reader->span;
        function->is_exported = (flags & UINT8_C(1)) != 0U;
        function->is_external = (flags & UINT8_C(2)) != 0U;
        function->is_declaration = true;
        if (!luna_metadata_read_identifier(reader, &function->name) ||
            !luna_metadata_read_parameters(reader, function) ||
            !luna_metadata_read_type(reader, &function->return_type, 0U)) {
            return false;
        }
        *next = function;
        next = &function->next;
    }
    return true;
}

static bool luna_metadata_read_imports(LunaMetadataReader *reader,
                                       LunaProgram *program,
                                       LunaModuleMetadata *metadata) {
    uint32_t import_count = 0U;
    if (!luna_metadata_read_count(reader, &import_count)) {
        return false;
    }
    LunaImport **next = &program->first_import;
    for (uint32_t index = 0U; index < import_count; index += 1U) {
        LunaImport *import = luna_metadata_allocate(reader, sizeof(LunaImport),
                                                    _Alignof(LunaImport));
        if (import == NULL ||
            !luna_metadata_read_module_name(reader, &import->module_name)) {
            return false;
        }
        LunaModuleMetadataDependency dependency = {
            .module_name = import->module_name,
        };
        if (!luna_metadata_read_u64(reader, &dependency.content_hash)) {
            return luna_metadata_reader_fail(
                reader, "metadata contains an invalid dependency fingerprint");
        }
        if (!luna_vector_push(&metadata->dependencies, &dependency)) {
            return luna_metadata_reader_fail(
                reader, "out of memory while decoding module metadata");
        }
        import->span = reader->span;
        *next = import;
        next = &import->next;
    }
    return true;
}

static uint16_t luna_metadata_load_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t luna_metadata_load_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint64_t luna_metadata_load_u64(const uint8_t *bytes) {
    uint64_t result = 0U;
    for (uint32_t index = 0U; index < 8U; index += 1U) {
        result |= (uint64_t)bytes[index] << (index * 8U);
    }
    return result;
}

static const char *luna_metadata_source_path(const LunaSourceFile *source) {
    return source != NULL && source->path != NULL ? source->path : "<metadata>";
}

void luna_module_metadata_destroy(LunaModuleMetadata *metadata) {
    if (metadata == NULL) {
        return;
    }
    luna_source_destroy(&metadata->diagnostic_source);
    luna_vector_destroy(&metadata->dependencies);
    metadata->interface_unit = NULL;
    metadata->content_hash = 0U;
}

bool luna_module_metadata_decode(const LunaSourceFile *source,
                                 const LunaTargetInfo *target, LunaArena *arena,
                                 LunaDiagnosticEngine *diagnostics,
                                 LunaModuleMetadata *metadata) {
    if (metadata == NULL) {
        luna_diagnostic_error_plain(diagnostics,
                                    "module metadata output must not be null");
        return false;
    }
    *metadata = (LunaModuleMetadata){0};
    luna_vector_init(&metadata->dependencies,
                     sizeof(LunaModuleMetadataDependency));
    if (source == NULL || source->text == NULL || arena == NULL ||
        !luna_target_info_is_supported(target)) {
        luna_diagnostic_error_plain(
            diagnostics, "module metadata decoder received invalid input");
        return false;
    }
    if (source->length < luna_metadata_header_size ||
        source->length > luna_metadata_max_file_size) {
        luna_diagnostic_error_plain(
            diagnostics, "invalid module metadata '%s': invalid file size",
            luna_metadata_source_path(source));
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)source->text;
    if (memcmp(bytes, luna_metadata_magic, sizeof(luna_metadata_magic)) != 0) {
        luna_diagnostic_error_plain(diagnostics,
                                    "invalid module metadata '%s': bad magic",
                                    luna_metadata_source_path(source));
        return false;
    }
    const uint16_t format_major = luna_metadata_load_u16(bytes + 8U);
    const uint16_t format_minor = luna_metadata_load_u16(bytes + 10U);
    const uint32_t language_abi = luna_metadata_load_u32(bytes + 12U);
    if (format_major != luna_metadata_format_major ||
        format_minor > luna_metadata_format_minor) {
        luna_diagnostic_error_plain(
            diagnostics,
            "unsupported module metadata format %" PRIu16 ".%" PRIu16
            " in '%s'; compiler supports %" PRIu16 ".%" PRIu16,
            format_major, format_minor, luna_metadata_source_path(source),
            luna_metadata_format_major, luna_metadata_format_minor);
        return false;
    }
    if (language_abi != luna_metadata_language_abi) {
        luna_diagnostic_error_plain(
            diagnostics,
            "incompatible module metadata language ABI %" PRIu32
            " in '%s'; compiler requires %" PRIu32,
            language_abi, luna_metadata_source_path(source),
            luna_metadata_language_abi);
        return false;
    }

    const uint64_t payload_size = luna_metadata_load_u64(bytes + 16U);
    const uint64_t expected_hash = luna_metadata_load_u64(bytes + 24U);
    const size_t available_payload = source->length - luna_metadata_header_size;
    if (payload_size != (uint64_t)available_payload) {
        luna_diagnostic_error_plain(
            diagnostics,
            "invalid module metadata '%s': payload length does not match "
            "the file",
            luna_metadata_source_path(source));
        return false;
    }
    const uint8_t *payload = bytes + luna_metadata_header_size;
    if (luna_metadata_hash(payload, available_payload) != expected_hash) {
        luna_diagnostic_error_plain(
            diagnostics,
            "invalid module metadata '%s': payload checksum mismatch",
            luna_metadata_source_path(source));
        return false;
    }
    metadata->content_hash = expected_hash;

    if (!luna_source_from_bytes(luna_metadata_source_path(source), "", 0U,
                                &metadata->diagnostic_source)) {
        luna_diagnostic_error_plain(
            diagnostics,
            "out of memory while preparing module metadata diagnostics");
        return false;
    }

    LunaMetadataReader reader = {
        .data = payload,
        .length = available_payload,
        .arena = arena,
        .span =
            {
                .source = &metadata->diagnostic_source,
                .line = 1U,
                .column = 1U,
            },
    };

    LunaStringView metadata_target = {0};
    if (!luna_metadata_read_raw_string(&reader, &metadata_target)) {
        goto decode_failure;
    }
    const LunaStringView requested_target =
        luna_string_view_from_c_string(target->triple);
    if (!luna_string_view_equal(metadata_target, requested_target)) {
        luna_diagnostic_error_plain(
            diagnostics,
            "module metadata '%s' targets '%.*s', but compilation target is "
            "'%s'",
            luna_metadata_source_path(source), (int)metadata_target.length,
            metadata_target.data, target->triple);
        luna_module_metadata_destroy(metadata);
        return false;
    }

    LunaProgram *program = luna_metadata_allocate(&reader, sizeof(LunaProgram),
                                                  _Alignof(LunaProgram));
    if (program == NULL) {
        goto decode_failure;
    }
    program->source = &metadata->diagnostic_source;
    program->module_span = reader.span;
    program->is_interface = true;
    if (!luna_metadata_read_module_name(&reader, &program->module_name) ||
        !luna_metadata_read_imports(&reader, program, metadata) ||
        !luna_metadata_read_type_declarations(&reader, program) ||
        !luna_metadata_read_functions(&reader, program)) {
        goto decode_failure;
    }
    if (reader.offset != reader.length) {
        (void)luna_metadata_reader_fail(
            &reader, "metadata payload contains trailing bytes");
        goto decode_failure;
    }

    metadata->interface_unit = program;
    return true;

decode_failure:
    luna_diagnostic_error_plain(diagnostics, "invalid module metadata '%s': %s",
                                luna_metadata_source_path(source),
                                reader.failure_reason == NULL
                                    ? "malformed payload"
                                    : reader.failure_reason);
    luna_module_metadata_destroy(metadata);
    return false;
}
