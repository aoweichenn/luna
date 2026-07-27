#include "elf_linker_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LUNA_DWARF_VERSION = 5,
    LUNA_DWARF_ADDRESS_SIZE = 8,
    LUNA_DWARF_UNIT_TYPE_COMPILE = 1,
    LUNA_DWARF_CHILDREN_NO = 0,
    LUNA_DWARF_CHILDREN_YES = 1,
    LUNA_DWARF_TAG_COMPILE_UNIT = 0x11,
    LUNA_DWARF_TAG_SUBPROGRAM = 0x2e,
    LUNA_DWARF_AT_NAME = 0x03,
    LUNA_DWARF_AT_STMT_LIST = 0x10,
    LUNA_DWARF_AT_LOW_PC = 0x11,
    LUNA_DWARF_AT_HIGH_PC = 0x12,
    LUNA_DWARF_AT_LANGUAGE = 0x13,
    LUNA_DWARF_AT_COMP_DIR = 0x1b,
    LUNA_DWARF_AT_PRODUCER = 0x25,
    LUNA_DWARF_AT_DECL_COLUMN = 0x39,
    LUNA_DWARF_AT_DECL_FILE = 0x3a,
    LUNA_DWARF_AT_DECL_LINE = 0x3b,
    LUNA_DWARF_AT_EXTERNAL = 0x3f,
    LUNA_DWARF_FORM_ADDR = 0x01,
    LUNA_DWARF_FORM_DATA2 = 0x05,
    LUNA_DWARF_FORM_DATA4 = 0x06,
    LUNA_DWARF_FORM_DATA8 = 0x07,
    LUNA_DWARF_FORM_FLAG = 0x0c,
    LUNA_DWARF_FORM_STRP = 0x0e,
    LUNA_DWARF_FORM_SEC_OFFSET = 0x17,
    LUNA_DWARF_FORM_LINE_STRP = 0x1f,
    LUNA_DWARF_LANGUAGE_LO_USER = 0x8000,
    LUNA_DWARF_LNS_COPY = 1,
    LUNA_DWARF_LNS_ADVANCE_PC = 2,
    LUNA_DWARF_LNS_ADVANCE_LINE = 3,
    LUNA_DWARF_LNS_SET_FILE = 4,
    LUNA_DWARF_LNS_SET_COLUMN = 5,
    LUNA_DWARF_LNS_NEGATE_STMT = 6,
    LUNA_DWARF_LNE_END_SEQUENCE = 1,
    LUNA_DWARF_LNE_SET_ADDRESS = 2,
    LUNA_DWARF_LNCT_PATH = 1,
    LUNA_DWARF_LINE_BASE = -5,
    LUNA_DWARF_LINE_RANGE = 14,
    LUNA_DWARF_OPCODE_BASE = 13
};

static bool luna_dwarf_append_u8(LunaStringBuilder *output, uint8_t value) {
    const char byte = (char)value;
    return luna_string_builder_append(output, &byte, 1U);
}

static bool luna_dwarf_append_u16(LunaStringBuilder *output, uint16_t value) {
    return luna_dwarf_append_u8(output, (uint8_t)value) &&
           luna_dwarf_append_u8(output, (uint8_t)(value >> 8U));
}

static bool luna_dwarf_append_u32(LunaStringBuilder *output, uint32_t value) {
    return luna_dwarf_append_u16(output, (uint16_t)value) &&
           luna_dwarf_append_u16(output, (uint16_t)(value >> 16U));
}

static bool luna_dwarf_append_u64(LunaStringBuilder *output, uint64_t value) {
    return luna_dwarf_append_u32(output, (uint32_t)value) &&
           luna_dwarf_append_u32(output, (uint32_t)(value >> 32U));
}

static bool luna_dwarf_patch_u32(LunaStringBuilder *output, size_t offset,
                                 uint32_t value) {
    if (output == NULL || offset > output->length ||
        output->length - offset < 4U) {
        return false;
    }
    output->data[offset] = (char)value;
    output->data[offset + 1U] = (char)(value >> 8U);
    output->data[offset + 2U] = (char)(value >> 16U);
    output->data[offset + 3U] = (char)(value >> 24U);
    return true;
}

static bool luna_dwarf_append_uleb128(LunaStringBuilder *output,
                                      uint64_t value) {
    do {
        uint8_t byte = (uint8_t)(value & UINT64_C(0x7f));
        value >>= 7U;
        if (value != 0U) {
            byte |= UINT8_C(0x80);
        }
        if (!luna_dwarf_append_u8(output, byte)) {
            return false;
        }
    } while (value != 0U);
    return true;
}

