#include "elf_linker_internal.h"

#include <inttypes.h>

enum {
    LUNA_ELF_LAYOUT_IMAGE_BASE = 0x400000,
    LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE = 512 * 1024 * 1024
};

static bool luna_elf_layout_append_zeros(LunaStringBuilder *output,
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

static bool luna_elf_layout_append_section(LunaElfLinkContext *context,
                                           LunaElfLinkObject *object,
                                           LunaElfLinkSection *section,
                                           LunaStringBuilder *output,
                                           uint64_t *maximum_alignment) {
    uint64_t aligned = 0U;
    const uint64_t allocated_size =
        (uint64_t)context->text.length + (uint64_t)context->rodata.length +
        (uint64_t)context->data.length - (uint64_t)output->length;
    if (!luna_elf_link_align_up((uint64_t)output->length, section->alignment,
                                &aligned) ||
        aligned > LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE ||
        section->size > (uint64_t)LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE - aligned ||
        allocated_size > LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE ||
        aligned + section->size >
            (uint64_t)LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE - allocated_size ||
        !luna_elf_layout_append_zeros(output,
                                      aligned - (uint64_t)output->length) ||
        !luna_string_builder_append(output,
                                    object->bytes.data + section->file_offset,
                                    (size_t)section->size)) {
        return luna_elf_link_error(
            context, object,
            "output size overflow while laying out section '%.*s'",
            (int)section->name.length, section->name.data);
    }
    section->region_offset = aligned;
    if (section->alignment > *maximum_alignment) {
        *maximum_alignment = section->alignment;
    }
    return true;
}

static bool luna_elf_layout_region(LunaElfLinkContext *context,
                                   LunaElfLinkRegion region,
                                   LunaStringBuilder *output,
                                   uint64_t *maximum_alignment) {
    for (size_t object_index = 0U; object_index < context->objects.length;
         object_index += 1U) {
        LunaElfLinkObject *object =
            luna_vector_at(&context->objects, object_index);
        if (object == NULL) {
            return luna_elf_link_error(context, NULL,
                                       "invalid object layout state");
        }
        for (size_t section_index = 1U; section_index < object->sections.length;
             section_index += 1U) {
            LunaElfLinkSection *section =
                luna_vector_at(&object->sections, section_index);
            if (section != NULL && section->region == region &&
                !luna_elf_layout_append_section(context, object, section,
                                                output, maximum_alignment)) {
                return false;
            }
        }
    }
    return true;
}

static bool luna_elf_layout_bss(LunaElfLinkContext *context) {
    uint64_t size = 0U;
    const uint64_t allocated_size = (uint64_t)context->text.length +
                                    (uint64_t)context->rodata.length +
                                    (uint64_t)context->data.length;
    for (size_t object_index = 0U; object_index < context->objects.length;
         object_index += 1U) {
        LunaElfLinkObject *object =
            luna_vector_at(&context->objects, object_index);
        if (object == NULL) {
            return luna_elf_link_error(context, NULL,
                                       "invalid BSS layout state");
        }
        for (size_t section_index = 1U; section_index < object->sections.length;
             section_index += 1U) {
            LunaElfLinkSection *section =
                luna_vector_at(&object->sections, section_index);
            if (section == NULL ||
                section->region != LUNA_ELF_LINK_REGION_BSS) {
                continue;
            }
            uint64_t aligned = 0U;
            if (!luna_elf_link_align_up(size, section->alignment, &aligned) ||
                aligned > LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE ||
                allocated_size > LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE ||
                section->size >
                    (uint64_t)LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE - aligned ||
                aligned + section->size >
                    (uint64_t)LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE -
                        allocated_size) {
                return luna_elf_link_error(
                    context, object,
                    "BSS size overflow while laying out section '%.*s'",
                    (int)section->name.length, section->name.data);
            }
            section->region_offset = aligned;
            size = aligned + section->size;
            if (section->alignment > context->bss_alignment) {
                context->bss_alignment = section->alignment;
            }
        }
    }
    context->bss_size = size;
    return true;
}

bool luna_elf_link_layout_regions(LunaElfLinkContext *context) {
    if (!luna_elf_layout_region(context, LUNA_ELF_LINK_REGION_TEXT,
                                &context->text, &context->text_alignment) ||
        !luna_elf_layout_region(context, LUNA_ELF_LINK_REGION_RODATA,
                                &context->rodata, &context->rodata_alignment) ||
        !luna_elf_layout_region(context, LUNA_ELF_LINK_REGION_DATA,
                                &context->data, &context->data_alignment) ||
        !luna_elf_layout_bss(context)) {
        return false;
    }
    if (context->text.length == 0U) {
        return luna_elf_link_error(context, NULL,
                                   "linked image has no executable code");
    }
    return true;
}

static bool luna_elf_layout_next_region(uint64_t cursor,
                                        uint64_t section_alignment,
                                        uint64_t *file_offset,
                                        uint64_t *address) {
    uint64_t alignment = LUNA_ELF_LINK_PAGE_SIZE;
    if (section_alignment > alignment) {
        alignment = section_alignment;
    }
    return luna_elf_link_align_up(cursor, alignment, file_offset) &&
           *file_offset <= UINT64_MAX - LUNA_ELF_LAYOUT_IMAGE_BASE &&
           ((*address = LUNA_ELF_LAYOUT_IMAGE_BASE + *file_offset), true);
}

bool luna_elf_link_layout_executable(LunaElfLinkContext *context) {
    uint64_t program_count = 1U;
    if (context->rodata.length > 0U) {
        program_count += 1U;
    }
    if (context->data.length > 0U || context->bss_size > 0U) {
        program_count += 1U;
    }

    uint64_t cursor = LUNA_ELF_LINK_HEADER_SIZE +
                      program_count * LUNA_ELF_LINK_PROGRAM_HEADER_SIZE;
    if (!luna_elf_layout_next_region(cursor, context->text_alignment,
                                     &context->text_file_offset,
                                     &context->text_address) ||
        (uint64_t)context->text.length >
            UINT64_MAX - context->text_file_offset) {
        return luna_elf_link_error(context, NULL,
                                   "executable text layout overflow");
    }
    cursor = context->text_file_offset + (uint64_t)context->text.length;

    if (context->rodata.length > 0U) {
        if (!luna_elf_layout_next_region(cursor, context->rodata_alignment,
                                         &context->rodata_file_offset,
                                         &context->rodata_address) ||
            (uint64_t)context->rodata.length >
                UINT64_MAX - context->rodata_file_offset) {
            return luna_elf_link_error(context, NULL,
                                       "executable rodata layout overflow");
        }
        cursor = context->rodata_file_offset + (uint64_t)context->rodata.length;
    }

    if (context->data.length > 0U || context->bss_size > 0U) {
        if (!luna_elf_layout_next_region(cursor, context->data_alignment,
                                         &context->data_file_offset,
                                         &context->data_address) ||
            (uint64_t)context->data.length >
                UINT64_MAX - context->data_file_offset) {
            return luna_elf_link_error(context, NULL,
                                       "executable data layout overflow");
        }
        cursor = context->data_file_offset + (uint64_t)context->data.length;
        const uint64_t data_memory_end =
            context->data_address + (uint64_t)context->data.length;
        if (!luna_elf_link_align_up(data_memory_end, context->bss_alignment,
                                    &context->bss_address) ||
            context->bss_size > UINT64_MAX - context->bss_address) {
            return luna_elf_link_error(context, NULL,
                                       "executable BSS layout overflow");
        }
    }

    if (cursor > LUNA_ELF_LAYOUT_MAX_OUTPUT_SIZE) {
        return luna_elf_link_error(context, NULL,
                                   "linked executable exceeds size limit");
    }
    return true;
}
