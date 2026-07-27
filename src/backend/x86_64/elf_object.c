#include "elf_object_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum {
    LUNA_ELF_HEADER_SIZE = 64,
    LUNA_ELF_SECTION_HEADER_SIZE = 64,
    LUNA_ELF_SYMBOL_SIZE = 24,
    LUNA_ELF_RELOCATION_SIZE = 24,
    LUNA_ELF_MACHINE_X86_64 = 62,
    LUNA_ELF_SECTION_NAME_LIMIT = 16
};

enum {
    LUNA_ELF_SECTION_NULL = 0,
    LUNA_ELF_SECTION_PROGBITS = 1,
    LUNA_ELF_SECTION_SYMTAB = 2,
    LUNA_ELF_SECTION_STRTAB = 3,
    LUNA_ELF_SECTION_RELA = 4
};

enum {
    LUNA_ELF_FLAG_WRITE = 1,
    LUNA_ELF_FLAG_ALLOC = 2,
    LUNA_ELF_FLAG_EXECUTE = 4
};

enum {
    LUNA_ELF_SYMBOL_LOCAL = 0,
    LUNA_ELF_SYMBOL_GLOBAL = 1,
    LUNA_ELF_SYMBOL_NOTYPE = 0,
    LUNA_ELF_SYMBOL_OBJECT = 1,
    LUNA_ELF_SYMBOL_FUNCTION = 2,
    LUNA_ELF_SYMBOL_SECTION = 3
};

typedef struct LunaElfSection {
    const char *name;
    uint32_t name_offset;
    uint32_t type;
    uint64_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
    const LunaStringBuilder *content;
} LunaElfSection;

static bool luna_elf_append_u8(LunaStringBuilder *output, uint8_t value) {
    const char byte = (char)value;
    return luna_string_builder_append(output, &byte, 1U);
}

static bool luna_elf_append_u16(LunaStringBuilder *output, uint16_t value) {
    const char bytes[2] = {
        (char)(value & UINT16_C(0xff)),
        (char)((value >> 8U) & UINT16_C(0xff)),
    };
    return luna_string_builder_append(output, bytes, sizeof(bytes));
}

static bool luna_elf_append_u32(LunaStringBuilder *output, uint32_t value) {
    const char bytes[4] = {
        (char)(value & UINT32_C(0xff)),
        (char)((value >> 8U) & UINT32_C(0xff)),
        (char)((value >> 16U) & UINT32_C(0xff)),
        (char)((value >> 24U) & UINT32_C(0xff)),
    };
    return luna_string_builder_append(output, bytes, sizeof(bytes));
}

static bool luna_elf_append_u64(LunaStringBuilder *output, uint64_t value) {
    const char bytes[8] = {
        (char)(value & UINT64_C(0xff)),
        (char)((value >> 8U) & UINT64_C(0xff)),
        (char)((value >> 16U) & UINT64_C(0xff)),
        (char)((value >> 24U) & UINT64_C(0xff)),
        (char)((value >> 32U) & UINT64_C(0xff)),
        (char)((value >> 40U) & UINT64_C(0xff)),
        (char)((value >> 48U) & UINT64_C(0xff)),
        (char)((value >> 56U) & UINT64_C(0xff)),
    };
    return luna_string_builder_append(output, bytes, sizeof(bytes));
}

static bool luna_elf_append_i64(LunaStringBuilder *output, int64_t value) {
    uint64_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return luna_elf_append_u64(output, bits);
}

static bool luna_elf_append_zeros(LunaStringBuilder *output, uint64_t count) {
    static const char zeros[64] = {0};
    while (count > 0U) {
        const size_t chunk =
            count > (uint64_t)sizeof(zeros) ? sizeof(zeros) : (size_t)count;
        if (!luna_string_builder_append(output, zeros, chunk)) {
            return false;
        }
        count -= (uint64_t)chunk;
    }
    return true;
}

