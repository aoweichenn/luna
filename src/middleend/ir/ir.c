#include "luna/middleend/ir/ir.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    case LUNA_IR_TYPE_I64:
        return "i64";
    }

    return "<invalid>";
}

static bool luna_ir_type_is_value(LunaIrType type) {
    return type == LUNA_IR_TYPE_BOOL || type == LUNA_IR_TYPE_I32 ||
           type == LUNA_IR_TYPE_I64;
}

static bool luna_ir_type_is_return(LunaIrType type) {
    return type == LUNA_IR_TYPE_VOID || luna_ir_type_is_value(type);
}

static bool luna_ir_reject(const char **reason, const char *message) {
    *reason = message;
    return false;
}

static bool luna_ir_verify_value(const LunaIrFunction *function,
                                 LunaIrValueId value, LunaIrType expected_type,
                                 const bool *defined_in_block,
                                 const char **reason) {
    if (value == LUNA_IR_INVALID_ID ||
        (size_t)value >= function->value_types.length) {
        return luna_ir_reject(reason, "value id is out of range");
    }

    if (!defined_in_block[value]) {
        return luna_ir_reject(
            reason, "value is used before its definition or across blocks");
    }

    const LunaIrType *actual_type =
        luna_vector_at_const(&function->value_types, (size_t)value);
    if (*actual_type != expected_type) {
        return luna_ir_reject(reason, "operand type does not match opcode");
    }
    return true;
}

static bool luna_ir_verify_result(const LunaIrFunction *function,
                                  const LunaIrInstruction *instruction,
                                  LunaIrType expected_type,
                                  const char **reason) {
    if (instruction->type != expected_type) {
        return luna_ir_reject(reason,
                              "instruction result type does not match opcode");
    }

    if (expected_type == LUNA_IR_TYPE_VOID) {
        if (instruction->result != LUNA_IR_INVALID_ID) {
            return luna_ir_reject(reason,
                                  "void instruction unexpectedly has a result");
        }
        return true;
    }

    if (instruction->result == LUNA_IR_INVALID_ID ||
        (size_t)instruction->result >= function->value_types.length) {
        return luna_ir_reject(reason, "result value id is out of range");
    }

    const LunaIrType *result_type = luna_vector_at_const(
        &function->value_types, (size_t)instruction->result);
    if (*result_type != expected_type) {
        return luna_ir_reject(reason,
                              "result value type does not match instruction");
    }
    return true;
}

static bool luna_ir_verify_binary(const LunaIrFunction *function,
                                  const LunaIrInstruction *instruction,
                                  const bool *defined_in_block,
                                  LunaIrType operand_type,
                                  LunaIrType result_type, const char **reason) {
    return luna_ir_verify_value(function, instruction->left, operand_type,
                                defined_in_block, reason) &&
           luna_ir_verify_value(function, instruction->right, operand_type,
                                defined_in_block, reason) &&
           luna_ir_verify_result(function, instruction, result_type, reason);
}

static bool luna_ir_verify_call(const LunaIrModule *module,
                                const LunaIrFunction *function,
                                const LunaIrInstruction *instruction,
                                const bool *defined_in_block,
                                bool *argument_used, const char **reason) {
    if ((size_t)instruction->callee >= module->functions.length) {
        return luna_ir_reject(reason, "callee id is out of range");
    }

    const LunaIrFunction *callee =
        luna_ir_module_function_const(module, instruction->callee);
    if ((size_t)instruction->argument_count != callee->parameter_types.length) {
        return luna_ir_reject(reason,
                              "call argument count does not match callee");
    }

    const size_t first_argument = (size_t)instruction->first_argument;
    const size_t argument_count = (size_t)instruction->argument_count;
    if (first_argument > function->arguments.length ||
        argument_count > function->arguments.length - first_argument) {
        return luna_ir_reject(reason, "call argument range is out of bounds");
    }

    if (!luna_ir_verify_result(function, instruction, callee->return_type,
                               reason)) {
        return false;
    }

    for (size_t index = 0U; index < argument_count; index += 1U) {
        const size_t argument_index = first_argument + index;
        if (argument_used[argument_index]) {
            return luna_ir_reject(
                reason, "call argument storage overlaps another call");
        }

        const LunaIrValueId *argument =
            luna_vector_at_const(&function->arguments, argument_index);
        const LunaIrType *parameter_type =
            luna_vector_at_const(&callee->parameter_types, index);
        if (!luna_ir_verify_value(function, *argument, *parameter_type,
                                  defined_in_block, reason)) {
            return false;
        }
        argument_used[argument_index] = true;
    }
    return true;
}

