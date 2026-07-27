#include "elf_linker_internal.h"

#include <inttypes.h>
#include <string.h>

enum {
    LUNA_ELF_READER_IDENTITY_SIZE = 16,
    LUNA_ELF_READER_SECTION_COUNT_LIMIT = 4096,
    LUNA_ELF_READER_SECTION_NAME_LIMIT = 4096
};

static bool luna_elf_reader_section_field(const LunaElfLinkObject *object,
                                          uint64_t section_headers,
                                          uint16_t section_index,
                                          uint64_t field_offset,
                                          uint64_t *value) {
    const uint64_t offset =
        section_headers +
        (uint64_t)section_index * LUNA_ELF_LINK_SECTION_HEADER_SIZE +
        field_offset;
    return luna_elf_link_read_u64(object->bytes, offset, value);
}

static bool luna_elf_reader_section_u32(const LunaElfLinkObject *object,
                                        uint64_t section_headers,
                                        uint16_t section_index,
                                        uint64_t field_offset,
                                        uint32_t *value) {
    const uint64_t offset =
        section_headers +
        (uint64_t)section_index * LUNA_ELF_LINK_SECTION_HEADER_SIZE +
        field_offset;
    return luna_elf_link_read_u32(object->bytes, offset, value);
}

static bool luna_elf_reader_classify_section(LunaElfLinkContext *context,
                                             LunaElfLinkObject *object,
                                             LunaElfLinkSection *section,
                                             uint16_t section_index) {
    if ((section->flags & LUNA_ELF_LINK_FLAG_ALLOC) == 0U) {
        section->region = LUNA_ELF_LINK_REGION_NONE;
        return true;
    }
    if ((section->flags &
         (LUNA_ELF_LINK_FLAG_TLS | LUNA_ELF_LINK_FLAG_COMPRESSED)) != 0U) {
        return luna_elf_link_error(context, object,
                                   "section '%.*s' uses unsupported TLS or "
                                   "compressed allocation",
                                   (int)section->name.length,
                                   section->name.data);
    }
    if (section->alignment > LUNA_ELF_LINK_MAX_ALIGNMENT) {
        return luna_elf_link_error(
            context, object,
            "section '%.*s' alignment exceeds the supported page size",
            (int)section->name.length, section->name.data);
    }
    const bool write = (section->flags & LUNA_ELF_LINK_FLAG_WRITE) != 0U;
    const bool execute = (section->flags & LUNA_ELF_LINK_FLAG_EXECUTE) != 0U;
    if (write && execute) {
        return luna_elf_link_error(
            context, object, "section '%.*s' violates W^X",
            (int)section->name.length, section->name.data);
    }
    if (execute) {
        if (section->type != LUNA_ELF_LINK_SECTION_PROGBITS) {
            return luna_elf_link_error(
                context, object, "executable section '%.*s' is not PROGBITS",
                (int)section->name.length, section->name.data);
        }
        section->region = LUNA_ELF_LINK_REGION_TEXT;
        return true;
    }
    if (write) {
        if (section->type == LUNA_ELF_LINK_SECTION_PROGBITS) {
            section->region = LUNA_ELF_LINK_REGION_DATA;
            return true;
        }
        if (section->type == LUNA_ELF_LINK_SECTION_NOBITS) {
            section->region = LUNA_ELF_LINK_REGION_BSS;
            return true;
        }
        return luna_elf_link_error(
            context, object, "writable section '%.*s' has an unsupported type",
            (int)section->name.length, section->name.data);
    }
    if (section->type != LUNA_ELF_LINK_SECTION_PROGBITS) {
        return luna_elf_link_error(
            context, object, "read-only section '%.*s' is not PROGBITS",
            (int)section->name.length, section->name.data);
    }
    section->region = LUNA_ELF_LINK_REGION_RODATA;
    (void)section_index;
    return true;
}

