#ifndef LUNA_X86_64_ELF_OBJECT_INTERNAL_H
#define LUNA_X86_64_ELF_OBJECT_INTERNAL_H

#include "luna/backend/debug/debug_ir.h"
#include "luna/backend/x86_64/elf_object.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum LunaX8664ObjectSection {
    LUNA_X86_64_OBJECT_SECTION_UNDEFINED = 0,
    LUNA_X86_64_OBJECT_SECTION_TEXT,
    LUNA_X86_64_OBJECT_SECTION_RODATA,
    LUNA_X86_64_OBJECT_SECTION_DATA
} LunaX8664ObjectSection;

typedef enum LunaX8664ObjectSymbolType {
    LUNA_X86_64_OBJECT_SYMBOL_NONE = 0,
    LUNA_X86_64_OBJECT_SYMBOL_OBJECT,
    LUNA_X86_64_OBJECT_SYMBOL_FUNCTION
} LunaX8664ObjectSymbolType;

typedef struct LunaX8664ObjectSymbol {
    LunaStringView name;
    LunaX8664ObjectSection section;
    uint64_t value;
    uint64_t size;
    LunaX8664ObjectSymbolType type;
    bool defined;
    bool global;
    bool external;
} LunaX8664ObjectSymbol;

typedef enum LunaX8664ObjectRelocationType {
    LUNA_X86_64_OBJECT_RELOCATION_PC32 = 2,
    LUNA_X86_64_OBJECT_RELOCATION_PLT32 = 4
} LunaX8664ObjectRelocationType;

typedef struct LunaX8664ObjectRelocation {
    LunaX8664ObjectSection section;
    uint64_t offset;
    size_t symbol_index;
    int64_t addend;
    LunaX8664ObjectRelocationType type;
} LunaX8664ObjectRelocation;

typedef struct LunaX8664ObjectImage {
    LunaStringBuilder text;
    LunaStringBuilder rodata;
    LunaStringBuilder data;
    LunaDebugIr debug_ir;
    LunaVector symbols;
    LunaVector relocations;
    uint64_t text_alignment;
    uint64_t rodata_alignment;
    uint64_t data_alignment;
} LunaX8664ObjectImage;

void luna_x86_64_object_image_init(LunaX8664ObjectImage *image);
void luna_x86_64_object_image_destroy(LunaX8664ObjectImage *image);
LunaStringBuilder *
luna_x86_64_object_section_builder(LunaX8664ObjectImage *image,
                                   LunaX8664ObjectSection section);
const LunaStringBuilder *
luna_x86_64_object_section_builder_const(const LunaX8664ObjectImage *image,
                                         LunaX8664ObjectSection section);
bool luna_x86_64_elf_object_serialize(const LunaX8664ObjectImage *image,
                                      LunaDiagnosticEngine *diagnostics,
                                      LunaStringBuilder *output);

#endif