static bool luna_elf_is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool luna_elf_align_up(uint64_t value, uint64_t alignment,
                              uint64_t *result) {
    if (!luna_elf_is_power_of_two(alignment)) {
        return false;
    }
    const uint64_t mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static bool luna_elf_append_padding_to(LunaStringBuilder *output,
                                       uint64_t offset) {
    if ((uint64_t)output->length > offset) {
        return false;
    }
    return luna_elf_append_zeros(output, offset - (uint64_t)output->length);
}

static bool luna_elf_report(LunaDiagnosticEngine *diagnostics,
                            const char *format, ...) LUNA_PRINTF_LIKE(2, 3);

static bool luna_elf_report(LunaDiagnosticEngine *diagnostics,
                            const char *format, ...) {
    if (diagnostics == NULL) {
        return false;
    }
    va_list arguments;
    va_start(arguments, format);
    (void)fputs("error: ", diagnostics->stream);
    (void)vfprintf(diagnostics->stream, format, arguments);
    (void)fputc('\n', diagnostics->stream);
    va_end(arguments);
    diagnostics->error_count += 1U;
    return false;
}

void luna_x86_64_object_image_init(LunaX8664ObjectImage *image) {
    if (image == NULL) {
        return;
    }
    luna_string_builder_init(&image->text);
    luna_string_builder_init(&image->rodata);
    luna_string_builder_init(&image->data);
    luna_vector_init(&image->symbols, sizeof(LunaX8664ObjectSymbol));
    luna_vector_init(&image->relocations, sizeof(LunaX8664ObjectRelocation));
    image->text_alignment = 16U;
    image->rodata_alignment = 1U;
    image->data_alignment = 1U;
}

void luna_x86_64_object_image_destroy(LunaX8664ObjectImage *image) {
    if (image == NULL) {
        return;
    }
    luna_vector_destroy(&image->relocations);
    luna_vector_destroy(&image->symbols);
    luna_string_builder_destroy(&image->data);
    luna_string_builder_destroy(&image->rodata);
    luna_string_builder_destroy(&image->text);
}

LunaStringBuilder *
luna_x86_64_object_section_builder(LunaX8664ObjectImage *image,
                                   LunaX8664ObjectSection section) {
    if (image == NULL) {
        return NULL;
    }
    switch (section) {
    case LUNA_X86_64_OBJECT_SECTION_TEXT:
        return &image->text;
    case LUNA_X86_64_OBJECT_SECTION_RODATA:
        return &image->rodata;
    case LUNA_X86_64_OBJECT_SECTION_DATA:
        return &image->data;
    case LUNA_X86_64_OBJECT_SECTION_UNDEFINED:
        return NULL;
    }
    return NULL;
}

const LunaStringBuilder *
luna_x86_64_object_section_builder_const(const LunaX8664ObjectImage *image,
                                         LunaX8664ObjectSection section) {
    if (image == NULL) {
        return NULL;
    }
    switch (section) {
    case LUNA_X86_64_OBJECT_SECTION_TEXT:
        return &image->text;
    case LUNA_X86_64_OBJECT_SECTION_RODATA:
        return &image->rodata;
    case LUNA_X86_64_OBJECT_SECTION_DATA:
        return &image->data;
    case LUNA_X86_64_OBJECT_SECTION_UNDEFINED:
        return NULL;
    }
    return NULL;
}

static bool luna_elf_string_table_add(LunaStringBuilder *table,
                                      LunaStringView value, uint32_t *offset) {
    if (table == NULL || offset == NULL || table->length > (size_t)UINT32_MAX ||
        value.length > (size_t)UINT32_MAX - table->length) {
        return false;
    }
    *offset = (uint32_t)table->length;
    return luna_string_builder_append_view(table, value) &&
           luna_elf_append_u8(table, 0U);
}

static uint8_t luna_elf_symbol_type(const LunaX8664ObjectSymbol *symbol) {
    switch (symbol->type) {
    case LUNA_X86_64_OBJECT_SYMBOL_OBJECT:
        return LUNA_ELF_SYMBOL_OBJECT;
    case LUNA_X86_64_OBJECT_SYMBOL_FUNCTION:
        return LUNA_ELF_SYMBOL_FUNCTION;
    case LUNA_X86_64_OBJECT_SYMBOL_NONE:
        return LUNA_ELF_SYMBOL_NOTYPE;
    }
    return LUNA_ELF_SYMBOL_NOTYPE;
}

static bool luna_elf_append_symbol(LunaStringBuilder *symtab,
                                   uint32_t name_offset, uint8_t info,
                                   uint16_t section_index, uint64_t value,
                                   uint64_t size) {
    return luna_elf_append_u32(symtab, name_offset) &&
           luna_elf_append_u8(symtab, info) && luna_elf_append_u8(symtab, 0U) &&
           luna_elf_append_u16(symtab, section_index) &&
           luna_elf_append_u64(symtab, value) &&
           luna_elf_append_u64(symtab, size);
}

static uint16_t luna_elf_image_section_index(LunaX8664ObjectSection section,
                                             uint16_t text_index,
                                             uint16_t rodata_index,
                                             uint16_t data_index) {
    switch (section) {
    case LUNA_X86_64_OBJECT_SECTION_TEXT:
        return text_index;
    case LUNA_X86_64_OBJECT_SECTION_RODATA:
        return rodata_index;
    case LUNA_X86_64_OBJECT_SECTION_DATA:
        return data_index;
    case LUNA_X86_64_OBJECT_SECTION_UNDEFINED:
        return 0U;
    }
    return 0U;
}

static bool luna_elf_validate_image(const LunaX8664ObjectImage *image,
                                    LunaDiagnosticEngine *diagnostics) {
    if (image == NULL || diagnostics == NULL ||
        !luna_elf_is_power_of_two(image->text_alignment) ||
        !luna_elf_is_power_of_two(image->rodata_alignment) ||
        !luna_elf_is_power_of_two(image->data_alignment)) {
        return luna_elf_report(diagnostics,
                               "invalid x86-64 object image state");
    }
    for (size_t index = 0U; index < image->symbols.length; index += 1U) {
        const LunaX8664ObjectSymbol *symbol =
            luna_vector_at_const(&image->symbols, index);
        const LunaStringBuilder *section =
            symbol == NULL ? NULL
                           : luna_x86_64_object_section_builder_const(
                                 image, symbol->section);
        if (symbol == NULL || symbol->name.length == 0U ||
            (symbol->defined &&
             (section == NULL || symbol->value > (uint64_t)section->length ||
              symbol->size > (uint64_t)section->length - symbol->value)) ||
            (!symbol->defined &&
             symbol->section != LUNA_X86_64_OBJECT_SECTION_UNDEFINED) ||
            (symbol->external && symbol->defined)) {
            return luna_elf_report(diagnostics,
                                   "invalid x86-64 object symbol at index %zu",
                                   index);
        }
    }
    for (size_t index = 0U; index < image->relocations.length; index += 1U) {
        const LunaX8664ObjectRelocation *relocation =
            luna_vector_at_const(&image->relocations, index);
        const LunaStringBuilder *section =
            relocation == NULL ? NULL
                               : luna_x86_64_object_section_builder_const(
                                     image, relocation->section);
        if (relocation == NULL ||
            relocation->section != LUNA_X86_64_OBJECT_SECTION_TEXT ||
            section == NULL || relocation->offset > (uint64_t)section->length ||
            (uint64_t)section->length - relocation->offset < 4U ||
            relocation->symbol_index >= image->symbols.length ||
            (relocation->type != LUNA_X86_64_OBJECT_RELOCATION_PC32 &&
             relocation->type != LUNA_X86_64_OBJECT_RELOCATION_PLT32)) {
            return luna_elf_report(
                diagnostics, "invalid x86-64 relocation at index %zu", index);
        }
    }
    return true;
}

static bool luna_elf_add_section(LunaElfSection *sections,
                                 uint16_t *section_count, const char *name,
                                 uint32_t type, uint64_t flags,
                                 uint64_t alignment, uint64_t entry_size,
                                 const LunaStringBuilder *content,
                                 uint16_t *index) {
    if (*section_count >= LUNA_ELF_SECTION_NAME_LIMIT || name == NULL ||
        !luna_elf_is_power_of_two(alignment)) {
        return false;
    }
    const uint16_t current = *section_count;
    sections[current] = (LunaElfSection){
        .name = name,
        .name_offset = 0U,
        .type = type,
        .flags = flags,
        .offset = 0U,
        .size = content == NULL ? 0U : (uint64_t)content->length,
        .link = 0U,
        .info = 0U,
        .alignment = alignment,
        .entry_size = entry_size,
        .content = content,
    };
    *section_count = (uint16_t)(current + 1U);
    if (index != NULL) {
        *index = current;
    }
    return true;
}

static bool luna_elf_image_uses_section(const LunaX8664ObjectImage *image,
                                        LunaX8664ObjectSection section) {
    const LunaStringBuilder *content =
        luna_x86_64_object_section_builder_const(image, section);
    if (content != NULL && content->length > 0U) {
        return true;
    }
    for (size_t index = 0U; index < image->symbols.length; index += 1U) {
        const LunaX8664ObjectSymbol *symbol =
            luna_vector_at_const(&image->symbols, index);
        if (symbol != NULL && symbol->defined && symbol->section == section) {
            return true;
        }
    }
    return false;
}

static bool
luna_elf_build_symbol_tables(const LunaX8664ObjectImage *image,
                             uint16_t text_index, uint16_t rodata_index,
                             uint16_t data_index, LunaStringBuilder *strtab,
                             LunaStringBuilder *symtab, LunaVector *symbol_map,
                             uint32_t *first_global_symbol) {
    if (!luna_elf_append_u8(strtab, 0U) ||
        !luna_elf_append_symbol(symtab, 0U, 0U, 0U, 0U, 0U)) {
        return false;
    }
    for (size_t index = 0U; index < image->symbols.length; index += 1U) {
        const uint32_t zero = 0U;
        if (!luna_vector_push(symbol_map, &zero)) {
            return false;
        }
    }

    uint32_t symbol_count = 1U;
    const uint16_t section_indices[] = {
        text_index,
        rodata_index,
        data_index,
    };
    for (size_t index = 0U;
         index < sizeof(section_indices) / sizeof(section_indices[0]);
         index += 1U) {
        if (section_indices[index] == 0U ||
            !luna_elf_append_symbol(symtab, 0U,
                                    (uint8_t)((LUNA_ELF_SYMBOL_LOCAL << 4U) |
                                              LUNA_ELF_SYMBOL_SECTION),
                                    section_indices[index], 0U, 0U)) {
            if (section_indices[index] != 0U) {
                return false;
            }
            continue;
        }
        symbol_count += 1U;
    }

    for (uint32_t pass = 0U; pass < 2U; pass += 1U) {
        const bool global_pass = pass != 0U;
        if (global_pass) {
            *first_global_symbol = symbol_count;
        }
        for (size_t index = 0U; index < image->symbols.length; index += 1U) {
            const LunaX8664ObjectSymbol *symbol =
                luna_vector_at_const(&image->symbols, index);
            if (symbol == NULL || symbol->global != global_pass ||
                symbol_count == UINT32_MAX) {
                if (symbol == NULL || symbol_count == UINT32_MAX) {
                    return false;
                }
                continue;
            }
            uint32_t name_offset = 0U;
            if (!luna_elf_string_table_add(strtab, symbol->name,
                                           &name_offset)) {
                return false;
            }
            const uint8_t binding =
                symbol->global ? LUNA_ELF_SYMBOL_GLOBAL : LUNA_ELF_SYMBOL_LOCAL;
            const uint8_t info =
                (uint8_t)((binding << 4U) | luna_elf_symbol_type(symbol));
            const uint16_t section_index =
                symbol->defined
                    ? luna_elf_image_section_index(symbol->section, text_index,
                                                   rodata_index, data_index)
                    : 0U;
            if (!luna_elf_append_symbol(symtab, name_offset, info,
                                        section_index, symbol->value,
                                        symbol->size)) {
                return false;
            }
            uint32_t *mapped = luna_vector_at(symbol_map, index);
            if (mapped == NULL) {
                return false;
            }
            *mapped = symbol_count;
            symbol_count += 1U;
        }
    }
    return true;
}

static bool luna_elf_build_relocations(const LunaX8664ObjectImage *image,
                                       const LunaVector *symbol_map,
                                       LunaStringBuilder *relocations) {
    for (size_t index = 0U; index < image->relocations.length; index += 1U) {
        const LunaX8664ObjectRelocation *relocation =
            luna_vector_at_const(&image->relocations, index);
        const uint32_t *symbol =
            relocation == NULL
                ? NULL
                : luna_vector_at_const(symbol_map, relocation->symbol_index);
        if (relocation == NULL || symbol == NULL || *symbol == 0U ||
            !luna_elf_append_u64(relocations, relocation->offset) ||
            !luna_elf_append_u64(relocations, ((uint64_t)*symbol << 32U) |
                                                  (uint64_t)relocation->type) ||
            !luna_elf_append_i64(relocations, relocation->addend)) {
            return false;
        }
    }
    return true;
}

static bool luna_elf_layout_sections(LunaElfSection *sections,
                                     uint16_t section_count,
                                     uint64_t *section_header_offset) {
    uint64_t offset = LUNA_ELF_HEADER_SIZE;
    for (uint16_t index = 1U; index < section_count; index += 1U) {
        if (!luna_elf_align_up(offset, sections[index].alignment, &offset)) {
            return false;
        }
        sections[index].offset = offset;
        if (sections[index].size > UINT64_MAX - offset) {
            return false;
        }
        offset += sections[index].size;
    }
    return luna_elf_align_up(offset, 8U, section_header_offset);
}

static bool luna_elf_write_header(LunaStringBuilder *output,
                                  uint64_t section_header_offset,
                                  uint16_t section_count,
                                  uint16_t shstrtab_index) {
    static const char identity[16] = {
        0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return luna_string_builder_append(output, identity, sizeof(identity)) &&
           luna_elf_append_u16(output, 1U) &&
           luna_elf_append_u16(output, LUNA_ELF_MACHINE_X86_64) &&
           luna_elf_append_u32(output, 1U) && luna_elf_append_u64(output, 0U) &&
           luna_elf_append_u64(output, 0U) &&
           luna_elf_append_u64(output, section_header_offset) &&
           luna_elf_append_u32(output, 0U) &&
           luna_elf_append_u16(output, LUNA_ELF_HEADER_SIZE) &&
           luna_elf_append_u16(output, 0U) && luna_elf_append_u16(output, 0U) &&
           luna_elf_append_u16(output, LUNA_ELF_SECTION_HEADER_SIZE) &&
           luna_elf_append_u16(output, section_count) &&
           luna_elf_append_u16(output, shstrtab_index);
}

static bool luna_elf_write_section_header(LunaStringBuilder *output,
                                          const LunaElfSection *section) {
    return luna_elf_append_u32(output, section->name_offset) &&
           luna_elf_append_u32(output, section->type) &&
           luna_elf_append_u64(output, section->flags) &&
           luna_elf_append_u64(output, 0U) &&
           luna_elf_append_u64(output, section->offset) &&
           luna_elf_append_u64(output, section->size) &&
           luna_elf_append_u32(output, section->link) &&
           luna_elf_append_u32(output, section->info) &&
           luna_elf_append_u64(output, section->alignment) &&
           luna_elf_append_u64(output, section->entry_size);
}

bool luna_x86_64_elf_object_serialize(const LunaX8664ObjectImage *image,
                                      LunaDiagnosticEngine *diagnostics,
                                      LunaStringBuilder *output) {
    if (output == NULL || output->length != 0U ||
        !luna_elf_validate_image(image, diagnostics)) {
        return false;
    }

    LunaStringBuilder strtab;
    LunaStringBuilder symtab;
    LunaStringBuilder rela_text;
    LunaStringBuilder shstrtab;
    LunaVector symbol_map;
    luna_string_builder_init(&strtab);
    luna_string_builder_init(&symtab);
    luna_string_builder_init(&rela_text);
    luna_string_builder_init(&shstrtab);
    luna_vector_init(&symbol_map, sizeof(uint32_t));

    LunaElfSection sections[LUNA_ELF_SECTION_NAME_LIMIT] = {{0}};
    uint16_t section_count = 1U;
    uint16_t text_index = 0U;
    uint16_t rodata_index = 0U;
    uint16_t data_index = 0U;
    uint16_t rela_text_index = 0U;
    uint16_t symtab_index = 0U;
    uint16_t strtab_index = 0U;
    uint16_t shstrtab_index = 0U;
    uint16_t note_index = 0U;
    uint32_t first_global_symbol = 0U;

    bool success = luna_elf_add_section(
        sections, &section_count, ".text", LUNA_ELF_SECTION_PROGBITS,
        LUNA_ELF_FLAG_ALLOC | LUNA_ELF_FLAG_EXECUTE, image->text_alignment, 0U,
        &image->text, &text_index);
    if (success &&
        luna_elf_image_uses_section(image, LUNA_X86_64_OBJECT_SECTION_RODATA)) {
        success = luna_elf_add_section(
            sections, &section_count, ".rodata", LUNA_ELF_SECTION_PROGBITS,
            LUNA_ELF_FLAG_ALLOC, image->rodata_alignment, 0U, &image->rodata,
            &rodata_index);
    }
    if (success &&
        luna_elf_image_uses_section(image, LUNA_X86_64_OBJECT_SECTION_DATA)) {
        success = luna_elf_add_section(
            sections, &section_count, ".data", LUNA_ELF_SECTION_PROGBITS,
            LUNA_ELF_FLAG_ALLOC | LUNA_ELF_FLAG_WRITE, image->data_alignment,
            0U, &image->data, &data_index);
    }
    if (success) {
        success = luna_elf_build_symbol_tables(
            image, text_index, rodata_index, data_index, &strtab, &symtab,
            &symbol_map, &first_global_symbol);
    }
    if (success) {
        success = luna_elf_build_relocations(image, &symbol_map, &rela_text);
    }
    if (success && rela_text.length > 0U) {
        success = luna_elf_add_section(
            sections, &section_count, ".rela.text", LUNA_ELF_SECTION_RELA, 0U,
            8U, LUNA_ELF_RELOCATION_SIZE, &rela_text, &rela_text_index);
    }
    if (success) {
        success = luna_elf_add_section(
            sections, &section_count, ".symtab", LUNA_ELF_SECTION_SYMTAB, 0U,
            8U, LUNA_ELF_SYMBOL_SIZE, &symtab, &symtab_index);
    }
    if (success) {
        success = luna_elf_add_section(sections, &section_count, ".strtab",
                                       LUNA_ELF_SECTION_STRTAB, 0U, 1U, 0U,
                                       &strtab, &strtab_index);
    }
    if (success) {
        success = luna_elf_add_section(
            sections, &section_count, ".note.GNU-stack",
            LUNA_ELF_SECTION_PROGBITS, 0U, 1U, 0U, NULL, &note_index);
    }
    if (success) {
        success = luna_elf_add_section(sections, &section_count, ".shstrtab",
                                       LUNA_ELF_SECTION_STRTAB, 0U, 1U, 0U,
                                       &shstrtab, &shstrtab_index);
    }

    if (success && !luna_elf_append_u8(&shstrtab, 0U)) {
        success = false;
    }
    for (uint16_t index = 1U; success && index < section_count; index += 1U) {
        success = luna_elf_string_table_add(
            &shstrtab, luna_string_view_from_c_string(sections[index].name),
            &sections[index].name_offset);
    }
    if (success) {
        sections[shstrtab_index].size = (uint64_t)shstrtab.length;
        if (rela_text_index != 0U) {
            sections[rela_text_index].link = symtab_index;
            sections[rela_text_index].info = text_index;
        }
        sections[symtab_index].link = strtab_index;
        sections[symtab_index].info = first_global_symbol;
    }

    uint64_t section_header_offset = 0U;
    if (success) {
        success = luna_elf_layout_sections(sections, section_count,
                                           &section_header_offset) &&
                  luna_elf_write_header(output, section_header_offset,
                                        section_count, shstrtab_index);
    }
    for (uint16_t index = 1U; success && index < section_count; index += 1U) {
        success = luna_elf_append_padding_to(output, sections[index].offset);
        if (success && sections[index].content != NULL) {
            success = luna_string_builder_append(
                output, luna_string_builder_data(sections[index].content),
                sections[index].content->length);
        }
    }
    if (success) {
        success = luna_elf_append_padding_to(output, section_header_offset);
    }
    for (uint16_t index = 0U; success && index < section_count; index += 1U) {
        success = luna_elf_write_section_header(output, &sections[index]);
    }
    if (!success) {
        (void)luna_elf_report(
            diagnostics,
            "out of memory or size overflow while serializing ELF64 object");
    }

    luna_vector_destroy(&symbol_map);
    luna_string_builder_destroy(&shstrtab);
    luna_string_builder_destroy(&rela_text);
    luna_string_builder_destroy(&symtab);
    luna_string_builder_destroy(&strtab);
    return success;
}

static bool luna_elf_verify_error(FILE *stream, const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);

static bool luna_elf_verify_error(FILE *stream, const char *format, ...) {
    if (stream != NULL) {
        va_list arguments;
        va_start(arguments, format);
        (void)fputs("ELF verification error: ", stream);
        (void)vfprintf(stream, format, arguments);
        (void)fputc('\n', stream);
        va_end(arguments);
    }
    return false;
}

static bool luna_elf_range_valid(uint64_t offset, uint64_t size,
                                 size_t length) {
    return offset <= (uint64_t)length && size <= (uint64_t)length - offset;
}

static bool luna_elf_read_u16(LunaStringView object, uint64_t offset,
                              uint16_t *value) {
    if (!luna_elf_range_valid(offset, 2U, object.length)) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)object.data + (size_t)offset;
    *value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
    return true;
}

static bool luna_elf_read_u32(LunaStringView object, uint64_t offset,
                              uint32_t *value) {
    if (!luna_elf_range_valid(offset, 4U, object.length)) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)object.data + (size_t)offset;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
             ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    return true;
}

