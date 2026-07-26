#include "luna/ir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool luna_ir_opcode_is_terminator(LunaIrOpcode opcode) {
    return opcode == LUNA_IR_JUMP || opcode == LUNA_IR_BRANCH ||
           opcode == LUNA_IR_RETURN;
}

static void luna_ir_function_destroy(LunaIrFunction *function) {
    for (size_t index = 0U; index < function->blocks.length; index += 1U) {
        LunaIrBlock *block = luna_vector_at(&function->blocks, index);
        luna_vector_destroy(&block->instructions);
    }

    luna_vector_destroy(&function->parameter_types);
    luna_vector_destroy(&function->slot_types);
    luna_vector_destroy(&function->value_types);
    luna_vector_destroy(&function->arguments);
    luna_vector_destroy(&function->blocks);
}

void luna_ir_module_init(LunaIrModule *module) {
    luna_vector_init(&module->functions, sizeof(LunaIrFunction));
    module->entry_function = LUNA_IR_INVALID_ID;
}

void luna_ir_module_destroy(LunaIrModule *module) {
    for (size_t index = 0U; index < module->functions.length; index += 1U) {
        LunaIrFunction *function = luna_vector_at(&module->functions, index);
        luna_ir_function_destroy(function);
    }

    luna_vector_destroy(&module->functions);
    module->entry_function = LUNA_IR_INVALID_ID;
}

LunaIrFunctionId luna_ir_module_add_function(LunaIrModule *module,
                                             LunaStringView module_name,
                                             LunaStringView name,
                                             LunaIrType return_type) {
    if (module->functions.length >= UINT32_MAX) {
        return LUNA_IR_INVALID_ID;
    }

    LunaIrFunction function = {
        .module_name = module_name,
        .name = name,
        .return_type = return_type,
    };
    luna_vector_init(&function.parameter_types, sizeof(LunaIrType));
    luna_vector_init(&function.slot_types, sizeof(LunaIrType));
    luna_vector_init(&function.value_types, sizeof(LunaIrType));
    luna_vector_init(&function.arguments, sizeof(LunaIrValueId));
    luna_vector_init(&function.blocks, sizeof(LunaIrBlock));

    if (!luna_vector_push(&module->functions, &function)) {
        luna_ir_function_destroy(&function);
        return LUNA_IR_INVALID_ID;
    }

    return (LunaIrFunctionId)(module->functions.length - 1U);
}

LunaIrFunction *luna_ir_module_function(LunaIrModule *module,
                                        LunaIrFunctionId function_id) {
    return luna_vector_at(&module->functions, (size_t)function_id);
}

const LunaIrFunction *
luna_ir_module_function_const(const LunaIrModule *module,
                              LunaIrFunctionId function_id) {
    return luna_vector_at_const(&module->functions, (size_t)function_id);
}

LunaIrSlotId luna_ir_function_add_slot(LunaIrFunction *function,
                                       LunaIrType type) {
    if (function->slot_types.length >= UINT32_MAX ||
        !luna_vector_push(&function->slot_types, &type)) {
        return LUNA_IR_INVALID_ID;
    }

    return (LunaIrSlotId)(function->slot_types.length - 1U);
}

LunaIrValueId luna_ir_function_add_value(LunaIrFunction *function,
                                         LunaIrType type) {
    if (function->value_types.length >= UINT32_MAX ||
        !luna_vector_push(&function->value_types, &type)) {
        return LUNA_IR_INVALID_ID;
    }

    return (LunaIrValueId)(function->value_types.length - 1U);
}

LunaIrBlockId luna_ir_function_add_block(LunaIrFunction *function) {
    if (function->blocks.length >= UINT32_MAX) {
        return LUNA_IR_INVALID_ID;
    }

    LunaIrBlock block = {0};
    luna_vector_init(&block.instructions, sizeof(LunaIrInstruction));

    if (!luna_vector_push(&function->blocks, &block)) {
        luna_vector_destroy(&block.instructions);
        return LUNA_IR_INVALID_ID;
    }

    return (LunaIrBlockId)(function->blocks.length - 1U);
}

LunaIrBlock *luna_ir_function_block(LunaIrFunction *function,
                                    LunaIrBlockId block_id) {
    return luna_vector_at(&function->blocks, (size_t)block_id);
}

bool luna_ir_block_append(LunaIrBlock *block,
                          const LunaIrInstruction *instruction) {
    if (block->terminated) {
        return false;
    }

    if (!luna_vector_push(&block->instructions, instruction)) {
        return false;
    }

    if (luna_ir_opcode_is_terminator(instruction->opcode)) {
        block->terminated = true;
    }
    return true;
}

