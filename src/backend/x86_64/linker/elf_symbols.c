#include "elf_linker_internal.h"

#include <inttypes.h>
#include <string.h>

enum {
    LUNA_ELF_SYMBOLS_INITIAL_SLOT_COUNT = 32,
    LUNA_ELF_SYMBOLS_MAX_GLOBAL_COUNT = 1048576,
    LUNA_ELF_SYMBOLS_NAME_LIMIT = 4096
};

static bool luna_elf_symbols_equal(LunaStringView left, LunaStringView right) {
    return left.length == right.length &&
           (left.length == 0U ||
            memcmp(left.data, right.data, left.length) == 0);
}

static uint64_t luna_elf_symbols_hash(LunaStringView name) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0U; index < name.length; index += 1U) {
        hash ^= (uint64_t)(unsigned char)name.data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool luna_elf_symbols_reset_slots(LunaElfLinkContext *context,
                                         size_t slot_count) {
    context->global_slots.length = 0U;
    if (!luna_vector_reserve(&context->global_slots, slot_count)) {
        return luna_elf_link_error(
            context, NULL, "out of memory while indexing global symbols");
    }
    const uint32_t empty_slot = 0U;
    for (size_t index = 0U; index < slot_count; index += 1U) {
        if (!luna_vector_push(&context->global_slots, &empty_slot)) {
            return luna_elf_link_error(
                context, NULL, "out of memory while indexing global symbols");
        }
    }
    return true;
}

static bool luna_elf_symbols_place_global(LunaElfLinkContext *context,
                                          uint32_t global_index) {
    const LunaElfLinkGlobal *global =
        luna_vector_at_const(&context->globals, global_index);
    if (global == NULL || context->global_slots.length == 0U) {
        return false;
    }
    const size_t mask = context->global_slots.length - 1U;
    size_t slot =
        (size_t)(luna_elf_symbols_hash(global->name) & (uint64_t)mask);
    for (;;) {
        uint32_t *stored = luna_vector_at(&context->global_slots, slot);
        if (stored == NULL) {
            return false;
        }
        if (*stored == 0U) {
            *stored = global_index + 1U;
            return true;
        }
        slot = (slot + 1U) & mask;
    }
}

static bool luna_elf_symbols_grow_slots(LunaElfLinkContext *context) {
    size_t slot_count = context->global_slots.length;
    if (slot_count == 0U) {
        slot_count = LUNA_ELF_SYMBOLS_INITIAL_SLOT_COUNT;
    } else {
        if (slot_count > SIZE_MAX / 2U) {
            return luna_elf_link_error(context, NULL,
                                       "global symbol index overflow");
        }
        slot_count *= 2U;
    }
    if (!luna_elf_symbols_reset_slots(context, slot_count)) {
        return false;
    }
    for (size_t index = 0U; index < context->globals.length; index += 1U) {
        if (index > UINT32_MAX ||
            !luna_elf_symbols_place_global(context, (uint32_t)index)) {
            return luna_elf_link_error(context, NULL,
                                       "global symbol index overflow");
        }
    }
    return true;
}

static LunaElfLinkGlobal *
luna_elf_symbols_find_global(LunaElfLinkContext *context, LunaStringView name) {
    if (context->global_slots.length == 0U) {
        return NULL;
    }
    const size_t mask = context->global_slots.length - 1U;
    size_t slot = (size_t)(luna_elf_symbols_hash(name) & (uint64_t)mask);
    for (;;) {
        const uint32_t *stored =
            luna_vector_at_const(&context->global_slots, slot);
        if (stored == NULL || *stored == 0U) {
            return NULL;
        }
        LunaElfLinkGlobal *global =
            luna_vector_at(&context->globals, (size_t)(*stored - 1U));
        if (global != NULL && luna_elf_symbols_equal(global->name, name)) {
            return global;
        }
        slot = (slot + 1U) & mask;
    }
}

static bool luna_elf_symbols_validate_symbol(
    LunaElfLinkContext *context, LunaElfLinkObject *object,
    const LunaElfLinkSection *symbol_table, uint32_t symbol_index,
    const LunaElfLinkSymbol *symbol) {
    const bool local_range = (uint64_t)symbol_index < symbol_table->info;
    if (symbol->binding > LUNA_ELF_LINK_SYMBOL_WEAK ||
        symbol->type > LUNA_ELF_LINK_SYMBOL_TYPE_FILE ||
        symbol->visibility > 3U ||
        (local_range && symbol->binding != LUNA_ELF_LINK_SYMBOL_LOCAL) ||
        (!local_range && symbol->binding == LUNA_ELF_LINK_SYMBOL_LOCAL)) {
        return luna_elf_link_error(context, object,
                                   "invalid symbol metadata at index %" PRIu32,
                                   symbol_index);
    }
    if (symbol_index == 0U) {
        if (symbol->name.length != 0U ||
            symbol->section_index != LUNA_ELF_LINK_SYMBOL_UNDEFINED ||
            symbol->value != 0U || symbol->size != 0U ||
            symbol->binding != LUNA_ELF_LINK_SYMBOL_LOCAL ||
            symbol->type != 0U || symbol->visibility != 0U) {
            return luna_elf_link_error(context, object, "invalid null symbol");
        }
        return true;
    }
    if (symbol->section_index == LUNA_ELF_LINK_SYMBOL_COMMON) {
        return luna_elf_link_error(
            context, object,
            "COMMON symbol '%.*s' is unsupported; compile with -fno-common",
            (int)symbol->name.length, symbol->name.data);
    }
    if (symbol->section_index != LUNA_ELF_LINK_SYMBOL_UNDEFINED &&
        symbol->section_index != LUNA_ELF_LINK_SYMBOL_ABSOLUTE) {
        const LunaElfLinkSection *section =
            luna_vector_at_const(&object->sections, symbol->section_index);
        if (section == NULL || symbol->value > section->size ||
            symbol->size > section->size - symbol->value) {
            return luna_elf_link_error(
                context, object, "symbol '%.*s' exceeds its defining section",
                (int)symbol->name.length, symbol->name.data);
        }
    }
    if (symbol->section_index == LUNA_ELF_LINK_SYMBOL_UNDEFINED &&
        (symbol->value != 0U || symbol->size != 0U)) {
        return luna_elf_link_error(context, object,
                                   "undefined symbol '%.*s' has a value",
                                   (int)symbol->name.length, symbol->name.data);
    }
    if (symbol->binding != LUNA_ELF_LINK_SYMBOL_LOCAL &&
        symbol->name.length == 0U) {
        return luna_elf_link_error(context, object,
                                   "global symbol has no name");
    }
    if (symbol->name.length > LUNA_ELF_SYMBOLS_NAME_LIMIT) {
        return luna_elf_link_error(context, object, "symbol name is too long");
    }
    return true;
}

static bool
luna_elf_symbols_record_definition(LunaElfLinkContext *context,
                                   uint32_t object_index, uint32_t symbol_index,
                                   const LunaElfLinkSymbol *symbol) {
    if (symbol->binding == LUNA_ELF_LINK_SYMBOL_LOCAL ||
        symbol->section_index == LUNA_ELF_LINK_SYMBOL_UNDEFINED) {
        return true;
    }
    const LunaElfLinkObject *object =
        luna_vector_at_const(&context->objects, object_index);
    const LunaElfLinkSection *section =
        symbol->section_index == LUNA_ELF_LINK_SYMBOL_ABSOLUTE
            ? NULL
            : luna_vector_at_const(&object->sections, symbol->section_index);
    if (section != NULL && section->region == LUNA_ELF_LINK_REGION_NONE) {
        return true;
    }

    LunaElfLinkGlobal *existing =
        luna_elf_symbols_find_global(context, symbol->name);
    const bool weak = symbol->binding == LUNA_ELF_LINK_SYMBOL_WEAK;
    if (existing != NULL) {
        if (!existing->weak && !weak) {
            const LunaElfLinkObject *previous =
                luna_vector_at_const(&context->objects, existing->object_index);
            return luna_elf_link_error(
                context, object,
                "duplicate strong definition of '%.*s' (first defined in "
                "%.*s)",
                (int)symbol->name.length, symbol->name.data,
                (int)previous->name.length, previous->name.data);
        }
        if (existing->weak && !weak) {
            existing->object_index = object_index;
            existing->symbol_index = symbol_index;
            existing->weak = false;
        }
        return true;
    }

    if (context->globals.length >= LUNA_ELF_SYMBOLS_MAX_GLOBAL_COUNT) {
        return luna_elf_link_error(context, NULL, "too many global symbols");
    }
    const bool slots_need_growth = context->global_slots.length == 0U ||
                                   (context->globals.length + 1U) * 10U >=
                                       context->global_slots.length * 7U;
    if (slots_need_growth && !luna_elf_symbols_grow_slots(context)) {
        return false;
    }

    const LunaElfLinkGlobal global = {
        .name = symbol->name,
        .object_index = object_index,
        .symbol_index = symbol_index,
        .weak = weak,
    };
    if (!luna_vector_push(&context->globals, &global) ||
        context->globals.length - 1U > UINT32_MAX ||
        !luna_elf_symbols_place_global(
            context, (uint32_t)(context->globals.length - 1U))) {
        return luna_elf_link_error(
            context, NULL, "out of memory while recording global symbols");
    }
    return true;
}

bool luna_elf_link_collect_globals(LunaElfLinkContext *context) {
    for (size_t object_index = 0U; object_index < context->objects.length;
         object_index += 1U) {
        LunaElfLinkObject *object =
            luna_vector_at(&context->objects, object_index);
        const LunaElfLinkSection *symbol_table =
            object == NULL ? NULL
                           : luna_vector_at_const(&object->sections,
                                                  object->symbol_table_index);
        if (object == NULL || symbol_table == NULL ||
            object_index > UINT32_MAX) {
            return luna_elf_link_error(context, NULL,
                                       "invalid symbol collection state");
        }
        const uint64_t symbol_count =
            symbol_table->size / LUNA_ELF_LINK_SYMBOL_SIZE;
        if (symbol_count > UINT32_MAX) {
            return luna_elf_link_error(context, object,
                                       "symbol table is too large");
        }
        for (uint32_t symbol_index = 0U; (uint64_t)symbol_index < symbol_count;
             symbol_index += 1U) {
            LunaElfLinkSymbol symbol = {0};
            if (!luna_elf_link_read_symbol(object, symbol_index, &symbol) ||
                !luna_elf_symbols_validate_symbol(context, object, symbol_table,
                                                  symbol_index, &symbol) ||
                !luna_elf_symbols_record_definition(
                    context, (uint32_t)object_index, symbol_index, &symbol)) {
                return false;
            }
        }
    }
    return true;
}

static bool luna_elf_symbols_section_address(LunaElfLinkContext *context,
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

static bool luna_elf_symbols_resolve_defined(LunaElfLinkContext *context,
                                             const LunaElfLinkObject *object,
                                             const LunaElfLinkSymbol *symbol,
                                             uint64_t *address) {
    if (symbol->section_index == LUNA_ELF_LINK_SYMBOL_ABSOLUTE) {
        *address = symbol->value;
        return true;
    }
    const LunaElfLinkSection *section =
        luna_vector_at_const(&object->sections, symbol->section_index);
    uint64_t section_address = 0U;
    if (section == NULL ||
        !luna_elf_symbols_section_address(context, section, &section_address) ||
        symbol->value > UINT64_MAX - section_address) {
        return false;
    }
    *address = section_address + symbol->value;
    return true;
}

static bool luna_elf_symbols_resolve_global(LunaElfLinkContext *context,
                                            LunaStringView name,
                                            uint64_t *address, bool *weak) {
    LunaElfLinkGlobal *global = luna_elf_symbols_find_global(context, name);
    if (global == NULL) {
        return false;
    }
    const LunaElfLinkObject *object =
        luna_vector_at_const(&context->objects, global->object_index);
    LunaElfLinkSymbol symbol = {0};
    if (object == NULL ||
        !luna_elf_link_read_symbol(object, global->symbol_index, &symbol) ||
        !luna_elf_symbols_resolve_defined(context, object, &symbol, address)) {
        return false;
    }
    if (weak != NULL) {
        *weak = global->weak;
    }
    return true;
}

bool luna_elf_link_resolve_symbol(LunaElfLinkContext *context,
                                  LunaElfLinkObject *object,
                                  uint32_t symbol_index, uint64_t *address) {
    LunaElfLinkSymbol symbol = {0};
    if (!luna_elf_link_read_symbol(object, symbol_index, &symbol)) {
        return luna_elf_link_error(context, object,
                                   "invalid relocation symbol %" PRIu32,
                                   symbol_index);
    }
    if (symbol.binding == LUNA_ELF_LINK_SYMBOL_LOCAL) {
        if (symbol.section_index == LUNA_ELF_LINK_SYMBOL_UNDEFINED ||
            !luna_elf_symbols_resolve_defined(context, object, &symbol,
                                              address)) {
            return luna_elf_link_error(
                context, object, "unresolvable local symbol at index %" PRIu32,
                symbol_index);
        }
        return true;
    }
    if (luna_elf_symbols_resolve_global(context, symbol.name, address, NULL)) {
        return true;
    }
    if (symbol.binding == LUNA_ELF_LINK_SYMBOL_WEAK &&
        symbol.section_index == LUNA_ELF_LINK_SYMBOL_UNDEFINED) {
        *address = 0U;
        return true;
    }
    return luna_elf_link_error(context, object, "undefined symbol '%.*s'",
                               (int)symbol.name.length, symbol.name.data);
}

bool luna_elf_link_resolve_entry(LunaElfLinkContext *context,
                                 LunaStringView entry_symbol) {
    LunaElfLinkGlobal *global =
        luna_elf_symbols_find_global(context, entry_symbol);
    uint64_t address = 0U;
    if (global == NULL) {
        return luna_elf_link_error(context, NULL,
                                   "entry symbol '%.*s' is undefined",
                                   (int)entry_symbol.length, entry_symbol.data);
    }
    const LunaElfLinkObject *object =
        luna_vector_at_const(&context->objects, global->object_index);
    LunaElfLinkSymbol symbol = {0};
    const LunaElfLinkSection *section = NULL;
    if (object != NULL &&
        luna_elf_link_read_symbol(object, global->symbol_index, &symbol) &&
        symbol.section_index != LUNA_ELF_LINK_SYMBOL_ABSOLUTE &&
        symbol.section_index != LUNA_ELF_LINK_SYMBOL_UNDEFINED) {
        section = luna_vector_at_const(&object->sections, symbol.section_index);
    }
    if (object == NULL || section == NULL ||
        section->region != LUNA_ELF_LINK_REGION_TEXT ||
        symbol.value >= section->size ||
        !luna_elf_symbols_resolve_defined(context, object, &symbol, &address)) {
        return luna_elf_link_error(context, NULL,
                                   "entry symbol '%.*s' is not executable code",
                                   (int)entry_symbol.length, entry_symbol.data);
    }
    context->entry_address = address;
    return true;
}