static bool luna_elf_read_u64(LunaStringView object, uint64_t offset,
                              uint64_t *value) {
    if (!luna_elf_range_valid(offset, 8U, object.length)) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)object.data + (size_t)offset;
    *value = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8U) |
             ((uint64_t)bytes[2] << 16U) | ((uint64_t)bytes[3] << 24U) |
             ((uint64_t)bytes[4] << 32U) | ((uint64_t)bytes[5] << 40U) |
             ((uint64_t)bytes[6] << 48U) | ((uint64_t)bytes[7] << 56U);
    return true;
}

static bool luna_elf_string_valid(LunaStringView object, uint64_t table_offset,
                                  uint64_t table_size, uint32_t name_offset) {
    if ((uint64_t)name_offset >= table_size ||
        !luna_elf_range_valid(table_offset, table_size, object.length)) {
        return false;
    }
    const uint64_t start = table_offset + (uint64_t)name_offset;
    const uint64_t end = table_offset + table_size;
    for (uint64_t index = start; index < end; index += 1U) {
        if (object.data[(size_t)index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool luna_elf_read_section_field(LunaStringView object,
                                        uint64_t section_headers,
                                        uint16_t section_index,
                                        uint64_t field_offset,
                                        uint64_t *value) {
    const uint64_t offset =
        section_headers +
        (uint64_t)section_index * LUNA_ELF_SECTION_HEADER_SIZE + field_offset;
    return luna_elf_read_u64(object, offset, value);
}

static bool luna_elf_read_section_u32(LunaStringView object,
                                      uint64_t section_headers,
                                      uint16_t section_index,
                                      uint64_t field_offset, uint32_t *value) {
    const uint64_t offset =
        section_headers +
        (uint64_t)section_index * LUNA_ELF_SECTION_HEADER_SIZE + field_offset;
    return luna_elf_read_u32(object, offset, value);
}

static bool luna_elf_verify_symbols(LunaStringView object,
                                    uint64_t section_headers,
                                    uint16_t section_count,
                                    uint64_t symtab_offset,
                                    uint64_t symtab_size, uint32_t strtab_index,
                                    uint32_t first_global, FILE *stream) {
    uint64_t strtab_offset = 0U;
    uint64_t strtab_size = 0U;
    uint32_t strtab_type = 0U;
    if (strtab_index >= section_count ||
        !luna_elf_read_section_u32(object, section_headers,
                                   (uint16_t)strtab_index, 4U, &strtab_type) ||
        !luna_elf_read_section_field(object, section_headers,
                                     (uint16_t)strtab_index, 24U,
                                     &strtab_offset) ||
        !luna_elf_read_section_field(object, section_headers,
                                     (uint16_t)strtab_index, 32U,
                                     &strtab_size) ||
        strtab_type != LUNA_ELF_SECTION_STRTAB ||
        !luna_elf_range_valid(strtab_offset, strtab_size, object.length) ||
        strtab_size == 0U || object.data[(size_t)strtab_offset] != '\0' ||
        symtab_size % LUNA_ELF_SYMBOL_SIZE != 0U) {
        return luna_elf_verify_error(stream, "invalid symbol string table");
    }
    const uint64_t symbol_count = symtab_size / LUNA_ELF_SYMBOL_SIZE;
    if (symbol_count == 0U || (uint64_t)first_global > symbol_count) {
        return luna_elf_verify_error(stream,
                                     "invalid symbol count or local boundary");
    }
    for (uint64_t index = 0U; index < symbol_count; index += 1U) {
        const uint64_t offset = symtab_offset + index * LUNA_ELF_SYMBOL_SIZE;
        uint32_t name_offset = 0U;
        uint16_t section_index = 0U;
        uint64_t value = 0U;
        uint64_t size = 0U;
        if (!luna_elf_read_u32(object, offset, &name_offset) ||
            !luna_elf_read_u16(object, offset + 6U, &section_index) ||
            !luna_elf_read_u64(object, offset + 8U, &value) ||
            !luna_elf_read_u64(object, offset + 16U, &size) ||
            !luna_elf_string_valid(object, strtab_offset, strtab_size,
                                   name_offset)) {
            return luna_elf_verify_error(stream, "invalid symbol %" PRIu64,
                                         index);
        }
        const uint8_t info = (uint8_t)object.data[(size_t)(offset + 4U)];
        const uint8_t other = (uint8_t)object.data[(size_t)(offset + 5U)];
        const uint8_t binding = (uint8_t)(info >> 4U);
        const uint8_t type = (uint8_t)(info & UINT8_C(0x0f));
        if (other != 0U || binding > LUNA_ELF_SYMBOL_GLOBAL ||
            type > LUNA_ELF_SYMBOL_SECTION ||
            (index < first_global && binding != LUNA_ELF_SYMBOL_LOCAL) ||
            (index >= first_global && binding == LUNA_ELF_SYMBOL_LOCAL) ||
            section_index >= section_count) {
            return luna_elf_verify_error(
                stream, "invalid binding or section for symbol %" PRIu64,
                index);
        }
        if (index == 0U && (name_offset != 0U || info != 0U ||
                            section_index != 0U || value != 0U || size != 0U)) {
            return luna_elf_verify_error(stream, "invalid null symbol");
        }
        if (index != 0U && section_index == 0U &&
            (binding != LUNA_ELF_SYMBOL_GLOBAL || value != 0U || size != 0U)) {
            return luna_elf_verify_error(stream, "invalid undefined symbol");
        }
        if (type == LUNA_ELF_SYMBOL_SECTION &&
            (binding != LUNA_ELF_SYMBOL_LOCAL || name_offset != 0U ||
             section_index == 0U || value != 0U || size != 0U)) {
            return luna_elf_verify_error(stream, "invalid section symbol");
        }
        if (section_index != 0U) {
            uint64_t section_size = 0U;
            if (!luna_elf_read_section_field(object, section_headers,
                                             section_index, 32U,
                                             &section_size) ||
                value > section_size || size > section_size - value) {
                return luna_elf_verify_error(
                    stream, "out-of-range symbol %" PRIu64, index);
            }
        }
    }
    return true;
}

static bool
luna_elf_verify_relocations(LunaStringView object, uint64_t section_headers,
                            uint16_t section_count, uint16_t relocation_index,
                            uint64_t relocation_offset,
                            uint64_t relocation_size, uint32_t symtab_index,
                            uint32_t target_index, FILE *stream) {
    uint64_t target_size = 0U;
    uint64_t target_flags = 0U;
    uint64_t symtab_size = 0U;
    uint32_t symtab_type = 0U;
    uint32_t target_type = 0U;
    if (symtab_index >= section_count || target_index >= section_count ||
        !luna_elf_read_section_u32(object, section_headers,
                                   (uint16_t)symtab_index, 4U, &symtab_type) ||
        !luna_elf_read_section_u32(object, section_headers,
                                   (uint16_t)target_index, 4U, &target_type) ||
        !luna_elf_read_section_field(object, section_headers,
                                     (uint16_t)target_index, 32U,
                                     &target_size) ||
        !luna_elf_read_section_field(object, section_headers,
                                     (uint16_t)target_index, 8U,
                                     &target_flags) ||
        !luna_elf_read_section_field(object, section_headers,
                                     (uint16_t)symtab_index, 32U,
                                     &symtab_size) ||
        symtab_type != LUNA_ELF_SECTION_SYMTAB ||
        target_type != LUNA_ELF_SECTION_PROGBITS ||
        target_flags != (LUNA_ELF_FLAG_ALLOC | LUNA_ELF_FLAG_EXECUTE) ||
        relocation_size % LUNA_ELF_RELOCATION_SIZE != 0U ||
        symtab_size % LUNA_ELF_SYMBOL_SIZE != 0U) {
        return luna_elf_verify_error(stream,
                                     "invalid relocation cross reference");
    }
    const uint64_t symbol_count = symtab_size / LUNA_ELF_SYMBOL_SIZE;
    const uint64_t relocation_count =
        relocation_size / LUNA_ELF_RELOCATION_SIZE;
    for (uint64_t index = 0U; index < relocation_count; index += 1U) {
        const uint64_t offset =
            relocation_offset + index * LUNA_ELF_RELOCATION_SIZE;
        uint64_t target_offset = 0U;
        uint64_t info = 0U;
        uint64_t addend = 0U;
        if (!luna_elf_read_u64(object, offset, &target_offset) ||
            !luna_elf_read_u64(object, offset + 8U, &info) ||
            !luna_elf_read_u64(object, offset + 16U, &addend)) {
            return luna_elf_verify_error(stream, "truncated relocation");
        }
        const uint32_t type = (uint32_t)info;
        const uint32_t symbol_index = (uint32_t)(info >> 32U);
        if (target_offset > target_size || target_size - target_offset < 4U ||
            symbol_index >= symbol_count ||
            (type != LUNA_X86_64_OBJECT_RELOCATION_PC32 &&
             type != LUNA_X86_64_OBJECT_RELOCATION_PLT32) ||
            addend != UINT64_C(0xfffffffffffffffc)) {
            return luna_elf_verify_error(
                stream, "invalid relocation %" PRIu64 " in section %u", index,
                (unsigned int)relocation_index);
        }
    }
    return true;
}

bool luna_x86_64_elf_object_verify(LunaStringView object,
                                   FILE *diagnostic_stream) {
    static const unsigned char identity[7] = {
        0x7f, 'E', 'L', 'F', 2, 1, 1,
    };
    if (object.data == NULL || object.length < LUNA_ELF_HEADER_SIZE ||
        memcmp(object.data, identity, sizeof(identity)) != 0) {
        return luna_elf_verify_error(diagnostic_stream,
                                     "invalid ELF64 identity");
    }

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
    uint16_t shstrtab_index = 0U;
    if (!luna_elf_read_u16(object, 16U, &type) ||
        !luna_elf_read_u16(object, 18U, &machine) ||
        !luna_elf_read_u32(object, 20U, &version) ||
        !luna_elf_read_u64(object, 24U, &entry) ||
        !luna_elf_read_u64(object, 32U, &program_headers) ||
        !luna_elf_read_u64(object, 40U, &section_headers) ||
        !luna_elf_read_u32(object, 48U, &flags) ||
        !luna_elf_read_u16(object, 52U, &header_size) ||
        !luna_elf_read_u16(object, 54U, &program_entry_size) ||
        !luna_elf_read_u16(object, 56U, &program_count) ||
        !luna_elf_read_u16(object, 58U, &section_entry_size) ||
        !luna_elf_read_u16(object, 60U, &section_count) ||
        !luna_elf_read_u16(object, 62U, &shstrtab_index) || type != 1U ||
        machine != LUNA_ELF_MACHINE_X86_64 || version != 1U ||
        header_size != LUNA_ELF_HEADER_SIZE ||
        section_entry_size != LUNA_ELF_SECTION_HEADER_SIZE ||
        section_count == 0U || shstrtab_index >= section_count ||
        !luna_elf_range_valid(section_headers,
                              (uint64_t)section_count *
                                  LUNA_ELF_SECTION_HEADER_SIZE,
                              object.length)) {
        return luna_elf_verify_error(diagnostic_stream,
                                     "invalid ELF64 relocatable header");
    }

    uint64_t shstrtab_offset = 0U;
    uint64_t shstrtab_size = 0U;
    uint32_t shstrtab_type = 0U;
    if (!luna_elf_read_section_u32(object, section_headers, shstrtab_index, 4U,
                                   &shstrtab_type) ||
        !luna_elf_read_section_field(object, section_headers, shstrtab_index,
                                     24U, &shstrtab_offset) ||
        !luna_elf_read_section_field(object, section_headers, shstrtab_index,
                                     32U, &shstrtab_size) ||
        shstrtab_type != LUNA_ELF_SECTION_STRTAB ||
        !luna_elf_range_valid(shstrtab_offset, shstrtab_size, object.length)) {
        return luna_elf_verify_error(diagnostic_stream,
                                     "invalid section-name string table");
    }

    if (entry != 0U || program_headers != 0U || flags != 0U ||
        program_entry_size != 0U || program_count != 0U ||
        section_headers < LUNA_ELF_HEADER_SIZE || section_headers % 8U != 0U ||
        section_headers +
                (uint64_t)section_count * LUNA_ELF_SECTION_HEADER_SIZE !=
            (uint64_t)object.length ||
        shstrtab_size == 0U || object.data[(size_t)shstrtab_offset] != '\0') {
        return luna_elf_verify_error(diagnostic_stream,
                                     "invalid ELF64 file layout");
    }

    uint16_t symtab_index = 0U;
    uint64_t previous_section_end = LUNA_ELF_HEADER_SIZE;
    for (uint16_t index = 0U; index < section_count; index += 1U) {
        uint32_t name_offset = 0U;
        uint32_t section_type = 0U;
        uint64_t section_flags = 0U;
        uint64_t address = 0U;
        uint64_t offset = 0U;
        uint64_t size = 0U;
        uint64_t alignment = 0U;
        uint64_t entry_size = 0U;
        uint32_t link = 0U;
        uint32_t info = 0U;
        if (!luna_elf_read_section_u32(object, section_headers, index, 0U,
                                       &name_offset) ||
            !luna_elf_read_section_u32(object, section_headers, index, 4U,
                                       &section_type) ||
            !luna_elf_read_section_field(object, section_headers, index, 8U,
                                         &section_flags) ||
            !luna_elf_read_section_field(object, section_headers, index, 16U,
                                         &address) ||
            !luna_elf_read_section_field(object, section_headers, index, 24U,
                                         &offset) ||
            !luna_elf_read_section_field(object, section_headers, index, 32U,
                                         &size) ||
            !luna_elf_read_section_u32(object, section_headers, index, 40U,
                                       &link) ||
            !luna_elf_read_section_u32(object, section_headers, index, 44U,
                                       &info) ||
            !luna_elf_read_section_field(object, section_headers, index, 48U,
                                         &alignment) ||
            !luna_elf_read_section_field(object, section_headers, index, 56U,
                                         &entry_size) ||
            !luna_elf_string_valid(object, shstrtab_offset, shstrtab_size,
                                   name_offset) ||
            section_type > LUNA_ELF_SECTION_RELA ||
            (index != 0U && section_type == LUNA_ELF_SECTION_NULL) ||
            address != 0U ||
            (index != 0U && !luna_elf_is_power_of_two(alignment)) ||
            !luna_elf_range_valid(offset, size, object.length) ||
            (index != 0U && offset % alignment != 0U) ||
            (index != 0U &&
             (offset < LUNA_ELF_HEADER_SIZE || offset > section_headers ||
              size > section_headers - offset ||
              offset < previous_section_end)) ||
            (section_flags &
             ~(uint64_t)(LUNA_ELF_FLAG_WRITE | LUNA_ELF_FLAG_ALLOC |
                         LUNA_ELF_FLAG_EXECUTE)) != 0U ||
            (section_type != LUNA_ELF_SECTION_PROGBITS &&
             section_flags != 0U) ||
            ((section_type == LUNA_ELF_SECTION_PROGBITS ||
              section_type == LUNA_ELF_SECTION_STRTAB) &&
             (link != 0U || info != 0U || entry_size != 0U))) {
            return luna_elf_verify_error(
                diagnostic_stream, "invalid section %u", (unsigned int)index);
        }
        if (index == 0U &&
            (name_offset != 0U || section_type != LUNA_ELF_SECTION_NULL ||
             section_flags != 0U || address != 0U || offset != 0U ||
             size != 0U || link != 0U || info != 0U || alignment != 0U ||
             entry_size != 0U)) {
            return luna_elf_verify_error(diagnostic_stream,
                                         "invalid null section");
        }
        if (index != 0U) {
            previous_section_end = offset + size;
        }
        if (section_type == LUNA_ELF_SECTION_SYMTAB) {
            if (symtab_index != 0U || entry_size != LUNA_ELF_SYMBOL_SIZE ||
                !luna_elf_verify_symbols(object, section_headers, section_count,
                                         offset, size, link, info,
                                         diagnostic_stream)) {
                return false;
            }
            symtab_index = index;
        } else if (section_type == LUNA_ELF_SECTION_RELA) {
            if (entry_size != LUNA_ELF_RELOCATION_SIZE ||
                !luna_elf_verify_relocations(object, section_headers,
                                             section_count, index, offset, size,
                                             link, info, diagnostic_stream)) {
                return false;
            }
        }
    }
    if (symtab_index == 0U) {
        return luna_elf_verify_error(diagnostic_stream,
                                     "missing ELF symbol table");
    }
    return true;
}
