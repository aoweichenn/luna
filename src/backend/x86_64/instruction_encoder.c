#include "assembler_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum LunaX8664RegisterClass {
    LUNA_X86_64_REGISTER_GPR,
    LUNA_X86_64_REGISTER_XMM
} LunaX8664RegisterClass;

typedef struct LunaX8664Register {
    LunaX8664RegisterClass class;
    uint8_t code;
    uint8_t width;
} LunaX8664Register;

typedef struct LunaX8664MemoryOperand {
    LunaStringView symbol;
    int64_t displacement;
    LunaX8664Register base;
    LunaX8664Register index;
    uint8_t scale;
    bool rip_relative;
    bool has_index;
} LunaX8664MemoryOperand;

typedef enum LunaX8664OperandKind {
    LUNA_X86_64_OPERAND_NONE,
    LUNA_X86_64_OPERAND_IMMEDIATE,
    LUNA_X86_64_OPERAND_REGISTER,
    LUNA_X86_64_OPERAND_MEMORY,
    LUNA_X86_64_OPERAND_LABEL
} LunaX8664OperandKind;

typedef struct LunaX8664Operand {
    LunaX8664OperandKind kind;
    uint64_t immediate;
    LunaX8664Register reg;
    LunaX8664MemoryOperand memory;
    LunaStringView label;
} LunaX8664Operand;

typedef struct LunaX8664OperandList {
    LunaX8664Operand values[3];
    uint32_t count;
} LunaX8664OperandList;

typedef struct LunaX8664RegisterName {
    const char *name;
    uint8_t code;
    uint8_t width;
} LunaX8664RegisterName;

static bool luna_encoder_is_space(char character) {
    return character == ' ' || character == '\t' || character == '\r';
}

static LunaStringView luna_encoder_trim(LunaStringView view) {
    while (view.length > 0U && luna_encoder_is_space(view.data[0])) {
        view.data += 1;
        view.length -= 1U;
    }
    while (view.length > 0U &&
           luna_encoder_is_space(view.data[view.length - 1U])) {
        view.length -= 1U;
    }
    return view;
}

static size_t luna_encoder_find(LunaStringView view, char character) {
    for (size_t index = 0U; index < view.length; index += 1U) {
        if (view.data[index] == character) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool luna_encoder_parse_bits(LunaStringView view, uint64_t *value) {
    view = luna_encoder_trim(view);
    if (view.length == 0U || view.length >= 64U || value == NULL) {
        return false;
    }
    char text[64];
    memcpy(text, view.data, view.length);
    text[view.length] = '\0';
    errno = 0;
    char *end = NULL;
    if (view.data[0] == '-') {
        const long long parsed = strtoll(text, &end, 0);
        if (errno != 0 || end == text || *end != '\0') {
            return false;
        }
        const int64_t signed_value = (int64_t)parsed;
        memcpy(value, &signed_value, sizeof(signed_value));
        return true;
    }
    const unsigned long long parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static bool luna_encoder_parse_i64(LunaStringView view, int64_t *value) {
    uint64_t bits = 0U;
    if (!luna_encoder_parse_bits(view, &bits)) {
        return false;
    }
    memcpy(value, &bits, sizeof(bits));
    return true;
}

static bool luna_encoder_parse_register(LunaStringView view,
                                        LunaX8664Register *reg) {
    static const LunaX8664RegisterName names[] = {
        {"al", 0U, 8U},   {"cl", 1U, 8U},   {"dl", 2U, 8U},   {"bl", 3U, 8U},
        {"spl", 4U, 8U},  {"bpl", 5U, 8U},  {"sil", 6U, 8U},  {"dil", 7U, 8U},
        {"ax", 0U, 16U},  {"cx", 1U, 16U},  {"dx", 2U, 16U},  {"bx", 3U, 16U},
        {"sp", 4U, 16U},  {"bp", 5U, 16U},  {"si", 6U, 16U},  {"di", 7U, 16U},
        {"eax", 0U, 32U}, {"ecx", 1U, 32U}, {"edx", 2U, 32U}, {"ebx", 3U, 32U},
        {"esp", 4U, 32U}, {"ebp", 5U, 32U}, {"esi", 6U, 32U}, {"edi", 7U, 32U},
        {"rax", 0U, 64U}, {"rcx", 1U, 64U}, {"rdx", 2U, 64U}, {"rbx", 3U, 64U},
        {"rsp", 4U, 64U}, {"rbp", 5U, 64U}, {"rsi", 6U, 64U}, {"rdi", 7U, 64U},
    };
    view = luna_encoder_trim(view);
    if (view.length < 2U || view.data[0] != '%' || reg == NULL) {
        return false;
    }
    view.data += 1;
    view.length -= 1U;
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]);
         index += 1U) {
        if (luna_string_view_equal_c_string(view, names[index].name)) {
            *reg = (LunaX8664Register){
                .class = LUNA_X86_64_REGISTER_GPR,
                .code = names[index].code,
                .width = names[index].width,
            };
            return true;
        }
    }

    bool xmm = false;
    size_t digit_offset = 0U;
    if (view.length >= 4U && memcmp(view.data, "xmm", 3U) == 0) {
        xmm = true;
        digit_offset = 3U;
    } else if (view.length >= 2U && view.data[0] == 'r') {
        digit_offset = 1U;
    } else {
        return false;
    }
    size_t digit_end = digit_offset;
    uint32_t code = 0U;
    while (digit_end < view.length && view.data[digit_end] >= '0' &&
           view.data[digit_end] <= '9') {
        code = code * 10U + (uint32_t)(view.data[digit_end] - '0');
        if (code > 15U) {
            return false;
        }
        digit_end += 1U;
    }
    if (digit_end == digit_offset || code > 15U) {
        return false;
    }
    if (xmm) {
        if (digit_end != view.length) {
            return false;
        }
        *reg = (LunaX8664Register){
            .class = LUNA_X86_64_REGISTER_XMM,
            .code = (uint8_t)code,
            .width = 128U,
        };
        return true;
    }
    uint8_t width = 64U;
    if (digit_end < view.length) {
        if (digit_end + 1U != view.length) {
            return false;
        }
        switch (view.data[digit_end]) {
        case 'b':
            width = 8U;
            break;
        case 'w':
            width = 16U;
            break;
        case 'd':
            width = 32U;
            break;
        default:
            return false;
        }
    }
    if (code < 8U) {
        return false;
    }
    *reg = (LunaX8664Register){
        .class = LUNA_X86_64_REGISTER_GPR,
        .code = (uint8_t)code,
        .width = width,
    };
    return true;
}

