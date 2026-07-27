#include "elf_linker_internal.h"

#include <inttypes.h>
#include <string.h>

enum {
    LUNA_ELF_EXECUTABLE_TYPE = 2,
    LUNA_ELF_EXECUTABLE_PROGRAM_LOAD = 1,
    LUNA_ELF_EXECUTABLE_PROGRAM_READ = 4,
    LUNA_ELF_EXECUTABLE_PROGRAM_WRITE = 2,
    LUNA_ELF_EXECUTABLE_PROGRAM_EXECUTE = 1,
    LUNA_ELF_EXECUTABLE_SECTION_LIMIT = 6
};

typedef struct LunaElfExecutableSection {
    const char *name;
    uint32_t name_offset;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
    const LunaStringBuilder *content;
} LunaElfExecutableSection;

static bool luna_elf_executable_append_u8(LunaStringBuilder *output,
                                          uint8_t value) {
    const char byte = (char)value;
    return luna_string_builder_append(output, &byte, 1U);
}

static bool luna_elf_executable_append_u16(LunaStringBuilder *output,
                                           uint16_t value) {
    const char bytes[2] = {
        (char)(value & UINT16_C(0xff)),
        (char)((value >> 8U) & UINT16_C(0xff)),
    };
    return luna_string_builder_append(output, bytes, sizeof(bytes));
}

static bool luna_elf_executable_append_u32(LunaStringBuilder *output,
                                           uint32_t value) {
    const char bytes[4] = {
        (char)(value & UINT32_C(0xff)),
        (char)((value >> 8U) & UINT32_C(0xff)),
        (char)((value >> 16U) & UINT32_C(0xff)),
        (char)((value >> 24U) & UINT32_C(0xff)),
    };
    return luna_string_builder_append(output, bytes, sizeof(bytes));
}

