#include "luna/backend/x86_64/x86_64.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t luna_x86_64_type_bit_width(LunaX8664MachineType type) {
    return luna_x86_64_machine_type_bit_width(type);
}

static bool luna_x86_64_append_hex_byte(LunaStringBuilder *output,
                                        unsigned char byte) {
    static const char digits[] = "0123456789abcdef";
    const char encoded[2] = {
        digits[(byte >> 4U) & 0x0fU],
        digits[byte & 0x0fU],
    };
    return luna_string_builder_append(output, encoded, sizeof(encoded));
}

static bool
luna_x86_64_append_symbol(LunaStringBuilder *output,
                          const LunaX8664MachineFunction *function) {
    if (!luna_string_builder_append_c_string(output, "_L")) {
        return false;
    }

    for (size_t index = 0U; index < function->module_name.length; index += 1U) {
        if (!luna_x86_64_append_hex_byte(
                output, (unsigned char)function->module_name.data[index])) {
            return false;
        }
    }

    if (function->has_module_metadata_hash &&
        !luna_string_builder_append_format(output, "_H%016" PRIx64,
                                           function->module_metadata_hash)) {
        return false;
    }

    if (!luna_string_builder_append_c_string(output, "_")) {
        return false;
    }

    for (size_t index = 0U; index < function->name.length; index += 1U) {
        if (!luna_x86_64_append_hex_byte(
                output, (unsigned char)function->name.data[index])) {
            return false;
        }
    }

    return true;
}

static bool
luna_x86_64_append_linkage_symbol(LunaStringBuilder *output,
                                  const LunaX8664MachineFunction *function) {
    if (function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C) {
        return luna_string_builder_append_view(output, function->name);
    }
    return luna_x86_64_append_symbol(output, function);
}

static bool luna_x86_64_append_quoted_string(LunaStringBuilder *output,
                                             const char *text) {
    if (output == NULL || text == NULL ||
        !luna_string_builder_append_c_string(output, "\"")) {
        return false;
    }
    for (size_t index = 0U; text[index] != '\0'; index += 1U) {
        const unsigned char byte = (unsigned char)text[index];
        if (byte == '\\' || byte == '"') {
            const char escaped[2] = {'\\', (char)byte};
            if (!luna_string_builder_append(output, escaped, sizeof(escaped))) {
                return false;
            }
        } else if (byte == '\n' || byte == '\r' || byte == '\t') {
            const char escape = byte == '\n' ? 'n' : (byte == '\r' ? 'r' : 't');
            const char escaped[2] = {'\\', escape};
            if (!luna_string_builder_append(output, escaped, sizeof(escaped))) {
                return false;
            }
        } else if (byte >= 0x20U && byte <= 0x7eU) {
            const char character = (char)byte;
            if (!luna_string_builder_append(output, &character, 1U)) {
                return false;
            }
        } else {
            const char escaped[4] = {
                '\\',
                (char)('0' + ((byte >> 6U) & 7U)),
                (char)('0' + ((byte >> 3U) & 7U)),
                (char)('0' + (byte & 7U)),
            };
            if (!luna_string_builder_append(output, escaped, sizeof(escaped))) {
                return false;
            }
        }
    }
    return luna_string_builder_append_c_string(output, "\"");
}

static bool luna_x86_64_debug_source_valid(LunaSourceSpan span) {
    return span.source != NULL && span.source->path != NULL && span.line > 0U &&
           span.column > 0U;
}

static uint32_t luna_x86_64_debug_file_id(const LunaVector *files,
                                          const LunaSourceFile *source) {
    if (files == NULL || source == NULL || source->path == NULL) {
        return 0U;
    }
    for (size_t index = 0U; index < files->length; index += 1U) {
        const LunaSourceFile *const *file = luna_vector_at_const(files, index);
        if (file != NULL && *file != NULL && (*file)->path != NULL &&
            strcmp((*file)->path, source->path) == 0) {
            return (uint32_t)index + 1U;
        }
    }
    return 0U;
}