static bool luna_encoder_parse_memory(LunaStringView view,
                                      LunaX8664MemoryOperand *memory) {
    const size_t open = luna_encoder_find(view, '(');
    if (open == SIZE_MAX || view.length < open + 3U ||
        view.data[view.length - 1U] != ')' || memory == NULL) {
        return false;
    }
    *memory = (LunaX8664MemoryOperand){
        .symbol = {0},
        .displacement = 0,
        .base = {0},
        .index = {0},
        .scale = 1U,
        .rip_relative = false,
        .has_index = false,
    };
    LunaStringView displacement = {
        .data = view.data,
        .length = open,
    };
    displacement = luna_encoder_trim(displacement);
    LunaStringView inside = {
        .data = view.data + open + 1U,
        .length = view.length - open - 2U,
    };

    LunaStringView pieces[3] = {{0}};
    uint32_t piece_count = 0U;
    size_t start = 0U;
    for (size_t index = 0U; index <= inside.length; index += 1U) {
        if (index == inside.length || inside.data[index] == ',') {
            if (piece_count >= 3U) {
                return false;
            }
            pieces[piece_count] = luna_encoder_trim((LunaStringView){
                .data = inside.data + start,
                .length = index - start,
            });
            piece_count += 1U;
            start = index + 1U;
        }
    }
    if (piece_count == 0U ||
        luna_string_view_equal_c_string(pieces[0], "%rip")) {
        if (piece_count != 1U || displacement.length == 0U) {
            return false;
        }
        memory->rip_relative = true;
        memory->symbol = displacement;
        return true;
    }
    if (!luna_encoder_parse_register(pieces[0], &memory->base) ||
        memory->base.class != LUNA_X86_64_REGISTER_GPR ||
        memory->base.width != 64U) {
        return false;
    }
    if (displacement.length > 0U &&
        !luna_encoder_parse_i64(displacement, &memory->displacement)) {
        return false;
    }
    if (memory->displacement < INT32_MIN || memory->displacement > INT32_MAX) {
        return false;
    }
    if (piece_count >= 2U && pieces[1].length > 0U) {
        if (!luna_encoder_parse_register(pieces[1], &memory->index) ||
            memory->index.class != LUNA_X86_64_REGISTER_GPR ||
            memory->index.width != 64U || memory->index.code == 4U) {
            return false;
        }
        memory->has_index = true;
    }
    if (piece_count == 3U) {
        uint64_t scale = 0U;
        if (!memory->has_index || !luna_encoder_parse_bits(pieces[2], &scale) ||
            (scale != 1U && scale != 2U && scale != 4U && scale != 8U)) {
            return false;
        }
        memory->scale = (uint8_t)scale;
    }
    return true;
}

static bool luna_encoder_parse_operand(LunaStringView view,
                                       LunaX8664Operand *operand) {
    view = luna_encoder_trim(view);
    *operand = (LunaX8664Operand){0};
    if (view.length == 0U) {
        return false;
    }
    if (view.data[0] == '$') {
        const LunaStringView immediate = {
            .data = view.data + 1U,
            .length = view.length - 1U,
        };
        operand->kind = LUNA_X86_64_OPERAND_IMMEDIATE;
        return luna_encoder_parse_bits(immediate, &operand->immediate);
    }
    if (view.data[0] == '%') {
        operand->kind = LUNA_X86_64_OPERAND_REGISTER;
        return luna_encoder_parse_register(view, &operand->reg);
    }
    if (luna_encoder_find(view, '(') != SIZE_MAX) {
        operand->kind = LUNA_X86_64_OPERAND_MEMORY;
        return luna_encoder_parse_memory(view, &operand->memory);
    }
    operand->kind = LUNA_X86_64_OPERAND_LABEL;
    operand->label = view;
    return true;
}

static bool luna_encoder_parse_operands(LunaStringView text,
                                        LunaX8664OperandList *operands) {
    *operands = (LunaX8664OperandList){0};
    text = luna_encoder_trim(text);
    if (text.length == 0U) {
        return true;
    }
    uint32_t depth = 0U;
    size_t start = 0U;
    for (size_t index = 0U; index <= text.length; index += 1U) {
        if (index < text.length && text.data[index] == '(') {
            depth += 1U;
        } else if (index < text.length && text.data[index] == ')') {
            if (depth == 0U) {
                return false;
            }
            depth -= 1U;
        }
        if (index == text.length || (text.data[index] == ',' && depth == 0U)) {
            if (operands->count >= 3U ||
                !luna_encoder_parse_operand(
                    (LunaStringView){
                        .data = text.data + start,
                        .length = index - start,
                    },
                    &operands->values[operands->count])) {
                return false;
            }
            operands->count += 1U;
            start = index + 1U;
        }
    }
    return depth == 0U;
}

static bool luna_encoder_is_gpr(const LunaX8664Operand *operand,
                                uint8_t width) {
    return operand->kind == LUNA_X86_64_OPERAND_REGISTER &&
           operand->reg.class == LUNA_X86_64_REGISTER_GPR &&
           operand->reg.width == width;
}

static bool luna_encoder_is_xmm(const LunaX8664Operand *operand) {
    return operand->kind == LUNA_X86_64_OPERAND_REGISTER &&
           operand->reg.class == LUNA_X86_64_REGISTER_XMM;
}

static bool luna_encoder_is_gpr_rm(const LunaX8664Operand *operand,
                                   uint8_t width) {
    return operand->kind == LUNA_X86_64_OPERAND_MEMORY ||
           luna_encoder_is_gpr(operand, width);
}

static uint8_t luna_encoder_rex_for_rm(const LunaX8664Operand *operand) {
    if (operand->kind == LUNA_X86_64_OPERAND_REGISTER) {
        return operand->reg.code >= 8U ? 1U : 0U;
    }
    if (operand->kind != LUNA_X86_64_OPERAND_MEMORY ||
        operand->memory.rip_relative) {
        return 0U;
    }
    uint8_t rex = operand->memory.base.code >= 8U ? 1U : 0U;
    if (operand->memory.has_index && operand->memory.index.code >= 8U) {
        rex |= 2U;
    }
    return rex;
}