static bool luna_elf_reader_validate_section(LunaElfLinkContext *context,
                                             LunaElfLinkObject *object,
                                             LunaElfLinkSection *section,
                                             uint16_t section_index) {
    if (section_index == 0U) {
        if (section->name.length != 0U ||
            section->type != LUNA_ELF_LINK_SECTION_NULL ||
            section->flags != 0U || section->file_offset != 0U ||
            section->size != 0U || section->link != 0U || section->info != 0U ||
            section->entry_size != 0U) {
            return luna_elf_link_error(context, object, "invalid null section");
        }
        return true;
    }

    if (!luna_elf_link_is_power_of_two(section->alignment)) {
        return luna_elf_link_error(
            context, object, "section '%.*s' has invalid alignment",
            (int)section->name.length, section->name.data);
    }
    if (section->type != LUNA_ELF_LINK_SECTION_NOBITS &&
        !luna_elf_link_range_valid(section->file_offset, section->size,
                                   (uint64_t)object->bytes.length)) {
        return luna_elf_link_error(
            context, object, "section '%.*s' exceeds the input file",
            (int)section->name.length, section->name.data);
    }
    if (section->type == LUNA_ELF_LINK_SECTION_SYMTAB) {
        if (object->symbol_table_index != 0U ||
            section->entry_size != LUNA_ELF_LINK_SYMBOL_SIZE ||
            section->size % LUNA_ELF_LINK_SYMBOL_SIZE != 0U) {
            return luna_elf_link_error(context, object,
                                       "invalid or duplicate symbol table");
        }
        object->symbol_table_index = section_index;
    }
    if (section->type == LUNA_ELF_LINK_SECTION_RELA &&
        (section->entry_size != LUNA_ELF_LINK_RELOCATION_SIZE ||
         section->size % LUNA_ELF_LINK_RELOCATION_SIZE != 0U)) {
        return luna_elf_link_error(
            context, object, "section '%.*s' has invalid RELA entries",
            (int)section->name.length, section->name.data);
    }
    if (section->type == LUNA_ELF_LINK_SECTION_REL) {
        return luna_elf_link_error(context, object,
                                   "REL relocations are unsupported");
    }
    return luna_elf_reader_classify_section(context, object, section,
                                            section_index);
}

static bool luna_elf_reader_read_sections(LunaElfLinkContext *context,
                                          LunaElfLinkObject *object,
                                          uint64_t section_headers,
                                          uint16_t section_count,
                                          uint16_t string_table_index) {
    uint32_t string_table_type = 0U;
    uint64_t string_table_offset = 0U;
    uint64_t string_table_size = 0U;
    if (!luna_elf_reader_section_u32(object, section_headers,
                                     string_table_index, 4U,
                                     &string_table_type) ||
        !luna_elf_reader_section_field(object, section_headers,
                                       string_table_index, 24U,
                                       &string_table_offset) ||
        !luna_elf_reader_section_field(object, section_headers,
                                       string_table_index, 32U,
                                       &string_table_size) ||
        string_table_type != LUNA_ELF_LINK_SECTION_STRTAB ||
        !luna_elf_link_range_valid(string_table_offset, string_table_size,
                                   (uint64_t)object->bytes.length)) {
        return luna_elf_link_error(context, object,
                                   "invalid section-name string table");
    }

    for (uint16_t index = 0U; index < section_count; index += 1U) {
        uint32_t name_offset = 0U;
        uint64_t section_address = 0U;
        LunaElfLinkSection section = {0};
        if (!luna_elf_reader_section_u32(object, section_headers, index, 0U,
                                         &name_offset) ||
            !luna_elf_reader_section_u32(object, section_headers, index, 4U,
                                         &section.type) ||
            !luna_elf_reader_section_field(object, section_headers, index, 8U,
                                           &section.flags) ||
            !luna_elf_reader_section_field(object, section_headers, index, 16U,
                                           &section_address) ||
            !luna_elf_reader_section_field(object, section_headers, index, 24U,
                                           &section.file_offset) ||
            !luna_elf_reader_section_field(object, section_headers, index, 32U,
                                           &section.size) ||
            !luna_elf_reader_section_u32(object, section_headers, index, 40U,
                                         &section.link) ||
            !luna_elf_reader_section_u32(object, section_headers, index, 44U,
                                         &section.info) ||
            !luna_elf_reader_section_field(object, section_headers, index, 48U,
                                           &section.alignment) ||
            !luna_elf_reader_section_field(object, section_headers, index, 56U,
                                           &section.entry_size) ||
            !luna_elf_link_string(object->bytes, string_table_offset,
                                  string_table_size, name_offset,
                                  &section.name)) {
            return luna_elf_link_error(
                context, object, "truncated section header %" PRIu16, index);
        }
        if (section_address != 0U) {
            return luna_elf_link_error(
                context, object,
                "relocatable section '%.*s' has a nonzero address",
                (int)section.name.length, section.name.data);
        }
        if (section.alignment == 0U) {
            section.alignment = 1U;
        }
        if (section.name.length > LUNA_ELF_READER_SECTION_NAME_LIMIT) {
            return luna_elf_link_error(context, object,
                                       "section name is too long");
        }
        if (!luna_elf_reader_validate_section(context, object, &section,
                                              index)) {
            return false;
        }
        if (!luna_vector_push(&object->sections, &section)) {
            return luna_elf_link_error(context, object,
                                       "out of memory while reading sections");
        }
    }
    return true;
}