static bool luna_dwarf_append_sleb128(LunaStringBuilder *output,
                                      int64_t value) {
    bool more = true;
    while (more) {
        uint8_t byte = (uint8_t)((uint64_t)value & UINT64_C(0x7f));
        const bool sign = (byte & UINT8_C(0x40)) != 0U;
        int64_t next = value / 128;
        if (value < 0 && value % 128 != 0) {
            next -= 1;
        }
        value = next;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more) {
            byte |= UINT8_C(0x80);
        }
        if (!luna_dwarf_append_u8(output, byte)) {
            return false;
        }
    }
    return true;
}

static bool luna_dwarf_add_string(LunaStringBuilder *table,
                                  LunaStringView value, uint32_t *offset) {
    if (table == NULL || value.data == NULL || value.length == 0U ||
        offset == NULL || table->length >= UINT32_MAX ||
        value.length > (size_t)UINT32_MAX - table->length - 1U) {
        return false;
    }
    *offset = (uint32_t)table->length;
    return luna_string_builder_append_view(table, value) &&
           luna_dwarf_append_u8(table, 0U);
}

static const LunaElfLinkSection *
luna_dwarf_object_text_section(const LunaElfLinkObject *object) {
    for (size_t index = 1U; index < object->sections.length; index += 1U) {
        const LunaElfLinkSection *section =
            luna_vector_at_const(&object->sections, index);
        if (section != NULL &&
            luna_string_view_equal_c_string(section->name, ".text") &&
            section->region == LUNA_ELF_LINK_REGION_TEXT) {
            return section;
        }
    }
    return NULL;
}

static bool luna_dwarf_code_address(const LunaElfLinkContext *context,
                                    const LunaElfLinkSection *text,
                                    uint64_t code_offset, uint64_t *address) {
    if (context == NULL || text == NULL || address == NULL ||
        code_offset > text->size ||
        text->region_offset > UINT64_MAX - code_offset ||
        context->text_address >
            UINT64_MAX - (text->region_offset + code_offset)) {
        return false;
    }
    *address = context->text_address + text->region_offset + code_offset;
    return true;
}

static bool luna_dwarf_append_abbreviation_attribute(LunaStringBuilder *output,
                                                     uint64_t attribute,
                                                     uint64_t form) {
    return luna_dwarf_append_uleb128(output, attribute) &&
           luna_dwarf_append_uleb128(output, form);
}

static bool luna_dwarf_build_abbreviations(LunaElfLinkContext *context) {
    LunaStringBuilder *output = &context->debug_abbrev;
    return
        /* 编译单元。 */
        luna_dwarf_append_uleb128(output, 1U) &&
        luna_dwarf_append_uleb128(output, LUNA_DWARF_TAG_COMPILE_UNIT) &&
        luna_dwarf_append_u8(output, LUNA_DWARF_CHILDREN_YES) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_PRODUCER,
                                                 LUNA_DWARF_FORM_STRP) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_LANGUAGE,
                                                 LUNA_DWARF_FORM_DATA2) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_NAME,
                                                 LUNA_DWARF_FORM_LINE_STRP) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_COMP_DIR,
                                                 LUNA_DWARF_FORM_LINE_STRP) &&
        luna_dwarf_append_abbreviation_attribute(
            output, LUNA_DWARF_AT_STMT_LIST, LUNA_DWARF_FORM_SEC_OFFSET) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_LOW_PC,
                                                 LUNA_DWARF_FORM_ADDR) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_HIGH_PC,
                                                 LUNA_DWARF_FORM_DATA8) &&
        luna_dwarf_append_uleb128(output, 0U) &&
        luna_dwarf_append_uleb128(output, 0U) &&
        /* 子程序。 */
        luna_dwarf_append_uleb128(output, 2U) &&
        luna_dwarf_append_uleb128(output, LUNA_DWARF_TAG_SUBPROGRAM) &&
        luna_dwarf_append_u8(output, LUNA_DWARF_CHILDREN_NO) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_NAME,
                                                 LUNA_DWARF_FORM_STRP) &&
        luna_dwarf_append_abbreviation_attribute(
            output, LUNA_DWARF_AT_DECL_FILE, LUNA_DWARF_FORM_DATA4) &&
        luna_dwarf_append_abbreviation_attribute(
            output, LUNA_DWARF_AT_DECL_LINE, LUNA_DWARF_FORM_DATA4) &&
        luna_dwarf_append_abbreviation_attribute(
            output, LUNA_DWARF_AT_DECL_COLUMN, LUNA_DWARF_FORM_DATA4) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_EXTERNAL,
                                                 LUNA_DWARF_FORM_FLAG) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_LOW_PC,
                                                 LUNA_DWARF_FORM_ADDR) &&
        luna_dwarf_append_abbreviation_attribute(output, LUNA_DWARF_AT_HIGH_PC,
                                                 LUNA_DWARF_FORM_DATA8) &&
        luna_dwarf_append_uleb128(output, 0U) &&
        luna_dwarf_append_uleb128(output, 0U) &&
        luna_dwarf_append_uleb128(output, 0U);
}