static bool luna_ir_verify_instruction(const LunaIrModule *module,
                                       const LunaIrFunction *function,
                                       const LunaIrInstruction *instruction,
                                       const bool *defined_in_block,
                                       bool *argument_used,
                                       const char **reason) {
    switch (instruction->opcode) {
    case LUNA_IR_CONST_I32:
        if (instruction->immediate < INT32_MIN ||
            instruction->immediate > INT32_MAX) {
            return luna_ir_reject(reason,
                                  "i32 constant is outside the i32 range");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_I32,
                                     reason);

    case LUNA_IR_CONST_I64:
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_I64,
                                     reason);

    case LUNA_IR_CONST_BOOL:
        if (instruction->immediate != 0 && instruction->immediate != 1) {
            return luna_ir_reject(reason,
                                  "bool constant must be exactly zero or one");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_BOOL,
                                     reason);

    case LUNA_IR_LOAD: {
        if ((size_t)instruction->slot >= function->slot_types.length) {
            return luna_ir_reject(reason, "load slot id is out of range");
        }
        const LunaIrType *slot_type = luna_vector_at_const(
            &function->slot_types, (size_t)instruction->slot);
        return luna_ir_verify_result(function, instruction, *slot_type, reason);
    }

    case LUNA_IR_STORE: {
        if ((size_t)instruction->slot >= function->slot_types.length) {
            return luna_ir_reject(reason, "store slot id is out of range");
        }
        const LunaIrType *slot_type = luna_vector_at_const(
            &function->slot_types, (size_t)instruction->slot);
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason) &&
               luna_ir_verify_value(function, instruction->left, *slot_type,
                                    defined_in_block, reason);
    }

    case LUNA_IR_NEG_I32:
    case LUNA_IR_BIT_NOT_I32:
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_I32, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_I32,
                                     reason);

    case LUNA_IR_NEG_I64:
    case LUNA_IR_BIT_NOT_I64:
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_I64, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_I64,
                                     reason);

    case LUNA_IR_BOOL_NOT:
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_BOOL, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_BOOL,
                                     reason);

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
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     LUNA_IR_TYPE_I32, LUNA_IR_TYPE_I32,
                                     reason);

    case LUNA_IR_ADD_I64:
    case LUNA_IR_SUB_I64:
    case LUNA_IR_MUL_I64:
    case LUNA_IR_DIV_I64:
    case LUNA_IR_REM_I64:
    case LUNA_IR_BIT_AND_I64:
    case LUNA_IR_BIT_OR_I64:
    case LUNA_IR_BIT_XOR_I64:
    case LUNA_IR_SHIFT_LEFT_I64:
    case LUNA_IR_SHIFT_RIGHT_I64:
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     LUNA_IR_TYPE_I64, LUNA_IR_TYPE_I64,
                                     reason);

    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL: {
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "comparison value id is out of range");
        }
        const LunaIrType *operand_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_value(*operand_type)) {
            return luna_ir_reject(reason,
                                  "equality operand type is not comparable");
        }
        return luna_ir_verify_value(function, instruction->left, *operand_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_value(function, instruction->right, *operand_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_BOOL,
                                     reason);
    }

    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     LUNA_IR_TYPE_I32, LUNA_IR_TYPE_BOOL,
                                     reason);

    case LUNA_IR_COMPARE_LESS_I64:
    case LUNA_IR_COMPARE_LESS_EQUAL_I64:
    case LUNA_IR_COMPARE_GREATER_I64:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I64:
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     LUNA_IR_TYPE_I64, LUNA_IR_TYPE_BOOL,
                                     reason);

    case LUNA_IR_CALL:
        return luna_ir_verify_call(module, function, instruction,
                                   defined_in_block, argument_used, reason);

    case LUNA_IR_JUMP:
        if ((size_t)instruction->true_block >= function->blocks.length) {
            return luna_ir_reject(reason, "jump target is out of range");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_BRANCH:
        if ((size_t)instruction->true_block >= function->blocks.length ||
            (size_t)instruction->false_block >= function->blocks.length) {
            return luna_ir_reject(reason, "branch target is out of range");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason) &&
               luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_BOOL, defined_in_block,
                                    reason);

    case LUNA_IR_RETURN:
        if (!luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                   reason)) {
            return false;
        }
        if (function->return_type == LUNA_IR_TYPE_VOID) {
            if (instruction->left != LUNA_IR_INVALID_ID) {
                return luna_ir_reject(
                    reason, "void function returns an unexpected value");
            }
            return true;
        }
        return luna_ir_verify_value(function, instruction->left,
                                    function->return_type, defined_in_block,
                                    reason);
    }

    return luna_ir_reject(reason, "opcode is invalid");
}