const char *luna_ir_type_name(LunaIrType type) {
    switch (type) {
    case LUNA_IR_TYPE_VOID:
        return "void";
    case LUNA_IR_TYPE_BOOL:
        return "bool";
    case LUNA_IR_TYPE_I32:
        return "i32";
    }

    return "<invalid>";
}

static bool luna_ir_verify_value(const LunaIrFunction *function,
                                 LunaIrValueId value) {
    return value != LUNA_IR_INVALID_ID &&
           (size_t)value < function->value_types.length;
}

static bool luna_ir_verify_result(const LunaIrFunction *function,
                                  const LunaIrInstruction *instruction) {
    if (instruction->type == LUNA_IR_TYPE_VOID) {
        return instruction->result == LUNA_IR_INVALID_ID;
    }

    if (!luna_ir_verify_value(function, instruction->result)) {
        return false;
    }

    const LunaIrType *type = luna_vector_at_const(&function->value_types,
                                                  (size_t)instruction->result);
    return *type == instruction->type;
}

static bool luna_ir_verify_instruction(const LunaIrModule *module,
                                       const LunaIrFunction *function,
                                       const LunaIrInstruction *instruction) {
    switch (instruction->opcode) {
    case LUNA_IR_CONST_I32:
    case LUNA_IR_CONST_BOOL:
        return luna_ir_verify_result(function, instruction);

    case LUNA_IR_LOAD:
        return (size_t)instruction->slot < function->slot_types.length &&
               luna_ir_verify_result(function, instruction);

    case LUNA_IR_STORE:
        return (size_t)instruction->slot < function->slot_types.length &&
               luna_ir_verify_value(function, instruction->left) &&
               instruction->result == LUNA_IR_INVALID_ID;

    case LUNA_IR_NEG_I32:
    case LUNA_IR_BIT_NOT_I32:
    case LUNA_IR_BOOL_NOT:
        return luna_ir_verify_value(function, instruction->left) &&
               luna_ir_verify_result(function, instruction);

    case LUNA_IR_ADD_I32:
    case LUNA_IR_SUB_I32:
    case LUNA_IR_MUL_I32:
    case LUNA_IR_DIV_I32:
    case LUNA_IR_REM_I32:
    case LUNA_IR_BIT_AND_I32:
    case LUNA_IR_BIT_OR_I32:
    case LUNA_IR_BIT_XOR_I32:
    case LUNA_IR_SHIFT_LEFT_I32:
    case LUNA_IR_SHIFT_RIGHT_I32:
    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
        return luna_ir_verify_value(function, instruction->left) &&
               luna_ir_verify_value(function, instruction->right) &&
               luna_ir_verify_result(function, instruction);

    case LUNA_IR_CALL:
        return (size_t)instruction->callee < module->functions.length &&
               (size_t)instruction->first_argument <=
                   function->arguments.length &&
               (size_t)instruction->argument_count <=
                   function->arguments.length -
                       (size_t)instruction->first_argument &&
               luna_ir_verify_result(function, instruction);

    case LUNA_IR_JUMP:
        return (size_t)instruction->true_block < function->blocks.length &&
               instruction->result == LUNA_IR_INVALID_ID;

    case LUNA_IR_BRANCH:
        return luna_ir_verify_value(function, instruction->left) &&
               (size_t)instruction->true_block < function->blocks.length &&
               (size_t)instruction->false_block < function->blocks.length &&
               instruction->result == LUNA_IR_INVALID_ID;

    case LUNA_IR_RETURN:
        return (function->return_type == LUNA_IR_TYPE_VOID &&
                instruction->left == LUNA_IR_INVALID_ID) ||
               (function->return_type != LUNA_IR_TYPE_VOID &&
                luna_ir_verify_value(function, instruction->left));
    }

    return false;
}