static bool luna_dwarf_append_set_address(LunaStringBuilder *output,
                                          uint64_t address) {
    return luna_dwarf_append_u8(output, 0U) &&
           luna_dwarf_append_uleb128(output, 9U) &&
           luna_dwarf_append_u8(output, LUNA_DWARF_LNE_SET_ADDRESS) &&
           luna_dwarf_append_u64(output, address);
}

static bool luna_dwarf_append_end_sequence(LunaStringBuilder *output) {
    return luna_dwarf_append_u8(output, 0U) &&
           luna_dwarf_append_uleb128(output, 1U) &&
           luna_dwarf_append_u8(output, LUNA_DWARF_LNE_END_SEQUENCE);
}

static bool
luna_dwarf_location_in_function(const LunaDebugIrLocation *location,
                                const LunaDebugIrFunction *function) {
    return location != NULL && function != NULL &&
           location->code_offset >= function->code_begin &&
           location->code_offset < function->code_end;
}

static const LunaDebugIrLocation *
luna_dwarf_first_function_location(const LunaDebugIr *debug_ir,
                                   const LunaDebugIrFunction *function) {
    for (size_t index = 0U; index < debug_ir->locations.length; index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        if (luna_dwarf_location_in_function(location, function)) {
            return location;
        }
    }
    return NULL;
}

static bool luna_dwarf_append_line_sequence(
    LunaElfLinkContext *context, const LunaElfLinkSection *text,
    const LunaDebugIr *debug_ir, const LunaDebugIrFunction *function) {
    const LunaDebugIrLocation *first =
        luna_dwarf_first_function_location(debug_ir, function);
    uint64_t address = 0U;
    if (first == NULL ||
        !luna_dwarf_code_address(context, text, first->code_offset, &address) ||
        first->file_id == 0U ||
        !luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_LNS_SET_FILE) ||
        !luna_dwarf_append_uleb128(&context->debug_line,
                                   (uint64_t)first->file_id - 1U) ||
        !luna_dwarf_append_u8(&context->debug_line,
                              LUNA_DWARF_LNS_SET_COLUMN) ||
        !luna_dwarf_append_uleb128(&context->debug_line, first->column) ||
        !luna_dwarf_append_u8(&context->debug_line,
                              LUNA_DWARF_LNS_ADVANCE_LINE) ||
        !luna_dwarf_append_sleb128(&context->debug_line,
                                   (int64_t)first->line - 1) ||
        (!first->is_statement &&
         !luna_dwarf_append_u8(&context->debug_line,
                               LUNA_DWARF_LNS_NEGATE_STMT)) ||
        !luna_dwarf_append_set_address(&context->debug_line, address) ||
        !luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_LNS_COPY)) {
        return false;
    }

    uint64_t previous_offset = first->code_offset;
    uint32_t previous_file = first->file_id;
    uint32_t previous_line = first->line;
    uint32_t previous_column = first->column;
    bool previous_statement = first->is_statement;
    bool passed_first = false;
    for (size_t index = 0U; index < debug_ir->locations.length; index += 1U) {
        const LunaDebugIrLocation *location =
            luna_vector_at_const(&debug_ir->locations, index);
        if (!luna_dwarf_location_in_function(location, function)) {
            continue;
        }
        if (!passed_first) {
            passed_first = true;
            continue;
        }
        if (location->code_offset <= previous_offset ||
            !luna_dwarf_append_u8(&context->debug_line,
                                  LUNA_DWARF_LNS_ADVANCE_PC) ||
            !luna_dwarf_append_uleb128(&context->debug_line,
                                       location->code_offset -
                                           previous_offset)) {
            return false;
        }
        if (location->file_id != previous_file &&
            (!luna_dwarf_append_u8(&context->debug_line,
                                   LUNA_DWARF_LNS_SET_FILE) ||
             !luna_dwarf_append_uleb128(&context->debug_line,
                                        (uint64_t)location->file_id - 1U))) {
            return false;
        }
        const int64_t line_delta =
            (int64_t)location->line - (int64_t)previous_line;
        if (line_delta != 0 &&
            (!luna_dwarf_append_u8(&context->debug_line,
                                   LUNA_DWARF_LNS_ADVANCE_LINE) ||
             !luna_dwarf_append_sleb128(&context->debug_line, line_delta))) {
            return false;
        }
        if (location->column != previous_column &&
            (!luna_dwarf_append_u8(&context->debug_line,
                                   LUNA_DWARF_LNS_SET_COLUMN) ||
             !luna_dwarf_append_uleb128(&context->debug_line,
                                        location->column))) {
            return false;
        }
        if (location->is_statement != previous_statement &&
            !luna_dwarf_append_u8(&context->debug_line,
                                  LUNA_DWARF_LNS_NEGATE_STMT)) {
            return false;
        }
        if (!luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_LNS_COPY)) {
            return false;
        }
        previous_offset = location->code_offset;
        previous_file = location->file_id;
        previous_line = location->line;
        previous_column = location->column;
        previous_statement = location->is_statement;
    }
    if (function->code_end <= previous_offset ||
        !luna_dwarf_append_u8(&context->debug_line,
                              LUNA_DWARF_LNS_ADVANCE_PC) ||
        !luna_dwarf_append_uleb128(&context->debug_line,
                                   function->code_end - previous_offset) ||
        !luna_dwarf_append_end_sequence(&context->debug_line)) {
        return false;
    }
    return true;
}

