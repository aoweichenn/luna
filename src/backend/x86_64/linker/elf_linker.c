#include "elf_linker_internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void luna_elf_link_context_init(LunaElfLinkContext *context,
                                FILE *diagnostic_stream) {
    context->diagnostic_stream = diagnostic_stream;
    luna_vector_init(&context->objects, sizeof(LunaElfLinkObject));
    luna_vector_init(&context->globals, sizeof(LunaElfLinkGlobal));
    luna_vector_init(&context->global_slots, sizeof(uint32_t));
    luna_string_builder_init(&context->text);
    luna_string_builder_init(&context->rodata);
    luna_string_builder_init(&context->data);
    luna_string_builder_init(&context->debug_abbrev);
    luna_string_builder_init(&context->debug_info);
    luna_string_builder_init(&context->debug_line);
    luna_string_builder_init(&context->debug_str);
    luna_string_builder_init(&context->debug_line_str);
    context->bss_size = 0U;
    context->text_alignment = 1U;
    context->rodata_alignment = 1U;
    context->data_alignment = 1U;
    context->bss_alignment = 1U;
    context->text_file_offset = 0U;
    context->rodata_file_offset = 0U;
    context->data_file_offset = 0U;
    context->text_address = 0U;
    context->rodata_address = 0U;
    context->data_address = 0U;
    context->bss_address = 0U;
    context->entry_address = 0U;
}

void luna_elf_link_context_destroy(LunaElfLinkContext *context) {
    if (context == NULL) {
        return;
    }
    for (size_t index = 0U; index < context->objects.length; index += 1U) {
        LunaElfLinkObject *object = luna_vector_at(&context->objects, index);
        if (object != NULL) {
            luna_debug_ir_destroy(&object->debug_ir);
            luna_vector_destroy(&object->sections);
        }
    }
    luna_string_builder_destroy(&context->debug_line_str);
    luna_string_builder_destroy(&context->debug_str);
    luna_string_builder_destroy(&context->debug_line);
    luna_string_builder_destroy(&context->debug_info);
    luna_string_builder_destroy(&context->debug_abbrev);
    luna_string_builder_destroy(&context->data);
    luna_string_builder_destroy(&context->rodata);
    luna_string_builder_destroy(&context->text);
    luna_vector_destroy(&context->global_slots);
    luna_vector_destroy(&context->globals);
    luna_vector_destroy(&context->objects);
}

bool luna_elf_link_error(LunaElfLinkContext *context,
                         const LunaElfLinkObject *object, const char *format,
                         ...) {
    va_list arguments;
    va_start(arguments, format);
    if (context != NULL && context->diagnostic_stream != NULL) {
        (void)fputs("lunalink: error: ", context->diagnostic_stream);
        if (object != NULL) {
            (void)fprintf(context->diagnostic_stream,
                          "%.*s: ", (int)object->name.length,
                          object->name.data);
        }
        (void)vfprintf(context->diagnostic_stream, format, arguments);
        (void)fputc('\n', context->diagnostic_stream);
    }
    va_end(arguments);
    return false;
}

bool luna_elf_link_verify_error(FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (stream != NULL) {
        (void)fputs("ELF executable verification error: ", stream);
        (void)vfprintf(stream, format, arguments);
        (void)fputc('\n', stream);
    }
    va_end(arguments);
    return false;
}

bool luna_elf_link_range_valid(uint64_t offset, uint64_t size,
                               uint64_t total_size) {
    return offset <= total_size && size <= total_size - offset;
}

