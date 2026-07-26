#include "luna/backend/x86_64/x86_64.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

static bool luna_x86_64_emit_binary(LunaStringBuilder *output,
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

    return luna_x86_64_emit_store_eax(output, function, instruction->result);
}

static bool luna_x86_64_emit_binary_i64(LunaStringBuilder *output,
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

static const char *luna_x86_64_set_condition(LunaIrOpcode opcode) {
    switch (opcode) {
    case LUNA_IR_COMPARE_EQUAL:
        return "sete";
    case LUNA_IR_COMPARE_NOT_EQUAL:
        return "setne";
    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_I64:
        return "setl";
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I64:
        return "setle";
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_I64:
        return "setg";
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I64:
        return "setge";
    default:
        return NULL;
    }
}

static bool luna_x86_64_emit_compare(LunaStringBuilder *output,
                                     const LunaIrFunction *function,
                                     const LunaIrInstruction *instruction) {
    const char *condition = luna_x86_64_set_condition(instruction->opcode);
    const LunaIrType *operand_type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    const bool is_i64 =
        operand_type != NULL && *operand_type == LUNA_IR_TYPE_I64;
    if (condition == NULL ||
        !(is_i64
              ? luna_x86_64_emit_load_rax(output, function, instruction->left)
              : luna_x86_64_emit_load_eax(output, function,
                                          instruction->left)) ||
        !luna_string_builder_append_c_string(output, is_i64 ? "    cmpq "
                                                            : "    cmpl ") ||
        !luna_x86_64_append_value_operand(output, function,
                                          instruction->right) ||
        !luna_string_builder_append_c_string(output, is_i64 ? ", %rax\n"
                                                            : ", %eax\n") ||
        !luna_string_builder_append_format(output, "    %s %%al\n",
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
    static const char *argument_registers_i32[] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d",
    };
    static const char *argument_registers_i64[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };

    if (instruction->argument_count >
        sizeof(argument_registers_i32) / sizeof(argument_registers_i32[0])) {
        return false;
    }

    const LunaIrFunction *callee =
        luna_ir_module_function_const(module, instruction->callee);
    if (callee == NULL) {
        return false;
    }

    for (uint32_t index = 0U; index < instruction->argument_count;
         index += 1U) {
        const LunaIrValueId *argument = luna_vector_at_const(
            &function->arguments, (size_t)instruction->first_argument + index);
        const LunaIrType *parameter_type =
            luna_vector_at_const(&callee->parameter_types, (size_t)index);
        const bool is_i64 =
            parameter_type != NULL && *parameter_type == LUNA_IR_TYPE_I64;
        if (!luna_string_builder_append_c_string(
                output, is_i64 ? "    movq " : "    movl ") ||
            !luna_x86_64_append_value_operand(output, function, *argument) ||
            !luna_string_builder_append_format(
                output, ", %s\n",
                is_i64 ? argument_registers_i64[index]
                       : argument_registers_i32[index])) {
            return false;
        }
    }

    if (!luna_string_builder_append_c_string(output, "    call ") ||
        !luna_x86_64_append_symbol(output, callee) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    if (instruction->result != LUNA_IR_INVALID_ID) {
        return instruction->type == LUNA_IR_TYPE_I64
                   ? luna_x86_64_emit_store_rax(output, function,
                                                instruction->result)
                   : luna_x86_64_emit_store_eax(output, function,
                                                instruction->result);
    }

    return true;
}

static bool luna_x86_64_emit_instruction(LunaStringBuilder *output,
                                         const LunaIrModule *module,
                                         const LunaIrFunction *function,
                                         size_t function_index,
                                         const LunaIrInstruction *instruction) {
    switch (instruction->opcode) {
    case LUNA_IR_CONST_I32:
    case LUNA_IR_CONST_BOOL:
        if (!luna_string_builder_append_format(
                output, "    movl $%" PRId64 ", ", instruction->immediate) ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->result)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_CONST_I64:
        if (!luna_string_builder_append_format(
                output, "    movabsq $%" PRId64 ", %%rax\n",
                instruction->immediate)) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function,
                                          instruction->result);

    case LUNA_IR_LOAD:
        if (!luna_string_builder_append_c_string(
                output, instruction->type == LUNA_IR_TYPE_I64 ? "    movq "
                                                              : "    movl ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot) ||
            !luna_string_builder_append_c_string(
                output, instruction->type == LUNA_IR_TYPE_I64 ? ", %rax\n"
                                                              : ", %eax\n")) {
            return false;
        }
        return instruction->type == LUNA_IR_TYPE_I64
                   ? luna_x86_64_emit_store_rax(output, function,
                                                instruction->result)
                   : luna_x86_64_emit_store_eax(output, function,
                                                instruction->result);

    case LUNA_IR_STORE: {
        const LunaIrType *value_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        const bool is_i64 =
            value_type != NULL && *value_type == LUNA_IR_TYPE_I64;
        if (!(is_i64 ? luna_x86_64_emit_load_rax(output, function,
                                                 instruction->left)
                     : luna_x86_64_emit_load_eax(output, function,
                                                 instruction->left)) ||
            !luna_string_builder_append_c_string(
                output, is_i64 ? "    movq %rax, " : "    movl %eax, ") ||
            !luna_x86_64_append_slot_operand(output, function,
                                             instruction->slot)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_NEG_I32:
    case LUNA_IR_BIT_NOT_I32:
    case LUNA_IR_BOOL_NOT:
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left)) {
            return false;
        }

        if (instruction->opcode == LUNA_IR_NEG_I32) {
            if (!luna_string_builder_append_c_string(output,
                                                     "    negl %eax\n")) {
                return false;
            }
        } else if (instruction->opcode == LUNA_IR_BIT_NOT_I32) {
            if (!luna_string_builder_append_c_string(output,
                                                     "    notl %eax\n")) {
                return false;
            }
        } else if (!luna_string_builder_append_c_string(
                       output, "    testl %eax, %eax\n"
                               "    sete %al\n"
                               "    movzbl %al, %eax\n")) {
            return false;
        }

        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);

    case LUNA_IR_NEG_I64:
    case LUNA_IR_BIT_NOT_I64:
        if (!luna_x86_64_emit_load_rax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(
                output, instruction->opcode == LUNA_IR_NEG_I64
                            ? "    negq %rax\n"
                            : "    notq %rax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function,
                                          instruction->result);

    case LUNA_IR_ADD_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "addl");

    case LUNA_IR_SUB_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "subl");

    case LUNA_IR_MUL_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "imull");

    case LUNA_IR_BIT_AND_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "andl");

    case LUNA_IR_BIT_OR_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "orl");

    case LUNA_IR_BIT_XOR_I32:
        return luna_x86_64_emit_binary(output, function, instruction, "xorl");

    case LUNA_IR_ADD_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "addq");
    case LUNA_IR_SUB_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "subq");
    case LUNA_IR_MUL_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "imulq");
    case LUNA_IR_BIT_AND_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "andq");
    case LUNA_IR_BIT_OR_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "orq");
    case LUNA_IR_BIT_XOR_I64:
        return luna_x86_64_emit_binary_i64(output, function, instruction,
                                           "xorq");

    case LUNA_IR_DIV_I32:
    case LUNA_IR_REM_I32:
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(output, "    cltd\n"
                                                         "    idivl ") ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }

        if (instruction->opcode == LUNA_IR_REM_I32) {
            if (!luna_string_builder_append_c_string(output,
                                                     "    movl %edx, %eax\n")) {
                return false;
            }
        }

        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);

    case LUNA_IR_DIV_I64:
    case LUNA_IR_REM_I64:
        if (!luna_x86_64_emit_load_rax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(output, "    cqto\n"
                                                         "    idivq ") ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }

        if (instruction->opcode == LUNA_IR_REM_I64 &&
            !luna_string_builder_append_c_string(output,
                                                 "    movq %rdx, %rax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function,
                                          instruction->result);

    case LUNA_IR_SHIFT_LEFT_I32:
    case LUNA_IR_SHIFT_RIGHT_I32:
        if (!luna_x86_64_emit_load_eax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(output, "    movl ") ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, ", %ecx\n") ||
            !luna_string_builder_append_c_string(
                output, instruction->opcode == LUNA_IR_SHIFT_LEFT_I32
                            ? "    shll %cl, %eax\n"
                            : "    sarl %cl, %eax\n")) {
            return false;
        }

        return luna_x86_64_emit_store_eax(output, function,
                                          instruction->result);

    case LUNA_IR_SHIFT_LEFT_I64:
    case LUNA_IR_SHIFT_RIGHT_I64:
        if (!luna_x86_64_emit_load_rax(output, function, instruction->left) ||
            !luna_string_builder_append_c_string(output, "    movq ") ||
            !luna_x86_64_append_value_operand(output, function,
                                              instruction->right) ||
            !luna_string_builder_append_c_string(output, ", %rcx\n") ||
            !luna_string_builder_append_c_string(
                output, instruction->opcode == LUNA_IR_SHIFT_LEFT_I64
                            ? "    shlq %cl, %rax\n"
                            : "    sarq %cl, %rax\n")) {
            return false;
        }
        return luna_x86_64_emit_store_rax(output, function,
                                          instruction->result);

    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
    case LUNA_IR_COMPARE_LESS_I64:
    case LUNA_IR_COMPARE_LESS_EQUAL_I64:
    case LUNA_IR_COMPARE_GREATER_I64:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I64:
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
            if (function->return_type == LUNA_IR_TYPE_I64) {
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
    static const char *argument_registers_i32[] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d",
    };
    static const char *argument_registers_i64[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };

    uint32_t frame_size = 0U;
    if (function->parameter_types.length >
            sizeof(argument_registers_i32) /
                sizeof(argument_registers_i32[0]) ||
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

    for (size_t parameter_index = 0U;
         parameter_index < function->parameter_types.length;
         parameter_index += 1U) {
        const LunaIrType *parameter_type =
            luna_vector_at_const(&function->parameter_types, parameter_index);
        const bool is_i64 =
            parameter_type != NULL && *parameter_type == LUNA_IR_TYPE_I64;
        if (!luna_string_builder_append_format(
                output, is_i64 ? "    movq %s, " : "    movl %s, ",
                is_i64 ? argument_registers_i64[parameter_index]
                       : argument_registers_i32[parameter_index]) ||
            !luna_x86_64_append_slot_operand(output, function,
                                             (LunaIrSlotId)parameter_index) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }
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