static bool luna_encoder_emit_prefix_opcode(LunaX8664Assembler *assembler,
                                            uint8_t legacy_prefix, bool rex_w,
                                            bool force_rex, uint8_t reg_field,
                                            const LunaX8664Operand *rm,
                                            const uint8_t *opcode,
                                            size_t opcode_length) {
    if (legacy_prefix != 0U &&
        !luna_x86_64_assembler_append_u8(assembler, legacy_prefix)) {
        return false;
    }
    uint8_t rex = UINT8_C(0x40);
    if (rex_w) {
        rex |= UINT8_C(0x08);
    }
    if (reg_field >= 8U) {
        rex |= UINT8_C(0x04);
    }
    rex |= luna_encoder_rex_for_rm(rm);
    if ((rex != UINT8_C(0x40) || force_rex) &&
        !luna_x86_64_assembler_append_u8(assembler, rex)) {
        return false;
    }
    return luna_string_builder_append(
        luna_x86_64_assembler_current_output(assembler), (const char *)opcode,
        opcode_length);
}

static bool luna_encoder_emit_modrm(LunaX8664Assembler *assembler,
                                    uint8_t reg_field,
                                    const LunaX8664Operand *rm) {
    if (rm->kind == LUNA_X86_64_OPERAND_REGISTER) {
        return luna_x86_64_assembler_append_u8(
            assembler,
            (uint8_t)(UINT8_C(0xc0) | ((reg_field & UINT8_C(7)) << 3U) |
                      (rm->reg.code & UINT8_C(7))));
    }
    if (rm->kind != LUNA_X86_64_OPERAND_MEMORY) {
        return false;
    }
    if (rm->memory.rip_relative) {
        const uint8_t modrm =
            (uint8_t)(((reg_field & UINT8_C(7)) << 3U) | UINT8_C(5));
        if (!luna_x86_64_assembler_append_u8(assembler, modrm)) {
            return false;
        }
        LunaStringBuilder *output =
            luna_x86_64_assembler_current_output(assembler);
        if (output == NULL) {
            return false;
        }
        const uint64_t offset = (uint64_t)output->length;
        return luna_x86_64_assembler_add_fixup(
                   assembler, rm->memory.symbol, offset,
                   LUNA_X86_64_ASSEMBLY_FIXUP_RIP_RELATIVE) &&
               luna_x86_64_assembler_append_u32(assembler, 0U);
    }

    const uint8_t base = (uint8_t)(rm->memory.base.code & UINT8_C(7));
    uint8_t mod = 0U;
    if (rm->memory.displacement == 0 && base != 5U) {
        mod = 0U;
    } else if (rm->memory.displacement >= -128 &&
               rm->memory.displacement <= 127) {
        mod = 1U;
    } else {
        mod = 2U;
    }
    const bool sib_required = base == 4U || rm->memory.has_index;
    const uint8_t rm_field = sib_required ? 4U : base;
    const uint8_t modrm =
        (uint8_t)((mod << 6U) | ((reg_field & UINT8_C(7)) << 3U) | rm_field);
    if (!luna_x86_64_assembler_append_u8(assembler, modrm)) {
        return false;
    }
    if (sib_required) {
        uint8_t scale_bits = 0U;
        if (rm->memory.scale == 2U) {
            scale_bits = 1U;
        } else if (rm->memory.scale == 4U) {
            scale_bits = 2U;
        } else if (rm->memory.scale == 8U) {
            scale_bits = 3U;
        }
        const uint8_t index =
            rm->memory.has_index ? (uint8_t)(rm->memory.index.code & UINT8_C(7))
                                 : UINT8_C(4);
        const uint8_t sib =
            (uint8_t)((scale_bits << 6U) | (index << 3U) | base);
        if (!luna_x86_64_assembler_append_u8(assembler, sib)) {
            return false;
        }
    }
    if (mod == 1U) {
        return luna_x86_64_assembler_append_u8(
            assembler, (uint8_t)(int8_t)rm->memory.displacement);
    }
    if (mod == 2U) {
        return luna_x86_64_assembler_append_u32(
            assembler, (uint32_t)(int32_t)rm->memory.displacement);
    }
    return true;
}

static bool luna_encoder_emit_rm_instruction(
    LunaX8664Assembler *assembler, uint8_t legacy_prefix, bool rex_w,
    bool force_rex, const uint8_t *opcode, size_t opcode_length,
    uint8_t reg_field, const LunaX8664Operand *rm) {
    return luna_encoder_emit_prefix_opcode(assembler, legacy_prefix, rex_w,
                                           force_rex, reg_field, rm, opcode,
                                           opcode_length) &&
           luna_encoder_emit_modrm(assembler, reg_field, rm);
}

static bool luna_encoder_emit_relative(LunaX8664Assembler *assembler,
                                       const uint8_t *opcode,
                                       size_t opcode_length,
                                       const LunaX8664OperandList *operands,
                                       LunaX8664AssemblyFixupKind kind) {
    if (operands->count != 1U ||
        operands->values[0].kind != LUNA_X86_64_OPERAND_LABEL ||
        !luna_string_builder_append(
            luna_x86_64_assembler_current_output(assembler),
            (const char *)opcode, opcode_length)) {
        return false;
    }
    LunaStringBuilder *output = luna_x86_64_assembler_current_output(assembler);
    const uint64_t offset = (uint64_t)output->length;
    return luna_x86_64_assembler_add_fixup(assembler, operands->values[0].label,
                                           offset, kind) &&
           luna_x86_64_assembler_append_u32(assembler, 0U);
}

static bool luna_encoder_immediate_fits_i32(uint64_t bits) {
    int64_t value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value >= INT32_MIN && value <= INT32_MAX;
}

static bool luna_encoder_immediate_fits_u32(uint64_t bits) {
    return bits <= UINT32_MAX || luna_encoder_immediate_fits_i32(bits);
}

static bool luna_encoder_immediate_fits_u16(uint64_t bits) {
    int64_t value = 0;
    memcpy(&value, &bits, sizeof(value));
    return bits <= UINT16_MAX || (value >= INT16_MIN && value <= INT16_MAX);
}

static bool luna_encoder_immediate_fits_u8(uint64_t bits) {
    int64_t value = 0;
    memcpy(&value, &bits, sizeof(value));
    return bits <= UINT8_MAX || (value >= INT8_MIN && value <= INT8_MAX);
}