bool luna_ir_verify(const LunaIrModule *module, FILE *error_stream) {
    if (module->entry_function == LUNA_IR_INVALID_ID ||
        (size_t)module->entry_function >= module->functions.length) {
        (void)fputs("IR verification: missing entry function\n", error_stream);
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaIrFunction *function =
            luna_vector_at_const(&module->functions, function_index);

        if (function->blocks.length == 0U) {
            (void)fprintf(error_stream,
                          "IR verification: function %zu has no blocks\n",
                          function_index);
            return false;
        }

        for (size_t block_index = 0U; block_index < function->blocks.length;
             block_index += 1U) {
            const LunaIrBlock *block =
                luna_vector_at_const(&function->blocks, block_index);
            const bool reachable =
                block_index == 0U || block->predecessor_count > 0U;

            if (reachable && !block->terminated) {
                (void)fprintf(
                    error_stream,
                    "IR verification: reachable block %zu in function %zu "
                    "has no terminator\n",
                    block_index, function_index);
                return false;
            }

            for (size_t instruction_index = 0U;
                 instruction_index < block->instructions.length;
                 instruction_index += 1U) {
                const LunaIrInstruction *instruction = luna_vector_at_const(
                    &block->instructions, instruction_index);

                if (!luna_ir_verify_instruction(module, function,
                                                instruction)) {
                    (void)fprintf(error_stream,
                                  "IR verification: invalid instruction %zu in "
                                  "function %zu block %zu\n",
                                  instruction_index, function_index,
                                  block_index);
                    return false;
                }

                if (luna_ir_opcode_is_terminator(instruction->opcode) &&
                    instruction_index + 1U != block->instructions.length) {
                    (void)fprintf(error_stream,
                                  "IR verification: terminator is not last in "
                                  "function %zu block %zu\n",
                                  function_index, block_index);
                    return false;
                }
            }
        }
    }

    return true;
}

static bool luna_ir_print_value(LunaStringBuilder *output,
                                LunaIrValueId value) {
    return luna_string_builder_append_format(output, "%%%u", value);
}

