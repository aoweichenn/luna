#ifndef LUNA_X86_64_ELF_LINKER_INTERNAL_H
#define LUNA_X86_64_ELF_LINKER_INTERNAL_H

#include "luna/backend/debug/debug_ir.h"
#include "luna/backend/x86_64/elf_linker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LUNA_ELF_LINK_HEADER_SIZE = 64,
    LUNA_ELF_LINK_PROGRAM_HEADER_SIZE = 56,
    LUNA_ELF_LINK_SECTION_HEADER_SIZE = 64,
    LUNA_ELF_LINK_SYMBOL_SIZE = 24,
    LUNA_ELF_LINK_RELOCATION_SIZE = 24,
    LUNA_ELF_LINK_MACHINE_X86_64 = 62,
    LUNA_ELF_LINK_PAGE_SIZE = 4096,
    LUNA_ELF_LINK_MAX_ALIGNMENT = 4096
};

enum {
    LUNA_ELF_LINK_SECTION_NULL = 0,
    LUNA_ELF_LINK_SECTION_PROGBITS = 1,
    LUNA_ELF_LINK_SECTION_SYMTAB = 2,
    LUNA_ELF_LINK_SECTION_STRTAB = 3,
    LUNA_ELF_LINK_SECTION_RELA = 4,
    LUNA_ELF_LINK_SECTION_NOBITS = 8,
    LUNA_ELF_LINK_SECTION_REL = 9
};

enum {
    LUNA_ELF_LINK_FLAG_WRITE = 1,
    LUNA_ELF_LINK_FLAG_ALLOC = 2,
    LUNA_ELF_LINK_FLAG_EXECUTE = 4,
    LUNA_ELF_LINK_FLAG_TLS = 0x400,
    LUNA_ELF_LINK_FLAG_COMPRESSED = 0x800
};

enum {
    LUNA_ELF_LINK_SYMBOL_LOCAL = 0,
    LUNA_ELF_LINK_SYMBOL_GLOBAL = 1,
    LUNA_ELF_LINK_SYMBOL_WEAK = 2,
    LUNA_ELF_LINK_SYMBOL_UNDEFINED = 0,
    LUNA_ELF_LINK_SYMBOL_ABSOLUTE = 0xfff1,
    LUNA_ELF_LINK_SYMBOL_COMMON = 0xfff2,
    LUNA_ELF_LINK_SYMBOL_TYPE_FILE = 4
};

enum {
    LUNA_ELF_LINK_RELOCATION_NONE = 0,
    LUNA_ELF_LINK_RELOCATION_64 = 1,
    LUNA_ELF_LINK_RELOCATION_PC32 = 2,
    LUNA_ELF_LINK_RELOCATION_PLT32 = 4,
    LUNA_ELF_LINK_RELOCATION_32 = 10,
    LUNA_ELF_LINK_RELOCATION_32S = 11
};

typedef enum LunaElfLinkRegion {
    LUNA_ELF_LINK_REGION_NONE,
    LUNA_ELF_LINK_REGION_TEXT,
    LUNA_ELF_LINK_REGION_RODATA,
    LUNA_ELF_LINK_REGION_DATA,
    LUNA_ELF_LINK_REGION_BSS
} LunaElfLinkRegion;

typedef struct LunaElfLinkSection {
    LunaStringView name;
    uint32_t type;
    uint64_t flags;
    uint64_t file_offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
    LunaElfLinkRegion region;
    uint64_t region_offset;
} LunaElfLinkSection;

typedef struct LunaElfLinkObject {
    LunaStringView name;
    LunaStringView bytes;
    LunaVector sections;
    LunaDebugIr debug_ir;
    uint16_t symbol_table_index;
    bool has_debug_ir;
} LunaElfLinkObject;

typedef struct LunaElfLinkSymbol {
    LunaStringView name;
    uint8_t binding;
    uint8_t type;
    uint8_t visibility;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} LunaElfLinkSymbol;

typedef struct LunaElfLinkGlobal {
    LunaStringView name;
    uint32_t object_index;
    uint32_t symbol_index;
    bool weak;
} LunaElfLinkGlobal;

typedef struct LunaElfLinkContext {
    FILE *diagnostic_stream;
    LunaVector objects;
    LunaVector globals;
    LunaVector global_slots;
    LunaStringBuilder text;
    LunaStringBuilder rodata;
    LunaStringBuilder data;
    LunaStringBuilder debug_abbrev;
    LunaStringBuilder debug_info;
    LunaStringBuilder debug_line;
    LunaStringBuilder debug_str;
    LunaStringBuilder debug_line_str;
    uint64_t bss_size;
    uint64_t text_alignment;
    uint64_t rodata_alignment;
    uint64_t data_alignment;
    uint64_t bss_alignment;
    uint64_t text_file_offset;
    uint64_t rodata_file_offset;
    uint64_t data_file_offset;
    uint64_t text_address;
    uint64_t rodata_address;
    uint64_t data_address;
    uint64_t bss_address;
    uint64_t entry_address;
} LunaElfLinkContext;

void luna_elf_link_context_init(LunaElfLinkContext *context,
                                FILE *diagnostic_stream);
void luna_elf_link_context_destroy(LunaElfLinkContext *context);

bool luna_elf_link_error(LunaElfLinkContext *context,
                         const LunaElfLinkObject *object, const char *format,
                         ...) LUNA_PRINTF_LIKE(3, 4);
bool luna_elf_link_verify_error(FILE *stream, const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);

bool luna_elf_link_range_valid(uint64_t offset, uint64_t size,
                               uint64_t total_size);
bool luna_elf_link_is_power_of_two(uint64_t value);
bool luna_elf_link_align_up(uint64_t value, uint64_t alignment,
                            uint64_t *result);
bool luna_elf_link_read_u8(LunaStringView bytes, uint64_t offset,
                           uint8_t *value);
bool luna_elf_link_read_u16(LunaStringView bytes, uint64_t offset,
                            uint16_t *value);
bool luna_elf_link_read_u32(LunaStringView bytes, uint64_t offset,
                            uint32_t *value);
bool luna_elf_link_read_u64(LunaStringView bytes, uint64_t offset,
                            uint64_t *value);
bool luna_elf_link_read_i64(LunaStringView bytes, uint64_t offset,
                            int64_t *value);
bool luna_elf_link_string(LunaStringView bytes, uint64_t table_offset,
                          uint64_t table_size, uint32_t string_offset,
                          LunaStringView *result);

bool luna_elf_link_parse_object(LunaElfLinkContext *context,
                                const LunaX8664ElfLinkInput *input);
bool luna_elf_link_read_symbol(const LunaElfLinkObject *object,
                               uint32_t symbol_index,
                               LunaElfLinkSymbol *symbol);

bool luna_elf_link_layout_regions(LunaElfLinkContext *context);
bool luna_elf_link_collect_globals(LunaElfLinkContext *context);
bool luna_elf_link_layout_executable(LunaElfLinkContext *context);
bool luna_elf_link_resolve_symbol(LunaElfLinkContext *context,
                                  LunaElfLinkObject *object,
                                  uint32_t symbol_index, uint64_t *address);
bool luna_elf_link_apply_relocations(LunaElfLinkContext *context);
bool luna_elf_link_resolve_entry(LunaElfLinkContext *context,
                                 LunaStringView entry_symbol);
bool luna_elf_link_build_debug(LunaElfLinkContext *context);
bool luna_elf_link_serialize_executable(LunaElfLinkContext *context,
                                        LunaStringBuilder *output);

#endif