bool luna_elf_link_is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool luna_elf_link_align_up(uint64_t value, uint64_t alignment,
                            uint64_t *result) {
    if (result == NULL || !luna_elf_link_is_power_of_two(alignment)) {
        return false;
    }
    const uint64_t mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

bool luna_elf_link_read_u8(LunaStringView bytes, uint64_t offset,
                           uint8_t *value) {
    if (value == NULL ||
        !luna_elf_link_range_valid(offset, 1U, (uint64_t)bytes.length)) {
        return false;
    }
    *value = (uint8_t)(unsigned char)bytes.data[offset];
    return true;
}

bool luna_elf_link_read_u16(LunaStringView bytes, uint64_t offset,
                            uint16_t *value) {
    uint8_t first = 0U;
    uint8_t second = 0U;
    if (value == NULL || !luna_elf_link_read_u8(bytes, offset, &first) ||
        !luna_elf_link_read_u8(bytes, offset + 1U, &second)) {
        return false;
    }
    *value = (uint16_t)((uint16_t)first | ((uint16_t)second << 8U));
    return true;
}

bool luna_elf_link_read_u32(LunaStringView bytes, uint64_t offset,
                            uint32_t *value) {
    uint16_t first = 0U;
    uint16_t second = 0U;
    if (value == NULL || !luna_elf_link_read_u16(bytes, offset, &first) ||
        !luna_elf_link_read_u16(bytes, offset + 2U, &second)) {
        return false;
    }
    *value = (uint32_t)first | ((uint32_t)second << 16U);
    return true;
}

bool luna_elf_link_read_u64(LunaStringView bytes, uint64_t offset,
                            uint64_t *value) {
    uint32_t first = 0U;
    uint32_t second = 0U;
    if (value == NULL || !luna_elf_link_read_u32(bytes, offset, &first) ||
        !luna_elf_link_read_u32(bytes, offset + 4U, &second)) {
        return false;
    }
    *value = (uint64_t)first | ((uint64_t)second << 32U);
    return true;
}

bool luna_elf_link_read_i64(LunaStringView bytes, uint64_t offset,
                            int64_t *value) {
    uint64_t bits = 0U;
    if (value == NULL || !luna_elf_link_read_u64(bytes, offset, &bits)) {
        return false;
    }
    memcpy(value, &bits, sizeof(bits));
    return true;
}

bool luna_elf_link_string(LunaStringView bytes, uint64_t table_offset,
                          uint64_t table_size, uint32_t string_offset,
                          LunaStringView *result) {
    if (result == NULL || (uint64_t)string_offset >= table_size ||
        !luna_elf_link_range_valid(table_offset, table_size,
                                   (uint64_t)bytes.length)) {
        return false;
    }
    const char *start = bytes.data + table_offset + (uint64_t)string_offset;
    const uint64_t remaining = table_size - (uint64_t)string_offset;
    const char *end = memchr(start, '\0', (size_t)remaining);
    if (end == NULL) {
        return false;
    }
    *result = (LunaStringView){
        .data = start,
        .length = (size_t)(end - start),
    };
    return true;
}

bool luna_x86_64_link_elf_executable(const LunaX8664ElfLinkInput *inputs,
                                     uint32_t input_count,
                                     LunaStringView entry_symbol,
                                     FILE *diagnostic_stream,
                                     LunaStringBuilder *output) {
    LunaElfLinkContext context;
    luna_elf_link_context_init(&context, diagnostic_stream);

    bool success = true;
    if (inputs == NULL || input_count == 0U ||
        input_count > LUNA_X86_64_ELF_LINK_MAX_INPUT_COUNT || output == NULL ||
        output->length != 0U || entry_symbol.data == NULL ||
        entry_symbol.length == 0U ||
        entry_symbol.length > LUNA_X86_64_ELF_LINK_MAX_NAME_LENGTH) {
        (void)luna_elf_link_error(&context, NULL,
                                  "invalid static linker arguments");
        if (output != NULL) {
            output->length = 0U;
            if (output->data != NULL) {
                output->data[0] = '\0';
            }
        }
        luna_elf_link_context_destroy(&context);
        return false;
    }
    for (uint32_t index = 0U; success && index < input_count; index += 1U) {
        if (inputs[index].name.data == NULL ||
            inputs[index].name.length == 0U ||
            inputs[index].name.length > LUNA_X86_64_ELF_LINK_MAX_NAME_LENGTH ||
            inputs[index].object.data == NULL ||
            inputs[index].object.length >
                LUNA_X86_64_ELF_LINK_MAX_OBJECT_SIZE) {
            success = luna_elf_link_error(
                &context, NULL, "invalid input object at index %" PRIu32,
                index);
        } else {
            success = luna_elf_link_parse_object(&context, &inputs[index]);
        }
    }

    if (success) {
        success = luna_elf_link_collect_globals(&context) &&
                  luna_elf_link_layout_regions(&context) &&
                  luna_elf_link_layout_executable(&context) &&
                  luna_elf_link_build_debug(&context) &&
                  luna_elf_link_apply_relocations(&context) &&
                  luna_elf_link_resolve_entry(&context, entry_symbol) &&
                  luna_elf_link_serialize_executable(&context, output);
    }
    if (success) {
        success = luna_x86_64_elf_executable_verify(
            (LunaStringView){
                .data = luna_string_builder_data(output),
                .length = output->length,
            },
            diagnostic_stream);
    }
    if (!success && output != NULL) {
        output->length = 0U;
        if (output->data != NULL) {
            output->data[0] = '\0';
        }
    }
    luna_elf_link_context_destroy(&context);
    return success;
}