static bool
luna_x86_64_collect_debug_files(const LunaX8664MachineModule *module,
                                LunaVector *files) {
    luna_vector_init(files, sizeof(const LunaSourceFile *));
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (function == NULL) {
            return false;
        }
        for (size_t block_index = 0U; block_index < function->blocks.length;
             block_index += 1U) {
            const LunaX8664MachineBlock *block =
                luna_vector_at_const(&function->blocks, block_index);
            if (block == NULL) {
                return false;
            }
            for (size_t instruction_index = 0U;
                 instruction_index < block->instructions.length;
                 instruction_index += 1U) {
                const LunaX8664MachineInstruction *instruction =
                    luna_vector_at_const(&block->instructions,
                                         instruction_index);
                if (instruction == NULL ||
                    !luna_x86_64_debug_source_valid(instruction->span) ||
                    luna_x86_64_debug_file_id(files,
                                              instruction->span.source) != 0U) {
                    continue;
                }
                const LunaSourceFile *source = instruction->span.source;
                if (files->length >= UINT32_MAX ||
                    !luna_vector_push(files, &source)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool luna_x86_64_emit_debug_files(LunaStringBuilder *output,
                                         const LunaVector *files) {
    for (size_t index = 0U; index < files->length; index += 1U) {
        const LunaSourceFile *const *file = luna_vector_at_const(files, index);
        if (file == NULL || *file == NULL || (*file)->path == NULL ||
            !luna_string_builder_append_format(output, "    .file %zu ",
                                               index + 1U) ||
            !luna_x86_64_append_quoted_string(output, (*file)->path) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }
    }
    return files->length == 0U ||
           luna_string_builder_append_c_string(output, "\n");
}

static bool luna_x86_64_emit_debug_location(
    LunaStringBuilder *output, const LunaVector *files,
    const LunaX8664MachineInstruction *instruction) {
    if (!luna_x86_64_debug_source_valid(instruction->span)) {
        return true;
    }
    const uint32_t file_id =
        luna_x86_64_debug_file_id(files, instruction->span.source);
    return file_id != 0U &&
           luna_string_builder_append_format(
               output, "    .loc %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
               file_id, instruction->span.line, instruction->span.column);
}

static bool luna_x86_64_align_up(uint64_t value, uint32_t alignment,
                                 uint64_t *aligned) {
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
        value > UINT64_MAX - ((uint64_t)alignment - 1U)) {
        return false;
    }
    *aligned = (value + (uint64_t)alignment - 1U) & ~((uint64_t)alignment - 1U);
    return true;
}

static bool
luna_x86_64_slot_area_size(const LunaX8664MachineFunction *function,
                           LunaX8664MachineStackSlotId requested_slot,
                           bool stop_at_requested_slot, uint64_t *byte_count) {
    if (stop_at_requested_slot &&
        (size_t)requested_slot >= function->slots.length) {
        return false;
    }

    uint64_t offset = 0U;
    for (size_t index = 0U; index < function->slots.length; index += 1U) {
        const LunaX8664MachineStackSlot *slot =
            luna_vector_at_const(&function->slots, index);
        if (slot == NULL || slot->size_bytes == 0U ||
            offset > UINT64_MAX - slot->size_bytes) {
            return false;
        }
        offset += slot->size_bytes;
        if (!luna_x86_64_align_up(offset, slot->alignment_bytes, &offset)) {
            return false;
        }
        if (stop_at_requested_slot && index == (size_t)requested_slot) {
            *byte_count = offset;
            return true;
        }
    }

    *byte_count = offset;
    return !stop_at_requested_slot;
}

static bool
luna_x86_64_stack_offset(const LunaX8664MachineFunction *function,
                         const LunaX8664FunctionInstructionRewrite *rewrite,
                         bool is_value, uint32_t id, int32_t *offset) {
    uint64_t byte_offset = 0U;
    if (!is_value) {
        if (!luna_x86_64_slot_area_size(function,
                                        (LunaX8664MachineStackSlotId)id, true,
                                        &byte_offset)) {
            return false;
        }
    } else {
        if (rewrite == NULL || (size_t)id >= function->value_types.length ||
            !luna_x86_64_slot_area_size(function,
                                        LUNA_X86_64_MACHINE_INVALID_ID, false,
                                        &byte_offset) ||
            !luna_x86_64_align_up(byte_offset, 8U, &byte_offset)) {
            return false;
        }
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&rewrite->value_locations, (size_t)id);
        if (location == NULL ||
            location->kind != LUNA_X86_64_ALLOCATION_SPILL ||
            location->spill_slot >= rewrite->spill_slot_count) {
            return false;
        }
        const uint64_t spill_count = (uint64_t)location->spill_slot + 1U;
        if (spill_count > (UINT64_MAX - byte_offset) / 8U) {
            return false;
        }
        byte_offset += spill_count * 8U;
    }

    if (byte_offset > (uint64_t)INT32_MAX) {
        return false;
    }

    *offset = -(int32_t)byte_offset;
    return true;
}

static bool luna_x86_64_hidden_return_offset(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, int32_t *offset) {
    uint64_t byte_offset = 0U;
    if (!luna_x86_64_slot_area_size(function, LUNA_X86_64_MACHINE_INVALID_ID,
                                    false, &byte_offset) ||
        !luna_x86_64_align_up(byte_offset, 8U, &byte_offset) ||
        rewrite == NULL ||
        rewrite->spill_slot_count > (UINT64_MAX - byte_offset) / 8U) {
        return false;
    }
    byte_offset += (uint64_t)rewrite->spill_slot_count * 8U;
    if (byte_offset > (uint64_t)INT32_MAX - 8U) {
        return false;
    }
    byte_offset += 8U;
    *offset = -(int32_t)byte_offset;
    return true;
}

static bool luna_x86_64_append_physical_register_operand(
    LunaStringBuilder *output, LunaX8664PhysicalRegister physical_register,
    uint32_t width) {
    static const char *const names_8_bit[] = {
        NULL,   "%al",   "%bl",   "%cl",   "%dl",   "%sil",  "%dil",  "%r8b",
        "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b",
    };
    static const char *const names_16_bit[] = {
        NULL,   "%ax",   "%bx",   "%cx",   "%dx",   "%si",   "%di",   "%r8w",
        "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w",
    };
    static const char *const names_32_bit[] = {
        NULL,   "%eax",  "%ebx",  "%ecx",  "%edx",  "%esi",  "%edi",  "%r8d",
        "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d",
    };
    static const char *const names_64_bit[] = {
        NULL,  "%rax", "%rbx", "%rcx", "%rdx", "%rsi", "%rdi", "%r8",
        "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
    };
    const char *name = NULL;
    if (physical_register >= LUNA_X86_64_PHYSICAL_REGISTER_XMM0 &&
        physical_register <= LUNA_X86_64_PHYSICAL_REGISTER_XMM15) {
        return luna_string_builder_append_format(
            output, "%%%s",
            luna_x86_64_physical_register_name(physical_register));
    }
    if (physical_register < LUNA_X86_64_PHYSICAL_REGISTER_RAX ||
        physical_register > LUNA_X86_64_PHYSICAL_REGISTER_R15) {
        return false;
    }
    const size_t register_index = (size_t)physical_register;
    switch (width) {
    case 8U:
        name = names_8_bit[register_index];
        break;
    case 16U:
        name = names_16_bit[register_index];
        break;
    case 32U:
        name = names_32_bit[register_index];
        break;
    case 64U:
        name = names_64_bit[register_index];
        break;
    default:
        return false;
    }
    return name != NULL && luna_string_builder_append_c_string(output, name);
}

static bool luna_x86_64_append_value_operand_width(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister value, uint32_t requested_width) {
    if (rewrite == NULL || (size_t)value >= function->value_types.length) {
        return false;
    }
    const LunaX8664VirtualRegisterAllocation *location =
        luna_vector_at_const(&rewrite->value_locations, (size_t)value);
    if (location == NULL) {
        return false;
    }
    if (location->kind == LUNA_X86_64_ALLOCATION_REGISTER) {
        return luna_x86_64_append_physical_register_operand(
            output, location->physical_register, requested_width);
    }

    int32_t offset = 0;
    if (location->kind != LUNA_X86_64_ALLOCATION_SPILL ||
        !luna_x86_64_stack_offset(function, rewrite, true, value, &offset)) {
        return false;
    }
    return luna_string_builder_append_format(output, "%d(%%rbp)", offset);
}

static bool luna_x86_64_append_value_operand(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister value) {
    const LunaX8664MachineType *type =
        luna_vector_at_const(&function->value_types, (size_t)value);
    if (type == NULL) {
        return false;
    }
    uint32_t width = luna_x86_64_machine_type_bit_width(*type);
    if (luna_x86_64_machine_type_is_float(*type)) {
        width = *type == LUNA_X86_64_MACHINE_TYPE_F32 ? 32U : 64U;
    } else if (width < 32U) {
        width = 32U;
    }
    return luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                  value, width);
}

static bool
luna_x86_64_append_slot_operand(LunaStringBuilder *output,
                                const LunaX8664MachineFunction *function,
                                LunaX8664MachineStackSlotId slot) {
    int32_t offset = 0;
    if (!luna_x86_64_stack_offset(function, NULL, false, slot, &offset)) {
        return false;
    }

    return luna_string_builder_append_format(output, "%d(%%rbp)", offset);
}

static bool
luna_x86_64_emit_store_eax(LunaStringBuilder *output,
                           const LunaX8664MachineFunction *function,
                           const LunaX8664FunctionInstructionRewrite *rewrite,
                           LunaX8664MachineVirtualRegister result) {
    const LunaX8664VirtualRegisterAllocation *location =
        rewrite == NULL
            ? NULL
            : luna_vector_at_const(&rewrite->value_locations, (size_t)result);
    if (location != NULL && location->kind == LUNA_X86_64_ALLOCATION_REGISTER &&
        luna_x86_64_physical_register_class(location->physical_register) ==
            LUNA_X86_64_MACHINE_REGISTER_FLOAT) {
        return luna_string_builder_append_format(
            output, "    movd %%eax, %%%s\n",
            luna_x86_64_physical_register_name(location->physical_register));
    }
    if (!luna_string_builder_append_c_string(output, "    movl %eax, ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                result, 32U)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, "\n");
}

static bool
luna_x86_64_emit_load_eax(LunaStringBuilder *output,
                          const LunaX8664MachineFunction *function,
                          const LunaX8664FunctionInstructionRewrite *rewrite,
                          LunaX8664MachineVirtualRegister value) {
    const LunaX8664VirtualRegisterAllocation *location =
        rewrite == NULL
            ? NULL
            : luna_vector_at_const(&rewrite->value_locations, (size_t)value);
    if (location != NULL && location->kind == LUNA_X86_64_ALLOCATION_REGISTER &&
        luna_x86_64_physical_register_class(location->physical_register) ==
            LUNA_X86_64_MACHINE_REGISTER_FLOAT) {
        return luna_string_builder_append_format(
            output, "    movd %%%s, %%eax\n",
            luna_x86_64_physical_register_name(location->physical_register));
    }
    if (!luna_string_builder_append_c_string(output, "    movl ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                value, 32U)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %eax\n");
}

static bool
luna_x86_64_emit_load_ecx(LunaStringBuilder *output,
                          const LunaX8664MachineFunction *function,
                          const LunaX8664FunctionInstructionRewrite *rewrite,
                          LunaX8664MachineVirtualRegister value) {
    if (!luna_string_builder_append_c_string(output, "    movl ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                value, 32U)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %ecx\n");
}

static bool
luna_x86_64_emit_store_rax(LunaStringBuilder *output,
                           const LunaX8664MachineFunction *function,
                           const LunaX8664FunctionInstructionRewrite *rewrite,
                           LunaX8664MachineVirtualRegister result) {
    if (!luna_string_builder_append_c_string(output, "    movq %rax, ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                result, 64U)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, "\n");
}

static bool
luna_x86_64_emit_load_rax(LunaStringBuilder *output,
                          const LunaX8664MachineFunction *function,
                          const LunaX8664FunctionInstructionRewrite *rewrite,
                          LunaX8664MachineVirtualRegister value) {
    if (!luna_string_builder_append_c_string(output, "    movq ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand_width(output, function, rewrite,
                                                value, 64U)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %rax\n");
}

static bool luna_x86_64_emit_normalize_bool_eax(LunaStringBuilder *output) {
    return luna_string_builder_append_c_string(output,
                                               "    testl %eax, %eax\n"
                                               "    setne %al\n"
                                               "    movzbl %al, %eax\n");
}

static bool luna_x86_64_emit_normalize_bool_al(LunaStringBuilder *output) {
    return luna_string_builder_append_c_string(output,
                                               "    testb %al, %al\n"
                                               "    setne %al\n"
                                               "    movzbl %al, %eax\n");
}

static const char *luna_x86_64_float_move(LunaX8664MachineType type) {
    if (type == LUNA_X86_64_MACHINE_TYPE_F32) {
        return "movss";
    }
    if (type == LUNA_X86_64_MACHINE_TYPE_F64) {
        return "movsd";
    }
    return NULL;
}

static bool luna_x86_64_emit_load_xmm0(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister value, LunaX8664MachineType type) {
    const char *move = luna_x86_64_float_move(type);
    return move != NULL &&
           luna_string_builder_append_format(output, "    %s ", move) &&
           luna_x86_64_append_value_operand(output, function, rewrite, value) &&
           luna_string_builder_append_c_string(output, ", %xmm0\n");
}

static bool luna_x86_64_emit_store_xmm0(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister result, LunaX8664MachineType type) {
    const char *move = luna_x86_64_float_move(type);
    return move != NULL &&
           luna_string_builder_append_format(output, "    %s %%xmm0, ", move) &&
           luna_x86_64_append_value_operand(output, function, rewrite,
                                            result) &&
           luna_string_builder_append_c_string(output, "\n");
}

static bool luna_x86_64_emit_signed_load_32(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister value, LunaX8664MachineType type,
    const char *register_name) {
    const char *mnemonic = NULL;
    switch (luna_x86_64_type_bit_width(type)) {
    case 8U:
        mnemonic = "movsbl";
        break;
    case 16U:
        mnemonic = "movswl";
        break;
    case 32U:
        mnemonic = "movl";
        break;
    default:
        return false;
    }

    if (!luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand_width(
            output, function, rewrite, value,
            luna_x86_64_type_bit_width(type)) ||
        !luna_string_builder_append_format(output, ", %s\n", register_name)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_signed_load_64(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister value, LunaX8664MachineType type) {
    const char *mnemonic = NULL;
    switch (luna_x86_64_type_bit_width(type)) {
    case 8U:
        mnemonic = "movsbq";
        break;
    case 16U:
        mnemonic = "movswq";
        break;
    case 32U:
        mnemonic = "movslq";
        break;
    default:
        return false;
    }

    if (!luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand_width(
            output, function, rewrite, value,
            luna_x86_64_type_bit_width(type)) ||
        !luna_string_builder_append_c_string(output, ", %rax\n")) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_truncate_eax(LunaStringBuilder *output,
                                          LunaX8664MachineType type) {
    switch (luna_x86_64_type_bit_width(type)) {
    case 8U:
        return luna_string_builder_append_c_string(output,
                                                   "    andl $255, %eax\n");
    case 16U:
        return luna_string_builder_append_c_string(output,
                                                   "    andl $65535, %eax\n");
    case 32U:
        return true;
    default:
        return false;
    }
}

static bool luna_x86_64_emit_store_integer_eax(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    LunaX8664MachineVirtualRegister result, LunaX8664MachineType type) {
    return luna_x86_64_emit_truncate_eax(output, type) &&
           luna_x86_64_emit_store_eax(output, function, rewrite, result);
}

static bool luna_x86_64_emit_binary_32(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction, const char *mnemonic) {
    if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                   instruction->left) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %eax\n")) {
        return false;
    }

    return luna_x86_64_emit_store_integer_eax(
        output, function, rewrite, instruction->result, instruction->type);
}

static bool luna_x86_64_emit_binary_64(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction, const char *mnemonic) {
    if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->left) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %rax\n")) {
        return false;
    }

    return luna_x86_64_emit_store_rax(output, function, rewrite,
                                      instruction->result);
}

static bool luna_x86_64_emit_binary_float(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction, const char *mnemonic) {
    if (!luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                    instruction->left, instruction->type) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %xmm0\n")) {
        return false;
    }
    return luna_x86_64_emit_store_xmm0(output, function, rewrite,
                                       instruction->result, instruction->type);
}

static bool luna_x86_64_type_is_64_bit(LunaX8664MachineType type) {
    return luna_x86_64_type_bit_width(type) == 64U;
}

static const char *
luna_x86_64_set_condition(LunaX8664MachineOpcode opcode,
                          LunaX8664MachineType operand_type) {
    const bool is_signed =
        luna_x86_64_machine_type_is_signed_integer(operand_type);
    switch (opcode) {
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
        return "sete";
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
        return "setne";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
        return is_signed ? "setl" : "setb";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
        return is_signed ? "setle" : "setbe";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
        return is_signed ? "setg" : "seta";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
        return is_signed ? "setge" : "setae";
    default:
        return NULL;
    }
}

static bool luna_x86_64_emit_float_compare(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction,
    LunaX8664MachineType operand_type) {
    const char *compare =
        operand_type == LUNA_X86_64_MACHINE_TYPE_F32 ? "ucomiss" : "ucomisd";
    const char *set_result = NULL;
    switch (instruction->opcode) {
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
        set_result = "    sete %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
        set_result = "    setne %al\n"
                     "    setp %cl\n"
                     "    orb %cl, %al\n";
        break;
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
        set_result = "    setb %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
        set_result = "    setbe %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
        set_result = "    seta %al\n";
        break;
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        set_result = "    setae %al\n";
        break;
    default:
        return false;
    }

    if (!luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                    instruction->left, operand_type) ||
        !luna_string_builder_append_format(output, "    %s ", compare) ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %xmm0\n") ||
        !luna_string_builder_append_c_string(output, set_result) ||
        !luna_string_builder_append_c_string(output,
                                             "    movzbl %al, %eax\n")) {
        return false;
    }
    return luna_x86_64_emit_store_eax(output, function, rewrite,
                                      instruction->result);
}

static bool
luna_x86_64_emit_compare(LunaStringBuilder *output,
                         const LunaX8664MachineFunction *function,
                         const LunaX8664FunctionInstructionRewrite *rewrite,
                         const LunaX8664MachineInstruction *instruction) {
    const LunaX8664MachineType *operand_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    if (operand_type == NULL) {
        return false;
    }
    if (luna_x86_64_machine_type_is_float(*operand_type)) {
        return luna_x86_64_emit_float_compare(output, function, rewrite,
                                              instruction, *operand_type);
    }
    const char *condition =
        luna_x86_64_set_condition(instruction->opcode, *operand_type);
    const bool is_64_bit = luna_x86_64_type_is_64_bit(*operand_type);
    const bool is_signed_narrow =
        luna_x86_64_machine_type_is_signed_integer(*operand_type) &&
        luna_x86_64_type_bit_width(*operand_type) < 32U;
    if (condition == NULL) {
        return false;
    }

    if (is_64_bit) {
        if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_c_string(output, "    cmpq ") ||
            !luna_x86_64_append_value_operand(output, function, rewrite,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, ", %rax\n")) {
            return false;
        }
    } else if (is_signed_narrow) {
        if (!luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                             instruction->left, *operand_type,
                                             "%eax") ||
            !luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                             instruction->right, *operand_type,
                                             "%ecx") ||
            !luna_string_builder_append_c_string(output,
                                                 "    cmpl %ecx, %eax\n")) {
            return false;
        }
    } else if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                          instruction->left) ||
               !luna_string_builder_append_c_string(output, "    cmpl ") ||
               !luna_x86_64_append_value_operand(output, function, rewrite,
                                                 instruction->right) ||
               !luna_string_builder_append_c_string(output, ", %eax\n")) {
        return false;
    }

    if (!luna_string_builder_append_format(output, "    %s %%al\n",
                                           condition) ||
        !luna_string_builder_append_c_string(output,
                                             "    movzbl %al, %eax\n")) {
        return false;
    }

    return luna_x86_64_emit_store_eax(output, function, rewrite,
                                      instruction->result);
}

