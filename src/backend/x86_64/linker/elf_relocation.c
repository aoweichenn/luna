#include "elf_linker_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

static LunaStringBuilder *
luna_elf_relocation_region_output(LunaElfLinkContext *context,
                                  LunaElfLinkRegion region) {
    switch (region) {
    case LUNA_ELF_LINK_REGION_TEXT:
        return &context->text;
    case LUNA_ELF_LINK_REGION_RODATA:
        return &context->rodata;
    case LUNA_ELF_LINK_REGION_DATA:
        return &context->data;
    case LUNA_ELF_LINK_REGION_BSS:
    case LUNA_ELF_LINK_REGION_NONE:
        return NULL;
    }
    return NULL;
}

static bool
luna_elf_relocation_section_address(LunaElfLinkContext *context,
                                    const LunaElfLinkSection *section,
                                    uint64_t *address) {
    uint64_t base = 0U;
    switch (section->region) {
    case LUNA_ELF_LINK_REGION_TEXT:
        base = context->text_address;
        break;
    case LUNA_ELF_LINK_REGION_RODATA:
        base = context->rodata_address;
        break;
    case LUNA_ELF_LINK_REGION_DATA:
        base = context->data_address;
        break;
    case LUNA_ELF_LINK_REGION_BSS:
        base = context->bss_address;
        break;
    case LUNA_ELF_LINK_REGION_NONE:
        return false;
    }
    if (section->region_offset > UINT64_MAX - base) {
        return false;
    }
    *address = base + section->region_offset;
    return true;
}

static bool luna_elf_relocation_add_signed(uint64_t base, int64_t addend,
                                           uint64_t *result) {
    if (addend >= 0) {
        const uint64_t amount = (uint64_t)addend;
        if (amount > UINT64_MAX - base) {
            return false;
        }
        *result = base + amount;
        return true;
    }
    const uint64_t amount = (uint64_t)(-(addend + 1)) + 1U;
    if (amount > base) {
        return false;
    }
    *result = base - amount;
    return true;
}

static bool luna_elf_relocation_signed32_value(uint64_t base, int64_t addend,
                                               int32_t *result) {
    if (addend >= 0) {
        const uint64_t amount = (uint64_t)addend;
        if (base > (uint64_t)INT32_MAX || amount > (uint64_t)INT32_MAX - base) {
            return false;
        }
        *result = (int32_t)(base + amount);
        return true;
    }

    const uint64_t amount = (uint64_t)(-(addend + 1)) + 1U;
    if (base >= amount) {
        const uint64_t value = base - amount;
        if (value > (uint64_t)INT32_MAX) {
            return false;
        }
        *result = (int32_t)value;
        return true;
    }
    const uint64_t magnitude = amount - base;
    if (magnitude > (uint64_t)INT32_MAX + 1U) {
        return false;
    }
    if (magnitude == (uint64_t)INT32_MAX + 1U) {
        *result = INT32_MIN;
    } else {
        *result = -(int32_t)magnitude;
    }
    return true;
}