static bool luna_ir_print_instruction(const LunaIrModule *module,
                                      const LunaIrFunction *function,
                                      const LunaIrInstruction *instruction,
                                      LunaStringBuilder *output) {
    if (instruction->result != LUNA_IR_INVALID_ID) {
        if (!luna_ir_print_value(output, instruction->result) ||
            !luna_string_builder_append_c_string(output, " = ")) {
            return false;
        }
    }

    switch (instruction->opcode) {
    case LUNA_IR_CONST_I32:
        return luna_string_builder_append_format(output, "const.i32 %d\n",
                                                 instruction->immediate);

    case LUNA_IR_CONST_BOOL:
        return luna_string_builder_append_format(
            output, "const.bool %s\n",
            instruction->immediate == 0 ? "false" : "true");

    case LUNA_IR_LOAD:
        return luna_string_builder_append_format(
            output, "load.%s $%u\n", luna_ir_type_name(instruction->type),
            instruction->slot);

    case LUNA_IR_STORE:
        if (!luna_string_builder_append_format(output, "store $%u, ",
                                               instruction->slot) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_NEG_I32:
    case LUNA_IR_BIT_NOT_I32:
    case LUNA_IR_BOOL_NOT: {
        const char *name = "not.bool";
        if (instruction->opcode == LUNA_IR_NEG_I32) {
            name = "neg.i32";
        } else if (instruction->opcode == LUNA_IR_BIT_NOT_I32) {
            name = "bit_not.i32";
        }

        if (!luna_string_builder_append_c_string(output, name) ||
            !luna_string_builder_append_c_string(output, " ") ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_ADD_I32:
    case LUNA_IR_SUB_I32:
    case LUNA_IR_MUL_I32:
    case LUNA_IR_DIV_I32:
    case LUNA_IR_REM_I32:
    case LUNA_IR_BIT_AND_I32:
    case LUNA_IR_BIT_OR_I32:
    case LUNA_IR_BIT_XOR_I32:
    case LUNA_IR_SHIFT_LEFT_I32:
    case LUNA_IR_SHIFT_RIGHT_I32:
    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32: {
        const char *name = "<invalid>";
        switch (instruction->opcode) {
        case LUNA_IR_ADD_I32:
            name = "add.i32";
            break;
        case LUNA_IR_SUB_I32:
            name = "sub.i32";
            break;
        case LUNA_IR_MUL_I32:
            name = "mul.i32";
            break;
        case LUNA_IR_DIV_I32:
            name = "div.i32";
            break;
        case LUNA_IR_REM_I32:
            name = "rem.i32";
            break;
        case LUNA_IR_BIT_AND_I32:
            name = "and.i32";
            break;
        case LUNA_IR_BIT_OR_I32:
            name = "or.i32";
            break;
        case LUNA_IR_BIT_XOR_I32:
            name = "xor.i32";
            break;
        case LUNA_IR_SHIFT_LEFT_I32:
            name = "shl.i32";
            break;
        case LUNA_IR_SHIFT_RIGHT_I32:
            name = "shr.i32";
            break;
        case LUNA_IR_COMPARE_EQUAL:
            name = "eq";
            break;
        case LUNA_IR_COMPARE_NOT_EQUAL:
            name = "ne";
            break;
        case LUNA_IR_COMPARE_LESS_I32:
            name = "lt.i32";
            break;
        case LUNA_IR_COMPARE_LESS_EQUAL_I32:
            name = "le.i32";
            break;
        case LUNA_IR_COMPARE_GREATER_I32:
            name = "gt.i32";
            break;
        case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
            name = "ge.i32";
            break;
        default:
            break;
        }

        if (!luna_string_builder_append_c_string(output, name) ||
            !luna_string_builder_append_c_string(output, " ") ||
            !luna_ir_print_value(output, instruction->left) ||
            !luna_string_builder_append_c_string(output, ", ") ||
            !luna_ir_print_value(output, instruction->right)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CALL: {
        const LunaIrFunction *callee =
            luna_ir_module_function_const(module, instruction->callee);
        if (!luna_string_builder_append_c_string(output, "call @") ||
            !luna_string_builder_append_view(output, callee->name) ||
            !luna_string_builder_append_c_string(output, "(")) {
            return false;
        }

        for (uint32_t index = 0U; index < instruction->argument_count;
             index += 1U) {
            const LunaIrValueId *argument = luna_vector_at_const(
                &function->arguments,
                (size_t)instruction->first_argument + index);
            if ((index > 0U &&
                 !luna_string_builder_append_c_string(output, ", ")) ||
                !luna_ir_print_value(output, *argument)) {
                return false;
            }
        }

        return luna_string_builder_append_c_string(output, ")\n");
    }

    case LUNA_IR_JUMP:
        return luna_string_builder_append_format(output, "jump bb%u\n",
                                                 instruction->true_block);

    case LUNA_IR_BRANCH:
        if (!luna_string_builder_append_c_string(output, "branch ") ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_format(output, ", bb%u, bb%u\n",
                                                 instruction->true_block,
                                                 instruction->false_block);

    case LUNA_IR_RETURN:
        if (!luna_string_builder_append_c_string(output, "return")) {
            return false;
        }
        if (instruction->left != LUNA_IR_INVALID_ID) {
            if (!luna_string_builder_append_c_string(output, " ") ||
                !luna_ir_print_value(output, instruction->left)) {
                return false;
            }
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    return false;
}

bool luna_ir_print(const LunaIrModule *module, LunaStringBuilder *output) {
    if (!luna_string_builder_append_c_string(output, "ir luna.v0\n\n")) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaIrFunction *function =
            luna_vector_at_const(&module->functions, function_index);

        if (!luna_string_builder_append_c_string(output, "fn @") ||
            !luna_string_builder_append_view(output, function->name) ||
            !luna_string_builder_append_c_string(output, "(")) {
            return false;
        }

        for (size_t parameter_index = 0U;
             parameter_index < function->parameter_types.length;
             parameter_index += 1U) {
            const LunaIrType *type = luna_vector_at_const(
                &function->parameter_types, parameter_index);
            if ((parameter_index > 0U &&
                 !luna_string_builder_append_c_string(output, ", ")) ||
                !luna_string_builder_append_format(output, "$%zu: %s",
                                                   parameter_index,
                                                   luna_ir_type_name(*type))) {
                return false;
            }
        }

        if (!luna_string_builder_append_format(
                output, ") -> %s {\n",
                luna_ir_type_name(function->return_type))) {
            return false;
        }

        for (size_t block_index = 0U; block_index < function->blocks.length;
             block_index += 1U) {
            const LunaIrBlock *block =
                luna_vector_at_const(&function->blocks, block_index);

            if (!luna_string_builder_append_format(output, "bb%zu:\n",
                                                   block_index)) {
                return false;
            }

            for (size_t instruction_index = 0U;
                 instruction_index < block->instructions.length;
                 instruction_index += 1U) {
                const LunaIrInstruction *instruction = luna_vector_at_const(
                    &block->instructions, instruction_index);
                if (!luna_string_builder_append_c_string(output, "  ") ||
                    !luna_ir_print_instruction(module, function, instruction,
                                               output)) {
                    return false;
                }
            }
        }

        if (!luna_string_builder_append_c_string(output, "}\n\n")) {
            return false;
        }
    }

    return true;
}