static bool luna_dwarf_build_line_table(LunaElfLinkContext *context,
                                        const LunaElfLinkObject *object,
                                        const LunaElfLinkSection *text,
                                        uint32_t *statement_list_offset,
                                        uint32_t *primary_file_offset) {
    const LunaDebugIr *debug_ir = &object->debug_ir;
    if (context->debug_line.length > UINT32_MAX ||
        debug_ir->files.length == 0U) {
        return false;
    }
    *statement_list_offset = (uint32_t)context->debug_line.length;

    LunaVector file_offsets;
    luna_vector_init(&file_offsets, sizeof(uint32_t));
    bool success = luna_vector_reserve(&file_offsets, debug_ir->files.length);
    for (size_t index = 0U; success && index < debug_ir->files.length;
         index += 1U) {
        LunaStringView path = {0};
        uint32_t offset = 0U;
        success =
            luna_debug_ir_file_path(debug_ir, (uint32_t)index + 1U, &path) &&
            luna_dwarf_add_string(&context->debug_line_str, path, &offset) &&
            luna_vector_push(&file_offsets, &offset);
    }
    const uint32_t *first_offset = luna_vector_at_const(&file_offsets, 0U);
    if (!success || first_offset == NULL) {
        luna_vector_destroy(&file_offsets);
        return false;
    }
    *primary_file_offset = *first_offset;

    const size_t unit_start = context->debug_line.length;
    const size_t header_length_offset = unit_start + 8U;
    success =
        luna_dwarf_append_u32(&context->debug_line, 0U) &&
        luna_dwarf_append_u16(&context->debug_line, LUNA_DWARF_VERSION) &&
        luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_ADDRESS_SIZE) &&
        luna_dwarf_append_u8(&context->debug_line, 0U) &&
        luna_dwarf_append_u32(&context->debug_line, 0U) &&
        luna_dwarf_append_u8(&context->debug_line, 1U) &&
        luna_dwarf_append_u8(&context->debug_line, 1U) &&
        luna_dwarf_append_u8(&context->debug_line, 1U) &&
        luna_dwarf_append_u8(&context->debug_line,
                             (uint8_t)(int8_t)LUNA_DWARF_LINE_BASE) &&
        luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_LINE_RANGE) &&
        luna_dwarf_append_u8(&context->debug_line, LUNA_DWARF_OPCODE_BASE);
    static const uint8_t standard_opcode_lengths[12] = {
        0U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 1U,
    };
    for (size_t index = 0U; success && index < sizeof(standard_opcode_lengths);
         index += 1U) {
        success = luna_dwarf_append_u8(&context->debug_line,
                                       standard_opcode_lengths[index]);
    }
    success =
        success &&
        /* 目录表声明必需的 path 字段，但本单元不需要目录项。 */
        luna_dwarf_append_u8(&context->debug_line, 1U) &&
        luna_dwarf_append_uleb128(&context->debug_line, LUNA_DWARF_LNCT_PATH) &&
        luna_dwarf_append_uleb128(&context->debug_line,
                                  LUNA_DWARF_FORM_LINE_STRP) &&
        luna_dwarf_append_uleb128(&context->debug_line, 0U) &&
        /* 文件表仅保存 DW_LNCT_path: DW_FORM_line_strp。 */
        luna_dwarf_append_u8(&context->debug_line, 1U) &&
        luna_dwarf_append_uleb128(&context->debug_line, LUNA_DWARF_LNCT_PATH) &&
        luna_dwarf_append_uleb128(&context->debug_line,
                                  LUNA_DWARF_FORM_LINE_STRP) &&
        luna_dwarf_append_uleb128(&context->debug_line, debug_ir->files.length);
    for (size_t index = 0U; success && index < file_offsets.length;
         index += 1U) {
        const uint32_t *offset = luna_vector_at_const(&file_offsets, index);
        success = offset != NULL &&
                  luna_dwarf_append_u32(&context->debug_line, *offset);
    }
    const size_t program_start = context->debug_line.length;
    for (size_t index = 0U; success && index < debug_ir->functions.length;
         index += 1U) {
        const LunaDebugIrFunction *function =
            luna_vector_at_const(&debug_ir->functions, index);
        success = function != NULL && luna_dwarf_append_line_sequence(
                                          context, text, debug_ir, function);
    }
    if (success) {
        const size_t unit_size = context->debug_line.length - unit_start;
        const size_t header_size = program_start - (header_length_offset + 4U);
        success =
            unit_size >= 4U && unit_size - 4U <= UINT32_MAX &&
            header_size <= UINT32_MAX &&
            luna_dwarf_patch_u32(&context->debug_line, unit_start,
                                 (uint32_t)(unit_size - 4U)) &&
            luna_dwarf_patch_u32(&context->debug_line, header_length_offset,
                                 (uint32_t)header_size);
    }
    luna_vector_destroy(&file_offsets);
    return success;
}