static bool
luna_x86_64_emit_load_integer_piece(LunaStringBuilder *output,
                                    uint64_t offset_bytes, uint64_t size_bytes,
                                    const char *destination_register) {
    if (size_bytes == 8U) {
        return luna_string_builder_append_format(
            output, "    movq %" PRIu64 "(%%r11), %s\n", offset_bytes,
            destination_register);
    }
    if (size_bytes == 4U) {
        return luna_string_builder_append_format(
                   output, "    movl %" PRIu64 "(%%r11), %%eax\n",
                   offset_bytes) &&
               luna_string_builder_append_format(output, "    movq %%rax, %s\n",
                                                 destination_register);
    }
    if (size_bytes == 2U) {
        return luna_string_builder_append_format(
                   output, "    movzwl %" PRIu64 "(%%r11), %%eax\n",
                   offset_bytes) &&
               luna_string_builder_append_format(output, "    movq %%rax, %s\n",
                                                 destination_register);
    }
    if (size_bytes == 1U) {
        return luna_string_builder_append_format(
                   output, "    movzbl %" PRIu64 "(%%r11), %%eax\n",
                   offset_bytes) &&
               luna_string_builder_append_format(output, "    movq %%rax, %s\n",
                                                 destination_register);
    }
    if (size_bytes == 0U || size_bytes > 8U ||
        !luna_string_builder_append_c_string(output, "    xorq %rax, %rax\n")) {
        return false;
    }
    for (uint64_t index = 0U; index < size_bytes; index += 1U) {
        if (!luna_string_builder_append_format(
                output, "    movzbl %" PRIu64 "(%%r11), %%r10d\n",
                offset_bytes + index) ||
            (index != 0U &&
             !luna_string_builder_append_format(
                 output, "    shlq $%" PRIu64 ", %%r10\n", index * 8U)) ||
            !luna_string_builder_append_c_string(output,
                                                 "    orq %r10, %rax\n")) {
            return false;
        }
    }
    return luna_string_builder_append_format(output, "    movq %%rax, %s\n",
                                             destination_register);
}

static bool luna_x86_64_emit_store_integer_piece(LunaStringBuilder *output,
                                                 const char *source_register,
                                                 uint64_t offset_bytes,
                                                 uint64_t size_bytes) {
    if (size_bytes == 0U || size_bytes > 8U ||
        !luna_string_builder_append_format(output, "    movq %s, %%r10\n",
                                           source_register)) {
        return false;
    }
    for (uint64_t index = 0U; index < size_bytes; index += 1U) {
        if (!luna_string_builder_append_format(
                output, "    movb %%r10b, %" PRIu64 "(%%r11)\n",
                offset_bytes + index) ||
            (index + 1U != size_bytes && !luna_string_builder_append_c_string(
                                             output, "    shrq $8, %r10\n"))) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_emit_call(
    LunaStringBuilder *output, const LunaX8664MachineModule *module,
    const LunaX8664ModuleAbi *abi, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction) {
    static const char *argument_registers_32_bit[] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d",
    };
    static const char *argument_registers_64_bit[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };
    static const char *argument_registers_float[] = {
        "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",
    };
    static const char *return_registers_integer[] = {"%rax", "%rdx"};
    static const char *return_registers_float[] = {"%xmm0", "%xmm1"};

    const LunaX8664MachineFunction *callee =
        luna_x86_64_machine_module_function_const(module, instruction->callee);
    const LunaX8664FunctionAbi *callee_abi =
        luna_vector_at_const(&abi->functions, (size_t)instruction->callee);
    if (callee == NULL || callee_abi == NULL ||
        callee_abi->parameter_locations.length != instruction->argument_count) {
        return false;
    }

    if (callee_abi->call_frame_size_bytes != 0U &&
        !luna_string_builder_append_format(output,
                                           "    subq $%" PRIu64 ", %%rsp\n",
                                           callee_abi->call_frame_size_bytes)) {
        return false;
    }

    for (uint32_t index = 0U; index < instruction->argument_count;
         index += 1U) {
        const LunaX8664MachineVirtualRegister *argument = luna_vector_at_const(
            &function->arguments, (size_t)instruction->first_argument + index);
        const LunaX8664MachineType *parameter_type =
            luna_vector_at_const(&callee->parameter_types, (size_t)index);
        const LunaX8664AbiParameterLocation *location = luna_vector_at_const(
            &callee_abi->parameter_locations, (size_t)index);
        if (argument == NULL || parameter_type == NULL || location == NULL) {
            return false;
        }
        if (location->kind != LUNA_X86_64_ABI_LOCATION_STACK) {
            continue;
        }
        if (location->is_aggregate) {
            if (!luna_string_builder_append_c_string(output, "    movq ") ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  *argument) ||
                !luna_string_builder_append_format(
                    output,
                    ", %%rsi\n"
                    "    leaq %" PRIu64 "(%%rsp), %%rdi\n"
                    "    movq $%" PRIu64 ", %%rcx\n"
                    "    rep movsb\n",
                    location->stack_offset_bytes, location->size_bytes)) {
                return false;
            }
            continue;
        }
        if (luna_x86_64_machine_type_is_float(*parameter_type)) {
            const char *move = luna_x86_64_float_move(*parameter_type);
            if (move == NULL ||
                !luna_string_builder_append_format(output, "    %s ", move) ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  *argument) ||
                !luna_string_builder_append_format(
                    output,
                    ", %%xmm15\n"
                    "    %s %%xmm15, %" PRIu64 "(%%rsp)\n",
                    move, location->stack_offset_bytes)) {
                return false;
            }
            continue;
        }

        const bool is_64_bit = luna_x86_64_type_is_64_bit(*parameter_type);
        const uint32_t width = luna_x86_64_type_bit_width(*parameter_type);
        const bool needs_external_sign_extension =
            callee->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
            luna_x86_64_machine_type_is_signed_integer(*parameter_type) &&
            width < 32U;
        if (needs_external_sign_extension) {
            if (!luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                                 *argument, *parameter_type,
                                                 "%eax")) {
                return false;
            }
        } else if (!luna_string_builder_append_c_string(
                       output, is_64_bit ? "    movq " : "    movl ") ||
                   !luna_x86_64_append_value_operand(output, function, rewrite,
                                                     *argument) ||
                   !luna_string_builder_append_c_string(
                       output, is_64_bit ? ", %rax\n" : ", %eax\n")) {
            return false;
        }
        if (!luna_string_builder_append_format(
                output, "    movq %%rax, %" PRIu64 "(%%rsp)\n",
                location->stack_offset_bytes)) {
            return false;
        }
    }

    if (callee_abi->return_location.uses_hidden_pointer) {
        if (!luna_string_builder_append_c_string(output, "    leaq ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(output, ", %rdi\n")) {
            return false;
        }
    }

    for (uint32_t index = 0U; index < instruction->argument_count;
         index += 1U) {
        const LunaX8664MachineVirtualRegister *argument = luna_vector_at_const(
            &function->arguments, (size_t)instruction->first_argument + index);
        const LunaX8664MachineType *parameter_type =
            luna_vector_at_const(&callee->parameter_types, (size_t)index);
        const LunaX8664AbiParameterLocation *location = luna_vector_at_const(
            &callee_abi->parameter_locations, (size_t)index);
        if (argument == NULL || parameter_type == NULL || location == NULL) {
            return false;
        }
        if (location->kind == LUNA_X86_64_ABI_LOCATION_STACK) {
            continue;
        }
        if (location->is_aggregate) {
            if (!luna_string_builder_append_c_string(output, "    movq ") ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  *argument) ||
                !luna_string_builder_append_c_string(output, ", %r11\n")) {
                return false;
            }
            for (uint32_t piece_index = 0U; piece_index < location->piece_count;
                 piece_index += 1U) {
                if (location->pieces[piece_index].abi_class ==
                    LUNA_X86_64_ABI_CLASS_INTEGER) {
                    if (location->pieces[piece_index].register_index >=
                            LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT ||
                        !luna_x86_64_emit_load_integer_piece(
                            output,
                            location->pieces[piece_index].value_offset_bytes,
                            location->pieces[piece_index].size_bytes,
                            argument_registers_64_bit[location
                                                          ->pieces[piece_index]
                                                          .register_index])) {
                        return false;
                    }
                } else {
                    const uint32_t register_index =
                        location->pieces[piece_index].register_index;
                    const uint64_t piece_size =
                        location->pieces[piece_index].size_bytes;
                    if (register_index >=
                            LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT ||
                        (piece_size != 4U && piece_size != 8U) ||
                        !luna_string_builder_append_format(
                            output,
                            piece_size == 4U
                                ? "    movss %" PRIu64 "(%%r11), %s\n"
                                : "    movq %" PRIu64 "(%%r11), %s\n",
                            location->pieces[piece_index].value_offset_bytes,
                            argument_registers_float[register_index])) {
                        return false;
                    }
                }
            }
            continue;
        }
        if (location->kind == LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER) {
            const char *move = luna_x86_64_float_move(*parameter_type);
            if (move == NULL ||
                location->register_index >=
                    LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT ||
                !luna_string_builder_append_format(output, "    %s ", move) ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  *argument) ||
                !luna_string_builder_append_format(
                    output, ", %s\n",
                    argument_registers_float[location->register_index])) {
                return false;
            }
            continue;
        }
        if (location->kind != LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER ||
            location->register_index >=
                LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT) {
            return false;
        }
        const bool is_64_bit = luna_x86_64_type_is_64_bit(*parameter_type);
        const uint32_t width = luna_x86_64_type_bit_width(*parameter_type);
        const bool needs_external_sign_extension =
            callee->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
            luna_x86_64_machine_type_is_signed_integer(*parameter_type) &&
            width < 32U;
        if (needs_external_sign_extension) {
            if (!luna_x86_64_emit_signed_load_32(
                    output, function, rewrite, *argument, *parameter_type,
                    argument_registers_32_bit[location->register_index])) {
                return false;
            }
        } else if (!luna_string_builder_append_c_string(
                       output, is_64_bit ? "    movq " : "    movl ") ||
                   !luna_x86_64_append_value_operand(output, function, rewrite,
                                                     *argument) ||
                   !luna_string_builder_append_format(
                       output, ", %s\n",
                       is_64_bit
                           ? argument_registers_64_bit[location->register_index]
                           : argument_registers_32_bit[location
                                                           ->register_index])) {
            return false;
        }
    }

    if (!luna_string_builder_append_c_string(output, "    call ") ||
        !luna_x86_64_append_linkage_symbol(output, callee) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    if (callee_abi->return_location.is_aggregate &&
        !callee_abi->return_location.uses_hidden_pointer) {
        if (!luna_string_builder_append_c_string(output, "    leaq ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(output, ", %r11\n")) {
            return false;
        }
        for (uint32_t index = 0U;
             index < callee_abi->return_location.piece_count; index += 1U) {
            const uint64_t offset =
                callee_abi->return_location.pieces[index].value_offset_bytes;
            const uint64_t size =
                callee_abi->return_location.pieces[index].size_bytes;
            if (callee_abi->return_location.pieces[index].abi_class ==
                LUNA_X86_64_ABI_CLASS_INTEGER) {
                const uint32_t register_index =
                    callee_abi->return_location.pieces[index].register_index;
                if (register_index >= 2U ||
                    !luna_x86_64_emit_store_integer_piece(
                        output, return_registers_integer[register_index],
                        offset, size)) {
                    return false;
                }
            } else {
                const uint32_t register_index =
                    callee_abi->return_location.pieces[index].register_index;
                if (register_index >= 2U || (size != 4U && size != 8U) ||
                    !luna_string_builder_append_format(
                        output,
                        size == 4U ? "    movss %s, %" PRIu64 "(%%r11)\n"
                                   : "    movq %s, %" PRIu64 "(%%r11)\n",
                        return_registers_float[register_index], offset)) {
                    return false;
                }
            }
        }
    }

    if (callee_abi->call_frame_size_bytes != 0U &&
        !luna_string_builder_append_format(output,
                                           "    addq $%" PRIu64 ", %%rsp\n",
                                           callee_abi->call_frame_size_bytes)) {
        return false;
    }

    if (callee_abi->return_location.is_aggregate) {
        if (!luna_string_builder_append_c_string(output, "    leaq ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(output, ", %rax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);
    }
    if (instruction->result != LUNA_X86_64_MACHINE_INVALID_ID) {
        if (callee->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
            instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
            !luna_x86_64_emit_normalize_bool_al(output)) {
            return false;
        }
        if (luna_x86_64_machine_type_is_float(instruction->type)) {
            return luna_x86_64_emit_store_xmm0(output, function, rewrite,
                                               instruction->result,
                                               instruction->type);
        }
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }
        if (luna_x86_64_machine_type_is_integer(instruction->type)) {
            return luna_x86_64_emit_store_integer_eax(output, function, rewrite,
                                                      instruction->result,
                                                      instruction->type);
        }
        return luna_x86_64_emit_store_eax(output, function, rewrite,
                                          instruction->result);
    }
    return true;
}

static bool luna_x86_64_emit_narrow_division(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    const LunaX8664MachineInstruction *instruction) {
    const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
    const bool is_signed =
        luna_x86_64_machine_type_is_signed_integer(instruction->type);
    if (width != 8U && width != 16U) {
        return false;
    }

    if (is_signed) {
        const int32_t minimum = width == 8U ? INT8_MIN : INT16_MIN;
        if (!luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                             instruction->left,
                                             instruction->type, "%eax") ||
            !luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                             instruction->right,
                                             instruction->type, "%ecx") ||
            !luna_string_builder_append_format(
                output,
                "    cmpl $%" PRId32 ", %%eax\n"
                "    jne .Lfn%zu_div%u_safe\n"
                "    cmpl $-1, %%ecx\n"
                "    jne .Lfn%zu_div%u_safe\n"
                "    xorl %%edx, %%edx\n"
                "    xorl %%ecx, %%ecx\n"
                "    divl %%ecx\n"
                ".Lfn%zu_div%u_safe:\n"
                "    cltd\n"
                "    idivl %%ecx\n",
                minimum, function_index, instruction->result, function_index,
                instruction->result, function_index, instruction->result)) {
            return false;
        }
    } else if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                          instruction->left) ||
               !luna_x86_64_emit_load_ecx(output, function, rewrite,
                                          instruction->right) ||
               !luna_string_builder_append_c_string(output,
                                                    "    xorl %edx, %edx\n"
                                                    "    divl %ecx\n")) {
        return false;
    }

    if (instruction->opcode == LUNA_X86_64_MACHINE_REM_INTEGER &&
        !luna_string_builder_append_c_string(output, "    movl %edx, %eax\n")) {
        return false;
    }
    return luna_x86_64_emit_store_integer_eax(
        output, function, rewrite, instruction->result, instruction->type);
}

