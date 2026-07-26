#include "luna/backend/x86_64/x86_64.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t luna_x86_64_type_bit_width(LunaIrType type) {
    const LunaTargetInfo *target = luna_target_info_default();
    return luna_ir_type_bit_width(type, &target->data_layout);
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

static bool luna_x86_64_append_symbol(LunaStringBuilder *output,
                                      const LunaIrFunction *function) {
    if (!luna_string_builder_append_c_string(output, "_L")) {
        return false;
    }

    for (size_t index = 0U; index < function->module_name.length; index += 1U) {
        if (!luna_x86_64_append_hex_byte(
                output, (unsigned char)function->module_name.data[index])) {
            return false;
        }
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

static bool luna_x86_64_stack_offset(const LunaIrFunction *function,
                                     bool is_value, uint32_t id,
                                     int32_t *offset) {
    uint64_t index = id;
    if (is_value) {
        index += function->slot_types.length;
    }
    index += 1U;

    const uint64_t byte_offset = index * 8U;
    if (byte_offset > (uint64_t)INT32_MAX) {
        return false;
    }

    *offset = -(int32_t)byte_offset;
    return true;
}

static bool luna_x86_64_append_value_operand(LunaStringBuilder *output,
                                             const LunaIrFunction *function,
                                             LunaIrValueId value) {
    int32_t offset = 0;
    if (!luna_x86_64_stack_offset(function, true, value, &offset)) {
        return false;
    }

    return luna_string_builder_append_format(output, "%d(%%rbp)", offset);
}

static bool luna_x86_64_append_slot_operand(LunaStringBuilder *output,
                                            const LunaIrFunction *function,
                                            LunaIrSlotId slot) {
    int32_t offset = 0;
    if (!luna_x86_64_stack_offset(function, false, slot, &offset)) {
        return false;
    }

    return luna_string_builder_append_format(output, "%d(%%rbp)", offset);
}

static bool luna_x86_64_emit_store_eax(LunaStringBuilder *output,
                                       const LunaIrFunction *function,
                                       LunaIrValueId result) {
    if (!luna_string_builder_append_c_string(output, "    movl %eax, ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand(output, function, result)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, "\n");
}

static bool luna_x86_64_emit_load_eax(LunaStringBuilder *output,
                                      const LunaIrFunction *function,
                                      LunaIrValueId value) {
    if (!luna_string_builder_append_c_string(output, "    movl ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand(output, function, value)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %eax\n");
}

static bool luna_x86_64_emit_load_ecx(LunaStringBuilder *output,
                                      const LunaIrFunction *function,
                                      LunaIrValueId value) {
    if (!luna_string_builder_append_c_string(output, "    movl ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand(output, function, value)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %ecx\n");
}

static bool luna_x86_64_emit_store_rax(LunaStringBuilder *output,
                                       const LunaIrFunction *function,
                                       LunaIrValueId result) {
    if (!luna_string_builder_append_c_string(output, "    movq %rax, ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand(output, function, result)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, "\n");
}

static bool luna_x86_64_emit_load_rax(LunaStringBuilder *output,
                                      const LunaIrFunction *function,
                                      LunaIrValueId value) {
    if (!luna_string_builder_append_c_string(output, "    movq ")) {
        return false;
    }
    if (!luna_x86_64_append_value_operand(output, function, value)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, ", %rax\n");
}

static const char *luna_x86_64_float_move(LunaIrType type) {
    if (type == LUNA_IR_TYPE_F32) {
        return "movss";
    }
    if (type == LUNA_IR_TYPE_F64) {
        return "movsd";
    }
    return NULL;
}

static bool luna_x86_64_emit_load_xmm0(LunaStringBuilder *output,
                                       const LunaIrFunction *function,
                                       LunaIrValueId value, LunaIrType type) {
    const char *move = luna_x86_64_float_move(type);
    return move != NULL &&
           luna_string_builder_append_format(output, "    %s ", move) &&
           luna_x86_64_append_value_operand(output, function, value) &&
           luna_string_builder_append_c_string(output, ", %xmm0\n");
}

static bool luna_x86_64_emit_store_xmm0(LunaStringBuilder *output,
                                        const LunaIrFunction *function,
                                        LunaIrValueId result, LunaIrType type) {
    const char *move = luna_x86_64_float_move(type);
    return move != NULL &&
           luna_string_builder_append_format(output, "    %s %%xmm0, ", move) &&
           luna_x86_64_append_value_operand(output, function, result) &&
           luna_string_builder_append_c_string(output, "\n");
}

static bool luna_x86_64_emit_signed_load_32(LunaStringBuilder *output,
                                            const LunaIrFunction *function,
                                            LunaIrValueId value,
                                            LunaIrType type,
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
        !luna_x86_64_append_value_operand(output, function, value) ||
        !luna_string_builder_append_format(output, ", %s\n", register_name)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_signed_load_64(LunaStringBuilder *output,
                                            const LunaIrFunction *function,
                                            LunaIrValueId value,
                                            LunaIrType type) {
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
        !luna_x86_64_append_value_operand(output, function, value) ||
        !luna_string_builder_append_c_string(output, ", %rax\n")) {
        return false;
    }
    return true;
}

static bool luna_x86_64_emit_truncate_eax(LunaStringBuilder *output,
                                          LunaIrType type) {
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

static bool luna_x86_64_emit_store_integer_eax(LunaStringBuilder *output,
                                               const LunaIrFunction *function,
                                               LunaIrValueId result,
                                               LunaIrType type) {
    return luna_x86_64_emit_truncate_eax(output, type) &&
           luna_x86_64_emit_store_eax(output, function, result);
}

static bool luna_x86_64_emit_binary_32(LunaStringBuilder *output,
                                       const LunaIrFunction *function,
                                       const LunaIrInstruction *instruction,
                                       const char *mnemonic) {
    if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %eax\n")) {
        return false;
    }

    return luna_x86_64_emit_store_integer_eax(
        output, function, instruction->result, instruction->type);
}

static bool luna_x86_64_emit_binary_64(LunaStringBuilder *output,
                                       const LunaIrFunction *function,
                                       const LunaIrInstruction *instruction,
                                       const char *mnemonic) {
    if (!luna_x86_64_emit_load_rax(output, function, instruction->left) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %rax\n")) {
        return false;
    }

    return luna_x86_64_emit_store_rax(output, function, instruction->result);
}

static bool luna_x86_64_emit_binary_float(LunaStringBuilder *output,
                                          const LunaIrFunction *function,
                                          const LunaIrInstruction *instruction,
                                          const char *mnemonic) {
    if (!luna_x86_64_emit_load_xmm0(output, function, instruction->left,
                                    instruction->type) ||
        !luna_string_builder_append_format(output, "    %s ", mnemonic) ||
        !luna_x86_64_append_value_operand(output, function,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %xmm0\n")) {
        return false;
    }
    return luna_x86_64_emit_store_xmm0(output, function, instruction->result,
                                       instruction->type);
}

static bool luna_x86_64_type_is_64_bit(LunaIrType type) {
    return luna_x86_64_type_bit_width(type) == 64U;
}

static const char *luna_x86_64_set_condition(LunaIrOpcode opcode,
                                             LunaIrType operand_type) {
    const bool is_signed = luna_ir_type_is_signed_integer(operand_type);
    switch (opcode) {
    case LUNA_IR_COMPARE_EQUAL:
        return "sete";
    case LUNA_IR_COMPARE_NOT_EQUAL:
        return "setne";
    case LUNA_IR_COMPARE_LESS_INTEGER:
        return is_signed ? "setl" : "setb";
    case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
        return is_signed ? "setle" : "setbe";
    case LUNA_IR_COMPARE_GREATER_INTEGER:
        return is_signed ? "setg" : "seta";
    case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER:
        return is_signed ? "setge" : "setae";
    default:
        return NULL;
    }
}

static bool luna_x86_64_emit_float_compare(LunaStringBuilder *output,
                                           const LunaIrFunction *function,
                                           const LunaIrInstruction *instruction,
                                           LunaIrType operand_type) {
    const char *compare =
        operand_type == LUNA_IR_TYPE_F32 ? "ucomiss" : "ucomisd";
    const char *set_result = NULL;
    switch (instruction->opcode) {
    case LUNA_IR_COMPARE_EQUAL:
        set_result = "    sete %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_IR_COMPARE_NOT_EQUAL:
        set_result = "    setne %al\n"
                     "    setp %cl\n"
                     "    orb %cl, %al\n";
        break;
    case LUNA_IR_COMPARE_LESS_FLOAT:
        set_result = "    setb %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
        set_result = "    setbe %al\n"
                     "    setnp %cl\n"
                     "    andb %cl, %al\n";
        break;
    case LUNA_IR_COMPARE_GREATER_FLOAT:
        set_result = "    seta %al\n";
        break;
    case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT:
        set_result = "    setae %al\n";
        break;
    default:
        return false;
    }

    if (!luna_x86_64_emit_load_xmm0(output, function, instruction->left,
                                    operand_type) ||
        !luna_string_builder_append_format(output, "    %s ", compare) ||
        !luna_x86_64_append_value_operand(output, function,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, ", %xmm0\n") ||
        !luna_string_builder_append_c_string(output, set_result) ||
        !luna_string_builder_append_c_string(output,
                                             "    movzbl %al, %eax\n")) {
        return false;
    }
    return luna_x86_64_emit_store_eax(output, function, instruction->result);
}

static bool luna_x86_64_emit_compare(LunaStringBuilder *output,
                                     const LunaIrFunction *function,
                                     const LunaIrInstruction *instruction) {
    const LunaIrType *operand_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    if (operand_type == NULL) {
        return false;
    }
    if (luna_ir_type_is_float(*operand_type)) {
        return luna_x86_64_emit_float_compare(output, function, instruction,
                                              *operand_type);
    }
    const char *condition =
        luna_x86_64_set_condition(instruction->opcode, *operand_type);
    const bool is_64_bit = luna_x86_64_type_is_64_bit(*operand_type);
    const bool is_signed_narrow =
        luna_ir_type_is_signed_integer(*operand_type) &&
        luna_x86_64_type_bit_width(*operand_type) < 32U;
    if (condition == NULL) {
        return false;
    }

    if (is_64_bit) {
        if (!luna_x86_64_emit_load_rax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(output, "    cmpq ") ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, ", %rax\n")) {
            return false;
        }
    } else if (is_signed_narrow) {
        if (!luna_x86_64_emit_signed_load_32(
                output, function, instruction->left, *operand_type, "%eax") ||
            !luna_x86_64_emit_signed_load_32(
                output, function, instruction->right, *operand_type, "%ecx") ||
            !luna_string_builder_append_c_string(output,
                                                 "    cmpl %ecx, %eax\n")) {
            return false;
        }
    } else if (!luna_x86_64_emit_load_eax(output, function,
                                          instruction->left) ||
               !luna_string_builder_append_c_string(output, "    cmpl ") ||
               !luna_x86_64_append_value_operand(output, function,
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

    return luna_x86_64_emit_store_eax(output, function, instruction->result);
}

static bool luna_x86_64_emit_call(LunaStringBuilder *output,
                                  const LunaIrModule *module,
                                  const LunaIrFunction *function,
                                  const LunaIrInstruction *instruction) {
    static const char *argument_registers_32_bit[] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d",
    };
    static const char *argument_registers_64_bit[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };
    static const char *argument_registers_float[] = {
        "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",
    };

    const LunaIrFunction *callee =
        luna_ir_module_function_const(module, instruction->callee);
    if (callee == NULL) {
        return false;
    }

    size_t integer_register = 0U;
    size_t float_register = 0U;
    for (uint32_t index = 0U; index < instruction->argument_count;
         index += 1U) {
        const LunaIrValueId *argument = luna_vector_at_const(
            &function->arguments, (size_t)instruction->first_argument + index);
        const LunaIrType *parameter_type =
            luna_vector_at_const(&callee->parameter_types, (size_t)index);
        if (argument == NULL || parameter_type == NULL) {
            return false;
        }
        if (luna_ir_type_is_float(*parameter_type)) {
            const char *move = luna_x86_64_float_move(*parameter_type);
            if (move == NULL ||
                float_register >= sizeof(argument_registers_float) /
                                      sizeof(argument_registers_float[0]) ||
                !luna_string_builder_append_format(output, "    %s ", move) ||
                !luna_x86_64_append_value_operand(output, function,
                                                  *argument) ||
                !luna_string_builder_append_format(
                    output, ", %s\n",
                    argument_registers_float[float_register])) {
                return false;
            }
            float_register += 1U;
            continue;
        }

        if (integer_register >= sizeof(argument_registers_32_bit) /
                                    sizeof(argument_registers_32_bit[0])) {
            return false;
        }
        const bool is_64_bit = luna_x86_64_type_is_64_bit(*parameter_type);
        if (!luna_string_builder_append_c_string(
                output, is_64_bit ? "    movq " : "    movl ") ||
            !luna_x86_64_append_value_operand(output, function, *argument) ||
            !luna_string_builder_append_format(
                output, ", %s\n",
                is_64_bit ? argument_registers_64_bit[integer_register]
                          : argument_registers_32_bit[integer_register])) {
            return false;
        }
        integer_register += 1U;
    }

    if (!luna_string_builder_append_c_string(output, "    call ") ||
        !luna_x86_64_append_symbol(output, callee) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    if (instruction->result != LUNA_IR_INVALID_ID) {
        if (luna_ir_type_is_float(instruction->type)) {
            return luna_x86_64_emit_store_xmm0(
                output, function, instruction->result, instruction->type);
        }
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }
        if (luna_ir_type_is_integer(instruction->type)) {
            return luna_x86_64_emit_store_integer_eax(
                output, function, instruction->result, instruction->type);
        }
        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);
    }

    return true;
}

static bool luna_x86_64_emit_narrow_division(
    LunaStringBuilder *output, const LunaIrFunction *function,
    size_t function_index, const LunaIrInstruction *instruction) {
    const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
    const bool is_signed = luna_ir_type_is_signed_integer(instruction->type);
    if (width != 8U && width != 16U) {
        return false;
    }

    if (is_signed) {
        const int32_t minimum = width == 8U ? INT8_MIN : INT16_MIN;
        if (!luna_x86_64_emit_signed_load_32(output, function,
                                             instruction->left,
                                             instruction->type, "%eax") ||
            !luna_x86_64_emit_signed_load_32(output, function,
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
    } else if (!luna_x86_64_emit_load_eax(output, function,
                                          instruction->left) ||
               !luna_x86_64_emit_load_ecx(output, function,
                                          instruction->right) ||
               !luna_string_builder_append_c_string(output,
                                                    "    xorl %edx, %edx\n"
                                                    "    divl %ecx\n")) {
        return false;
    }

    if (instruction->opcode == LUNA_IR_REM_INTEGER &&
        !luna_string_builder_append_c_string(output, "    movl %edx, %eax\n")) {
        return false;
    }
    return luna_x86_64_emit_store_integer_eax(
        output, function, instruction->result, instruction->type);
}

static bool luna_x86_64_emit_instruction(LunaStringBuilder *output,
                                         const LunaIrModule *module,
                                         const LunaIrFunction *function,
                                         size_t function_index,
                                         const LunaIrInstruction *instruction) {
    switch (instruction->opcode) {
    case LUNA_IR_CONST_BOOL:
        if (!luna_string_builder_append_format(
                output, "    movl $%" PRIu64 ", ", instruction->immediate) ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->result)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_CONST_INTEGER:
    case LUNA_IR_CONST_FLOAT:
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            if (!luna_string_builder_append_format(
                    output, "    movabsq $0x%016" PRIx64 ", %%rax\n",
                    instruction->immediate)) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }
        if (!luna_string_builder_append_format(
                output, "    movl $0x%08" PRIx32 ", ",
                (uint32_t)instruction->immediate) ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->result)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_LOAD:
        if (!luna_string_builder_append_c_string(
                output, luna_x86_64_type_is_64_bit(instruction->type)
                            ? "    movq "
                            : "    movl ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(
                output, luna_x86_64_type_is_64_bit(instruction->type)
                            ? ", %rax\n"
                            : ", %eax\n")) {
            return false;
        }
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_store_rax(output, function,
                                                instruction->result)
                   : luna_x86_64_emit_store_eax(output, function,
                                                instruction->result);

    case LUNA_IR_STORE: {
        const LunaIrType *value_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        const bool is_64_bit =
            value_type != NULL && luna_x86_64_type_is_64_bit(*value_type);
        if (!(is_64_bit ? luna_x86_64_emit_load_rax(output, function,
                                                    instruction->left)
                        : luna_x86_64_emit_load_eax(output, function,
                                                    instruction->left)) ||
            !luna_string_builder_append_c_string(
                output, is_64_bit ? "    movq %rax, " : "    movl %eax, ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_NEG_FLOAT:
        if (instruction->type == LUNA_IR_TYPE_F64) {
            if (!luna_x86_64_emit_load_rax(output, function,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(
                    output, "    movabsq $0x8000000000000000, %rdx\n"
                            "    xorq %rdx, %rax\n")) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(
                output, "    xorl $0x80000000, %eax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);

    case LUNA_IR_NEG_INTEGER:
    case LUNA_IR_BIT_NOT_INTEGER:
        if (luna_x86_64_type_is_64_bit(instruction->type)) {
            if (!luna_x86_64_emit_load_rax(output, function,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(
                    output, instruction->opcode == LUNA_IR_NEG_INTEGER
                                ? "    negq %rax\n"
                                : "    notq %rax\n")) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }

        if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(
                output, instruction->opcode == LUNA_IR_NEG_INTEGER
                            ? "    negl %eax\n"
                            : "    notl %eax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_integer_eax(
            output, function, instruction->result, instruction->type);

    case LUNA_IR_BOOL_NOT:
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left)) {
            return false;
        }

        if (!luna_string_builder_append_c_string(output,
                                                 "    testl %eax, %eax\n"
                                                 "    sete %al\n"
                                                 "    movzbl %al, %eax\n")) {
            return false;
        }

        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);

    case LUNA_IR_CONVERT_INTEGER: {
        const LunaIrType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (source_type == NULL) {
            return false;
        }
        const uint32_t source_width = luna_x86_64_type_bit_width(*source_type);
        const uint32_t target_width =
            luna_x86_64_type_bit_width(instruction->type);
        if (target_width == 64U) {
            if (source_width == 64U) {
                return luna_x86_64_emit_load_rax(output, function,
                                                 instruction->left) &&
                       luna_x86_64_emit_store_rax(output, function,
                                                  instruction->result);
            }
            if (luna_ir_type_is_signed_integer(*source_type)) {
                if (!luna_x86_64_emit_signed_load_64(
                        output, function, instruction->left, *source_type)) {
                    return false;
                }
            } else if (!luna_x86_64_emit_load_eax(output, function,
                                                  instruction->left)) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }

        if (source_width < target_width &&
            luna_ir_type_is_signed_integer(*source_type)) {
            if (!luna_x86_64_emit_signed_load_32(output, function,
                                                 instruction->left,
                                                 *source_type, "%eax")) {
                return false;
            }
        } else if (!luna_x86_64_emit_load_eax(output, function,
                                              instruction->left)) {
            return false;
        }
        return luna_x86_64_emit_store_integer_eax(
            output, function, instruction->result, instruction->type);
    }

    case LUNA_IR_ADD_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "addq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "addl");

    case LUNA_IR_SUB_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "subq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "subl");

    case LUNA_IR_MUL_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "imulq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "imull");

    case LUNA_IR_ADD_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, instruction,
            instruction->type == LUNA_IR_TYPE_F32 ? "addss" : "addsd");

    case LUNA_IR_SUB_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, instruction,
            instruction->type == LUNA_IR_TYPE_F32 ? "subss" : "subsd");

    case LUNA_IR_MUL_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, instruction,
            instruction->type == LUNA_IR_TYPE_F32 ? "mulss" : "mulsd");

    case LUNA_IR_DIV_FLOAT:
        return luna_x86_64_emit_binary_float(
            output, function, instruction,
            instruction->type == LUNA_IR_TYPE_F32 ? "divss" : "divsd");

    case LUNA_IR_BIT_AND_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "andq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "andl");

    case LUNA_IR_BIT_OR_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "orq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "orl");

    case LUNA_IR_BIT_XOR_INTEGER:
        return luna_x86_64_type_is_64_bit(instruction->type)
                   ? luna_x86_64_emit_binary_64(output, function, instruction,
                                                "xorq")
                   : luna_x86_64_emit_binary_32(output, function, instruction,
                                                "xorl");

    case LUNA_IR_DIV_INTEGER:
    case LUNA_IR_REM_INTEGER: {
        if (luna_x86_64_type_bit_width(instruction->type) < 32U) {
            return luna_x86_64_emit_narrow_division(
                output, function, function_index, instruction);
        }

        const bool is_64_bit = luna_x86_64_type_is_64_bit(instruction->type);
        const bool is_signed =
            luna_ir_type_is_signed_integer(instruction->type);
        if (!(is_64_bit ? luna_x86_64_emit_load_rax(output, function,
                                                    instruction->left)
                        : luna_x86_64_emit_load_eax(output, function,
                                                    instruction->left)) ||
            !luna_string_builder_append_c_string(
                output, is_64_bit
                            ? (is_signed ? "    cqto\n    idivq "
                                         : "    xorq %rdx, %rdx\n    divq ")
                            : (is_signed ? "    cltd\n    idivl "
                                         : "    xorl %edx, %edx\n    divl ")) ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }

        if (instruction->opcode == LUNA_IR_REM_INTEGER &&
            !luna_string_builder_append_c_string(
                output, is_64_bit ? "    movq %rdx, %rax\n"
                                  : "    movl %edx, %eax\n")) {
            return false;
        }
        return is_64_bit ? luna_x86_64_emit_store_rax(output, function,
                                                      instruction->result)
                         : luna_x86_64_emit_store_eax(output, function,
                                                      instruction->result);
    }

    case LUNA_IR_SHIFT_LEFT_INTEGER:
    case LUNA_IR_SHIFT_RIGHT_INTEGER: {
        const uint32_t width = luna_x86_64_type_bit_width(instruction->type);
        const bool is_64_bit = width == 64U;
        const bool is_left = instruction->opcode == LUNA_IR_SHIFT_LEFT_INTEGER;
        const bool is_signed =
            luna_ir_type_is_signed_integer(instruction->type);
        if (is_64_bit) {
            if (!luna_x86_64_emit_load_rax(output, function,
                                           instruction->left) ||
                !luna_string_builder_append_c_string(output, "    movq ") ||
                !luna_x86_64_append_value_operand(output, function,
                                                  instruction->right) ||
                !luna_string_builder_append_c_string(output, ", %rcx\n") ||
                !luna_string_builder_append_c_string(
                    output, is_left ? "    shlq %cl, %rax\n"
                                    : (is_signed ? "    sarq %cl, %rax\n"
                                                 : "    shrq %cl, %rax\n"))) {
                return false;
            }
            return luna_x86_64_emit_store_rax(output, function,
                                              instruction->result);
        }

        const bool needs_signed_load = !is_left && is_signed && width < 32U;
        if (!(needs_signed_load ? luna_x86_64_emit_signed_load_32(
                                      output, function, instruction->left,
                                      instruction->type, "%eax")
                                : luna_x86_64_emit_load_eax(
                                      output, function, instruction->left)) ||
            !luna_x86_64_emit_load_ecx(output, function, instruction->right)) {
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
            output, function, instruction->result, instruction->type);
    }

    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_INTEGER:
    case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_IR_COMPARE_GREATER_INTEGER:
    case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER:
    case LUNA_IR_COMPARE_LESS_FLOAT:
    case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_IR_COMPARE_GREATER_FLOAT:
    case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT:
        return luna_x86_64_emit_compare(output, function, instruction);

    case LUNA_IR_CALL:
        return luna_x86_64_emit_call(output, module, function, instruction);

    case LUNA_IR_JUMP:
        return luna_string_builder_append_format(
            output, "    jmp .Lfn%zu_bb%u\n", function_index,
            instruction->true_block);

    case LUNA_IR_BRANCH:
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
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

    case LUNA_IR_RETURN:
        if (instruction->left != LUNA_IR_INVALID_ID) {
            if (luna_ir_type_is_float(function->return_type)) {
                if (!luna_x86_64_emit_load_xmm0(output, function,
                                                instruction->left,
                                                function->return_type)) {
                    return false;
                }
            } else if (luna_x86_64_type_is_64_bit(function->return_type)) {
                if (!luna_x86_64_emit_load_rax(output, function,
                                               instruction->left)) {
                    return false;
                }
            } else if (!luna_x86_64_emit_load_eax(output, function,
                                                  instruction->left)) {
                return false;
            }
        }
        return luna_string_builder_append_format(
            output, "    jmp .Lfn%zu_return\n", function_index);
    }

    return false;
}

static bool luna_x86_64_frame_size(const LunaIrFunction *function,
                                   uint32_t *frame_size) {
    const uint64_t home_count =
        function->slot_types.length + function->value_types.length;
    const uint64_t byte_count = home_count * 8U;
    const uint64_t aligned = (byte_count + 15U) & ~UINT64_C(15);

    if (aligned > (uint64_t)INT32_MAX) {
        return false;
    }

    *frame_size = (uint32_t)aligned;
    return true;
}

static bool luna_x86_64_emit_function(LunaStringBuilder *output,
                                      const LunaIrModule *module,
                                      const LunaIrFunction *function,
                                      size_t function_index) {
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
    size_t integer_parameter_count = 0U;
    size_t float_parameter_count = 0U;
    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaIrType *type =
            luna_vector_at_const(&function->parameter_types, index);
        if (type == NULL) {
            return false;
        }
        if (luna_ir_type_is_float(*type)) {
            float_parameter_count += 1U;
        } else {
            integer_parameter_count += 1U;
        }
    }
    if (integer_parameter_count > sizeof(argument_registers_32_bit) /
                                      sizeof(argument_registers_32_bit[0]) ||
        float_parameter_count > sizeof(argument_registers_float) /
                                    sizeof(argument_registers_float[0]) ||
        !luna_x86_64_frame_size(function, &frame_size)) {
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

    size_t integer_register = 0U;
    size_t float_register = 0U;
    for (size_t parameter_index = 0U;
         parameter_index < function->parameter_types.length;
         parameter_index += 1U) {
        const LunaIrType *parameter_type =
            luna_vector_at_const(&function->parameter_types, parameter_index);
        if (parameter_type == NULL) {
            return false;
        }
        if (luna_ir_type_is_float(*parameter_type)) {
            const char *move = luna_x86_64_float_move(*parameter_type);
            if (move == NULL ||
                !luna_string_builder_append_format(
                    output, "    %s %s, ", move,
                    argument_registers_float[float_register]) ||
                !luna_x86_64_append_slot_operand(
                    output, function, (LunaIrSlotId)parameter_index) ||
                !luna_string_builder_append_c_string(output, "\n")) {
                return false;
            }
            float_register += 1U;
            continue;
        }

        const bool is_64_bit = luna_x86_64_type_is_64_bit(*parameter_type);
        if (!luna_string_builder_append_format(
                output, is_64_bit ? "    movq %s, " : "    movl %s, ",
                is_64_bit ? argument_registers_64_bit[integer_register]
                          : argument_registers_32_bit[integer_register]) ||
            !luna_x86_64_append_slot_operand(output, function,
                                             (LunaIrSlotId)parameter_index) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }
        integer_register += 1U;
    }

    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (!luna_string_builder_append_format(output, ".Lfn%zu_bb%zu:\n",
                                               function_index, block_index)) {
            return false;
        }

        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (!luna_x86_64_emit_instruction(output, module, function,
                                              function_index, instruction)) {
                return false;
            }
        }
    }

    if (!luna_string_builder_append_format(output,
                                           ".Lfn%zu_return:\n"
                                           "    leave\n"
                                           "    ret\n"
                                           "    .size ",
                                           function_index) ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, ", .-") ||
        !luna_x86_64_append_symbol(output, function) ||
        !luna_string_builder_append_c_string(output, "\n\n")) {
        return false;
    }

    return true;
}

bool luna_x86_64_emit_assembly(const LunaIrModule *module,
                               LunaDiagnosticEngine *diagnostics,
                               LunaStringBuilder *output) {
    if (module == NULL || !luna_target_info_is_supported(module->target)) {
        luna_diagnostic_error_plain(
            diagnostics,
            "x86-64 backend requires target x86_64-unknown-linux-gnu");
        return false;
    }

    const LunaIrFunction *entry =
        luna_ir_module_function_const(module, module->entry_function);
    if (entry == NULL) {
        luna_diagnostic_error_plain(
            diagnostics, "x86-64 backend received no entry function");
        return false;
    }

    if (!luna_string_builder_append_c_string(output,
                                             "    .text\n"
                                             "    .globl _start\n"
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
                    "    .size _start, .-_start\n\n")) {
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while emitting x86-64 assembly");
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaIrFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (!luna_x86_64_emit_function(output, module, function,
                                       function_index)) {
            luna_diagnostic_error_plain(
                diagnostics,
                "x86-64 code generation failed for function '%.*s'",
                (int)function->name.length, function->name.data);
            return false;
        }
    }

    if (!luna_string_builder_append_c_string(
            output, "    .section .note.GNU-stack,\"\",@progbits\n")) {
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while finalizing x86-64 assembly");
        return false;
    }

    return true;
}