static bool luna_elf_reader_read_debug_ir(LunaElfLinkContext *context,
                                          LunaElfLinkObject *object) {
    const LunaElfLinkSection *text = NULL;
    const LunaElfLinkSection *encoded_debug = NULL;
    for (size_t index = 1U; index < object->sections.length; index += 1U) {
        const LunaElfLinkSection *section =
            luna_vector_at_const(&object->sections, index);
        if (section == NULL) {
            return luna_elf_link_error(context, object,
                                       "invalid debug section state");
        }
        if (luna_string_view_equal_c_string(section->name, ".text")) {
            if (text != NULL) {
                return luna_elf_link_error(context, object,
                                           "duplicate .text section");
            }
            text = section;
        } else if ((section->name.length >= 7U &&
                    memcmp(section->name.data, ".debug_", 7U) == 0) ||
                   (section->name.length >= 8U &&
                    memcmp(section->name.data, ".zdebug_", 8U) == 0)) {
            return luna_elf_link_error(
                context, object,
                "foreign DWARF section '%.*s' is unsupported; use Luna "
                "Debug IR or remove host debug information",
                (int)section->name.length, section->name.data);
        } else if (luna_string_view_equal_c_string(section->name,
                                                   ".luna.debug")) {
            if (encoded_debug != NULL) {
                return luna_elf_link_error(context, object,
                                           "duplicate .luna.debug section");
            }
            encoded_debug = section;
        }
    }
    if (encoded_debug == NULL) {
        return true;
    }
    if (text == NULL || text->region != LUNA_ELF_LINK_REGION_TEXT ||
        encoded_debug->type != LUNA_ELF_LINK_SECTION_PROGBITS ||
        encoded_debug->flags != 0U || encoded_debug->alignment != 1U) {
        return luna_elf_link_error(context, object,
                                   "invalid .luna.debug section contract");
    }
    LunaDebugIr decoded;
    if (!luna_debug_ir_decode(
            (LunaStringView){
                .data = object->bytes.data + encoded_debug->file_offset,
                .length = (size_t)encoded_debug->size,
            },
            &decoded, context->diagnostic_stream)) {
        return luna_elf_link_error(context, object,
                                   "malformed .luna.debug payload");
    }
    if (!luna_debug_ir_verify(&decoded, text->size,
                              context->diagnostic_stream)) {
        luna_debug_ir_destroy(&decoded);
        return luna_elf_link_error(context, object,
                                   "out-of-range .luna.debug payload");
    }
    luna_debug_ir_destroy(&object->debug_ir);
    object->debug_ir = decoded;
    object->has_debug_ir = true;
    return true;
}