static bool luna_x86_64_float_power_bits(LunaX8664MachineType type,
                                         uint32_t exponent, bool is_negative,
                                         uint64_t *bits) {
    if (type == LUNA_X86_64_MACHINE_TYPE_F32) {
        const uint32_t sign = is_negative ? UINT32_C(0x80000000) : 0U;
        *bits = sign | ((uint64_t)(exponent + 127U) << 23U);
        return true;
    }
    if (type == LUNA_X86_64_MACHINE_TYPE_F64) {
        const uint64_t sign = is_negative ? UINT64_C(0x8000000000000000) : 0U;
        *bits = sign | ((uint64_t)(exponent + 1023U) << 52U);
        return true;
    }
    return false;
}

static bool luna_x86_64_float_integer_maximum_bits(LunaX8664MachineType type,
                                                   uint32_t integer_width,
                                                   uint64_t *bits) {
    uint32_t precision = 0U;
    uint32_t fraction_width = 0U;
    uint32_t exponent_bias = 0U;
    if (type == LUNA_X86_64_MACHINE_TYPE_F32) {
        precision = 24U;
        fraction_width = 23U;
        exponent_bias = 127U;
    } else if (type == LUNA_X86_64_MACHINE_TYPE_F64) {
        precision = 53U;
        fraction_width = 52U;
        exponent_bias = 1023U;
    } else {
        return false;
    }
    if (integer_width == 0U || integer_width > precision) {
        return false;
    }

    const uint64_t fraction = ((UINT64_C(1) << (integer_width - 1U)) - 1U)
                              << (precision - integer_width);
    *bits = ((uint64_t)(integer_width - 1U + exponent_bias) << fraction_width) |
            fraction;
    return true;
}

static bool luna_x86_64_emit_float_bits_xmm1(LunaStringBuilder *output,
                                             LunaX8664MachineType type,
                                             uint64_t bits) {
    if (type == LUNA_X86_64_MACHINE_TYPE_F32) {
        return luna_string_builder_append_format(output,
                                                 "    movl $0x%08" PRIx32
                                                 ", %%eax\n"
                                                 "    movd %%eax, %%xmm1\n",
                                                 (uint32_t)bits);
    }
    if (type == LUNA_X86_64_MACHINE_TYPE_F64) {
        return luna_string_builder_append_format(output,
                                                 "    movabsq $0x%016" PRIx64
                                                 ", %%rax\n"
                                                 "    movq %%rax, %%xmm1\n",
                                                 bits);
    }
    return false;
}

static bool luna_x86_64_emit_float_to_integer_range_check(
    LunaStringBuilder *output, size_t function_index,
    const LunaX8664MachineInstruction *instruction,
    LunaX8664MachineType source_type) {
    const uint32_t target_width = luna_x86_64_type_bit_width(instruction->type);
    const bool is_signed =
        luna_x86_64_machine_type_is_signed_integer(instruction->type);
    const char *compare =
        source_type == LUNA_X86_64_MACHINE_TYPE_F32 ? "ucomiss" : "ucomisd";
    uint64_t lower_bits = 0U;
    uint64_t upper_bits = 0U;
    const uint32_t bound_exponent =
        is_signed ? target_width - 1U : target_width;
    const uint32_t maximum_integer_width =
        is_signed ? target_width - 1U : target_width;
    const bool has_exact_maximum = luna_x86_64_float_integer_maximum_bits(
        source_type, maximum_integer_width, &upper_bits);
    if ((is_signed && !luna_x86_64_float_power_bits(source_type, bound_exponent,
                                                    true, &lower_bits)) ||
        (!has_exact_maximum &&
         !luna_x86_64_float_power_bits(source_type, bound_exponent, false,
                                       &upper_bits)) ||
        !luna_x86_64_emit_float_bits_xmm1(output, source_type, lower_bits) ||
        !luna_string_builder_append_format(output,
                                           "    %s %%xmm1, %%xmm0\n"
                                           "    jb .Lfn%zu_fptoi%u_trap\n",
                                           compare, function_index,
                                           instruction->result) ||
        !luna_x86_64_emit_float_bits_xmm1(output, source_type, upper_bits) ||
        !luna_string_builder_append_format(
            output,
            "    %s %%xmm1, %%xmm0\n"
            "    %s .Lfn%zu_fptoi%u_trap\n",
            compare, has_exact_maximum ? "ja" : "jae", function_index,
            instruction->result)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_float_conversion(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction) {
    const LunaX8664MachineType *source_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    if (source_type == NULL ||
        !luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                    instruction->left, *source_type)) {
        return false;
    }

    const char *mnemonic =
        *source_type == LUNA_X86_64_MACHINE_TYPE_F32 ? "cvtss2sd" : "cvtsd2ss";
    return luna_string_builder_append_format(output, "    %s %%xmm0, %%xmm0\n",
                                             mnemonic) &&
           luna_x86_64_emit_store_xmm0(output, function, rewrite,
                                       instruction->result, instruction->type);
}

static bool luna_x86_64_emit_integer_to_float_conversion(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    const LunaX8664MachineInstruction *instruction) {
    const LunaX8664MachineType *source_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    if (source_type == NULL) {
        return false;
    }

    const uint32_t source_width = luna_x86_64_type_bit_width(*source_type);
    const char *convert = instruction->type == LUNA_X86_64_MACHINE_TYPE_F32
                              ? "cvtsi2ssq"
                              : "cvtsi2sdq";
    const char *add =
        instruction->type == LUNA_X86_64_MACHINE_TYPE_F32 ? "addss" : "addsd";
    if (luna_x86_64_machine_type_is_signed_integer(*source_type)) {
        if (!(source_width == 64U
                  ? luna_x86_64_emit_load_rax(output, function, rewrite,
                                              instruction->left)
                  : luna_x86_64_emit_signed_load_64(output, function, rewrite,
                                                    instruction->left,
                                                    *source_type)) ||
            !luna_string_builder_append_format(output, "    %s %%rax, %%xmm0\n",
                                               convert)) {
            return false;
        }
    } else if (source_width < 64U) {
        if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_format(output, "    %s %%rax, %%xmm0\n",
                                               convert)) {
            return false;
        }
    } else {
        if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_format(
                output,
                "    testq %%rax, %%rax\n"
                "    js .Lfn%zu_uitofp%u_large\n"
                "    %s %%rax, %%xmm0\n"
                "    jmp .Lfn%zu_uitofp%u_done\n"
                ".Lfn%zu_uitofp%u_large:\n"
                "    movq %%rax, %%rdx\n"
                "    shrq $1, %%rax\n"
                "    andl $1, %%edx\n"
                "    orq %%rdx, %%rax\n"
                "    %s %%rax, %%xmm0\n"
                "    %s %%xmm0, %%xmm0\n"
                ".Lfn%zu_uitofp%u_done:\n",
                function_index, instruction->result, convert, function_index,
                instruction->result, function_index, instruction->result,
                convert, add, function_index, instruction->result)) {
            return false;
        }
    }

    return luna_x86_64_emit_store_xmm0(output, function, rewrite,
                                       instruction->result, instruction->type);
}