static bool luna_encoder_emit_binary_alu(LunaX8664Assembler *assembler,
                                         const LunaX8664OperandList *operands,
                                         uint8_t width, uint8_t register_opcode,
                                         uint8_t immediate_group) {
    if (operands->count != 2U ||
        !luna_encoder_is_gpr_rm(&operands->values[1], width)) {
        return false;
    }
    const LunaX8664Operand *source = &operands->values[0];
    const LunaX8664Operand *destination = &operands->values[1];
    const bool byte = width == 8U;
    const bool word = width == 16U;
    const bool rex_w = width == 64U;
    const uint8_t legacy = word ? UINT8_C(0x66) : 0U;
    if (source->kind == LUNA_X86_64_OPERAND_IMMEDIATE) {
        if ((width == 64U &&
             !luna_encoder_immediate_fits_i32(source->immediate)) ||
            (width == 32U &&
             !luna_encoder_immediate_fits_u32(source->immediate)) ||
            (width == 16U &&
             !luna_encoder_immediate_fits_u16(source->immediate)) ||
            (width == 8U &&
             !luna_encoder_immediate_fits_u8(source->immediate))) {
            return false;
        }
        const uint8_t opcode = byte ? UINT8_C(0x80) : UINT8_C(0x81);
        if (!luna_encoder_emit_rm_instruction(
                assembler, legacy, rex_w,
                byte && destination->kind == LUNA_X86_64_OPERAND_REGISTER &&
                    destination->reg.code >= 4U,
                &opcode, 1U, immediate_group, destination)) {
            return false;
        }
        if (byte) {
            return luna_x86_64_assembler_append_u8(assembler,
                                                   (uint8_t)source->immediate);
        }
        if (word) {
            return luna_x86_64_assembler_append_u16(
                assembler, (uint16_t)source->immediate);
        }
        return luna_x86_64_assembler_append_u32(assembler,
                                                (uint32_t)source->immediate);
    }
    if (!luna_encoder_is_gpr(source, width)) {
        return false;
    }
    const bool force_rex =
        byte && (source->reg.code >= 4U ||
                 (destination->kind == LUNA_X86_64_OPERAND_REGISTER &&
                  destination->reg.code >= 4U));
    return luna_encoder_emit_rm_instruction(assembler, legacy, rex_w, force_rex,
                                            &register_opcode, 1U,
                                            source->reg.code, destination);
}

static bool luna_encoder_emit_move(LunaX8664Assembler *assembler,
                                   const LunaX8664OperandList *operands,
                                   uint8_t width) {
    if (operands->count != 2U) {
        return false;
    }
    const LunaX8664Operand *source = &operands->values[0];
    const LunaX8664Operand *destination = &operands->values[1];
    const bool byte = width == 8U;
    const bool word = width == 16U;
    const bool rex_w = width == 64U;
    const uint8_t legacy = word ? UINT8_C(0x66) : 0U;
    if (source->kind == LUNA_X86_64_OPERAND_IMMEDIATE) {
        if (!luna_encoder_is_gpr_rm(destination, width) ||
            (width == 64U &&
             !luna_encoder_immediate_fits_i32(source->immediate)) ||
            (width == 32U &&
             !luna_encoder_immediate_fits_u32(source->immediate)) ||
            (width == 16U &&
             !luna_encoder_immediate_fits_u16(source->immediate)) ||
            (width == 8U &&
             !luna_encoder_immediate_fits_u8(source->immediate))) {
            return false;
        }
        const uint8_t opcode = byte ? UINT8_C(0xc6) : UINT8_C(0xc7);
        const bool force_rex =
            byte && destination->kind == LUNA_X86_64_OPERAND_REGISTER &&
            destination->reg.code >= 4U;
        if (!luna_encoder_emit_rm_instruction(assembler, legacy, rex_w,
                                              force_rex, &opcode, 1U, 0U,
                                              destination)) {
            return false;
        }
        if (byte) {
            return luna_x86_64_assembler_append_u8(assembler,
                                                   (uint8_t)source->immediate);
        }
        if (word) {
            return luna_x86_64_assembler_append_u16(
                assembler, (uint16_t)source->immediate);
        }
        return luna_x86_64_assembler_append_u32(assembler,
                                                (uint32_t)source->immediate);
    }
    if (luna_encoder_is_gpr(source, width) &&
        luna_encoder_is_gpr_rm(destination, width)) {
        const uint8_t opcode = byte ? UINT8_C(0x88) : UINT8_C(0x89);
        const bool force_rex =
            byte && (source->reg.code >= 4U ||
                     (destination->kind == LUNA_X86_64_OPERAND_REGISTER &&
                      destination->reg.code >= 4U));
        return luna_encoder_emit_rm_instruction(assembler, legacy, rex_w,
                                                force_rex, &opcode, 1U,
                                                source->reg.code, destination);
    }
    if (source->kind == LUNA_X86_64_OPERAND_MEMORY &&
        luna_encoder_is_gpr(destination, width)) {
        const uint8_t opcode = byte ? UINT8_C(0x8a) : UINT8_C(0x8b);
        const bool force_rex = byte && destination->reg.code >= 4U;
        return luna_encoder_emit_rm_instruction(assembler, legacy, rex_w,
                                                force_rex, &opcode, 1U,
                                                destination->reg.code, source);
    }
    return false;
}

static bool luna_encoder_emit_xmm_move(LunaX8664Assembler *assembler,
                                       const LunaX8664OperandList *operands,
                                       bool double_precision) {
    if (operands->count != 2U) {
        return false;
    }
    const LunaX8664Operand *source = &operands->values[0];
    const LunaX8664Operand *destination = &operands->values[1];
    const uint8_t prefix = double_precision ? UINT8_C(0xf2) : UINT8_C(0xf3);
    if (luna_encoder_is_xmm(destination) &&
        (luna_encoder_is_xmm(source) ||
         source->kind == LUNA_X86_64_OPERAND_MEMORY)) {
        const uint8_t opcode[] = {0x0f, 0x10};
        return luna_encoder_emit_rm_instruction(assembler, prefix, false, false,
                                                opcode, sizeof(opcode),
                                                destination->reg.code, source);
    }
    if (luna_encoder_is_xmm(source) &&
        destination->kind == LUNA_X86_64_OPERAND_MEMORY) {
        const uint8_t opcode[] = {0x0f, 0x11};
        return luna_encoder_emit_rm_instruction(assembler, prefix, false, false,
                                                opcode, sizeof(opcode),
                                                source->reg.code, destination);
    }
    return false;
}