static bool luna_dwarf_function_range(LunaElfLinkContext *context,
                                      const LunaElfLinkSection *text,
                                      const LunaDebugIr *debug_ir,
                                      uint64_t *low_pc, uint64_t *high_pc) {
    const LunaDebugIrFunction *first =
        luna_vector_at_const(&debug_ir->functions, 0U);
    const LunaDebugIrFunction *last = luna_vector_at_const(
        &debug_ir->functions, debug_ir->functions.length - 1U);
    return first != NULL && last != NULL &&
           luna_dwarf_code_address(context, text, first->code_begin, low_pc) &&
           luna_dwarf_code_address(context, text, last->code_end, high_pc) &&
           *low_pc < *high_pc;
}

static bool luna_dwarf_build_info_unit(LunaElfLinkContext *context,
                                       const LunaElfLinkObject *object,
                                       const LunaElfLinkSection *text,
                                       uint32_t producer_offset,
                                       uint32_t statement_list_offset,
                                       uint32_t primary_file_offset,
                                       uint32_t comp_dir_offset) {
    const LunaDebugIr *debug_ir = &object->debug_ir;
    uint64_t low_pc = 0U;
    uint64_t high_pc = 0U;
    if (debug_ir->functions.length == 0U ||
        !luna_dwarf_function_range(context, text, debug_ir, &low_pc,
                                   &high_pc)) {
        return false;
    }

    const size_t unit_start = context->debug_info.length;
    bool success =
        luna_dwarf_append_u32(&context->debug_info, 0U) &&
        luna_dwarf_append_u16(&context->debug_info, LUNA_DWARF_VERSION) &&
        luna_dwarf_append_u8(&context->debug_info,
                             LUNA_DWARF_UNIT_TYPE_COMPILE) &&
        luna_dwarf_append_u8(&context->debug_info, LUNA_DWARF_ADDRESS_SIZE) &&
        luna_dwarf_append_u32(&context->debug_info, 0U) &&
        luna_dwarf_append_uleb128(&context->debug_info, 1U) &&
        luna_dwarf_append_u32(&context->debug_info, producer_offset) &&
        luna_dwarf_append_u16(&context->debug_info,
                              LUNA_DWARF_LANGUAGE_LO_USER) &&
        luna_dwarf_append_u32(&context->debug_info, primary_file_offset) &&
        luna_dwarf_append_u32(&context->debug_info, comp_dir_offset) &&
        luna_dwarf_append_u32(&context->debug_info, statement_list_offset) &&
        luna_dwarf_append_u64(&context->debug_info, low_pc) &&
        luna_dwarf_append_u64(&context->debug_info, high_pc - low_pc);
    for (size_t index = 0U; success && index < debug_ir->functions.length;
         index += 1U) {
        const LunaDebugIrFunction *function =
            luna_vector_at_const(&debug_ir->functions, index);
        const LunaDebugIrLocation *declaration =
            luna_dwarf_first_function_location(debug_ir, function);
        LunaStringView name = {0};
        uint32_t name_offset = 0U;
        uint64_t function_address = 0U;
        success =
            function != NULL && declaration != NULL &&
            luna_debug_ir_function_name(debug_ir, function, &name) &&
            luna_dwarf_add_string(&context->debug_str, name, &name_offset) &&
            luna_dwarf_code_address(context, text, function->code_begin,
                                    &function_address) &&
            luna_dwarf_append_uleb128(&context->debug_info, 2U) &&
            luna_dwarf_append_u32(&context->debug_info, name_offset) &&
            luna_dwarf_append_u32(&context->debug_info,
                                  declaration->file_id - 1U) &&
            luna_dwarf_append_u32(&context->debug_info, declaration->line) &&
            luna_dwarf_append_u32(&context->debug_info, declaration->column) &&
            luna_dwarf_append_u8(&context->debug_info,
                                 function->is_external ? 1U : 0U) &&
            luna_dwarf_append_u64(&context->debug_info, function_address) &&
            luna_dwarf_append_u64(&context->debug_info,
                                  function->code_end - function->code_begin);
    }
    success = success && luna_dwarf_append_uleb128(&context->debug_info, 0U);
    if (success) {
        const size_t unit_size = context->debug_info.length - unit_start;
        success = unit_size >= 4U && unit_size - 4U <= UINT32_MAX &&
                  luna_dwarf_patch_u32(&context->debug_info, unit_start,
                                       (uint32_t)(unit_size - 4U));
    }
    return success;
}