static bool luna_x86_64_emit_float_to_integer_conversion(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    const LunaX8664MachineInstruction *instruction) {
    const LunaX8664MachineType *source_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    if (source_type == NULL ||
        !luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                    instruction->left, *source_type) ||
        !luna_x86_64_emit_float_to_integer_range_check(
            output, function_index, instruction, *source_type)) {
        return false;
    }

    const uint32_t target_width = luna_x86_64_type_bit_width(instruction->type);
    const bool is_signed =
        luna_x86_64_machine_type_is_signed_integer(instruction->type);
    const char *convert_32 = *source_type == LUNA_X86_64_MACHINE_TYPE_F32
                                 ? "cvttss2si"
                                 : "cvttsd2si";
    const char *convert_64 = *source_type == LUNA_X86_64_MACHINE_TYPE_F32
                                 ? "cvttss2siq"
                                 : "cvttsd2siq";

    if (is_signed) {
        if (target_width == 64U) {
            if (!luna_string_builder_append_format(
                    output, "    %s %%xmm0, %%rax\n", convert_64) ||
                !luna_x86_64_emit_store_rax(output, function, rewrite,
                                            instruction->result)) {
                return false;
            }
        } else if (!luna_string_builder_append_format(
                       output, "    %s %%xmm0, %%eax\n", convert_32) ||
                   !luna_x86_64_emit_store_integer_eax(
                       output, function, rewrite, instruction->result,
                       instruction->type)) {
            return false;
        }
    } else if (target_width < 64U) {
        if (!luna_string_builder_append_format(output, "    %s %%xmm0, %%rax\n",
                                               convert_64) ||
            !luna_x86_64_emit_store_integer_eax(output, function, rewrite,
                                                instruction->result,
                                                instruction->type)) {
            return false;
        }
    } else {
        uint64_t split_bits = 0U;
        const char *subtract =
            *source_type == LUNA_X86_64_MACHINE_TYPE_F32 ? "subss" : "subsd";
        if (!luna_x86_64_float_power_bits(*source_type, 63U, false,
                                          &split_bits) ||
            !luna_x86_64_emit_float_bits_xmm1(output, *source_type,
                                              split_bits) ||
            !luna_string_builder_append_format(
                output,
                "    %s %%xmm1, %%xmm0\n"
                "    jb .Lfn%zu_fptoi%u_low\n"
                "    %s %%xmm1, %%xmm0\n"
                "    %s %%xmm0, %%rax\n"
                "    movabsq $0x8000000000000000, %%rdx\n"
                "    orq %%rdx, %%rax\n"
                "    jmp .Lfn%zu_fptoi%u_converted\n"
                ".Lfn%zu_fptoi%u_low:\n"
                "    %s %%xmm0, %%rax\n"
                ".Lfn%zu_fptoi%u_converted:\n",
                *source_type == LUNA_X86_64_MACHINE_TYPE_F32 ? "ucomiss"
                                                             : "ucomisd",
                function_index, instruction->result, subtract, convert_64,
                function_index, instruction->result, function_index,
                instruction->result, convert_64, function_index,
                instruction->result) ||
            !luna_x86_64_emit_store_rax(output, function, rewrite,
                                        instruction->result)) {
            return false;
        }
    }

    return luna_string_builder_append_format(
        output,
        "    jmp .Lfn%zu_fptoi%u_done\n"
        ".Lfn%zu_fptoi%u_trap:\n"
        "    ud2\n"
        ".Lfn%zu_fptoi%u_done:\n",
        function_index, instruction->result, function_index,
        instruction->result, function_index, instruction->result);
}

static bool luna_x86_64_emit_indirect_load(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction) {
    if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->left)) {
        return false;
    }

    if (luna_x86_64_machine_type_is_float(instruction->type)) {
        const char *move = luna_x86_64_float_move(instruction->type);
        return move != NULL &&
               luna_string_builder_append_format(
                   output, "    %s (%%rax), %%xmm0\n", move) &&
               luna_x86_64_emit_store_xmm0(output, function, rewrite,
                                           instruction->result,
                                           instruction->type);
    }

    const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
    const char *load = NULL;
    switch (width) {
    case 1U:
    case 8U:
        load = "movzbl";
        break;
    case 16U:
        load = "movzwl";
        break;
    case 32U:
        load = "movl";
        break;
    case 64U:
        load = "movq";
        break;
    default:
        return false;
    }

    if (!luna_string_builder_append_format(output, "    %s (%%rax), %s\n", load,
                                           width == 64U ? "%rax" : "%eax")) {
        return false;
    }
    if (instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
        !luna_x86_64_emit_normalize_bool_eax(output)) {
        return false;
    }
    return width == 64U ? luna_x86_64_emit_store_rax(output, function, rewrite,
                                                     instruction->result)
                        : luna_x86_64_emit_store_eax(output, function, rewrite,
                                                     instruction->result);
}

static bool luna_x86_64_emit_indirect_store(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction) {
    if (luna_x86_64_machine_type_is_float(instruction->memory_type)) {
        const char *move = luna_x86_64_float_move(instruction->memory_type);
        return move != NULL &&
               luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                          instruction->right,
                                          instruction->memory_type) &&
               luna_x86_64_emit_load_rax(output, function, rewrite,
                                         instruction->left) &&
               luna_string_builder_append_format(
                   output, "    %s %%xmm0, (%%rax)\n", move);
    }

    const uint32_t width = luna_x86_64_type_bit_width(instruction->memory_type);
    const char *move = NULL;
    const char *value_register = NULL;
    switch (width) {
    case 1U:
    case 8U:
        move = "movb";
        value_register = "%cl";
        break;
    case 16U:
        move = "movw";
        value_register = "%cx";
        break;
    case 32U:
        move = "movl";
        value_register = "%ecx";
        break;
    case 64U:
        move = "movq";
        value_register = "%rcx";
        break;
    default:
        return false;
    }

    if (!luna_string_builder_append_c_string(
            output, width == 64U ? "    movq " : "    movl ") ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(
            output, width == 64U ? ", %rcx\n" : ", %ecx\n") ||
        !luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->left)) {
        return false;
    }
    return luna_string_builder_append_format(output, "    %s %s, (%%rax)\n",
                                             move, value_register);
}