static bool luna_encoder_emit_movd_or_movq(LunaX8664Assembler *assembler,
                                           const LunaX8664OperandList *operands,
                                           bool quad) {
    if (operands->count != 2U) {
        return false;
    }
    const LunaX8664Operand *source = &operands->values[0];
    const LunaX8664Operand *destination = &operands->values[1];
    const uint8_t gpr_width = quad ? 64U : 32U;
    if (luna_encoder_is_gpr(source, gpr_width) &&
        luna_encoder_is_xmm(destination)) {
        const uint8_t opcode[] = {0x0f, 0x6e};
        return luna_encoder_emit_rm_instruction(assembler, UINT8_C(0x66), quad,
                                                false, opcode, sizeof(opcode),
                                                destination->reg.code, source);
    }
    if (luna_encoder_is_xmm(source) &&
        luna_encoder_is_gpr(destination, gpr_width)) {
        const uint8_t opcode[] = {0x0f, 0x7e};
        return luna_encoder_emit_rm_instruction(assembler, UINT8_C(0x66), quad,
                                                false, opcode, sizeof(opcode),
                                                source->reg.code, destination);
    }
    if (quad && luna_encoder_is_xmm(source) &&
        destination->kind == LUNA_X86_64_OPERAND_MEMORY) {
        const uint8_t opcode[] = {0x0f, 0xd6};
        return luna_encoder_emit_rm_instruction(assembler, UINT8_C(0x66), false,
                                                false, opcode, sizeof(opcode),
                                                source->reg.code, destination);
    }
    if (quad && luna_encoder_is_xmm(destination) &&
        (source->kind == LUNA_X86_64_OPERAND_MEMORY ||
         luna_encoder_is_xmm(source))) {
        const uint8_t opcode[] = {0x0f, 0x7e};
        return luna_encoder_emit_rm_instruction(assembler, UINT8_C(0xf3), false,
                                                false, opcode, sizeof(opcode),
                                                destination->reg.code, source);
    }
    return false;
}

static bool luna_encoder_emit_extend(LunaX8664Assembler *assembler,
                                     const LunaX8664OperandList *operands,
                                     uint8_t source_width,
                                     uint8_t destination_width,
                                     bool sign_extend) {
    if (operands->count != 2U ||
        !(operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY ||
          luna_encoder_is_gpr(&operands->values[0], source_width)) ||
        !luna_encoder_is_gpr(&operands->values[1], destination_width)) {
        return false;
    }
    uint8_t second_opcode = 0U;
    if (source_width == 8U) {
        second_opcode = sign_extend ? UINT8_C(0xbe) : UINT8_C(0xb6);
    } else if (source_width == 16U) {
        second_opcode = sign_extend ? UINT8_C(0xbf) : UINT8_C(0xb7);
    } else {
        return false;
    }
    const uint8_t opcode[] = {0x0f, second_opcode};
    const bool force_rex =
        operands->values[0].kind == LUNA_X86_64_OPERAND_REGISTER &&
        source_width == 8U && operands->values[0].reg.code >= 4U;
    return luna_encoder_emit_rm_instruction(
        assembler, 0U, destination_width == 64U, force_rex, opcode,
        sizeof(opcode), operands->values[1].reg.code, &operands->values[0]);
}

static bool luna_encoder_emit_unary(LunaX8664Assembler *assembler,
                                    const LunaX8664OperandList *operands,
                                    uint8_t width, uint8_t group) {
    if (operands->count != 1U ||
        !(operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY ||
          luna_encoder_is_gpr(&operands->values[0], width))) {
        return false;
    }
    const uint8_t opcode = UINT8_C(0xf7);
    return luna_encoder_emit_rm_instruction(
        assembler, width == 16U ? UINT8_C(0x66) : 0U, width == 64U, false,
        &opcode, 1U, group, &operands->values[0]);
}

static bool luna_encoder_emit_shift(LunaX8664Assembler *assembler,
                                    const LunaX8664OperandList *operands,
                                    uint8_t width, uint8_t group) {
    if (operands->count != 2U ||
        !luna_encoder_is_gpr(&operands->values[1], width)) {
        return false;
    }
    if (operands->values[0].kind == LUNA_X86_64_OPERAND_IMMEDIATE &&
        operands->values[0].immediate <= UINT8_MAX) {
        const uint8_t opcode = UINT8_C(0xc1);
        return luna_encoder_emit_rm_instruction(assembler, 0U, width == 64U,
                                                false, &opcode, 1U, group,
                                                &operands->values[1]) &&
               luna_x86_64_assembler_append_u8(
                   assembler, (uint8_t)operands->values[0].immediate);
    }
    if (luna_encoder_is_gpr(&operands->values[0], 8U) &&
        operands->values[0].reg.code == 1U) {
        const uint8_t opcode = UINT8_C(0xd3);
        return luna_encoder_emit_rm_instruction(assembler, 0U, width == 64U,
                                                false, &opcode, 1U, group,
                                                &operands->values[1]);
    }
    return false;
}

static bool luna_encoder_emit_setcc(LunaX8664Assembler *assembler,
                                    const LunaX8664OperandList *operands,
                                    uint8_t condition) {
    if (operands->count != 1U ||
        !luna_encoder_is_gpr(&operands->values[0], 8U)) {
        return false;
    }
    const uint8_t opcode[] = {0x0f, (uint8_t)(UINT8_C(0x90) | condition)};
    return luna_encoder_emit_rm_instruction(
        assembler, 0U, false, operands->values[0].reg.code >= 4U, opcode,
        sizeof(opcode), 0U, &operands->values[0]);
}

static bool luna_encoder_condition(LunaStringView mnemonic, uint8_t *value) {
    struct ConditionName {
        const char *name;
        uint8_t code;
    };
    static const struct ConditionName names[] = {
        {"o", 0U},   {"no", 1U},  {"b", 2U},   {"c", 2U},    {"nae", 2U},
        {"ae", 3U},  {"nb", 3U},  {"nc", 3U},  {"e", 4U},    {"z", 4U},
        {"ne", 5U},  {"nz", 5U},  {"be", 6U},  {"na", 6U},   {"a", 7U},
        {"nbe", 7U}, {"s", 8U},   {"ns", 9U},  {"p", 10U},   {"pe", 10U},
        {"np", 11U}, {"po", 11U}, {"l", 12U},  {"nge", 12U}, {"ge", 13U},
        {"nl", 13U}, {"le", 14U}, {"ng", 14U}, {"g", 15U},   {"nle", 15U},
    };
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]);
         index += 1U) {
        if (luna_string_view_equal_c_string(mnemonic, names[index].name)) {
            *value = names[index].code;
            return true;
        }
    }
    return false;
}