bool luna_elf_link_parse_object(LunaElfLinkContext *context,
                                const LunaX8664ElfLinkInput *input) {
    LunaElfLinkObject object = {
        .name = input->name,
        .bytes = input->object,
    };
    luna_vector_init(&object.sections, sizeof(LunaElfLinkSection));
    luna_debug_ir_init(&object.debug_ir);

    static const uint8_t identity[LUNA_ELF_READER_IDENTITY_SIZE] = {
        0x7fU, 'E', 'L', 'F', 2U, 1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    bool valid_identity =
        object.bytes.length >= sizeof(identity) &&
        memcmp(object.bytes.data, identity, sizeof(identity)) == 0;

    uint16_t type = 0U;
    uint16_t machine = 0U;
    uint32_t version = 0U;
    uint64_t entry = 0U;
    uint64_t program_headers = 0U;
    uint64_t section_headers = 0U;
    uint32_t flags = 0U;
    uint16_t header_size = 0U;
    uint16_t program_entry_size = 0U;
    uint16_t program_count = 0U;
    uint16_t section_entry_size = 0U;
    uint16_t section_count = 0U;
    uint16_t string_table_index = 0U;
    if (!valid_identity || !luna_elf_link_read_u16(object.bytes, 16U, &type) ||
        !luna_elf_link_read_u16(object.bytes, 18U, &machine) ||
        !luna_elf_link_read_u32(object.bytes, 20U, &version) ||
        !luna_elf_link_read_u64(object.bytes, 24U, &entry) ||
        !luna_elf_link_read_u64(object.bytes, 32U, &program_headers) ||
        !luna_elf_link_read_u64(object.bytes, 40U, &section_headers) ||
        !luna_elf_link_read_u32(object.bytes, 48U, &flags) ||
        !luna_elf_link_read_u16(object.bytes, 52U, &header_size) ||
        !luna_elf_link_read_u16(object.bytes, 54U, &program_entry_size) ||
        !luna_elf_link_read_u16(object.bytes, 56U, &program_count) ||
        !luna_elf_link_read_u16(object.bytes, 58U, &section_entry_size) ||
        !luna_elf_link_read_u16(object.bytes, 60U, &section_count) ||
        !luna_elf_link_read_u16(object.bytes, 62U, &string_table_index) ||
        type != 1U || machine != LUNA_ELF_LINK_MACHINE_X86_64 ||
        version != 1U || entry != 0U || program_headers != 0U || flags != 0U ||
        header_size != LUNA_ELF_LINK_HEADER_SIZE || program_entry_size != 0U ||
        program_count != 0U ||
        section_entry_size != LUNA_ELF_LINK_SECTION_HEADER_SIZE ||
        section_count == 0U ||
        section_count > LUNA_ELF_READER_SECTION_COUNT_LIMIT ||
        string_table_index == 0U || string_table_index >= section_count ||
        !luna_elf_link_range_valid(section_headers,
                                   (uint64_t)section_count *
                                       LUNA_ELF_LINK_SECTION_HEADER_SIZE,
                                   (uint64_t)object.bytes.length)) {
        luna_debug_ir_destroy(&object.debug_ir);
        luna_vector_destroy(&object.sections);
        return luna_elf_link_error(context, &object,
                                   "unsupported or malformed ELF64 object");
    }

    if (!luna_elf_reader_read_sections(context, &object, section_headers,
                                       section_count, string_table_index) ||
        !luna_elf_reader_read_debug_ir(context, &object) ||
        object.symbol_table_index == 0U) {
        if (object.symbol_table_index == 0U &&
            object.sections.length == (size_t)section_count) {
            (void)luna_elf_link_error(context, &object,
                                      "object has no symbol table");
        }
        luna_debug_ir_destroy(&object.debug_ir);
        luna_vector_destroy(&object.sections);
        return false;
    }

    const LunaElfLinkSection *symbol_table =
        luna_vector_at_const(&object.sections, object.symbol_table_index);
    const LunaElfLinkSection *symbol_strings =
        symbol_table == NULL
            ? NULL
            : luna_vector_at_const(&object.sections, symbol_table->link);
    if (symbol_table == NULL || symbol_strings == NULL ||
        symbol_strings->type != LUNA_ELF_LINK_SECTION_STRTAB ||
        symbol_table->info == 0U ||
        symbol_table->info > symbol_table->size / LUNA_ELF_LINK_SYMBOL_SIZE) {
        luna_debug_ir_destroy(&object.debug_ir);
        luna_vector_destroy(&object.sections);
        return luna_elf_link_error(context, &object,
                                   "invalid symbol table references");
    }

    if (!luna_vector_push(&context->objects, &object)) {
        luna_debug_ir_destroy(&object.debug_ir);
        luna_vector_destroy(&object.sections);
        return luna_elf_link_error(
            context, NULL, "out of memory while recording input objects");
    }
    return true;
}

bool luna_elf_link_read_symbol(const LunaElfLinkObject *object,
                               uint32_t symbol_index,
                               LunaElfLinkSymbol *symbol) {
    if (object == NULL || symbol == NULL) {
        return false;
    }
    const LunaElfLinkSection *symbol_table =
        luna_vector_at_const(&object->sections, object->symbol_table_index);
    if (symbol_table == NULL ||
        (uint64_t)symbol_index >=
            symbol_table->size / LUNA_ELF_LINK_SYMBOL_SIZE) {
        return false;
    }
    const LunaElfLinkSection *string_table =
        luna_vector_at_const(&object->sections, symbol_table->link);
    if (string_table == NULL) {
        return false;
    }

    const uint64_t offset = symbol_table->file_offset +
                            (uint64_t)symbol_index * LUNA_ELF_LINK_SYMBOL_SIZE;
    uint32_t name_offset = 0U;
    uint8_t info = 0U;
    uint8_t other = 0U;
    if (!luna_elf_link_read_u32(object->bytes, offset, &name_offset) ||
        !luna_elf_link_read_u8(object->bytes, offset + 4U, &info) ||
        !luna_elf_link_read_u8(object->bytes, offset + 5U, &other) ||
        !luna_elf_link_read_u16(object->bytes, offset + 6U,
                                &symbol->section_index) ||
        !luna_elf_link_read_u64(object->bytes, offset + 8U, &symbol->value) ||
        !luna_elf_link_read_u64(object->bytes, offset + 16U, &symbol->size) ||
        !luna_elf_link_string(object->bytes, string_table->file_offset,
                              string_table->size, name_offset, &symbol->name)) {
        return false;
    }
    symbol->binding = info >> 4U;
    symbol->type = info & UINT8_C(0x0f);
    symbol->visibility = other;
    return true;
}
