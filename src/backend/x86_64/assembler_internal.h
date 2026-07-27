#ifndef LUNA_X86_64_ASSEMBLER_INTERNAL_H
#define LUNA_X86_64_ASSEMBLER_INTERNAL_H

#include "elf_object_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum LunaX8664AssemblyFixupKind {
    LUNA_X86_64_ASSEMBLY_FIXUP_BRANCH,
    LUNA_X86_64_ASSEMBLY_FIXUP_CALL,
    LUNA_X86_64_ASSEMBLY_FIXUP_RIP_RELATIVE
} LunaX8664AssemblyFixupKind;

typedef struct LunaX8664AssemblyFixup {
    LunaStringView target;
    LunaX8664ObjectSection section;
    uint64_t offset;
    size_t line;
    uint32_t numeric_label;
    LunaX8664AssemblyFixupKind kind;
    bool is_numeric;
    bool numeric_forward;
} LunaX8664AssemblyFixup;

typedef struct LunaX8664AssemblyNumericLabel {
    LunaX8664ObjectSection section;
    uint64_t offset;
    uint32_t number;
} LunaX8664AssemblyNumericLabel;

typedef struct LunaX8664Assembler {
    LunaX8664ObjectImage *image;
    LunaDiagnosticEngine *diagnostics;
    LunaX8664ObjectSection section;
    LunaVector fixups;
    LunaVector numeric_labels;
    size_t line;
} LunaX8664Assembler;

bool luna_x86_64_assembler_error(LunaX8664Assembler *assembler,
                                 const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);
LunaStringBuilder *
luna_x86_64_assembler_current_output(LunaX8664Assembler *assembler);
bool luna_x86_64_assembler_append_u8(LunaX8664Assembler *assembler,
                                     uint8_t value);
bool luna_x86_64_assembler_append_u16(LunaX8664Assembler *assembler,
                                      uint16_t value);
bool luna_x86_64_assembler_append_u32(LunaX8664Assembler *assembler,
                                      uint32_t value);
bool luna_x86_64_assembler_append_u64(LunaX8664Assembler *assembler,
                                      uint64_t value);
bool luna_x86_64_assembler_add_fixup(LunaX8664Assembler *assembler,
                                     LunaStringView target, uint64_t offset,
                                     LunaX8664AssemblyFixupKind kind);
bool luna_x86_64_encode_instruction(LunaX8664Assembler *assembler,
                                    LunaStringView mnemonic,
                                    LunaStringView operands);

#endif