static bool luna_encoder_emit_sse_binary(LunaX8664Assembler *assembler,
                                         const LunaX8664OperandList *operands,
                                         uint8_t prefix, uint8_t operation) {
    if (operands->count != 2U ||
        !(luna_encoder_is_xmm(&operands->values[0]) ||
          operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY) ||
        !luna_encoder_is_xmm(&operands->values[1])) {
        return false;
    }
    const uint8_t opcode[] = {0x0f, operation};
    return luna_encoder_emit_rm_instruction(
        assembler, prefix, false, false, opcode, sizeof(opcode),
        operands->values[1].reg.code, &operands->values[0]);
}

static bool luna_encoder_emit_conversion(LunaX8664Assembler *assembler,
                                         const LunaX8664OperandList *operands,
                                         uint8_t prefix, uint8_t operation,
                                         bool rex_w, bool source_xmm) {
    if (operands->count != 2U) {
        return false;
    }
    if (source_xmm) {
        if (!(luna_encoder_is_xmm(&operands->values[0]) ||
              operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY) ||
            !luna_encoder_is_gpr(&operands->values[1], rex_w ? 64U : 32U)) {
            return false;
        }
    } else if (!(luna_encoder_is_gpr(&operands->values[0], rex_w ? 64U : 32U) ||
                 operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY) ||
               !luna_encoder_is_xmm(&operands->values[1])) {
        return false;
    }
    const uint8_t opcode[] = {0x0f, operation};
    return luna_encoder_emit_rm_instruction(
        assembler, prefix, rex_w, false, opcode, sizeof(opcode),
        operands->values[1].reg.code, &operands->values[0]);
}

static bool
luna_encoder_emit_test_or_compare(LunaX8664Assembler *assembler,
                                  const LunaX8664OperandList *operands,
                                  uint8_t width, bool compare) {
    if (operands->count != 2U ||
        !luna_encoder_is_gpr_rm(&operands->values[1], width)) {
        return false;
    }
    if (operands->values[0].kind == LUNA_X86_64_OPERAND_IMMEDIATE) {
        if (!compare ||
            (width == 64U &&
             !luna_encoder_immediate_fits_i32(operands->values[0].immediate)) ||
            (width == 32U &&
             !luna_encoder_immediate_fits_u32(operands->values[0].immediate)) ||
            (width == 8U &&
             !luna_encoder_immediate_fits_u8(operands->values[0].immediate))) {
            return false;
        }
        const bool byte = width == 8U;
        const LunaX8664Operand *destination = &operands->values[1];
        const uint8_t opcode = byte ? UINT8_C(0x80) : UINT8_C(0x81);
        const bool force_rex =
            byte && destination->kind == LUNA_X86_64_OPERAND_REGISTER &&
            destination->reg.code >= 4U;
        if (!luna_encoder_emit_rm_instruction(assembler, 0U, width == 64U,
                                              force_rex, &opcode, 1U, 7U,
                                              destination)) {
            return false;
        }
        return byte ? luna_x86_64_assembler_append_u8(
                          assembler, (uint8_t)operands->values[0].immediate)
                    : luna_x86_64_assembler_append_u32(
                          assembler, (uint32_t)operands->values[0].immediate);
    }
    if (!luna_encoder_is_gpr(&operands->values[0], width)) {
        return false;
    }
    const uint8_t opcode = compare
                               ? (width == 8U ? UINT8_C(0x38) : UINT8_C(0x39))
                               : (width == 8U ? UINT8_C(0x84) : UINT8_C(0x85));
    const bool force_rex =
        width == 8U &&
        (operands->values[0].reg.code >= 4U ||
         (operands->values[1].kind == LUNA_X86_64_OPERAND_REGISTER &&
          operands->values[1].reg.code >= 4U));
    return luna_encoder_emit_rm_instruction(
        assembler, 0U, width == 64U, force_rex, &opcode, 1U,
        operands->values[0].reg.code, &operands->values[1]);
}

static bool luna_encoder_emit_imul(LunaX8664Assembler *assembler,
                                   const LunaX8664OperandList *operands,
                                   uint8_t width) {
    if (operands->count == 2U &&
        (luna_encoder_is_gpr(&operands->values[0], width) ||
         operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY) &&
        luna_encoder_is_gpr(&operands->values[1], width)) {
        const uint8_t opcode[] = {0x0f, 0xaf};
        return luna_encoder_emit_rm_instruction(
            assembler, 0U, width == 64U, false, opcode, sizeof(opcode),
            operands->values[1].reg.code, &operands->values[0]);
    }
    if (operands->count == 3U &&
        operands->values[0].kind == LUNA_X86_64_OPERAND_IMMEDIATE &&
        luna_encoder_immediate_fits_i32(operands->values[0].immediate) &&
        (luna_encoder_is_gpr(&operands->values[1], width) ||
         operands->values[1].kind == LUNA_X86_64_OPERAND_MEMORY) &&
        luna_encoder_is_gpr(&operands->values[2], width)) {
        const uint8_t opcode = UINT8_C(0x69);
        return luna_encoder_emit_rm_instruction(
                   assembler, 0U, width == 64U, false, &opcode, 1U,
                   operands->values[2].reg.code, &operands->values[1]) &&
               luna_x86_64_assembler_append_u32(
                   assembler, (uint32_t)operands->values[0].immediate);
    }
    return false;
}

static bool
luna_encoder_emit_special_move(LunaX8664Assembler *assembler,
                               LunaStringView mnemonic,
                               const LunaX8664OperandList *operands) {
    if (luna_string_view_equal_c_string(mnemonic, "movabsq")) {
        if (operands->count != 2U ||
            operands->values[0].kind != LUNA_X86_64_OPERAND_IMMEDIATE ||
            !luna_encoder_is_gpr(&operands->values[1], 64U)) {
            return false;
        }
        const LunaX8664Register reg = operands->values[1].reg;
        if (reg.code >= 8U &&
            !luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x49))) {
            return false;
        }
        if (reg.code < 8U &&
            !luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x48))) {
            return false;
        }
        return luna_x86_64_assembler_append_u8(
                   assembler,
                   (uint8_t)(UINT8_C(0xb8) | (reg.code & UINT8_C(7)))) &&
               luna_x86_64_assembler_append_u64(assembler,
                                                operands->values[0].immediate);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movd")) {
        return luna_encoder_emit_movd_or_movq(assembler, operands, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movq") &&
        ((operands->count == 2U &&
          (luna_encoder_is_xmm(&operands->values[0]) ||
           luna_encoder_is_xmm(&operands->values[1]))))) {
        return luna_encoder_emit_movd_or_movq(assembler, operands, true);
    }
    return false;
}

