#include "assembler_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { LUNA_X86_64_ASSEMBLY_MAX_ALIGNMENT = 4096 };

static bool luna_assembly_is_space(char character) {
    return character == ' ' || character == '\t' || character == '\r';
}

static LunaStringView luna_assembly_trim(LunaStringView view) {
    while (view.length > 0U && luna_assembly_is_space(view.data[0])) {
        view.data += 1;
        view.length -= 1U;
    }
    while (view.length > 0U &&
           luna_assembly_is_space(view.data[view.length - 1U])) {
        view.length -= 1U;
    }
    return view;
}

static size_t luna_assembly_find_character(LunaStringView view,
                                           char character) {
    for (size_t index = 0U; index < view.length; index += 1U) {
        if (view.data[index] == character) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool luna_assembly_symbol_name_valid(LunaStringView name) {
    if (name.length == 0U || !((name.data[0] >= 'A' && name.data[0] <= 'Z') ||
                               (name.data[0] >= 'a' && name.data[0] <= 'z') ||
                               name.data[0] == '_' || name.data[0] == '.')) {
        return false;
    }
    for (size_t index = 1U; index < name.length; index += 1U) {
        const char character = name.data[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_' ||
              character == '.')) {
            return false;
        }
    }
    return true;
}

static bool luna_assembly_parse_u32(LunaStringView view, uint32_t *value) {
    view = luna_assembly_trim(view);
    if (view.length == 0U || view.length >= 32U || value == NULL) {
        return false;
    }
    char text[32];
    memcpy(text, view.data, view.length);
    text[view.length] = '\0';
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > (unsigned long)UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool luna_assembly_parse_i64(LunaStringView view, int64_t *value) {
    view = luna_assembly_trim(view);
    if (view.length == 0U || view.length >= 64U || value == NULL) {
        return false;
    }
    char text[64];
    memcpy(text, view.data, view.length);
    text[view.length] = '\0';
    errno = 0;
    char *end = NULL;
    if (view.data[0] == '-') {
        const long long parsed = strtoll(text, &end, 0);
        if (errno != 0 || end == text || *end != '\0') {
            return false;
        }
        *value = (int64_t)parsed;
        return true;
    }
    const unsigned long long parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    uint64_t bits = (uint64_t)parsed;
    memcpy(value, &bits, sizeof(bits));
    return true;
}

static bool luna_assembly_unescape_string(LunaStringView encoded,
                                          LunaStringBuilder *decoded) {
    encoded = luna_assembly_trim(encoded);
    if (decoded == NULL || encoded.length < 2U || encoded.data[0] != '"' ||
        encoded.data[encoded.length - 1U] != '"') {
        return false;
    }
    for (size_t index = 1U; index + 1U < encoded.length; index += 1U) {
        unsigned char value = (unsigned char)encoded.data[index];
        if (value != '\\') {
            const char byte = (char)value;
            if (value == 0U ||
                !luna_string_builder_append(decoded, &byte, 1U)) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (index + 1U >= encoded.length) {
            return false;
        }
        const char escape = encoded.data[index];
        if (escape == '\\' || escape == '"') {
            value = (unsigned char)escape;
        } else if (escape == 'n') {
            value = '\n';
        } else if (escape == 'r') {
            value = '\r';
        } else if (escape == 't') {
            value = '\t';
        } else if (escape >= '0' && escape <= '7') {
            uint32_t octal = (uint32_t)(escape - '0');
            uint32_t digit_count = 1U;
            while (digit_count < 3U && index + 2U < encoded.length &&
                   encoded.data[index + 1U] >= '0' &&
                   encoded.data[index + 1U] <= '7') {
                index += 1U;
                octal = octal * 8U + (uint32_t)(encoded.data[index] - '0');
                digit_count += 1U;
            }
            if (octal == 0U || octal > UINT8_MAX) {
                return false;
            }
            value = (unsigned char)octal;
        } else {
            return false;
        }
        const char byte = (char)value;
        if (!luna_string_builder_append(decoded, &byte, 1U)) {
            return false;
        }
    }
    return decoded->length > 0U;
}

bool luna_x86_64_assembler_error(LunaX8664Assembler *assembler,
                                 const char *format, ...) {
    if (assembler == NULL || assembler->diagnostics == NULL) {
        return false;
    }
    va_list arguments;
    va_start(arguments, format);
    (void)fprintf(assembler->diagnostics->stream,
                  "error: x86-64 object assembler line %zu: ", assembler->line);
    (void)vfprintf(assembler->diagnostics->stream, format, arguments);
    (void)fputc('\n', assembler->diagnostics->stream);
    va_end(arguments);
    assembler->diagnostics->error_count += 1U;
    return false;
}

LunaStringBuilder *
luna_x86_64_assembler_current_output(LunaX8664Assembler *assembler) {
    if (assembler == NULL || assembler->image == NULL) {
        return NULL;
    }
    return luna_x86_64_object_section_builder(assembler->image,
                                              assembler->section);
}

bool luna_x86_64_assembler_append_u8(LunaX8664Assembler *assembler,
                                     uint8_t value) {
    LunaStringBuilder *output = luna_x86_64_assembler_current_output(assembler);
    const char byte = (char)value;
    return output != NULL && luna_string_builder_append(output, &byte, 1U);
}

bool luna_x86_64_assembler_append_u16(LunaX8664Assembler *assembler,
                                      uint16_t value) {
    return luna_x86_64_assembler_append_u8(assembler,
                                           (uint8_t)(value & UINT16_C(0xff))) &&
           luna_x86_64_assembler_append_u8(
               assembler, (uint8_t)((value >> 8U) & UINT16_C(0xff)));
}

bool luna_x86_64_assembler_append_u32(LunaX8664Assembler *assembler,
                                      uint32_t value) {
    return luna_x86_64_assembler_append_u8(assembler,
                                           (uint8_t)(value & UINT32_C(0xff))) &&
           luna_x86_64_assembler_append_u8(
               assembler, (uint8_t)((value >> 8U) & UINT32_C(0xff))) &&
           luna_x86_64_assembler_append_u8(
               assembler, (uint8_t)((value >> 16U) & UINT32_C(0xff))) &&
           luna_x86_64_assembler_append_u8(
               assembler, (uint8_t)((value >> 24U) & UINT32_C(0xff)));
}

bool luna_x86_64_assembler_append_u64(LunaX8664Assembler *assembler,
                                      uint64_t value) {
    return luna_x86_64_assembler_append_u32(assembler, (uint32_t)value) &&
           luna_x86_64_assembler_append_u32(assembler,
                                            (uint32_t)(value >> 32U));
}

static bool luna_assembly_parse_numeric_reference(LunaStringView target,
                                                  uint32_t *number,
                                                  bool *forward) {
    if (target.length < 2U || number == NULL || forward == NULL) {
        return false;
    }
    const char direction = target.data[target.length - 1U];
    if (direction != 'f' && direction != 'b') {
        return false;
    }
    const LunaStringView number_view = {
        .data = target.data,
        .length = target.length - 1U,
    };
    if (!luna_assembly_parse_u32(number_view, number)) {
        return false;
    }
    *forward = direction == 'f';
    return true;
}

bool luna_x86_64_assembler_add_fixup(LunaX8664Assembler *assembler,
                                     LunaStringView target, uint64_t offset,
                                     LunaX8664AssemblyFixupKind kind) {
    LunaX8664AssemblyFixup fixup = {
        .target = luna_assembly_trim(target),
        .section = assembler->section,
        .offset = offset,
        .line = assembler->line,
        .numeric_label = 0U,
        .kind = kind,
        .is_numeric = false,
        .numeric_forward = false,
    };
    fixup.is_numeric = luna_assembly_parse_numeric_reference(
        fixup.target, &fixup.numeric_label, &fixup.numeric_forward);
    if ((!fixup.is_numeric && !luna_assembly_symbol_name_valid(fixup.target)) ||
        !luna_vector_push(&assembler->fixups, &fixup)) {
        return luna_x86_64_assembler_error(
            assembler, "failed to record instruction fixup");
    }
    return true;
}

static LunaX8664ObjectSymbol *
luna_assembly_find_symbol(LunaX8664ObjectImage *image, LunaStringView name,
                          size_t *index) {
    for (size_t symbol_index = 0U; symbol_index < image->symbols.length;
         symbol_index += 1U) {
        LunaX8664ObjectSymbol *symbol =
            luna_vector_at(&image->symbols, symbol_index);
        if (symbol != NULL && luna_string_view_equal(symbol->name, name)) {
            if (index != NULL) {
                *index = symbol_index;
            }
            return symbol;
        }
    }
    return NULL;
}

static LunaX8664ObjectSymbol *
luna_assembly_get_symbol(LunaX8664Assembler *assembler, LunaStringView name,
                         size_t *index) {
    LunaX8664ObjectSymbol *symbol =
        luna_assembly_find_symbol(assembler->image, name, index);
    if (symbol != NULL) {
        return symbol;
    }
    const LunaX8664ObjectSymbol created = {
        .name = name,
        .section = LUNA_X86_64_OBJECT_SECTION_UNDEFINED,
        .value = 0U,
        .size = 0U,
        .type = LUNA_X86_64_OBJECT_SYMBOL_NONE,
        .defined = false,
        .global = false,
        .external = false,
    };
    if (!luna_vector_push(&assembler->image->symbols, &created)) {
        return NULL;
    }
    if (index != NULL) {
        *index = assembler->image->symbols.length - 1U;
    }
    return luna_vector_at(&assembler->image->symbols,
                          assembler->image->symbols.length - 1U);
}

static bool luna_assembly_set_section(LunaX8664Assembler *assembler,
                                      LunaX8664ObjectSection section) {
    assembler->section = section;
    return true;
}

static bool luna_assembly_parse_section(LunaX8664Assembler *assembler,
                                        LunaStringView arguments) {
    const size_t comma = luna_assembly_find_character(arguments, ',');
    LunaStringView name = {
        .data = arguments.data,
        .length = comma == SIZE_MAX ? arguments.length : comma,
    };
    name = luna_assembly_trim(name);
    if (luna_string_view_equal_c_string(name, ".rodata")) {
        return luna_assembly_set_section(assembler,
                                         LUNA_X86_64_OBJECT_SECTION_RODATA);
    }
    if (luna_string_view_equal_c_string(name, ".data")) {
        return luna_assembly_set_section(assembler,
                                         LUNA_X86_64_OBJECT_SECTION_DATA);
    }
    if (luna_string_view_equal_c_string(name, ".text")) {
        return luna_assembly_set_section(assembler,
                                         LUNA_X86_64_OBJECT_SECTION_TEXT);
    }
    if (luna_string_view_equal_c_string(name, ".note.GNU-stack")) {
        assembler->section = LUNA_X86_64_OBJECT_SECTION_UNDEFINED;
        return true;
    }
    return luna_x86_64_assembler_error(assembler, "unsupported section '%.*s'",
                                       (int)name.length, name.data);
}

static bool luna_assembly_mark_symbol(LunaX8664Assembler *assembler,
                                      LunaStringView name, bool external) {
    name = luna_assembly_trim(name);
    if (!luna_assembly_symbol_name_valid(name)) {
        return luna_x86_64_assembler_error(assembler,
                                           "invalid symbol declaration");
    }
    LunaX8664ObjectSymbol *symbol =
        luna_assembly_get_symbol(assembler, name, NULL);
    if (symbol == NULL) {
        return luna_x86_64_assembler_error(
            assembler, "failed to create symbol declaration");
    }
    symbol->global = true;
    symbol->external = external;
    return true;
}

static bool luna_assembly_set_symbol_type(LunaX8664Assembler *assembler,
                                          LunaStringView arguments) {
    const size_t comma = luna_assembly_find_character(arguments, ',');
    if (comma == SIZE_MAX) {
        return luna_x86_64_assembler_error(assembler,
                                           "malformed .type directive");
    }
    LunaStringView name = {
        .data = arguments.data,
        .length = comma,
    };
    LunaStringView type = {
        .data = arguments.data + comma + 1U,
        .length = arguments.length - comma - 1U,
    };
    name = luna_assembly_trim(name);
    type = luna_assembly_trim(type);
    if (!luna_assembly_symbol_name_valid(name)) {
        return luna_x86_64_assembler_error(assembler,
                                           "invalid typed symbol name");
    }
    LunaX8664ObjectSymbol *symbol =
        luna_assembly_get_symbol(assembler, name, NULL);
    if (symbol == NULL) {
        return luna_x86_64_assembler_error(assembler,
                                           "failed to create typed symbol");
    }
    if (luna_string_view_equal_c_string(type, "@function")) {
        symbol->type = LUNA_X86_64_OBJECT_SYMBOL_FUNCTION;
        return true;
    }
    if (luna_string_view_equal_c_string(type, "@object")) {
        symbol->type = LUNA_X86_64_OBJECT_SYMBOL_OBJECT;
        return true;
    }
    return luna_x86_64_assembler_error(assembler,
                                       "unsupported symbol type '%.*s'",
                                       (int)type.length, type.data);
}

static bool luna_assembly_define_label(LunaX8664Assembler *assembler,
                                       LunaStringView name) {
    LunaStringBuilder *section =
        luna_x86_64_assembler_current_output(assembler);
    if (section == NULL || name.length == 0U) {
        return luna_x86_64_assembler_error(
            assembler, "label appears outside an object section");
    }
    bool numeric = name.length > 0U;
    for (size_t index = 0U; index < name.length; index += 1U) {
        numeric = numeric && name.data[index] >= '0' && name.data[index] <= '9';
    }
    if (numeric) {
        uint32_t number = 0U;
        if (!luna_assembly_parse_u32(name, &number)) {
            return luna_x86_64_assembler_error(
                assembler, "numeric label exceeds the supported range");
        }
        const LunaX8664AssemblyNumericLabel defined = {
            .section = assembler->section,
            .offset = (uint64_t)section->length,
            .number = number,
        };
        if (!luna_vector_push(&assembler->numeric_labels, &defined)) {
            return luna_x86_64_assembler_error(
                assembler, "out of memory while recording numeric label");
        }
        return true;
    }
    if (!luna_assembly_symbol_name_valid(name)) {
        return luna_x86_64_assembler_error(assembler, "invalid label name");
    }
    LunaX8664ObjectSymbol *symbol =
        luna_assembly_get_symbol(assembler, name, NULL);
    if (symbol == NULL) {
        return luna_x86_64_assembler_error(assembler,
                                           "failed to create label symbol");
    }
    if (symbol->defined || symbol->external) {
        return luna_x86_64_assembler_error(assembler,
                                           "duplicate or external label '%.*s'",
                                           (int)name.length, name.data);
    }
    symbol->defined = true;
    symbol->section = assembler->section;
    symbol->value = (uint64_t)section->length;
    return true;
}

static bool luna_assembly_set_symbol_size(LunaX8664Assembler *assembler,
                                          LunaStringView arguments) {
    const size_t comma = luna_assembly_find_character(arguments, ',');
    if (comma == SIZE_MAX) {
        return luna_x86_64_assembler_error(assembler,
                                           "malformed .size directive");
    }
    LunaStringView name = {
        .data = arguments.data,
        .length = comma,
    };
    LunaStringView expression = {
        .data = arguments.data + comma + 1U,
        .length = arguments.length - comma - 1U,
    };
    name = luna_assembly_trim(name);
    expression = luna_assembly_trim(expression);
    if (!luna_assembly_symbol_name_valid(name)) {
        return luna_x86_64_assembler_error(assembler,
                                           "invalid sized symbol name");
    }
    LunaX8664ObjectSymbol *symbol =
        luna_assembly_find_symbol(assembler->image, name, NULL);
    const LunaStringBuilder *section =
        luna_x86_64_assembler_current_output(assembler);
    if (symbol == NULL || !symbol->defined ||
        symbol->section != assembler->section || section == NULL ||
        expression.length != name.length + 2U || expression.data[0] != '.' ||
        expression.data[1] != '-' ||
        memcmp(expression.data + 2U, name.data, name.length) != 0 ||
        symbol->value > (uint64_t)section->length) {
        return luna_x86_64_assembler_error(
            assembler, "invalid .size expression for '%.*s'", (int)name.length,
            name.data);
    }
    symbol->size = (uint64_t)section->length - symbol->value;
    return true;
}

static bool luna_assembly_emit_byte(LunaX8664Assembler *assembler,
                                    LunaStringView arguments) {
    int64_t value = 0;
    if (!luna_assembly_parse_i64(arguments, &value) || value < -128 ||
        value > 255) {
        return luna_x86_64_assembler_error(
            assembler, "invalid .byte value '%.*s'", (int)arguments.length,
            arguments.data);
    }
    return luna_x86_64_assembler_append_u8(assembler, (uint8_t)value);
}

static bool luna_assembly_emit_alignment(LunaX8664Assembler *assembler,
                                         LunaStringView arguments,
                                         bool exponent) {
    uint32_t parsed = 0U;
    if (!luna_assembly_parse_u32(arguments, &parsed) ||
        (exponent && parsed > 12U)) {
        return luna_x86_64_assembler_error(
            assembler, "invalid alignment '%.*s'", (int)arguments.length,
            arguments.data);
    }
    const uint64_t alignment =
        exponent ? UINT64_C(1) << parsed : (uint64_t)parsed;
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U) {
        return luna_x86_64_assembler_error(assembler,
                                           "alignment must be a power of two");
    }
    if (alignment > LUNA_X86_64_ASSEMBLY_MAX_ALIGNMENT) {
        return luna_x86_64_assembler_error(
            assembler, "alignment exceeds the supported object maximum");
    }
    LunaStringBuilder *output = luna_x86_64_assembler_current_output(assembler);
    if (output == NULL) {
        return luna_x86_64_assembler_error(
            assembler, "alignment appears outside an object section");
    }
    const uint64_t remainder = (uint64_t)output->length & (alignment - 1U);
    uint64_t padding = remainder == 0U ? 0U : alignment - remainder;
    const uint8_t fill = assembler->section == LUNA_X86_64_OBJECT_SECTION_TEXT
                             ? UINT8_C(0x90)
                             : UINT8_C(0);
    while (padding > 0U) {
        if (!luna_x86_64_assembler_append_u8(assembler, fill)) {
            return luna_x86_64_assembler_error(
                assembler, "out of memory while applying alignment");
        }
        padding -= 1U;
    }
    uint64_t *maximum_alignment = NULL;
    if (assembler->section == LUNA_X86_64_OBJECT_SECTION_TEXT) {
        maximum_alignment = &assembler->image->text_alignment;
    } else if (assembler->section == LUNA_X86_64_OBJECT_SECTION_RODATA) {
        maximum_alignment = &assembler->image->rodata_alignment;
    } else if (assembler->section == LUNA_X86_64_OBJECT_SECTION_DATA) {
        maximum_alignment = &assembler->image->data_alignment;
    }
    if (maximum_alignment != NULL && *maximum_alignment < alignment) {
        *maximum_alignment = alignment;
    }
    return true;
}

static bool luna_assembly_define_debug_file(LunaX8664Assembler *assembler,
                                            LunaStringView arguments) {
    size_t split = 0U;
    while (split < arguments.length &&
           !luna_assembly_is_space(arguments.data[split])) {
        split += 1U;
    }
    const LunaStringView id_text = {
        .data = arguments.data,
        .length = split,
    };
    LunaStringView encoded_path = {
        .data = arguments.data + split,
        .length = arguments.length - split,
    };
    encoded_path = luna_assembly_trim(encoded_path);
    uint32_t requested_id = 0U;
    LunaStringBuilder path;
    luna_string_builder_init(&path);
    uint32_t actual_id = 0U;
    const bool success =
        luna_assembly_parse_u32(id_text, &requested_id) && requested_id != 0U &&
        luna_assembly_unescape_string(encoded_path, &path) &&
        luna_debug_ir_add_file(&assembler->image->debug_ir,
                               (LunaStringView){
                                   .data = luna_string_builder_data(&path),
                                   .length = path.length,
                               },
                               &actual_id) &&
        actual_id == requested_id;
    luna_string_builder_destroy(&path);
    return success || luna_x86_64_assembler_error(
                          assembler, "malformed or inconsistent .file");
}

static bool luna_assembly_next_u32(LunaStringView arguments, size_t *cursor,
                                   uint32_t *value) {
    while (*cursor < arguments.length &&
           luna_assembly_is_space(arguments.data[*cursor])) {
        *cursor += 1U;
    }
    const size_t start = *cursor;
    while (*cursor < arguments.length &&
           !luna_assembly_is_space(arguments.data[*cursor])) {
        *cursor += 1U;
    }
    return start < *cursor && luna_assembly_parse_u32(
                                  (LunaStringView){
                                      .data = arguments.data + start,
                                      .length = *cursor - start,
                                  },
                                  value);
}

static bool luna_assembly_define_debug_location(LunaX8664Assembler *assembler,
                                                LunaStringView arguments) {
    LunaStringBuilder *text = luna_x86_64_object_section_builder(
        assembler->image, LUNA_X86_64_OBJECT_SECTION_TEXT);
    size_t cursor = 0U;
    uint32_t file_id = 0U;
    uint32_t line = 0U;
    uint32_t column = 0U;
    const bool parsed = assembler->section == LUNA_X86_64_OBJECT_SECTION_TEXT &&
                        text != NULL &&
                        luna_assembly_next_u32(arguments, &cursor, &file_id) &&
                        luna_assembly_next_u32(arguments, &cursor, &line) &&
                        luna_assembly_next_u32(arguments, &cursor, &column);
    while (cursor < arguments.length &&
           luna_assembly_is_space(arguments.data[cursor])) {
        cursor += 1U;
    }
    return (parsed && cursor == arguments.length &&
            luna_debug_ir_add_location(&assembler->image->debug_ir,
                                       (uint64_t)text->length, file_id, line,
                                       column, true)) ||
           luna_x86_64_assembler_error(assembler, "malformed .loc");
}

static bool luna_assembly_handle_directive(LunaX8664Assembler *assembler,
                                           LunaStringView line) {
    size_t split = 0U;
    while (split < line.length && !luna_assembly_is_space(line.data[split])) {
        split += 1U;
    }
    const LunaStringView directive = {
        .data = line.data,
        .length = split,
    };
    LunaStringView arguments = {
        .data = line.data + split,
        .length = line.length - split,
    };
    arguments = luna_assembly_trim(arguments);

    if (luna_string_view_equal_c_string(directive, ".text")) {
        return luna_assembly_set_section(assembler,
                                         LUNA_X86_64_OBJECT_SECTION_TEXT);
    }
    if (luna_string_view_equal_c_string(directive, ".data")) {
        return luna_assembly_set_section(assembler,
                                         LUNA_X86_64_OBJECT_SECTION_DATA);
    }
    if (luna_string_view_equal_c_string(directive, ".section")) {
        return luna_assembly_parse_section(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".globl")) {
        return luna_assembly_mark_symbol(assembler, arguments, false);
    }
    if (luna_string_view_equal_c_string(directive, ".extern")) {
        return luna_assembly_mark_symbol(assembler, arguments, true);
    }
    if (luna_string_view_equal_c_string(directive, ".type")) {
        return luna_assembly_set_symbol_type(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".size")) {
        return luna_assembly_set_symbol_size(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".file")) {
        return luna_assembly_define_debug_file(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".loc")) {
        return luna_assembly_define_debug_location(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".byte")) {
        return luna_assembly_emit_byte(assembler, arguments);
    }
    if (luna_string_view_equal_c_string(directive, ".p2align")) {
        return luna_assembly_emit_alignment(assembler, arguments, true);
    }
    if (luna_string_view_equal_c_string(directive, ".balign")) {
        return luna_assembly_emit_alignment(assembler, arguments, false);
    }
    return luna_x86_64_assembler_error(assembler,
                                       "unsupported directive '%.*s'",
                                       (int)directive.length, directive.data);
}

static bool luna_assembly_handle_instruction(LunaX8664Assembler *assembler,
                                             LunaStringView line) {
    if (assembler->section != LUNA_X86_64_OBJECT_SECTION_TEXT) {
        return luna_x86_64_assembler_error(assembler,
                                           "instruction appears outside .text");
    }
    size_t split = 0U;
    while (split < line.length && !luna_assembly_is_space(line.data[split])) {
        split += 1U;
    }
    const LunaStringView mnemonic = {
        .data = line.data,
        .length = split,
    };
    LunaStringView operands = {
        .data = line.data + split,
        .length = line.length - split,
    };
    operands = luna_assembly_trim(operands);
    return luna_x86_64_encode_instruction(assembler, mnemonic, operands);
}

static bool luna_assembly_handle_line(LunaX8664Assembler *assembler,
                                      LunaStringView line) {
    line = luna_assembly_trim(line);
    if (line.length == 0U) {
        return true;
    }
    if (line.data[line.length - 1U] == ':') {
        const LunaStringView name = {
            .data = line.data,
            .length = line.length - 1U,
        };
        return luna_assembly_define_label(assembler, name);
    }
    if (line.data[0] == '.') {
        return luna_assembly_handle_directive(assembler, line);
    }
    return luna_assembly_handle_instruction(assembler, line);
}

static bool luna_assembly_parse(LunaX8664Assembler *assembler,
                                LunaStringView assembly) {
    size_t offset = 0U;
    assembler->line = 1U;
    while (offset < assembly.length) {
        size_t end = offset;
        while (end < assembly.length && assembly.data[end] != '\n') {
            end += 1U;
        }
        const LunaStringView line = {
            .data = assembly.data + offset,
            .length = end - offset,
        };
        if (!luna_assembly_handle_line(assembler, line)) {
            return false;
        }
        if (end == assembly.length) {
            break;
        }
        offset = end + 1U;
        assembler->line += 1U;
    }
    return true;
}

static bool
luna_assembly_patch_displacement(LunaX8664Assembler *assembler,
                                 const LunaX8664AssemblyFixup *fixup,
                                 uint64_t target) {
    LunaStringBuilder *section =
        luna_x86_64_object_section_builder(assembler->image, fixup->section);
    if (section == NULL || fixup->offset > (uint64_t)section->length ||
        (uint64_t)section->length - fixup->offset < 4U ||
        fixup->offset > (uint64_t)INT64_MAX - 4U ||
        target > (uint64_t)INT64_MAX) {
        assembler->line = fixup->line;
        return luna_x86_64_assembler_error(assembler,
                                           "invalid PC-relative fixup range");
    }
    const int64_t displacement = (int64_t)target - ((int64_t)fixup->offset + 4);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        assembler->line = fixup->line;
        return luna_x86_64_assembler_error(
            assembler, "PC-relative displacement exceeds 32 bits");
    }
    const uint32_t bits = (uint32_t)(int32_t)displacement;
    const size_t offset = (size_t)fixup->offset;
    section->data[offset] = (char)(bits & UINT32_C(0xff));
    section->data[offset + 1U] = (char)((bits >> 8U) & UINT32_C(0xff));
    section->data[offset + 2U] = (char)((bits >> 16U) & UINT32_C(0xff));
    section->data[offset + 3U] = (char)((bits >> 24U) & UINT32_C(0xff));
    return true;
}

static bool
luna_assembly_find_numeric_target(const LunaX8664Assembler *assembler,
                                  const LunaX8664AssemblyFixup *fixup,
                                  uint64_t *target) {
    bool found = false;
    uint64_t selected = fixup->numeric_forward ? UINT64_MAX : 0U;
    for (size_t index = 0U; index < assembler->numeric_labels.length;
         index += 1U) {
        const LunaX8664AssemblyNumericLabel *label =
            luna_vector_at_const(&assembler->numeric_labels, index);
        if (label == NULL || label->section != fixup->section ||
            label->number != fixup->numeric_label) {
            continue;
        }
        if (fixup->numeric_forward && label->offset > fixup->offset &&
            label->offset < selected) {
            selected = label->offset;
            found = true;
        } else if (!fixup->numeric_forward && label->offset <= fixup->offset &&
                   (!found || label->offset > selected)) {
            selected = label->offset;
            found = true;
        }
    }
    if (found) {
        *target = selected;
    }
    return found;
}

static bool luna_assembly_add_relocation(LunaX8664Assembler *assembler,
                                         const LunaX8664AssemblyFixup *fixup,
                                         size_t symbol_index) {
    const LunaX8664ObjectRelocation relocation = {
        .section = fixup->section,
        .offset = fixup->offset,
        .symbol_index = symbol_index,
        .addend = -4,
        .type = fixup->kind == LUNA_X86_64_ASSEMBLY_FIXUP_CALL
                    ? LUNA_X86_64_OBJECT_RELOCATION_PLT32
                    : LUNA_X86_64_OBJECT_RELOCATION_PC32,
    };
    return luna_vector_push(&assembler->image->relocations, &relocation);
}

static bool luna_assembly_resolve_fixups(LunaX8664Assembler *assembler) {
    for (size_t index = 0U; index < assembler->fixups.length; index += 1U) {
        const LunaX8664AssemblyFixup *fixup =
            luna_vector_at_const(&assembler->fixups, index);
        if (fixup == NULL) {
            return false;
        }
        assembler->line = fixup->line;
        if (fixup->is_numeric) {
            uint64_t target = 0U;
            if (fixup->kind == LUNA_X86_64_ASSEMBLY_FIXUP_RIP_RELATIVE ||
                !luna_assembly_find_numeric_target(assembler, fixup, &target)) {
                return luna_x86_64_assembler_error(
                    assembler, "unresolved numeric label '%.*s'",
                    (int)fixup->target.length, fixup->target.data);
            }
            if (!luna_assembly_patch_displacement(assembler, fixup, target)) {
                return false;
            }
            continue;
        }

        size_t symbol_index = 0U;
        LunaX8664ObjectSymbol *symbol = luna_assembly_find_symbol(
            assembler->image, fixup->target, &symbol_index);
        if (symbol == NULL && fixup->kind == LUNA_X86_64_ASSEMBLY_FIXUP_CALL) {
            symbol = luna_assembly_get_symbol(assembler, fixup->target,
                                              &symbol_index);
            if (symbol != NULL) {
                symbol->global = true;
                symbol->external = true;
            }
        }
        if (symbol == NULL) {
            return luna_x86_64_assembler_error(
                assembler, "unresolved symbol '%.*s'",
                (int)fixup->target.length, fixup->target.data);
        }
        if (symbol->defined && symbol->section == fixup->section) {
            if (!luna_assembly_patch_displacement(assembler, fixup,
                                                  symbol->value)) {
                return false;
            }
            continue;
        }
        if (fixup->kind == LUNA_X86_64_ASSEMBLY_FIXUP_BRANCH) {
            return luna_x86_64_assembler_error(
                assembler, "branch target '%.*s' is not in .text",
                (int)fixup->target.length, fixup->target.data);
        }
        if (!symbol->defined) {
            symbol->global = true;
        }
        if (!luna_assembly_add_relocation(assembler, fixup, symbol_index)) {
            return luna_x86_64_assembler_error(
                assembler, "out of memory while recording relocation");
        }
    }
    return true;
}

static bool luna_assembly_validate_symbols(LunaX8664Assembler *assembler) {
    for (size_t index = 0U; index < assembler->image->symbols.length;
         index += 1U) {
        const LunaX8664ObjectSymbol *symbol =
            luna_vector_at_const(&assembler->image->symbols, index);
        if (symbol == NULL ||
            (!symbol->defined && !symbol->global && !symbol->external)) {
            return luna_x86_64_assembler_error(
                assembler, "symbol table contains an unresolved local symbol");
        }
    }
    return true;
}

static bool luna_assembly_function_has_location(const LunaDebugIr *debug_ir,
                                                uint64_t begin, uint64_t end) {
    for (size_t index = 0U; index < debug_ir->locations.length; index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        if (location != NULL && location->code_offset >= begin &&
            location->code_offset < end) {
            return true;
        }
    }
    return false;
}

static uint8_t luna_assembly_hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return (uint8_t)(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return (uint8_t)(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        return (uint8_t)(character - 'A' + 10);
    }
    return UINT8_MAX;
}

static bool luna_assembly_decode_function_name(LunaStringView linkage_name,
                                               LunaStringBuilder *name) {
    if (linkage_name.length < 4U || linkage_name.data[0] != '_' ||
        linkage_name.data[1] != 'L') {
        return luna_string_builder_append_view(name, linkage_name);
    }
    size_t separator = SIZE_MAX;
    for (size_t index = 2U; index < linkage_name.length; index += 1U) {
        if (linkage_name.data[index] == '_') {
            separator = index;
        }
    }
    if (separator == SIZE_MAX ||
        (linkage_name.length - separator - 1U) % 2U != 0U) {
        return false;
    }
    for (size_t index = separator + 1U; index < linkage_name.length;
         index += 2U) {
        const uint8_t high = luna_assembly_hex_value(linkage_name.data[index]);
        const uint8_t low =
            luna_assembly_hex_value(linkage_name.data[index + 1U]);
        if (high == UINT8_MAX || low == UINT8_MAX) {
            return false;
        }
        const char byte = (char)((uint8_t)(high << 4U) | low);
        if (byte == '\0' || !luna_string_builder_append(name, &byte, 1U)) {
            return false;
        }
    }
    return name->length > 0U;
}

static bool luna_assembly_finalize_debug(LunaX8664Assembler *assembler) {
    LunaDebugIr *debug_ir = &assembler->image->debug_ir;
    if (debug_ir->locations.length == 0U) {
        return debug_ir->files.length == 0U;
    }
    for (size_t index = 0U; index < assembler->image->symbols.length;
         index += 1U) {
        const LunaX8664ObjectSymbol *symbol =
            luna_vector_at_const(&assembler->image->symbols, index);
        if (symbol == NULL || !symbol->defined ||
            symbol->section != LUNA_X86_64_OBJECT_SECTION_TEXT ||
            symbol->type != LUNA_X86_64_OBJECT_SYMBOL_FUNCTION ||
            symbol->size == 0U ||
            !luna_assembly_function_has_location(
                debug_ir, symbol->value, symbol->value + symbol->size)) {
            continue;
        }
        LunaStringBuilder display_name;
        luna_string_builder_init(&display_name);
        const bool success =
            luna_assembly_decode_function_name(symbol->name, &display_name) &&
            luna_debug_ir_add_function(
                debug_ir, symbol->value, symbol->value + symbol->size,
                (LunaStringView){
                    .data = luna_string_builder_data(&display_name),
                    .length = display_name.length,
                },
                symbol->name, symbol->global);
        luna_string_builder_destroy(&display_name);
        if (!success) {
            return luna_x86_64_assembler_error(
                assembler, "failed to finalize function debug records");
        }
    }
    return debug_ir->functions.length > 0U &&
           luna_debug_ir_verify(debug_ir,
                                (uint64_t)assembler->image->text.length,
                                assembler->diagnostics->stream);
}

bool luna_x86_64_assemble_elf_object(LunaStringView assembly,
                                     LunaDiagnosticEngine *diagnostics,
                                     LunaStringBuilder *output) {
    if (assembly.data == NULL || diagnostics == NULL || output == NULL ||
        output->length != 0U) {
        if (diagnostics != NULL) {
            luna_diagnostic_error_plain(
                diagnostics, "ELF object emission received invalid state");
        }
        return false;
    }

    LunaX8664ObjectImage image;
    luna_x86_64_object_image_init(&image);
    LunaX8664Assembler assembler = {
        .image = &image,
        .diagnostics = diagnostics,
        .section = LUNA_X86_64_OBJECT_SECTION_UNDEFINED,
        .fixups = {0},
        .numeric_labels = {0},
        .line = 1U,
    };
    luna_vector_init(&assembler.fixups, sizeof(LunaX8664AssemblyFixup));
    luna_vector_init(&assembler.numeric_labels,
                     sizeof(LunaX8664AssemblyNumericLabel));

    bool success =
        luna_assembly_parse(&assembler, assembly) &&
        luna_assembly_resolve_fixups(&assembler) &&
        luna_assembly_validate_symbols(&assembler) &&
        luna_assembly_finalize_debug(&assembler) &&
        luna_x86_64_elf_object_serialize(&image, diagnostics, output);
    if (success) {
        const LunaStringView object = {
            .data = luna_string_builder_data(output),
            .length = output->length,
        };
        success = luna_x86_64_elf_object_verify(object, diagnostics->stream);
        if (!success) {
            diagnostics->error_count += 1U;
            output->length = 0U;
            if (output->data != NULL) {
                output->data[0] = '\0';
            }
        }
    }
    if (!success) {
        output->length = 0U;
        if (output->data != NULL) {
            output->data[0] = '\0';
        }
    }

    luna_vector_destroy(&assembler.numeric_labels);
    luna_vector_destroy(&assembler.fixups);
    luna_x86_64_object_image_destroy(&image);
    return success;
}