static bool luna_ir_add_computed_predecessor(uint32_t *predecessors,
                                             LunaIrBlockId block,
                                             const char **reason) {
    if (predecessors[block] == UINT32_MAX) {
        return luna_ir_reject(reason, "predecessor count overflows");
    }
    predecessors[block] += 1U;
    return true;
}

static bool luna_ir_verify_function(const LunaIrModule *module,
                                    const LunaIrFunction *function,
                                    size_t function_index, FILE *error_stream) {
    bool *globally_defined = NULL;
    bool *defined_in_block = NULL;
    bool *argument_used = NULL;
    uint32_t *computed_predecessors = NULL;
    bool *reachable = NULL;
    LunaIrBlockId *worklist = NULL;
    bool success = false;

    if (!luna_ir_type_is_return(function->return_type)) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has invalid return type\n",
                      function_index);
        goto cleanup;
    }

    if (function->blocks.length == 0U) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has no blocks\n",
                      function_index);
        goto cleanup;
    }

    if (function->parameter_types.length > function->slot_types.length) {
        (void)fprintf(
            error_stream,
            "IR verification: function %zu has fewer slots than parameters\n",
            function_index);
        goto cleanup;
    }

    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaIrType *parameter_type =
            luna_vector_at_const(&function->parameter_types, index);
        const LunaIrType *slot_type =
            luna_vector_at_const(&function->slot_types, index);
        if (!luna_ir_type_is_value(*parameter_type) ||
            *slot_type != *parameter_type) {
            (void)fprintf(error_stream,
                          "IR verification: invalid parameter %zu in function "
                          "%zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    for (size_t index = 0U; index < function->slot_types.length; index += 1U) {
        const LunaIrType *type =
            luna_vector_at_const(&function->slot_types, index);
        if (!luna_ir_type_is_value(*type)) {
            (void)fprintf(error_stream,
                          "IR verification: invalid slot type at %zu in "
                          "function %zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    for (size_t index = 0U; index < function->value_types.length; index += 1U) {
        const LunaIrType *type =
            luna_vector_at_const(&function->value_types, index);
        if (!luna_ir_type_is_value(*type)) {
            (void)fprintf(error_stream,
                          "IR verification: invalid value type at %zu in "
                          "function %zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    if (function->value_types.length > 0U) {
        globally_defined =
            calloc(function->value_types.length, sizeof(*globally_defined));
        defined_in_block =
            calloc(function->value_types.length, sizeof(*defined_in_block));
        if (globally_defined == NULL || defined_in_block == NULL) {
            (void)fputs("IR verification: out of memory\n", error_stream);
            goto cleanup;
        }
    }

    if (function->arguments.length > 0U) {
        argument_used =
            calloc(function->arguments.length, sizeof(*argument_used));
        if (argument_used == NULL) {
            (void)fputs("IR verification: out of memory\n", error_stream);
            goto cleanup;
        }
    }

    computed_predecessors =
        calloc(function->blocks.length, sizeof(*computed_predecessors));
    reachable = calloc(function->blocks.length, sizeof(*reachable));
    worklist = calloc(function->blocks.length, sizeof(*worklist));
    if (computed_predecessors == NULL || reachable == NULL ||
        worklist == NULL) {
        (void)fputs("IR verification: out of memory\n", error_stream);
        goto cleanup;
    }

    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (function->value_types.length > 0U) {
            memset(defined_in_block, 0,
                   function->value_types.length * sizeof(*defined_in_block));
        }

        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            const char *reason = "unknown instruction error";

            if (!luna_ir_verify_instruction(module, function, instruction,
                                            defined_in_block, argument_used,
                                            &reason)) {
                (void)fprintf(error_stream,
                              "IR verification: invalid instruction %zu in "
                              "function %zu block %zu: %s\n",
                              instruction_index, function_index, block_index,
                              reason);
                goto cleanup;
            }

            if (instruction->result != LUNA_IR_INVALID_ID) {
                if (globally_defined == NULL ||
                    (size_t)instruction->result >=
                        function->value_types.length) {
                    (void)fprintf(error_stream,
                                  "IR verification: result value is invalid "
                                  "in function %zu block %zu\n",
                                  function_index, block_index);
                    goto cleanup;
                }
                if (globally_defined[instruction->result]) {
                    (void)fprintf(
                        error_stream,
                        "IR verification: value %u is defined more than once "
                        "in function %zu\n",
                        instruction->result, function_index);
                    goto cleanup;
                }
                globally_defined[instruction->result] = true;
                defined_in_block[instruction->result] = true;
            }

            if (luna_ir_opcode_is_terminator(instruction->opcode) &&
                instruction_index + 1U != block->instructions.length) {
                (void)fprintf(error_stream,
                              "IR verification: terminator is not last in "
                              "function %zu block %zu\n",
                              function_index, block_index);
                goto cleanup;
            }
        }

        const bool has_terminator =
            block->instructions.length > 0U &&
            luna_ir_opcode_is_terminator(
                ((const LunaIrInstruction *)luna_vector_at_const(
                     &block->instructions, block->instructions.length - 1U))
                    ->opcode);
        if (block->terminated != has_terminator) {
            (void)fprintf(error_stream,
                          "IR verification: cached termination state is wrong "
                          "in function %zu block %zu\n",
                          function_index, block_index);
            goto cleanup;
        }
        if (block->instructions.length > 0U && !has_terminator) {
            (void)fprintf(error_stream,
                          "IR verification: non-empty block has no terminator "
                          "in function %zu block %zu\n",
                          function_index, block_index);
            goto cleanup;
        }

        if (has_terminator) {
            const LunaIrInstruction *terminator = luna_vector_at_const(
                &block->instructions, block->instructions.length - 1U);
            const char *reason = "invalid predecessor";
            if (terminator->opcode == LUNA_IR_JUMP) {
                if (!luna_ir_add_computed_predecessor(computed_predecessors,
                                                      terminator->true_block,
                                                      &reason)) {
                    (void)fprintf(error_stream,
                                  "IR verification: function %zu: %s\n",
                                  function_index, reason);
                    goto cleanup;
                }
            } else if (terminator->opcode == LUNA_IR_BRANCH) {
                if (!luna_ir_add_computed_predecessor(computed_predecessors,
                                                      terminator->true_block,
                                                      &reason) ||
                    !luna_ir_add_computed_predecessor(computed_predecessors,
                                                      terminator->false_block,
                                                      &reason)) {
                    (void)fprintf(error_stream,
                                  "IR verification: function %zu: %s\n",
                                  function_index, reason);
                    goto cleanup;
                }
            }
        }
    }

    for (size_t index = 0U; index < function->value_types.length; index += 1U) {
        if (!globally_defined[index]) {
            (void)fprintf(error_stream,
                          "IR verification: value %zu has no definition in "
                          "function %zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    for (size_t index = 0U; index < function->arguments.length; index += 1U) {
        if (!argument_used[index]) {
            (void)fprintf(error_stream,
                          "IR verification: call argument %zu is unused in "
                          "function %zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block->predecessor_count != computed_predecessors[block_index]) {
            (void)fprintf(error_stream,
                          "IR verification: predecessor count mismatch in "
                          "function %zu block %zu\n",
                          function_index, block_index);
            goto cleanup;
        }
    }

    size_t worklist_head = 0U;
    size_t worklist_length = 1U;
    reachable[0] = true;
    worklist[0] = 0U;
    while (worklist_head < worklist_length) {
        const LunaIrBlockId block_id = worklist[worklist_head];
        worklist_head += 1U;
        const LunaIrBlock *block =
            luna_vector_at_const(&function->blocks, (size_t)block_id);
        if (!block->terminated) {
            (void)fprintf(error_stream,
                          "IR verification: reachable block %u in function "
                          "%zu has no terminator\n",
                          block_id, function_index);
            goto cleanup;
        }

        const LunaIrInstruction *terminator = luna_vector_at_const(
            &block->instructions, block->instructions.length - 1U);
        LunaIrBlockId targets[2] = {
            LUNA_IR_INVALID_ID,
            LUNA_IR_INVALID_ID,
        };
        size_t target_count = 0U;
        if (terminator->opcode == LUNA_IR_JUMP) {
            targets[0] = terminator->true_block;
            target_count = 1U;
        } else if (terminator->opcode == LUNA_IR_BRANCH) {
            targets[0] = terminator->true_block;
            targets[1] = terminator->false_block;
            target_count = 2U;
        }

        for (size_t index = 0U; index < target_count; index += 1U) {
            const LunaIrBlockId target = targets[index];
            if (!reachable[target]) {
                if (worklist_length >= function->blocks.length) {
                    (void)fprintf(error_stream,
                                  "IR verification: reachability worklist "
                                  "overflow in function %zu\n",
                                  function_index);
                    goto cleanup;
                }
                reachable[target] = true;
                worklist[worklist_length] = target;
                worklist_length += 1U;
            }
        }
    }

    success = true;

cleanup:
    free(worklist);
    free(reachable);
    free(computed_predecessors);
    free(argument_used);
    free(defined_in_block);
    free(globally_defined);
    return success;
}

bool luna_ir_verify(const LunaIrModule *module, FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL) {
        (void)fputs("IR verification: module is null\n", stream);
        return false;
    }

    if (module->entry_function == LUNA_IR_INVALID_ID ||
        (size_t)module->entry_function >= module->functions.length) {
        (void)fputs("IR verification: missing entry function\n", stream);
        return false;
    }

    const LunaIrFunction *entry =
        luna_ir_module_function_const(module, module->entry_function);
    if (entry->return_type != LUNA_IR_TYPE_I32 ||
        entry->parameter_types.length != 0U) {
        (void)fputs(
            "IR verification: entry function must have type fn() -> i32\n",
            stream);
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaIrFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (!luna_ir_verify_function(module, function, function_index,
                                     stream)) {
            return false;
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
        return luna_string_builder_append_format(
            output, "const.i32 %" PRId64 "\n", instruction->immediate);

    case LUNA_IR_CONST_I64:
        return luna_string_builder_append_format(
            output, "const.i64 %" PRId64 "\n", instruction->immediate);

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
    case LUNA_IR_NEG_I64:
    case LUNA_IR_BIT_NOT_I32:
    case LUNA_IR_BIT_NOT_I64:
    case LUNA_IR_BOOL_NOT: {
        const char *name = "not.bool";
        if (instruction->opcode == LUNA_IR_NEG_I32) {
            name = "neg.i32";
        } else if (instruction->opcode == LUNA_IR_NEG_I64) {
            name = "neg.i64";
        } else if (instruction->opcode == LUNA_IR_BIT_NOT_I32) {
            name = "bit_not.i32";
        } else if (instruction->opcode == LUNA_IR_BIT_NOT_I64) {
            name = "bit_not.i64";
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
    case LUNA_IR_ADD_I64:
    case LUNA_IR_SUB_I64:
    case LUNA_IR_MUL_I64:
    case LUNA_IR_DIV_I64:
    case LUNA_IR_REM_I64:
    case LUNA_IR_BIT_AND_I64:
    case LUNA_IR_BIT_OR_I64:
    case LUNA_IR_BIT_XOR_I64:
    case LUNA_IR_SHIFT_LEFT_I64:
    case LUNA_IR_SHIFT_RIGHT_I64:
    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_I32:
    case LUNA_IR_COMPARE_LESS_EQUAL_I32:
    case LUNA_IR_COMPARE_GREATER_I32:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I32:
    case LUNA_IR_COMPARE_LESS_I64:
    case LUNA_IR_COMPARE_LESS_EQUAL_I64:
    case LUNA_IR_COMPARE_GREATER_I64:
    case LUNA_IR_COMPARE_GREATER_EQUAL_I64: {
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
        case LUNA_IR_ADD_I64:
            name = "add.i64";
            break;
        case LUNA_IR_SUB_I64:
            name = "sub.i64";
            break;
        case LUNA_IR_MUL_I64:
            name = "mul.i64";
            break;
        case LUNA_IR_DIV_I64:
            name = "div.i64";
            break;
        case LUNA_IR_REM_I64:
            name = "rem.i64";
            break;
        case LUNA_IR_BIT_AND_I64:
            name = "and.i64";
            break;
        case LUNA_IR_BIT_OR_I64:
            name = "or.i64";
            break;
        case LUNA_IR_BIT_XOR_I64:
            name = "xor.i64";
            break;
        case LUNA_IR_SHIFT_LEFT_I64:
            name = "shl.i64";
            break;
        case LUNA_IR_SHIFT_RIGHT_I64:
            name = "shr.i64";
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
        case LUNA_IR_COMPARE_LESS_I64:
            name = "lt.i64";
            break;
        case LUNA_IR_COMPARE_LESS_EQUAL_I64:
            name = "le.i64";
            break;
        case LUNA_IR_COMPARE_GREATER_I64:
            name = "gt.i64";
            break;
        case LUNA_IR_COMPARE_GREATER_EQUAL_I64:
            name = "ge.i64";
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