static bool luna_elf_executable_append_u64(LunaStringBuilder *output,
                                           uint64_t value) {
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

static bool luna_elf_executable_append_zeros(LunaStringBuilder *output,
                                             uint64_t count) {
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

static bool luna_elf_executable_pad_to(LunaStringBuilder *output,
                                       uint64_t offset) {
    return (uint64_t)output->length <= offset &&
           luna_elf_executable_append_zeros(output,
                                            offset - (uint64_t)output->length);
}

static bool luna_elf_executable_add_section(
    LunaElfExecutableSection *sections, uint16_t *section_count,
    const char *name, uint32_t type, uint64_t flags, uint64_t address,
    uint64_t offset, uint64_t size, uint64_t alignment,
    const LunaStringBuilder *content, uint16_t *section_index) {
    if (*section_count >= LUNA_ELF_EXECUTABLE_SECTION_LIMIT ||
        !luna_elf_link_is_power_of_two(alignment)) {
        return false;
    }
    const uint16_t index = *section_count;
    sections[index] = (LunaElfExecutableSection){
        .name = name,
        .type = type,
        .flags = flags,
        .address = address,
        .offset = offset,
        .size = size,
        .alignment = alignment,
        .content = content,
    };
    *section_count = (uint16_t)(index + 1U);
    if (section_index != NULL) {
        *section_index = index;
    }
    return true;
}

static bool luna_elf_executable_write_header(LunaStringBuilder *output,
                                             const LunaElfLinkContext *context,
                                             uint16_t program_count,
                                             uint64_t section_headers,
                                             uint16_t section_count,
                                             uint16_t string_table_index) {
    static const char identity[16] = {
        0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return luna_string_builder_append(output, identity, sizeof(identity)) &&
           luna_elf_executable_append_u16(output, LUNA_ELF_EXECUTABLE_TYPE) &&
           luna_elf_executable_append_u16(output,
                                          LUNA_ELF_LINK_MACHINE_X86_64) &&
           luna_elf_executable_append_u32(output, 1U) &&
           luna_elf_executable_append_u64(output, context->entry_address) &&
           luna_elf_executable_append_u64(output, LUNA_ELF_LINK_HEADER_SIZE) &&
           luna_elf_executable_append_u64(output, section_headers) &&
           luna_elf_executable_append_u32(output, 0U) &&
           luna_elf_executable_append_u16(output, LUNA_ELF_LINK_HEADER_SIZE) &&
           luna_elf_executable_append_u16(output,
                                          LUNA_ELF_LINK_PROGRAM_HEADER_SIZE) &&
           luna_elf_executable_append_u16(output, program_count) &&
           luna_elf_executable_append_u16(output,
                                          LUNA_ELF_LINK_SECTION_HEADER_SIZE) &&
           luna_elf_executable_append_u16(output, section_count) &&
           luna_elf_executable_append_u16(output, string_table_index);
}

static bool luna_elf_executable_write_program(LunaStringBuilder *output,
                                              uint32_t flags, uint64_t offset,
                                              uint64_t address,
                                              uint64_t file_size,
                                              uint64_t memory_size,
                                              uint64_t alignment) {
    return luna_elf_executable_append_u32(output,
                                          LUNA_ELF_EXECUTABLE_PROGRAM_LOAD) &&
           luna_elf_executable_append_u32(output, flags) &&
           luna_elf_executable_append_u64(output, offset) &&
           luna_elf_executable_append_u64(output, address) &&
           luna_elf_executable_append_u64(output, address) &&
           luna_elf_executable_append_u64(output, file_size) &&
           luna_elf_executable_append_u64(output, memory_size) &&
           luna_elf_executable_append_u64(output, alignment);
}

static bool luna_elf_executable_write_section_header(
    LunaStringBuilder *output, const LunaElfExecutableSection *section) {
    return luna_elf_executable_append_u32(output, section->name_offset) &&
           luna_elf_executable_append_u32(output, section->type) &&
           luna_elf_executable_append_u64(output, section->flags) &&
           luna_elf_executable_append_u64(output, section->address) &&
           luna_elf_executable_append_u64(output, section->offset) &&
           luna_elf_executable_append_u64(output, section->size) &&
           luna_elf_executable_append_u32(output, 0U) &&
           luna_elf_executable_append_u32(output, 0U) &&
           luna_elf_executable_append_u64(output, section->alignment) &&
           luna_elf_executable_append_u64(output, 0U);
}

bool luna_elf_link_serialize_executable(LunaElfLinkContext *context,
                                        LunaStringBuilder *output) {
    LunaElfExecutableSection sections[LUNA_ELF_EXECUTABLE_SECTION_LIMIT] = {0};
    uint16_t section_count = 1U;
    uint16_t string_table_index = 0U;
    uint16_t program_count = 1U;
    if (context->rodata.length > 0U) {
        program_count = (uint16_t)(program_count + 1U);
    }
    if (context->data.length > 0U || context->bss_size > 0U) {
        program_count = (uint16_t)(program_count + 1U);
    }

    LunaStringBuilder string_table;
    luna_string_builder_init(&string_table);
    bool success =
        luna_elf_executable_append_u8(&string_table, 0U) &&
        luna_elf_executable_add_section(
            sections, &section_count, ".text", LUNA_ELF_LINK_SECTION_PROGBITS,
            LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_EXECUTE,
            context->text_address, context->text_file_offset,
            (uint64_t)context->text.length, context->text_alignment,
            &context->text, NULL);
    if (success && context->rodata.length > 0U) {
        success = luna_elf_executable_add_section(
            sections, &section_count, ".rodata", LUNA_ELF_LINK_SECTION_PROGBITS,
            LUNA_ELF_LINK_FLAG_ALLOC, context->rodata_address,
            context->rodata_file_offset, (uint64_t)context->rodata.length,
            context->rodata_alignment, &context->rodata, NULL);
    }
    if (success && context->data.length > 0U) {
        success = luna_elf_executable_add_section(
            sections, &section_count, ".data", LUNA_ELF_LINK_SECTION_PROGBITS,
            LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_WRITE,
            context->data_address, context->data_file_offset,
            (uint64_t)context->data.length, context->data_alignment,
            &context->data, NULL);
    }
    if (success && context->bss_size > 0U) {
        success = luna_elf_executable_add_section(
            sections, &section_count, ".bss", LUNA_ELF_LINK_SECTION_NOBITS,
            LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_WRITE,
            context->bss_address,
            context->data_file_offset + (uint64_t)context->data.length,
            context->bss_size, context->bss_alignment, NULL, NULL);
    }

    uint64_t payload_end =
        context->text_file_offset + (uint64_t)context->text.length;
    if (context->rodata.length > 0U) {
        payload_end =
            context->rodata_file_offset + (uint64_t)context->rodata.length;
    }
    if (context->data.length > 0U || context->bss_size > 0U) {
        payload_end =
            context->data_file_offset + (uint64_t)context->data.length;
    }
    const uint64_t string_table_offset = payload_end;
    if (success) {
        success = luna_elf_executable_add_section(
            sections, &section_count, ".shstrtab", LUNA_ELF_LINK_SECTION_STRTAB,
            0U, 0U, string_table_offset, 0U, 1U, &string_table,
            &string_table_index);
    }
    for (uint16_t index = 1U; success && index < section_count; index += 1U) {
        const size_t name_length = strlen(sections[index].name);
        if (string_table.length > UINT32_MAX ||
            !luna_string_builder_append(&string_table, sections[index].name,
                                        name_length) ||
            !luna_elf_executable_append_u8(&string_table, 0U)) {
            success = false;
            break;
        }
        sections[index].name_offset =
            (uint32_t)(string_table.length - name_length - 1U);
    }
    sections[string_table_index].size = (uint64_t)string_table.length;

    uint64_t section_headers = 0U;
    if (success && !luna_elf_link_align_up(string_table_offset +
                                               (uint64_t)string_table.length,
                                           8U, &section_headers)) {
        success = false;
    }
    if (success) {
        success = luna_elf_executable_write_header(
            output, context, program_count, section_headers, section_count,
            string_table_index);
    }
    uint64_t text_segment_alignment = LUNA_ELF_LINK_PAGE_SIZE;
    if (context->text_alignment > text_segment_alignment) {
        text_segment_alignment = context->text_alignment;
    }
    if (success) {
        success = luna_elf_executable_write_program(
            output,
            LUNA_ELF_EXECUTABLE_PROGRAM_READ |
                LUNA_ELF_EXECUTABLE_PROGRAM_EXECUTE,
            context->text_file_offset, context->text_address,
            (uint64_t)context->text.length, (uint64_t)context->text.length,
            text_segment_alignment);
    }
    if (success && context->rodata.length > 0U) {
        uint64_t alignment = LUNA_ELF_LINK_PAGE_SIZE;
        if (context->rodata_alignment > alignment) {
            alignment = context->rodata_alignment;
        }
        success = luna_elf_executable_write_program(
            output, LUNA_ELF_EXECUTABLE_PROGRAM_READ,
            context->rodata_file_offset, context->rodata_address,
            (uint64_t)context->rodata.length, (uint64_t)context->rodata.length,
            alignment);
    }
    if (success && (context->data.length > 0U || context->bss_size > 0U)) {
        uint64_t alignment = LUNA_ELF_LINK_PAGE_SIZE;
        if (context->data_alignment > alignment) {
            alignment = context->data_alignment;
        }
        const uint64_t memory_size =
            context->bss_size == 0U ? (uint64_t)context->data.length
                                    : context->bss_address + context->bss_size -
                                          context->data_address;
        success = luna_elf_executable_write_program(
            output,
            LUNA_ELF_EXECUTABLE_PROGRAM_READ |
                LUNA_ELF_EXECUTABLE_PROGRAM_WRITE,
            context->data_file_offset, context->data_address,
            (uint64_t)context->data.length, memory_size, alignment);
    }

    for (uint16_t index = 1U; success && index < section_count; index += 1U) {
        const LunaElfExecutableSection *section = &sections[index];
        if (section->type != LUNA_ELF_LINK_SECTION_NOBITS) {
            success = luna_elf_executable_pad_to(output, section->offset) &&
                      luna_string_builder_append(
                          output, luna_string_builder_data(section->content),
                          section->content->length);
        }
    }
    if (success) {
        success = luna_elf_executable_pad_to(output, section_headers);
    }
    for (uint16_t index = 0U; success && index < section_count; index += 1U) {
        success =
            luna_elf_executable_write_section_header(output, &sections[index]);
    }
    if (!success) {
        (void)luna_elf_link_error(
            context, NULL,
            "out of memory or size overflow while writing executable");
    }
    luna_string_builder_destroy(&string_table);
    return success;
}

static bool luna_elf_executable_verify_programs(LunaStringView executable,
                                                uint64_t program_headers,
                                                uint16_t program_count,
                                                uint64_t entry) {
    bool entry_mapped = false;
    bool saw_executable = false;
    bool saw_writable = false;
    uint64_t previous_file_end = 0U;
    uint64_t previous_memory_end = 0U;
    for (uint16_t index = 0U; index < program_count; index += 1U) {
        const uint64_t offset =
            program_headers +
            (uint64_t)index * LUNA_ELF_LINK_PROGRAM_HEADER_SIZE;
        uint32_t type = 0U;
        uint32_t flags = 0U;
        uint64_t file_offset = 0U;
        uint64_t virtual_address = 0U;
        uint64_t physical_address = 0U;
        uint64_t file_size = 0U;
        uint64_t memory_size = 0U;
        uint64_t alignment = 0U;
        if (!luna_elf_link_read_u32(executable, offset, &type) ||
            !luna_elf_link_read_u32(executable, offset + 4U, &flags) ||
            !luna_elf_link_read_u64(executable, offset + 8U, &file_offset) ||
            !luna_elf_link_read_u64(executable, offset + 16U,
                                    &virtual_address) ||
            !luna_elf_link_read_u64(executable, offset + 24U,
                                    &physical_address) ||
            !luna_elf_link_read_u64(executable, offset + 32U, &file_size) ||
            !luna_elf_link_read_u64(executable, offset + 40U, &memory_size) ||
            !luna_elf_link_read_u64(executable, offset + 48U, &alignment) ||
            type != LUNA_ELF_EXECUTABLE_PROGRAM_LOAD ||
            (flags != LUNA_ELF_EXECUTABLE_PROGRAM_READ &&
             flags != (LUNA_ELF_EXECUTABLE_PROGRAM_READ |
                       LUNA_ELF_EXECUTABLE_PROGRAM_EXECUTE) &&
             flags != (LUNA_ELF_EXECUTABLE_PROGRAM_READ |
                       LUNA_ELF_EXECUTABLE_PROGRAM_WRITE)) ||
            virtual_address != physical_address || file_size > memory_size ||
            !luna_elf_link_is_power_of_two(alignment) ||
            alignment != LUNA_ELF_LINK_PAGE_SIZE ||
            file_offset % alignment != virtual_address % alignment ||
            !luna_elf_link_range_valid(file_offset, file_size,
                                       (uint64_t)executable.length) ||
            file_offset < previous_file_end ||
            virtual_address < previous_memory_end ||
            memory_size > UINT64_MAX - virtual_address) {
            return false;
        }
        previous_file_end = file_offset + file_size;
        previous_memory_end = virtual_address + memory_size;
        if ((flags & LUNA_ELF_EXECUTABLE_PROGRAM_EXECUTE) != 0U) {
            if (saw_executable || index != 0U || file_size == 0U) {
                return false;
            }
            saw_executable = true;
            entry_mapped = entry >= virtual_address &&
                           entry - virtual_address < memory_size;
        }
        if ((flags & LUNA_ELF_EXECUTABLE_PROGRAM_WRITE) != 0U) {
            if (saw_writable || index + 1U != program_count) {
                return false;
            }
            saw_writable = true;
        }
    }
    return saw_executable && entry_mapped;
}

static bool luna_elf_executable_section_mapped(
    LunaStringView executable, uint64_t program_headers, uint16_t program_count,
    uint32_t section_type, uint64_t section_flags, uint64_t section_address,
    uint64_t section_offset, uint64_t section_size) {
    uint32_t required_program_flags = LUNA_ELF_EXECUTABLE_PROGRAM_READ;
    if ((section_flags & LUNA_ELF_LINK_FLAG_WRITE) != 0U) {
        required_program_flags |= LUNA_ELF_EXECUTABLE_PROGRAM_WRITE;
    }
    if ((section_flags & LUNA_ELF_LINK_FLAG_EXECUTE) != 0U) {
        required_program_flags |= LUNA_ELF_EXECUTABLE_PROGRAM_EXECUTE;
    }
    for (uint16_t index = 0U; index < program_count; index += 1U) {
        const uint64_t offset =
            program_headers +
            (uint64_t)index * LUNA_ELF_LINK_PROGRAM_HEADER_SIZE;
        uint32_t program_flags = 0U;
        uint64_t file_offset = 0U;
        uint64_t address = 0U;
        uint64_t file_size = 0U;
        uint64_t memory_size = 0U;
        if (!luna_elf_link_read_u32(executable, offset + 4U, &program_flags) ||
            !luna_elf_link_read_u64(executable, offset + 8U, &file_offset) ||
            !luna_elf_link_read_u64(executable, offset + 16U, &address) ||
            !luna_elf_link_read_u64(executable, offset + 32U, &file_size) ||
            !luna_elf_link_read_u64(executable, offset + 40U, &memory_size) ||
            program_flags != required_program_flags ||
            section_address < address) {
            continue;
        }
        const uint64_t memory_delta = section_address - address;
        if (memory_delta > memory_size ||
            section_size > memory_size - memory_delta) {
            continue;
        }
        if (section_type == LUNA_ELF_LINK_SECTION_NOBITS) {
            return true;
        }
        if (section_offset < file_offset) {
            continue;
        }
        const uint64_t file_delta = section_offset - file_offset;
        if (file_delta > file_size || section_size > file_size - file_delta) {
            continue;
        }
        return memory_delta == file_delta;
    }
    return false;
}

bool luna_x86_64_elf_executable_verify(LunaStringView executable,
                                       FILE *diagnostic_stream) {
    static const char identity[16] = {
        0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
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
    if (executable.data == NULL || executable.length < sizeof(identity) ||
        memcmp(executable.data, identity, sizeof(identity)) != 0 ||
        !luna_elf_link_read_u16(executable, 16U, &type) ||
        !luna_elf_link_read_u16(executable, 18U, &machine) ||
        !luna_elf_link_read_u32(executable, 20U, &version) ||
        !luna_elf_link_read_u64(executable, 24U, &entry) ||
        !luna_elf_link_read_u64(executable, 32U, &program_headers) ||
        !luna_elf_link_read_u64(executable, 40U, &section_headers) ||
        !luna_elf_link_read_u32(executable, 48U, &flags) ||
        !luna_elf_link_read_u16(executable, 52U, &header_size) ||
        !luna_elf_link_read_u16(executable, 54U, &program_entry_size) ||
        !luna_elf_link_read_u16(executable, 56U, &program_count) ||
        !luna_elf_link_read_u16(executable, 58U, &section_entry_size) ||
        !luna_elf_link_read_u16(executable, 60U, &section_count) ||
        !luna_elf_link_read_u16(executable, 62U, &string_table_index) ||
        type != LUNA_ELF_EXECUTABLE_TYPE ||
        machine != LUNA_ELF_LINK_MACHINE_X86_64 || version != 1U ||
        entry == 0U || program_headers != LUNA_ELF_LINK_HEADER_SIZE ||
        flags != 0U || header_size != LUNA_ELF_LINK_HEADER_SIZE ||
        program_entry_size != LUNA_ELF_LINK_PROGRAM_HEADER_SIZE ||
        program_count == 0U || program_count > 3U ||
        section_entry_size != LUNA_ELF_LINK_SECTION_HEADER_SIZE ||
        section_count < 3U ||
        section_count > LUNA_ELF_EXECUTABLE_SECTION_LIMIT ||
        string_table_index == 0U || string_table_index >= section_count ||
        !luna_elf_link_range_valid(program_headers,
                                   (uint64_t)program_count *
                                       LUNA_ELF_LINK_PROGRAM_HEADER_SIZE,
                                   (uint64_t)executable.length) ||
        !luna_elf_link_range_valid(section_headers,
                                   (uint64_t)section_count *
                                       LUNA_ELF_LINK_SECTION_HEADER_SIZE,
                                   (uint64_t)executable.length)) {
        return luna_elf_link_verify_error(diagnostic_stream,
                                          "invalid ELF64 executable header");
    }
    if (!luna_elf_executable_verify_programs(executable, program_headers,
                                             program_count, entry)) {
        return luna_elf_link_verify_error(diagnostic_stream,
                                          "invalid ELF64 load segments");
    }

    const uint64_t null_section = section_headers;
    uint64_t null_value = 0U;
    for (uint64_t offset = 0U; offset < LUNA_ELF_LINK_SECTION_HEADER_SIZE;
         offset += 8U) {
        if (!luna_elf_link_read_u64(executable, null_section + offset,
                                    &null_value) ||
            null_value != 0U) {
            return luna_elf_link_verify_error(diagnostic_stream,
                                              "invalid null section header");
        }
    }

    const uint64_t string_section =
        section_headers +
        (uint64_t)string_table_index * LUNA_ELF_LINK_SECTION_HEADER_SIZE;
    uint32_t string_type = 0U;
    uint64_t string_flags = 0U;
    uint64_t string_address = 0U;
    uint64_t string_offset = 0U;
    uint64_t string_size = 0U;
    uint64_t string_alignment = 0U;
    if (!luna_elf_link_read_u32(executable, string_section + 4U,
                                &string_type) ||
        !luna_elf_link_read_u64(executable, string_section + 8U,
                                &string_flags) ||
        !luna_elf_link_read_u64(executable, string_section + 16U,
                                &string_address) ||
        !luna_elf_link_read_u64(executable, string_section + 24U,
                                &string_offset) ||
        !luna_elf_link_read_u64(executable, string_section + 32U,
                                &string_size) ||
        !luna_elf_link_read_u64(executable, string_section + 48U,
                                &string_alignment) ||
        string_type != LUNA_ELF_LINK_SECTION_STRTAB || string_flags != 0U ||
        string_address != 0U || string_alignment != 1U ||
        !luna_elf_link_range_valid(string_offset, string_size,
                                   (uint64_t)executable.length) ||
        string_size == 0U || executable.data[string_offset] != '\0') {
        return luna_elf_link_verify_error(diagnostic_stream,
                                          "invalid section-name string table");
    }

    bool saw_rodata = false;
    bool saw_data = false;
    bool saw_bss = false;
    for (uint16_t index = 1U; index < section_count; index += 1U) {
        const uint64_t offset =
            section_headers +
            (uint64_t)index * LUNA_ELF_LINK_SECTION_HEADER_SIZE;
        uint32_t name_offset = 0U;
        uint32_t section_type = 0U;
        uint64_t section_flags = 0U;
        uint64_t section_address = 0U;
        uint64_t file_offset = 0U;
        uint64_t size = 0U;
        uint32_t link = 0U;
        uint32_t info = 0U;
        uint64_t alignment = 0U;
        uint64_t entry_size = 0U;
        LunaStringView name = {0};
        if (!luna_elf_link_read_u32(executable, offset, &name_offset) ||
            !luna_elf_link_read_u32(executable, offset + 4U, &section_type) ||
            !luna_elf_link_read_u64(executable, offset + 8U, &section_flags) ||
            !luna_elf_link_read_u64(executable, offset + 16U,
                                    &section_address) ||
            !luna_elf_link_read_u64(executable, offset + 24U, &file_offset) ||
            !luna_elf_link_read_u64(executable, offset + 32U, &size) ||
            !luna_elf_link_read_u32(executable, offset + 40U, &link) ||
            !luna_elf_link_read_u32(executable, offset + 44U, &info) ||
            !luna_elf_link_read_u64(executable, offset + 48U, &alignment) ||
            !luna_elf_link_read_u64(executable, offset + 56U, &entry_size) ||
            !luna_elf_link_string(executable, string_offset, string_size,
                                  name_offset, &name) ||
            name.length == 0U || !luna_elf_link_is_power_of_two(alignment) ||
            alignment > LUNA_ELF_LINK_PAGE_SIZE || link != 0U || info != 0U ||
            entry_size != 0U ||
            (section_type != LUNA_ELF_LINK_SECTION_PROGBITS &&
             section_type != LUNA_ELF_LINK_SECTION_NOBITS &&
             section_type != LUNA_ELF_LINK_SECTION_STRTAB) ||
            (section_type != LUNA_ELF_LINK_SECTION_NOBITS &&
             !luna_elf_link_range_valid(file_offset, size,
                                        (uint64_t)executable.length)) ||
            (section_flags &
             ~(uint64_t)(LUNA_ELF_LINK_FLAG_WRITE | LUNA_ELF_LINK_FLAG_ALLOC |
                         LUNA_ELF_LINK_FLAG_EXECUTE)) != 0U ||
            ((section_flags & LUNA_ELF_LINK_FLAG_WRITE) != 0U &&
             (section_flags & LUNA_ELF_LINK_FLAG_EXECUTE) != 0U) ||
            (index == string_table_index &&
             (section_flags != 0U || section_address != 0U))) {
            return luna_elf_link_verify_error(
                diagnostic_stream, "invalid section header %" PRIu16, index);
        }

        const bool is_text = luna_string_view_equal_c_string(name, ".text");
        const bool is_rodata = luna_string_view_equal_c_string(name, ".rodata");
        const bool is_data = luna_string_view_equal_c_string(name, ".data");
        const bool is_bss = luna_string_view_equal_c_string(name, ".bss");
        const bool is_string_table =
            luna_string_view_equal_c_string(name, ".shstrtab");
        const bool known_contract =
            (is_text && index == 1U &&
             section_type == LUNA_ELF_LINK_SECTION_PROGBITS &&
             section_flags ==
                 (LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_EXECUTE) &&
             entry >= section_address && entry - section_address < size) ||
            (is_rodata && !saw_rodata && !saw_data && !saw_bss &&
             section_type == LUNA_ELF_LINK_SECTION_PROGBITS &&
             section_flags == LUNA_ELF_LINK_FLAG_ALLOC && size != 0U) ||
            (is_data && !saw_data && !saw_bss &&
             section_type == LUNA_ELF_LINK_SECTION_PROGBITS &&
             section_flags ==
                 (LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_WRITE) &&
             size != 0U) ||
            (is_bss && !saw_bss &&
             section_type == LUNA_ELF_LINK_SECTION_NOBITS &&
             section_flags ==
                 (LUNA_ELF_LINK_FLAG_ALLOC | LUNA_ELF_LINK_FLAG_WRITE) &&
             size != 0U) ||
            (is_string_table && index == string_table_index &&
             index + 1U == section_count &&
             section_type == LUNA_ELF_LINK_SECTION_STRTAB &&
             section_flags == 0U);
        if (!known_contract ||
            ((section_flags & LUNA_ELF_LINK_FLAG_ALLOC) != 0U &&
             !luna_elf_executable_section_mapped(
                 executable, program_headers, program_count, section_type,
                 section_flags, section_address, file_offset, size))) {
            return luna_elf_link_verify_error(
                diagnostic_stream,
                "section %" PRIu16 " violates the output contract", index);
        }
        saw_rodata = saw_rodata || is_rodata;
        saw_data = saw_data || is_data;
        saw_bss = saw_bss || is_bss;
    }
    const uint16_t expected_program_count =
        (uint16_t)(1U + (saw_rodata ? 1U : 0U) +
                   ((saw_data || saw_bss) ? 1U : 0U));
    if (program_count != expected_program_count) {
        return luna_elf_link_verify_error(
            diagnostic_stream,
            "load segments do not match the allocated sections");
    }
    return true;
}