static bool luna_elf_relocation_pc_value(uint64_t symbol_address,
                                         int64_t addend, uint64_t place_address,
                                         int32_t *result) {
    int64_t difference = 0;
    if (symbol_address >= place_address) {
        const uint64_t amount = symbol_address - place_address;
        if (amount > (uint64_t)INT64_MAX) {
            return false;
        }
        difference = (int64_t)amount;
    } else {
        const uint64_t amount = place_address - symbol_address;
        if (amount > (uint64_t)INT64_MAX + 1U) {
            return false;
        }
        if (amount == (uint64_t)INT64_MAX + 1U) {
            difference = INT64_MIN;
        } else {
            difference = -(int64_t)amount;
        }
    }

    if ((addend > 0 && difference > INT64_MAX - addend) ||
        (addend < 0 && difference < INT64_MIN - addend)) {
        return false;
    }
    const int64_t value = difference + addend;
    if (value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    *result = (int32_t)value;
    return true;
}

static void luna_elf_relocation_store_u32(char *destination, uint32_t value) {
    destination[0] = (char)(value & UINT32_C(0xff));
    destination[1] = (char)((value >> 8U) & UINT32_C(0xff));
    destination[2] = (char)((value >> 16U) & UINT32_C(0xff));
    destination[3] = (char)((value >> 24U) & UINT32_C(0xff));
}

static void luna_elf_relocation_store_u64(char *destination, uint64_t value) {
    for (uint32_t index = 0U; index < 8U; index += 1U) {
        destination[index] = (char)((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

static uint64_t luna_elf_relocation_width(uint32_t type) {
    switch (type) {
    case LUNA_ELF_LINK_RELOCATION_NONE:
        return 0U;
    case LUNA_ELF_LINK_RELOCATION_64:
        return 8U;
    case LUNA_ELF_LINK_RELOCATION_PC32:
    case LUNA_ELF_LINK_RELOCATION_PLT32:
    case LUNA_ELF_LINK_RELOCATION_32:
    case LUNA_ELF_LINK_RELOCATION_32S:
        return 4U;
    default:
        return UINT64_MAX;
    }
}

static bool luna_elf_relocation_apply(LunaElfLinkContext *context,
                                      LunaElfLinkObject *object,
                                      const LunaElfLinkSection *target,
                                      uint64_t target_offset,
                                      uint32_t symbol_index,
                                      uint32_t relocation_type, int64_t addend,
                                      uint64_t relocation_index) {
    const uint64_t width = luna_elf_relocation_width(relocation_type);
    if (width == UINT64_MAX) {
        return luna_elf_link_error(context, object,
                                   "unsupported x86-64 relocation %" PRIu32
                                   " at index %" PRIu64,
                                   relocation_type, relocation_index);
    }
    if (width == 0U) {
        if (target_offset <= target->size) {
            return true;
        }
        return luna_elf_link_error(
            context, object,
            "empty relocation %" PRIu64 " exceeds section '%.*s'",
            relocation_index, (int)target->name.length, target->name.data);
    }
    if (!luna_elf_link_range_valid(target_offset, width, target->size)) {
        return luna_elf_link_error(
            context, object, "relocation %" PRIu64 " exceeds section '%.*s'",
            relocation_index, (int)target->name.length, target->name.data);
    }

    LunaStringBuilder *region =
        luna_elf_relocation_region_output(context, target->region);
    uint64_t target_address = 0U;
    if (region == NULL ||
        !luna_elf_relocation_section_address(context, target,
                                             &target_address) ||
        target->region_offset > UINT64_MAX - target_offset) {
        return luna_elf_link_error(
            context, object,
            "relocation targets an unsupported output section");
    }
    const uint64_t output_offset = target->region_offset + target_offset;
    if (!luna_elf_link_range_valid(output_offset, width,
                                   (uint64_t)region->length) ||
        target_address > UINT64_MAX - target_offset) {
        return luna_elf_link_error(context, object,
                                   "relocation output offset overflow");
    }

    uint64_t symbol_address = 0U;
    if (!luna_elf_link_resolve_symbol(context, object, symbol_index,
                                      &symbol_address)) {
        return false;
    }
    char *destination = region->data + output_offset;
    if (relocation_type == LUNA_ELF_LINK_RELOCATION_PC32 ||
        relocation_type == LUNA_ELF_LINK_RELOCATION_PLT32) {
        int32_t value = 0;
        if (!luna_elf_relocation_pc_value(symbol_address, addend,
                                          target_address + target_offset,
                                          &value)) {
            return luna_elf_link_error(context, object,
                                       "PC-relative relocation %" PRIu64
                                       " overflows 32 bits",
                                       relocation_index);
        }
        uint32_t bits = 0U;
        memcpy(&bits, &value, sizeof(bits));
        luna_elf_relocation_store_u32(destination, bits);
        return true;
    }
    if (relocation_type == LUNA_ELF_LINK_RELOCATION_32S) {
        int32_t value = 0;
        if (!luna_elf_relocation_signed32_value(symbol_address, addend,
                                                &value)) {
            return luna_elf_link_error(context, object,
                                       "signed relocation %" PRIu64
                                       " overflows 32 bits",
                                       relocation_index);
        }
        uint32_t bits = 0U;
        memcpy(&bits, &value, sizeof(bits));
        luna_elf_relocation_store_u32(destination, bits);
        return true;
    }

    uint64_t absolute = 0U;
    if (!luna_elf_relocation_add_signed(symbol_address, addend, &absolute)) {
        return luna_elf_link_error(context, object,
                                   "absolute relocation %" PRIu64
                                   " overflows 64 bits",
                                   relocation_index);
    }
    if (relocation_type == LUNA_ELF_LINK_RELOCATION_64) {
        luna_elf_relocation_store_u64(destination, absolute);
        return true;
    }
    if (relocation_type == LUNA_ELF_LINK_RELOCATION_32 &&
        absolute > UINT32_MAX) {
        return luna_elf_link_error(context, object,
                                   "unsigned relocation %" PRIu64
                                   " overflows 32 bits",
                                   relocation_index);
    }
    luna_elf_relocation_store_u32(destination, (uint32_t)absolute);
    return true;
}

static bool luna_elf_relocation_apply_section(
    LunaElfLinkContext *context, LunaElfLinkObject *object,
    const LunaElfLinkSection *relocations, uint64_t relocation_section_index) {
    if (relocations->info >= object->sections.length ||
        relocations->link != object->symbol_table_index) {
        return luna_elf_link_error(context, object,
                                   "relocation section %" PRIu64
                                   " has invalid references",
                                   relocation_section_index);
    }
    const LunaElfLinkSection *target =
        luna_vector_at_const(&object->sections, relocations->info);
    if (target == NULL) {
        return luna_elf_link_error(context, object,
                                   "missing relocation target section");
    }
    if (target->region == LUNA_ELF_LINK_REGION_NONE) {
        return true;
    }
    if (target->region == LUNA_ELF_LINK_REGION_BSS) {
        return luna_elf_link_error(context, object,
                                   "cannot relocate a NOBITS section");
    }

    const LunaElfLinkSection *symbol_table =
        luna_vector_at_const(&object->sections, relocations->link);
    const uint64_t symbol_count =
        symbol_table->size / LUNA_ELF_LINK_SYMBOL_SIZE;
    const uint64_t relocation_count =
        relocations->size / LUNA_ELF_LINK_RELOCATION_SIZE;
    for (uint64_t index = 0U; index < relocation_count; index += 1U) {
        const uint64_t offset =
            relocations->file_offset + index * LUNA_ELF_LINK_RELOCATION_SIZE;
        uint64_t target_offset = 0U;
        uint64_t info = 0U;
        int64_t addend = 0;
        if (!luna_elf_link_read_u64(object->bytes, offset, &target_offset) ||
            !luna_elf_link_read_u64(object->bytes, offset + 8U, &info) ||
            !luna_elf_link_read_i64(object->bytes, offset + 16U, &addend)) {
            return luna_elf_link_error(context, object,
                                       "truncated relocation entry");
        }
        const uint64_t symbol_index = info >> 32U;
        const uint32_t relocation_type = (uint32_t)info;
        if (symbol_index >= symbol_count || symbol_index > UINT32_MAX ||
            !luna_elf_relocation_apply(context, object, target, target_offset,
                                       (uint32_t)symbol_index, relocation_type,
                                       addend, index)) {
            if (symbol_index >= symbol_count || symbol_index > UINT32_MAX) {
                return luna_elf_link_error(
                    context, object,
                    "relocation %" PRIu64 " has invalid symbol index", index);
            }
            return false;
        }
    }
    return true;
}

bool luna_elf_link_apply_relocations(LunaElfLinkContext *context) {
    for (size_t object_index = 0U; object_index < context->objects.length;
         object_index += 1U) {
        LunaElfLinkObject *object =
            luna_vector_at(&context->objects, object_index);
        if (object == NULL) {
            return luna_elf_link_error(context, NULL,
                                       "invalid relocation state");
        }
        for (size_t section_index = 1U; section_index < object->sections.length;
             section_index += 1U) {
            const LunaElfLinkSection *section =
                luna_vector_at_const(&object->sections, section_index);
            if (section == NULL) {
                return luna_elf_link_error(context, object,
                                           "invalid relocation section");
            }
            if (section->type == LUNA_ELF_LINK_SECTION_RELA &&
                !luna_elf_relocation_apply_section(context, object, section,
                                                   (uint64_t)section_index)) {
                return false;
            }
        }
    }
    return true;
}