static bool luna_x86_64_emit_zero_slot(LunaStringBuilder *output,
                                       const LunaX8664MachineFunction *function,
                                       LunaX8664MachineStackSlotId slot_id) {
    const LunaX8664MachineStackSlot *slot =
        luna_vector_at_const(&function->slots, (size_t)slot_id);
    if (slot == NULL ||
        !luna_string_builder_append_c_string(output, "    leaq ") ||
        !luna_x86_64_append_slot_operand(output, function, slot_id) ||
        !luna_string_builder_append_format(output,
                                           ", %%rdi\n"
                                           "    xorl %%eax, %%eax\n"
                                           "    movq $%" PRIu64 ", %%rcx\n"
                                           "    rep stosb\n",
                                           slot->size_bytes)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_pointer_offset(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaX8664MachineInstruction *instruction) {
    if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->left) ||
        !luna_string_builder_append_c_string(output, "    movq ") ||
        !luna_x86_64_append_value_operand(output, function, rewrite,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %rcx\n")) {
        return false;
    }

    if (instruction->immediate == 1U || instruction->immediate == 2U ||
        instruction->immediate == 4U || instruction->immediate == 8U) {
        if (!luna_string_builder_append_format(
                output, "    leaq (%%rax,%%rcx,%" PRIu64 "), %%rax\n",
                instruction->immediate)) {
            return false;
        }
    } else if (!luna_string_builder_append_format(output,
                                                  "    imulq $%" PRIu64
                                                  ", %%rcx, %%rcx\n"
                                                  "    addq %%rcx, %%rax\n",
                                                  instruction->immediate)) {
        return false;
    }

    return luna_x86_64_emit_store_rax(output, function, rewrite,
                                      instruction->result);
}

static bool
luna_x86_64_emit_memory_copy(LunaStringBuilder *output,
                             const LunaX8664MachineFunction *function,
                             const LunaX8664FunctionInstructionRewrite *rewrite,
                             const LunaX8664MachineInstruction *instruction) {
    if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->left) ||
        !luna_string_builder_append_c_string(output, "    movq %rax, %rdi\n") ||
        !luna_x86_64_emit_load_rax(output, function, rewrite,
                                   instruction->right) ||
        !luna_string_builder_append_format(
            output,
            "    movq %%rax, %%rsi\n"
            "    movq $%" PRIu64 ", %%rcx\n"
            "    cmpq %%rsi, %%rdi\n"
            "    jbe 1f\n"
            "    leaq %" PRIu64 "(%%rsi), %%rax\n"
            "    cmpq %%rax, %%rdi\n"
            "    jae 1f\n"
            "    leaq -1(%%rsi,%%rcx), %%rsi\n"
            "    leaq -1(%%rdi,%%rcx), %%rdi\n"
            "    std\n"
            "    rep movsb\n"
            "    cld\n"
            "    jmp 2f\n"
            "1:\n"
            "    cld\n"
            "    rep movsb\n"
            "2:\n",
            instruction->immediate, instruction->immediate)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_instruction(
    LunaStringBuilder *output, const LunaX8664MachineModule *module,
    const LunaX8664ModuleAbi *abi, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    const LunaX8664MachineInstruction *instruction) {
    switch (instruction->opcode) {
    case LUNA_X86_64_MACHINE_CONST_BOOL:
        if (!luna_string_builder_append_format(
                output, "    movl $%" PRIu64 ", ", instruction->immediate) ||
            !luna_x86_64_append_value_operand(output, function, rewrite,
                                              instruction->result)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_X86_64_MACHINE_CONST_NULL:
        return luna_string_builder_append_c_string(output,
                                                   "    xorq %rax, %rax\n") &&
               luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_CONST_FLOAT:
        if (instruction->type == LUNA_X86_64_MACHINE_TYPE_F32) {
            if (!luna_string_builder_append_format(
                    output, "    movl $0x%08" PRIx32 ", %%eax\n",
                    (uint32_t)instruction->immediate)) {
                return false;
            }
            return luna_x86_64_emit_store_eax(output, function, rewrite,
                                              instruction->result);
        }
        if (!luna_string_builder_append_format(
                output, "    movabsq $0x%016" PRIx64 ", %%rax\n",
                instruction->immediate)) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_CONST_INTEGER:
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            if (!luna_string_builder_append_format(
                    output, "    movabsq $0x%016" PRIx64 ", %%rax\n",
                    instruction->immediate)) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }
        if (!luna_string_builder_append_format(
                output, "    movl $0x%08" PRIx32 ", ",
                (uint32_t)instruction->immediate) ||
            !luna_x86_64_append_value_operand(output, function, rewrite,
                                              instruction->result)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
        if (!luna_string_builder_append_c_string(output, "    leaq ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(output, ", %rax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
        return luna_x86_64_emit_load_rax(output, function, rewrite,
                                         instruction->left) &&
               luna_string_builder_append_format(
                   output, "    leaq %" PRIu64 "(%%rax), %%rax\n",
                   instruction->immediate) &&
               luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
        if (!luna_string_builder_append_format(
                output, "    leaq .Lglobal%u(%%rip), %%rax\n",
                instruction->global)) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_ZERO_SLOT:
        return luna_x86_64_emit_zero_slot(output, function, instruction->slot);

    case LUNA_X86_64_MACHINE_MEMORY_COPY:
        return luna_x86_64_emit_memory_copy(output, function, rewrite,
                                            instruction);

    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
        return luna_x86_64_emit_indirect_load(output, function, rewrite,
                                              instruction);

    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
        return luna_x86_64_emit_indirect_store(output, function, rewrite,
                                               instruction);

    case LUNA_X86_64_MACHINE_NULL_CHECK:
        return luna_x86_64_emit_load_rax(output, function, rewrite,
                                         instruction->left) &&
               luna_string_builder_append_c_string(output,
                                                   "    testq %rax, %rax\n"
                                                   "    jne 1f\n"
                                                   "    ud2\n"
                                                   "1:\n");

    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
        return luna_x86_64_emit_load_rax(output, function, rewrite,
                                         instruction->left) &&
               luna_string_builder_append_format(output,
                                                 "    cmpq $%" PRIu64
                                                 ", %%rax\n"
                                                 "    jb 1f\n"
                                                 "    ud2\n"
                                                 "1:\n",
                                                 instruction->immediate);

    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
        return luna_x86_64_emit_pointer_offset(output, function, rewrite,
                                               instruction);

    case LUNA_X86_64_MACHINE_LOAD: {
        const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
        const char *load = NULL;
        switch (width) {
        case 1U:
        case 8U:
            load = "    movzbl ";
            break;
        case 16U:
            load = "    movzwl ";
            break;
        case 32U:
            load = "    movl ";
            break;
        case 64U:
            load = "    movq ";
            break;
        default:
            return false;
        }
        if (!luna_string_builder_append_c_string(output, load) ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(
                output, width == 64U ? ", %rax\n" : ", %eax\n")) {
            return false;
        }
        if (instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
            !luna_x86_64_emit_normalize_bool_eax(output)) {
            return false;
        }
        return width == 64U
                   ? luna_x86_64_emit_store_rax(output, function, rewrite,
                                                instruction->result)
                   : luna_x86_64_emit_store_eax(output, function, rewrite,
                                                instruction->result);
    }

    case LUNA_X86_64_MACHINE_STORE: {
        const LunaX8664MachineType *value_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        const bool is_64_bit =
            value_type != NULL && luna_x86_64_type_is_64_bit(*value_type);
        if (!(is_64_bit ? luna_x86_64_emit_load_rax(output, function, rewrite,
                                                    instruction->left)
                        : luna_x86_64_emit_load_eax(output, function, rewrite,
                                                    instruction->left)) ||
            !luna_string_builder_append_c_string(
                output, is_64_bit ? "    movq %rax, " : "    movl %eax, ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_X86_64_MACHINE_NEG_FLOAT:
        if (instruction->type == LUNA_X86_64_MACHINE_TYPE_F64) {
            if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(
                    output, "    movabsq $0x8000000000000000, %rdx\n"
                            "    xorq %rdx, %rax\n")) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }
        if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_c_string(
                output, "    xorl $0x80000000, %eax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_eax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_NEG_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(
                    output,
                    instruction->opcode == LUNA_X86_64_MACHINE_NEG_INTEGER
                        ? "    negq %rax\n"
                        : "    notq %rax\n")) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }

        if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_c_string(
                output, instruction->opcode == LUNA_X86_64_MACHINE_NEG_INTEGER
                            ? "    negl %eax\n"
                            : "    notl %eax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_integer_eax(
            output, function, rewrite, instruction->result, instruction->type);

    case LUNA_X86_64_MACHINE_BOOL_NOT:
        if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                       instruction->left)) {
            return false;
        }

        if (!luna_string_builder_append_c_string(output,
                                                 "    testl %eax, %eax\n"
                                                 "    sete %al\n"
                                                 "    movzbl %al, %eax\n")) {
            return false;
        }

        return luna_x86_64_emit_store_eax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_CONVERT_INTEGER: {
        const LunaX8664MachineType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (source_type == NULL) {
            return false;
        }
        const uint32_t source_width = luna_x86_64_type_bit_width(*source_type);
        const uint32_t target_width =
            luna_x86_64_type_bit_width(instruction->type);
        if (target_width == 64U) {
            if (source_width == 64U) {
                return luna_x86_64_emit_load_rax(output, function, rewrite,
                                                 instruction->left) &&
                       luna_x86_64_emit_store_rax(output, function, rewrite,
                                                  instruction->result);
            }
            if (luna_x86_64_machine_type_is_signed_integer(*source_type)) {
                if (!luna_x86_64_emit_signed_load_64(output, function, rewrite,
                                                     instruction->left,
                                                     *source_type)) {
                    return false;
                }
            } else if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                                  instruction->left)) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }

        if (source_width < target_width &&
            luna_x86_64_machine_type_is_signed_integer(*source_type)) {
            if (!luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                                 instruction->left,
                                                 *source_type, "%eax")) {
                return false;
            }
        } else if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                              instruction->left)) {
            return false;
        }
        return luna_x86_64_emit_store_integer_eax(
            output, function, rewrite, instruction->result, instruction->type);
    }

    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
        return luna_x86_64_emit_float_conversion(output, function, rewrite,
                                                 instruction);

    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
        return luna_x86_64_emit_integer_to_float_conversion(
            output, function, rewrite, function_index, instruction);

    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
        return luna_x86_64_emit_float_to_integer_conversion(
            output, function, rewrite, function_index, instruction);

    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
        return luna_x86_64_emit_load_rax(output, function, rewrite,
                                         instruction->left) &&
               luna_x86_64_emit_store_rax(output, function, rewrite,
                                          instruction->result);

    case LUNA_X86_64_MACHINE_ADD_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "addq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "addl");

    case LUNA_X86_64_MACHINE_SUB_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "subq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "subl");

    case LUNA_X86_64_MACHINE_MUL_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "imulq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "imull");

    case LUNA_X86_64_MACHINE_ADD_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, rewrite, instruction,
            instruction->type == LUNA_X86_64_MACHINE_TYPE_F32 ? "addss"
                                                              : "addsd");

    case LUNA_X86_64_MACHINE_SUB_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, rewrite, instruction,
            instruction->type == LUNA_X86_64_MACHINE_TYPE_F32 ? "subss"
                                                              : "subsd");

    case LUNA_X86_64_MACHINE_MUL_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, rewrite, instruction,
            instruction->type == LUNA_X86_64_MACHINE_TYPE_F32 ? "mulss"
                                                              : "mulsd");

    case LUNA_X86_64_MACHINE_DIV_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, rewrite, instruction,
            instruction->type == LUNA_X86_64_MACHINE_TYPE_F32 ? "divss"
                                                              : "divsd");

    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "andq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "andl");

    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "orq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "orl");

    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, rewrite,
                                                instruction, "xorq")
                   : luna_x86_64_emit_binary_32(output, function, rewrite,
                                                instruction, "xorl");

    case LUNA_X86_64_MACHINE_DIV_INTEGER:
    case LUNA_X86_64_MACHINE_REM_INTEGER: {
        if (luna_x86_64_type_bit_width(instruction->type) < 32U) {
            return luna_x86_64_emit_narrow_division(
                output, function, rewrite, function_index, instruction);
        }

        const bool is_64_bit = luna_x86_64_type_is_64_bit(instruction->type);
        const bool is_signed =
            luna_x86_64_machine_type_is_signed_integer(instruction->type);
        if (!(is_64_bit ? luna_x86_64_emit_load_rax(output, function, rewrite,
                                                    instruction->left)
                        : luna_x86_64_emit_load_eax(output, function, rewrite,
                                                    instruction->left)) ||
            !luna_string_builder_append_c_string(
                output, is_64_bit
                            ? (is_signed ? "    cqto\n    idivq "
                                         : "    xorq %rdx, %rdx\n    divq ")
                            : (is_signed ? "    cltd\n    idivl "
                                         : "    xorl %edx, %edx\n    divl ")) ||
            !luna_x86_64_append_value_operand(output, function, rewrite,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }

        if (instruction->opcode == LUNA_X86_64_MACHINE_REM_INTEGER &&
            !luna_string_builder_append_c_string(
                output, is_64_bit ? "    movq %rdx, %rax\n"
                                  : "    movl %edx, %eax\n")) {
            return false;
        }
        return is_64_bit ? luna_x86_64_emit_store_rax(output, function, rewrite,
                                                      instruction->result)
                         : luna_x86_64_emit_store_eax(output, function, rewrite,
                                                      instruction->result);
    }

    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER: {
        const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
        const bool is_64_bit = width == 64U;
        const bool is_left =
            instruction->opcode == LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER;
        const bool is_signed =
            luna_x86_64_machine_type_is_signed_integer(instruction->type);
        if (is_64_bit) {
            if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(output, "    movq ") ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  instruction->right) ||
                !luna_string_builder_append_c_string(output, ", %rcx\n") ||
                !luna_string_builder_append_c_string(
                    output, is_left ? "    shlq %cl, %rax\n"
                                    : (is_signed ? "    sarq %cl, %rax\n"
                                                 : "    shrq %cl, %rax\n"))) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function, rewrite,
                                              instruction->result);
        }

        const bool needs_signed_load = !is_left && is_signed && width < 32U;
        if (!(needs_signed_load
                  ? luna_x86_64_emit_signed_load_32(output, function, rewrite,
                                                    instruction->left,
                                                    instruction->type, "%eax")
                  : luna_x86_64_emit_load_eax(output, function, rewrite,
                                              instruction->left)) ||
            !luna_x86_64_emit_load_ecx(output, function, rewrite,
                                       instruction->right)) {
            return false;
        }
        if (width < 32U && !luna_string_builder_append_format(
                               output, "    andl $%u, %%ecx\n", width - 1U)) {
            return false;
        }
        if (!luna_string_builder_append_c_string(
                output, is_left ? "    shll %cl, %eax\n"
                                : (is_signed ? "    sarl %cl, %eax\n"
                                             : "    shrl %cl, %eax\n"))) {
            return false;
        }
        return luna_x86_64_emit_store_integer_eax(
            output, function, rewrite, instruction->result, instruction->type);
    }

    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        return luna_x86_64_emit_compare(output, function, rewrite, instruction);

    case LUNA_X86_64_MACHINE_CALL:
        return luna_x86_64_emit_call(output, module, abi, function, rewrite,
                                     instruction);

    case LUNA_X86_64_MACHINE_JUMP:
        return luna_string_builder_append_format(
            output, "    jmp .Lfn%zu_bb%u\n", function_index,
            instruction->true_block);

    case LUNA_X86_64_MACHINE_BRANCH:
        if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                       instruction->left) ||
            !luna_string_builder_append_c_string(output,
                                                 "    testl %eax, %eax\n") ||
            !luna_string_builder_append_format(
                output,
                "    jne .Lfn%zu_bb%u\n"
                "    jmp .Lfn%zu_bb%u\n",
                function_index, instruction->true_block, function_index,
                instruction->false_block)) {
            return false;
        }
        return true;

    case LUNA_X86_64_MACHINE_RETURN: {
        const LunaX8664FunctionAbi *function_abi =
            luna_vector_at_const(&abi->functions, function_index);
        static const char *return_registers_integer[] = {"%rax", "%rdx"};
        static const char *return_registers_float[] = {"%xmm0", "%xmm1"};
        if (function_abi == NULL) {
            return false;
        }
        if (function_abi->return_location.is_aggregate) {
            if (instruction->left == LUNA_X86_64_MACHINE_INVALID_ID ||
                !luna_string_builder_append_c_string(output, "    movq ") ||
                !luna_x86_64_append_value_operand(output, function, rewrite,
                                                  instruction->left) ||
                !luna_string_builder_append_c_string(output, ", %r11\n")) {
                return false;
            }
            if (function_abi->return_location.uses_hidden_pointer) {
                int32_t hidden_offset = 0;
                if (!luna_x86_64_hidden_return_offset(function, rewrite,
                                                      &hidden_offset) ||
                    !luna_string_builder_append_format(
                        output,
                        "    movq %d(%%rbp), %%rdi\n"
                        "    movq %%r11, %%rsi\n"
                        "    movq $%" PRIu64 ", %%rcx\n"
                        "    rep movsb\n"
                        "    movq %d(%%rbp), %%rax\n",
                        hidden_offset, function_abi->return_location.size_bytes,
                        hidden_offset)) {
                    return false;
                }
            } else {
                for (uint32_t reverse_index =
                         function_abi->return_location.piece_count;
                     reverse_index > 0U; reverse_index -= 1U) {
                    const uint32_t index = reverse_index - 1U;
                    const uint64_t offset =
                        function_abi->return_location.pieces[index]
                            .value_offset_bytes;
                    const uint64_t size =
                        function_abi->return_location.pieces[index].size_bytes;
                    const uint32_t register_index =
                        function_abi->return_location.pieces[index]
                            .register_index;
                    if (function_abi->return_location.pieces[index].abi_class ==
                        LUNA_X86_64_ABI_CLASS_INTEGER) {
                        if (register_index >= 2U ||
                            !luna_x86_64_emit_load_integer_piece(
                                output, offset, size,
                                return_registers_integer[register_index])) {
                            return false;
                        }
                    } else if (register_index >= 2U ||
                               (size != 4U && size != 8U) ||
                               !luna_string_builder_append_format(
                                   output,
                                   size == 4U
                                       ? "    movss %" PRIu64 "(%%r11), %s\n"
                                       : "    movq %" PRIu64 "(%%r11), %s\n",
                                   offset,
                                   return_registers_float[register_index])) {
                        return false;
                    }
                }
            }
            return luna_string_builder_append_format(
                output, "    jmp .Lfn%zu_return\n", function_index);
        }
    }
        if (instruction->left != LUNA_X86_64_MACHINE_INVALID_ID) {
            if (luna_x86_64_machine_type_is_float(function->return_type)) {
                if (!luna_x86_64_emit_load_xmm0(output, function, rewrite,
                                                instruction->left,
                                                function->return_type)) {
                    return false;
                }
            } else if (luna_x86_64_type_is_64_bit(function->return_type)) {
                if (!luna_x86_64_emit_load_rax(output, function, rewrite,
                                               instruction->left)) {
                    return false;
                }
            } else if (!luna_x86_64_emit_load_eax(output, function, rewrite,
                                                  instruction->left)) {
                return false;
            }
        }
        return luna_string_builder_append_format(
            output, "    jmp .Lfn%zu_return\n", function_index);
    }

    return false;
}