static bool luna_encoder_encode_known(LunaX8664Assembler *assembler,
                                      LunaStringView mnemonic,
                                      LunaStringView raw_operands,
                                      const LunaX8664OperandList *operands) {
    if (luna_string_view_equal_c_string(mnemonic, "cld")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xfc));
    }
    if (luna_string_view_equal_c_string(mnemonic, "std")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xfd));
    }
    if (luna_string_view_equal_c_string(mnemonic, "cltd")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x99));
    }
    if (luna_string_view_equal_c_string(mnemonic, "cqto")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x48)) &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x99));
    }
    if (luna_string_view_equal_c_string(mnemonic, "leave")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xc9));
    }
    if (luna_string_view_equal_c_string(mnemonic, "ret")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xc3));
    }
    if (luna_string_view_equal_c_string(mnemonic, "syscall")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x0f)) &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x05));
    }
    if (luna_string_view_equal_c_string(mnemonic, "ud2")) {
        return operands->count == 0U &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x0f)) &&
               luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x0b));
    }
    if (luna_string_view_equal_c_string(mnemonic, "rep")) {
        if (operands->count != 1U ||
            operands->values[0].kind != LUNA_X86_64_OPERAND_LABEL ||
            !luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xf3))) {
            return false;
        }
        if (luna_string_view_equal_c_string(raw_operands, "movsb")) {
            return luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xa4));
        }
        if (luna_string_view_equal_c_string(raw_operands, "stosb")) {
            return luna_x86_64_assembler_append_u8(assembler, UINT8_C(0xaa));
        }
        return false;
    }
    if (luna_string_view_equal_c_string(mnemonic, "pushq")) {
        if (operands->count != 1U ||
            !luna_encoder_is_gpr(&operands->values[0], 64U)) {
            return false;
        }
        const uint8_t code = operands->values[0].reg.code;
        return (code < 8U ||
                luna_x86_64_assembler_append_u8(assembler, UINT8_C(0x41))) &&
               luna_x86_64_assembler_append_u8(
                   assembler, (uint8_t)(UINT8_C(0x50) | (code & UINT8_C(7))));
    }
    if (luna_string_view_equal_c_string(mnemonic, "call")) {
        const uint8_t opcode = UINT8_C(0xe8);
        return luna_encoder_emit_relative(assembler, &opcode, 1U, operands,
                                          LUNA_X86_64_ASSEMBLY_FIXUP_CALL);
    }
    if (luna_string_view_equal_c_string(mnemonic, "jmp")) {
        const uint8_t opcode = UINT8_C(0xe9);
        return luna_encoder_emit_relative(assembler, &opcode, 1U, operands,
                                          LUNA_X86_64_ASSEMBLY_FIXUP_BRANCH);
    }
    if (mnemonic.length > 1U && mnemonic.data[0] == 'j') {
        const LunaStringView condition_name = {
            .data = mnemonic.data + 1U,
            .length = mnemonic.length - 1U,
        };
        uint8_t condition = 0U;
        if (luna_encoder_condition(condition_name, &condition)) {
            const uint8_t opcode[] = {0x0f,
                                      (uint8_t)(UINT8_C(0x80) | condition)};
            return luna_encoder_emit_relative(
                assembler, opcode, sizeof(opcode), operands,
                LUNA_X86_64_ASSEMBLY_FIXUP_BRANCH);
        }
    }
    if (mnemonic.length > 3U && memcmp(mnemonic.data, "set", 3U) == 0) {
        const LunaStringView condition_name = {
            .data = mnemonic.data + 3U,
            .length = mnemonic.length - 3U,
        };
        uint8_t condition = 0U;
        if (luna_encoder_condition(condition_name, &condition)) {
            return luna_encoder_emit_setcc(assembler, operands, condition);
        }
    }

    if (luna_encoder_emit_special_move(assembler, mnemonic, operands)) {
        return true;
    }
    if (luna_string_view_equal_c_string(mnemonic, "movb")) {
        return luna_encoder_emit_move(assembler, operands, 8U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movw")) {
        return luna_encoder_emit_move(assembler, operands, 16U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movl")) {
        return luna_encoder_emit_move(assembler, operands, 32U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movq")) {
        return luna_encoder_emit_move(assembler, operands, 64U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movss")) {
        return luna_encoder_emit_xmm_move(assembler, operands, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movsd")) {
        return luna_encoder_emit_xmm_move(assembler, operands, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movsbl")) {
        return luna_encoder_emit_extend(assembler, operands, 8U, 32U, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movsbq")) {
        return luna_encoder_emit_extend(assembler, operands, 8U, 64U, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movswl")) {
        return luna_encoder_emit_extend(assembler, operands, 16U, 32U, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movswq")) {
        return luna_encoder_emit_extend(assembler, operands, 16U, 64U, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movzbl")) {
        return luna_encoder_emit_extend(assembler, operands, 8U, 32U, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movzwl")) {
        return luna_encoder_emit_extend(assembler, operands, 16U, 32U, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "movslq")) {
        if (operands->count != 2U ||
            !(luna_encoder_is_gpr(&operands->values[0], 32U) ||
              operands->values[0].kind == LUNA_X86_64_OPERAND_MEMORY) ||
            !luna_encoder_is_gpr(&operands->values[1], 64U)) {
            return false;
        }
        const uint8_t opcode = UINT8_C(0x63);
        return luna_encoder_emit_rm_instruction(
            assembler, 0U, true, false, &opcode, 1U,
            operands->values[1].reg.code, &operands->values[0]);
    }
    if (luna_string_view_equal_c_string(mnemonic, "leaq")) {
        if (operands->count != 2U ||
            operands->values[0].kind != LUNA_X86_64_OPERAND_MEMORY ||
            !luna_encoder_is_gpr(&operands->values[1], 64U)) {
            return false;
        }
        const uint8_t opcode = UINT8_C(0x8d);
        return luna_encoder_emit_rm_instruction(
            assembler, 0U, true, false, &opcode, 1U,
            operands->values[1].reg.code, &operands->values[0]);
    }

#define LUNA_ALU(NAME, WIDTH, OPCODE, GROUP)                                   \
    if (luna_string_view_equal_c_string(mnemonic, NAME)) {                     \
        return luna_encoder_emit_binary_alu(assembler, operands, WIDTH,        \
                                            OPCODE, GROUP);                    \
    }
    LUNA_ALU("addl", 32U, 0x01U, 0U)
    LUNA_ALU("addq", 64U, 0x01U, 0U)
    LUNA_ALU("subl", 32U, 0x29U, 5U)
    LUNA_ALU("subq", 64U, 0x29U, 5U)
    LUNA_ALU("andb", 8U, 0x20U, 4U)
    LUNA_ALU("andl", 32U, 0x21U, 4U)
    LUNA_ALU("andq", 64U, 0x21U, 4U)
    LUNA_ALU("orb", 8U, 0x08U, 1U)
    LUNA_ALU("orl", 32U, 0x09U, 1U)
    LUNA_ALU("orq", 64U, 0x09U, 1U)
    LUNA_ALU("xorl", 32U, 0x31U, 6U)
    LUNA_ALU("xorq", 64U, 0x31U, 6U)
#undef LUNA_ALU

    if (luna_string_view_equal_c_string(mnemonic, "cmpb")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 8U, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cmpl")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 32U,
                                                 true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cmpq")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 64U,
                                                 true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "testb")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 8U,
                                                 false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "testl")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 32U,
                                                 false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "testq")) {
        return luna_encoder_emit_test_or_compare(assembler, operands, 64U,
                                                 false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "negl")) {
        return luna_encoder_emit_unary(assembler, operands, 32U, 3U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "negq")) {
        return luna_encoder_emit_unary(assembler, operands, 64U, 3U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "notl")) {
        return luna_encoder_emit_unary(assembler, operands, 32U, 2U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "notq")) {
        return luna_encoder_emit_unary(assembler, operands, 64U, 2U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "divl")) {
        return luna_encoder_emit_unary(assembler, operands, 32U, 6U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "divq")) {
        return luna_encoder_emit_unary(assembler, operands, 64U, 6U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "idivl")) {
        return luna_encoder_emit_unary(assembler, operands, 32U, 7U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "idivq")) {
        return luna_encoder_emit_unary(assembler, operands, 64U, 7U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "imull")) {
        return luna_encoder_emit_imul(assembler, operands, 32U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "imulq")) {
        return luna_encoder_emit_imul(assembler, operands, 64U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "shll")) {
        return luna_encoder_emit_shift(assembler, operands, 32U, 4U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "shlq")) {
        return luna_encoder_emit_shift(assembler, operands, 64U, 4U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "shrl")) {
        return luna_encoder_emit_shift(assembler, operands, 32U, 5U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "shrq")) {
        return luna_encoder_emit_shift(assembler, operands, 64U, 5U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "sarl")) {
        return luna_encoder_emit_shift(assembler, operands, 32U, 7U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "sarq")) {
        return luna_encoder_emit_shift(assembler, operands, 64U, 7U);
    }
    if (luna_string_view_equal_c_string(mnemonic, "ldmxcsr")) {
        if (operands->count != 1U ||
            operands->values[0].kind != LUNA_X86_64_OPERAND_MEMORY) {
            return false;
        }
        const uint8_t opcode[] = {0x0f, 0xae};
        return luna_encoder_emit_rm_instruction(assembler, 0U, false, false,
                                                opcode, sizeof(opcode), 2U,
                                                &operands->values[0]);
    }

#define LUNA_SSE(NAME, PREFIX, OPCODE)                                         \
    if (luna_string_view_equal_c_string(mnemonic, NAME)) {                     \
        return luna_encoder_emit_sse_binary(assembler, operands, PREFIX,       \
                                            OPCODE);                           \
    }
    LUNA_SSE("addss", 0xf3U, 0x58U)
    LUNA_SSE("addsd", 0xf2U, 0x58U)
    LUNA_SSE("mulss", 0xf3U, 0x59U)
    LUNA_SSE("mulsd", 0xf2U, 0x59U)
    LUNA_SSE("subss", 0xf3U, 0x5cU)
    LUNA_SSE("subsd", 0xf2U, 0x5cU)
    LUNA_SSE("divss", 0xf3U, 0x5eU)
    LUNA_SSE("divsd", 0xf2U, 0x5eU)
    LUNA_SSE("ucomiss", 0U, 0x2eU)
    LUNA_SSE("ucomisd", 0x66U, 0x2eU)
#undef LUNA_SSE

    if (luna_string_view_equal_c_string(mnemonic, "cvtss2sd")) {
        return luna_encoder_emit_sse_binary(assembler, operands, 0xf3U, 0x5aU);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvtsd2ss")) {
        return luna_encoder_emit_sse_binary(assembler, operands, 0xf2U, 0x5aU);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvtsi2ssq")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf3U, 0x2aU,
                                            true, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvtsi2sdq")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf2U, 0x2aU,
                                            true, false);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvttss2si")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf3U, 0x2cU,
                                            false, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvttss2siq")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf3U, 0x2cU,
                                            true, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvttsd2si")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf2U, 0x2cU,
                                            false, true);
    }
    if (luna_string_view_equal_c_string(mnemonic, "cvttsd2siq")) {
        return luna_encoder_emit_conversion(assembler, operands, 0xf2U, 0x2cU,
                                            true, true);
    }
    return false;
}

bool luna_x86_64_encode_instruction(LunaX8664Assembler *assembler,
                                    LunaStringView mnemonic,
                                    LunaStringView operands_text) {
    LunaX8664OperandList operands;
    if (!luna_encoder_parse_operands(operands_text, &operands)) {
        return luna_x86_64_assembler_error(assembler,
                                           "cannot parse operands for '%.*s'",
                                           (int)mnemonic.length, mnemonic.data);
    }
    const size_t old_length =
        luna_x86_64_assembler_current_output(assembler)->length;
    if (luna_encoder_encode_known(assembler, mnemonic, operands_text,
                                  &operands)) {
        return true;
    }
    LunaStringBuilder *output = luna_x86_64_assembler_current_output(assembler);
    output->length = old_length;
    if (output->data != NULL) {
        output->data[old_length] = '\0';
    }
    return luna_x86_64_assembler_error(
        assembler, "unsupported instruction form '%.*s %.*s'",
        (int)mnemonic.length, mnemonic.data, (int)operands_text.length,
        operands_text.data);
}