bool luna_elf_link_build_debug(LunaElfLinkContext *context) {
    if (context == NULL || context->debug_abbrev.length != 0U ||
        context->debug_info.length != 0U || context->debug_line.length != 0U ||
        context->debug_str.length != 0U ||
        context->debug_line_str.length != 0U) {
        return false;
    }
    bool has_debug = false;
    for (size_t index = 0U; index < context->objects.length; index += 1U) {
        const LunaElfLinkObject *object =
            luna_vector_at_const(&context->objects, index);
        has_debug = has_debug || (object != NULL && object->has_debug_ir &&
                                  object->debug_ir.functions.length > 0U);
    }
    if (!has_debug) {
        return true;
    }

    const LunaStringView producer =
        luna_string_view_from_c_string("Luna bootstrap compiler 0.1.0");
    const LunaStringView comp_dir = luna_string_view_from_c_string(".");
    uint32_t producer_offset = 0U;
    uint32_t comp_dir_offset = 0U;
    if (!luna_dwarf_build_abbreviations(context) ||
        !luna_dwarf_add_string(&context->debug_str, producer,
                               &producer_offset) ||
        !luna_dwarf_add_string(&context->debug_line_str, comp_dir,
                               &comp_dir_offset)) {
        return luna_elf_link_error(context, NULL,
                                   "failed to initialize DWARF 5 sections");
    }

    for (size_t index = 0U; index < context->objects.length; index += 1U) {
        const LunaElfLinkObject *object =
            luna_vector_at_const(&context->objects, index);
        if (object == NULL ||
            !(object->has_debug_ir && object->debug_ir.functions.length > 0U)) {
            continue;
        }
        const LunaElfLinkSection *text = luna_dwarf_object_text_section(object);
        uint32_t statement_list_offset = 0U;
        uint32_t primary_file_offset = 0U;
        if (text == NULL ||
            !luna_dwarf_build_line_table(context, object, text,
                                         &statement_list_offset,
                                         &primary_file_offset) ||
            !luna_dwarf_build_info_unit(context, object, text, producer_offset,
                                        statement_list_offset,
                                        primary_file_offset, comp_dir_offset)) {
            return luna_elf_link_error(context, object,
                                       "failed to build DWARF 5 debug unit");
        }
    }
    return true;
}