static bool luna_x86_64_frame_local_data_size(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionAbi *function_abi,
    const LunaX8664FunctionInstructionRewrite *rewrite, uint64_t *byte_count) {
    if (function == NULL || function_abi == NULL || rewrite == NULL ||
        byte_count == NULL ||
        !luna_x86_64_slot_area_size(function, LUNA_X86_64_MACHINE_INVALID_ID,
                                    false, byte_count) ||
        !luna_x86_64_align_up(*byte_count, 8U, byte_count) ||
        rewrite->spill_slot_count > (UINT64_MAX - *byte_count) / 8U) {
        return false;
    }
    *byte_count += (uint64_t)rewrite->spill_slot_count * 8U;
    if (function_abi->return_location.uses_hidden_pointer) {
        if (*byte_count > UINT64_MAX - 8U) {
            return false;
        }
        *byte_count += 8U;
    }
    return true;
}

static bool
luna_x86_64_frame_size(const LunaX8664MachineFunction *function,
                       const LunaX8664FunctionAbi *function_abi,
                       const LunaX8664FunctionInstructionRewrite *rewrite,
                       uint32_t *frame_size) {
    uint64_t byte_count = 0U;
    if (!luna_x86_64_frame_local_data_size(function, function_abi, rewrite,
                                           &byte_count)) {
        return false;
    }

    uint32_t callee_saved_count = 0U;
    for (uint32_t register_index = 1U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        const uint64_t register_bit = UINT64_C(1) << register_index;
        if ((rewrite->used_callee_saved_register_mask & register_bit) != 0U) {
            callee_saved_count += 1U;
        }
    }
    if (callee_saved_count > (UINT64_MAX - byte_count) / 8U) {
        return false;
    }
    byte_count += (uint64_t)callee_saved_count * 8U;

    uint64_t aligned = 0U;
    if (!luna_x86_64_align_up(byte_count, 16U, &aligned) ||
        aligned > (uint64_t)INT32_MAX) {
        return false;
    }

    *frame_size = (uint32_t)aligned;
    return true;
}

static bool luna_x86_64_emit_callee_saved_registers(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionAbi *function_abi,
    const LunaX8664FunctionInstructionRewrite *rewrite, bool restore) {
    uint64_t byte_offset = 0U;
    if (!luna_x86_64_frame_local_data_size(function, function_abi, rewrite,
                                           &byte_offset)) {
        return false;
    }
    for (uint32_t register_index = 1U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        const uint64_t register_bit = UINT64_C(1) << register_index;
        if ((rewrite->used_callee_saved_register_mask & register_bit) == 0U) {
            continue;
        }
        if (byte_offset > UINT64_MAX - 8U) {
            return false;
        }
        byte_offset += 8U;
        const LunaX8664PhysicalRegister physical_register =
            (LunaX8664PhysicalRegister)register_index;
        const char *register_name =
            luna_x86_64_physical_register_name(physical_register);
        if (byte_offset > (uint64_t)INT32_MAX ||
            !luna_x86_64_physical_register_is_callee_saved(physical_register)) {
            return false;
        }
        if (restore) {
            if (!luna_string_builder_append_format(
                    output, "    movq -%" PRIu64 "(%%rbp), %%%s\n", byte_offset,
                    register_name)) {
                return false;
            }
        } else if (!luna_string_builder_append_format(
                       output, "    movq %%%s, -%" PRIu64 "(%%rbp)\n",
                       register_name, byte_offset)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_emit_function(
    LunaStringBuilder *output, const LunaX8664MachineModule *module,
    const LunaX8664ModuleAbi *abi, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionAbi *function_abi,
    const LunaX8664FunctionInstructionRewrite *rewrite,
    const LunaVector *debug_files, size_t function_index) {
    static const char *argument_registers_32_bit[] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d",
    };
    static const char *argument_registers_64_bit[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };
    static const char *argument_registers_float[] = {
        "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",
    };

    uint32_t frame_size = 0U;
    if (function_abi == NULL ||
        function_abi->parameter_locations.length !=
            function->parameter_types.length ||
        !luna_x86_64_frame_size(function, function_abi, rewrite, &frame_size)) {
        return false;
    }

    if (function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_EXPORT &&
        (!luna_string_builder_append_c_string(output, "    .globl ") ||
         !luna_x86_64_append_symbol(output, function) ||
         !luna_string_builder_append_c_string(output, "\n"))) {
        return false;
    }

    if (!luna_string_builder_append_c_string(output, "    .p2align 4\n"
                                                     "    .type ") ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, ", @function\n") ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, ":\n"
                                                     "    pushq %rbp\n"
                                                     "    movq %rsp, %rbp\n")) {
        return false;
    }

    if (frame_size > 0U && !luna_string_builder_append_format(
                               output, "    subq $%u, %%rsp\n", frame_size)) {
        return false;
    }
    if (!luna_x86_64_emit_callee_saved_registers(output, function, function_abi,
                                                 rewrite, false)) {
        return false;
    }

    if (function_abi->return_location.uses_hidden_pointer) {
        int32_t hidden_offset = 0;
        if (!luna_x86_64_hidden_return_offset(function, rewrite,
                                              &hidden_offset) ||
            !luna_string_builder_append_format(
                output, "    movq %%rdi, %d(%%rbp)\n", hidden_offset)) {
            return false;
        }
    }

    for (uint32_t phase = 0U; phase < 2U; phase += 1U) {
        const bool stack_phase = phase != 0U;
        for (size_t parameter_index = 0U;
             parameter_index < function->parameter_types.length;
             parameter_index += 1U) {
            const LunaX8664MachineType *parameter_type = luna_vector_at_const(
                &function->parameter_types, parameter_index);
            const LunaX8664AbiParameterLocation *location =
                luna_vector_at_const(&function_abi->parameter_locations,
                                     parameter_index);
            if (parameter_type == NULL || location == NULL) {
                return false;
            }
            const bool is_stack =
                location->kind == LUNA_X86_64_ABI_LOCATION_STACK;
            if (is_stack != stack_phase) {
                continue;
            }

            if (location->is_aggregate) {
                if (is_stack) {
                    const uint64_t source_offset =
                        location->stack_offset_bytes + 16U;
                    if (!luna_string_builder_append_format(output,
                                                           "    leaq %" PRIu64
                                                           "(%%rbp), %%rsi\n"
                                                           "    leaq ",
                                                           source_offset) ||
                        !luna_x86_64_append_slot_operand(
                            output, function,
                            (LunaX8664MachineStackSlotId)parameter_index) ||
                        !luna_string_builder_append_format(
                            output,
                            ", %%rdi\n"
                            "    movq $%" PRIu64 ", %%rcx\n"
                            "    rep movsb\n",
                            location->size_bytes)) {
                        return false;
                    }
                    continue;
                }

                if (!luna_string_builder_append_c_string(output, "    leaq ") ||
                    !luna_x86_64_append_slot_operand(
                        output, function,
                        (LunaX8664MachineStackSlotId)parameter_index) ||
                    !luna_string_builder_append_c_string(output, ", %r11\n")) {
                    return false;
                }
                for (uint32_t piece_index = 0U;
                     piece_index < location->piece_count; piece_index += 1U) {
                    const uint32_t register_index =
                        location->pieces[piece_index].register_index;
                    const uint64_t offset =
                        location->pieces[piece_index].value_offset_bytes;
                    const uint64_t size =
                        location->pieces[piece_index].size_bytes;
                    if (location->pieces[piece_index].abi_class ==
                        LUNA_X86_64_ABI_CLASS_INTEGER) {
                        if (register_index >=
                                LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT ||
                            !luna_x86_64_emit_store_integer_piece(
                                output,
                                argument_registers_64_bit[register_index],
                                offset, size)) {
                            return false;
                        }
                    } else if (register_index >=
                                   LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT ||
                               (size != 4U && size != 8U) ||
                               !luna_string_builder_append_format(
                                   output,
                                   size == 4U
                                       ? "    movss %s, %" PRIu64 "(%%r11)\n"
                                       : "    movq %s, %" PRIu64 "(%%r11)\n",
                                   argument_registers_float[register_index],
                                   offset)) {
                        return false;
                    }
                }
                continue;
            }

            if (is_stack) {
                const uint64_t source_offset =
                    location->stack_offset_bytes + 16U;
                if (luna_x86_64_machine_type_is_float(*parameter_type)) {
                    const char *move = luna_x86_64_float_move(*parameter_type);
                    if (move == NULL ||
                        !luna_string_builder_append_format(
                            output,
                            "    %s %" PRIu64 "(%%rbp), %%xmm15\n"
                            "    %s %%xmm15, ",
                            move, source_offset, move) ||
                        !luna_x86_64_append_slot_operand(
                            output, function,
                            (LunaX8664MachineStackSlotId)parameter_index) ||
                        !luna_string_builder_append_c_string(output, "\n")) {
                        return false;
                    }
                    continue;
                }

                const bool is_64_bit =
                    luna_x86_64_type_is_64_bit(*parameter_type);
                if (!luna_string_builder_append_format(
                        output,
                        is_64_bit ? "    movq %" PRIu64 "(%%rbp), %%rax\n"
                                    "    movq %%rax, "
                                  : "    movl %" PRIu64 "(%%rbp), %%eax\n"
                                    "    movl %%eax, ",
                        source_offset) ||
                    !luna_x86_64_append_slot_operand(
                        output, function,
                        (LunaX8664MachineStackSlotId)parameter_index) ||
                    !luna_string_builder_append_c_string(output, "\n")) {
                    return false;
                }
                continue;
            }

            if (location->kind == LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER) {
                const char *move = luna_x86_64_float_move(*parameter_type);
                if (move == NULL ||
                    location->register_index >=
                        LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT ||
                    !luna_string_builder_append_format(
                        output, "    %s %s, ", move,
                        argument_registers_float[location->register_index]) ||
                    !luna_x86_64_append_slot_operand(
                        output, function,
                        (LunaX8664MachineStackSlotId)parameter_index) ||
                    !luna_string_builder_append_c_string(output, "\n")) {
                    return false;
                }
                continue;
            }

            if (location->kind != LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER ||
                location->register_index >=
                    LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT) {
                return false;
            }
            const bool is_64_bit = luna_x86_64_type_is_64_bit(*parameter_type);
            if (!luna_string_builder_append_format(
                    output, is_64_bit ? "    movq %s, " : "    movl %s, ",
                    is_64_bit
                        ? argument_registers_64_bit[location->register_index]
                        : argument_registers_32_bit[location
                                                        ->register_index]) ||
                !luna_x86_64_append_slot_operand(
                    output, function,
                    (LunaX8664MachineStackSlotId)parameter_index) ||
                !luna_string_builder_append_c_string(output, "\n")) {
                return false;
            }
        }
    }

    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (!luna_string_builder_append_format(output, ".Lfn%zu_bb%zu:\n",
                                               function_index, block_index)) {
            return false;
        }

        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (instruction == NULL ||
                !luna_x86_64_emit_debug_location(output, debug_files,
                                                 instruction) ||
                !luna_x86_64_emit_instruction(output, module, abi, function,
                                              rewrite, function_index,
                                              instruction)) {
                return false;
            }
        }
    }

    if (!luna_string_builder_append_format(output, ".Lfn%zu_return:\n",
                                           function_index) ||
        !luna_x86_64_emit_callee_saved_registers(output, function, function_abi,
                                                 rewrite, true) ||
        !luna_string_builder_append_c_string(output, "    leave\n"
                                                     "    ret\n"
                                                     "    .size ") ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, ", .-") ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, "\n\n")) {
        return false;
    }

    return true;
}

static bool luna_x86_64_emit_globals(LunaStringBuilder *output,
                                     const LunaX8664MachineModule *module) {
    for (size_t global_index = 0U; global_index < module->globals.length;
         global_index += 1U) {
        const LunaX8664MachineGlobal *global =
            luna_vector_at_const(&module->globals, global_index);
        if (global == NULL ||
            !luna_string_builder_append_c_string(
                output, global->is_read_only
                            ? "    .section .rodata,\"a\",@progbits\n"
                            : "    .data\n") ||
            !luna_string_builder_append_format(
                output,
                "    .balign %u\n"
                "    .type .Lglobal%zu, @object\n"
                ".Lglobal%zu:\n",
                global->alignment_bytes, global_index, global_index)) {
            return false;
        }

        for (size_t byte_index = 0U; byte_index < global->bytes.length;
             byte_index += 1U) {
            const uint8_t *byte =
                luna_vector_at_const(&global->bytes, byte_index);
            if (byte == NULL ||
                !luna_string_builder_append_format(output, "    .byte 0x%02x\n",
                                                   (unsigned int)*byte)) {
                return false;
            }
        }
        if (!luna_string_builder_append_format(
                output, "    .size .Lglobal%zu, .-.Lglobal%zu\n\n",
                global_index, global_index)) {
            return false;
        }
    }
    return true;
}

static bool
luna_x86_64_emit_external_declarations(LunaStringBuilder *output,
                                       const LunaX8664MachineModule *module) {
    bool emitted_declaration = false;
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (function == NULL ||
            (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
             function->linkage != LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT)) {
            continue;
        }
        if (!luna_string_builder_append_c_string(output, "    .extern ") ||
            !luna_x86_64_append_linkage_symbol(output, function) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }
        emitted_declaration = true;
    }
    return !emitted_declaration ||
           luna_string_builder_append_c_string(output, "\n");
}

bool luna_x86_64_machine_emit_assembly(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite,
    LunaDiagnosticEngine *diagnostics, LunaStringBuilder *output) {
    if (module == NULL || abi == NULL || liveness == NULL ||
        allocation == NULL || rewrite == NULL || diagnostics == NULL ||
        output == NULL || !luna_target_info_is_supported(module->target)) {
        luna_diagnostic_error_plain(
            diagnostics,
            "x86-64 backend requires target x86_64-unknown-linux-gnu");
        return false;
    }
    if (!luna_x86_64_machine_verify(module, diagnostics->stream)) {
        luna_diagnostic_error_plain(
            diagnostics, "x86-64 backend rejected invalid machine IR");
        return false;
    }
    if (!luna_x86_64_instruction_rewrite_verify(
            module, abi, liveness, allocation, rewrite, diagnostics->stream)) {
        luna_diagnostic_error_plain(
            diagnostics, "x86-64 backend rejected invalid instruction rewrite");
        return false;
    }

    const LunaX8664MachineFunction *entry = NULL;
    if (module->kind == LUNA_X86_64_MACHINE_MODULE_EXECUTABLE) {
        entry = luna_x86_64_machine_module_function_const(
            module, module->entry_function);
        if (entry == NULL) {
            luna_diagnostic_error_plain(
                diagnostics, "x86-64 backend received no entry function");
            return false;
        }
    } else if (module->kind != LUNA_X86_64_MACHINE_MODULE_LIBRARY) {
        luna_diagnostic_error_plain(diagnostics,
                                    "x86-64 backend received invalid module "
                                    "kind");
        return false;
    }

    LunaVector debug_files;
    if (!luna_x86_64_collect_debug_files(module, &debug_files) ||
        !luna_x86_64_emit_debug_files(output, &debug_files)) {
        luna_vector_destroy(&debug_files);
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while emitting source file records");
        return false;
    }

    if (!luna_x86_64_emit_globals(output, module) ||
        !luna_x86_64_emit_external_declarations(output, module) ||
        !luna_string_builder_append_c_string(output, "    .text\n")) {
        luna_vector_destroy(&debug_files);
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while emitting x86-64 assembly");
        return false;
    }

    if (entry != NULL && (!luna_string_builder_append_c_string(
                              output, "    .globl _start\n"
                                      "    .type _start, @function\n"
                                      "_start:\n"
                                      "    xorl %ebp, %ebp\n"
                                      "    andq $-16, %rsp\n"
                                      "    subq $16, %rsp\n"
                                      "    movl $0x1f80, (%rsp)\n"
                                      "    ldmxcsr (%rsp)\n"
                                      "    addq $16, %rsp\n"
                                      "    call ") ||
                          !luna_x86_64_append_symbol(output, entry) ||
                          !luna_string_builder_append_c_string(
                              output, "\n"
                                      "    movl %eax, %edi\n"
                                      "    movl $60, %eax\n"
                                      "    syscall\n"
                                      "    .size _start, .-_start\n\n"))) {
        luna_vector_destroy(&debug_files);
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while emitting x86-64 entry point");
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C ||
            function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT) {
            continue;
        }
        const LunaX8664FunctionAbi *function_abi =
            luna_vector_at_const(&abi->functions, function_index);
        const LunaX8664FunctionInstructionRewrite *function_rewrite =
            luna_vector_at_const(&rewrite->functions, function_index);
        if (!luna_x86_64_emit_function(output, module, abi, function,
                                       function_abi, function_rewrite,
                                       &debug_files, function_index)) {
            luna_vector_destroy(&debug_files);
            luna_diagnostic_error_plain(
                diagnostics,
                "x86-64 code generation failed for function '%.*s'",
                (int)function->name.length, function->name.data);
            return false;
        }
    }

    if (!luna_string_builder_append_c_string(
            output, "    .section .note.GNU-stack,\"\",@progbits\n")) {
        luna_vector_destroy(&debug_files);
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while finalizing x86-64 assembly");
        return false;
    }

    luna_vector_destroy(&debug_files);
    return true;
}
