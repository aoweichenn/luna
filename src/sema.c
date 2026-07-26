#include "luna/sema.h"

#include "luna/buffer.h"
#include "luna/string_view.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct LunaSemaFunction {
    const LunaFunction *syntax;
    LunaIrFunctionId ir_id;
} LunaSemaFunction;

typedef struct LunaSemaLocal {
    LunaStringView name;
    LunaTypeKind type;
    LunaIrSlotId slot;
    uint32_t scope_depth;
    bool is_mutable;
} LunaSemaLocal;

typedef struct LunaSemaLoop {
    LunaIrBlockId continue_block;
    LunaIrBlockId break_block;
} LunaSemaLoop;

typedef struct LunaCheckedValue {
    LunaIrValueId id;
    LunaTypeKind type;
} LunaCheckedValue;

typedef struct LunaSemaContext {
    const LunaProgram *program;
    LunaDiagnosticEngine *diagnostics;
    LunaIrModule *module;
    LunaVector functions;
    LunaVector locals;
    LunaVector loops;
    LunaIrFunction *current_function;
    const LunaFunction *current_syntax_function;
    LunaIrBlockId current_block;
    uint32_t scope_depth;
    bool reachable;
    bool allocation_failed;
} LunaSemaContext;

static LunaIrType luna_sema_ir_type(LunaTypeKind type) {
    switch (type) {
    case LUNA_TYPE_VOID:
        return LUNA_IR_TYPE_VOID;
    case LUNA_TYPE_BOOL:
        return LUNA_IR_TYPE_BOOL;
    case LUNA_TYPE_I32:
        return LUNA_IR_TYPE_I32;
    case LUNA_TYPE_INVALID:
        break;
    }

    return LUNA_IR_TYPE_VOID;
}

static void luna_sema_report_allocation_failure(LunaSemaContext *context) {
    if (!context->allocation_failed) {
        luna_diagnostic_error_plain(context->diagnostics,
                                    "out of memory while building typed IR");
        context->allocation_failed = true;
    }
}

static LunaIrInstruction luna_sema_instruction(LunaIrOpcode opcode,
                                               LunaSourceSpan span) {
    return (LunaIrInstruction){
        .opcode = opcode,
        .type = LUNA_IR_TYPE_VOID,
        .result = LUNA_IR_INVALID_ID,
        .left = LUNA_IR_INVALID_ID,
        .right = LUNA_IR_INVALID_ID,
        .slot = LUNA_IR_INVALID_ID,
        .true_block = LUNA_IR_INVALID_ID,
        .false_block = LUNA_IR_INVALID_ID,
        .callee = LUNA_IR_INVALID_ID,
        .first_argument = 0U,
        .argument_count = 0U,
        .immediate = 0,
        .span = span,
    };
}

static LunaIrBlock *luna_sema_current_block(LunaSemaContext *context) {
    return luna_ir_function_block(context->current_function,
                                  context->current_block);
}

static bool luna_sema_append_instruction(LunaSemaContext *context,
                                         const LunaIrInstruction *instruction) {
    if (!context->reachable ||
        !luna_ir_block_append(luna_sema_current_block(context), instruction)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    return true;
}

static LunaIrBlockId luna_sema_add_block(LunaSemaContext *context) {
    const LunaIrBlockId block_id =
        luna_ir_function_add_block(context->current_function);
    if (block_id == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return block_id;
}

static LunaIrValueId luna_sema_add_value(LunaSemaContext *context,
                                         LunaTypeKind type) {
    const LunaIrValueId value = luna_ir_function_add_value(
        context->current_function, luna_sema_ir_type(type));
    if (value == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return value;
}

static bool luna_sema_add_predecessor(LunaSemaContext *context,
                                      LunaIrBlockId block_id) {
    LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    if (block == NULL || block->predecessor_count == UINT32_MAX) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    block->predecessor_count += 1U;
    return true;
}

static bool luna_sema_emit_jump(LunaSemaContext *context, LunaIrBlockId target,
                                LunaSourceSpan span) {
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_JUMP, span);
    instruction.true_block = target;

    if (!luna_sema_append_instruction(context, &instruction) ||
        !luna_sema_add_predecessor(context, target)) {
        return false;
    }

    context->reachable = false;
    return true;
}

static bool luna_sema_emit_branch(LunaSemaContext *context,
                                  LunaIrValueId condition,
                                  LunaIrBlockId true_block,
                                  LunaIrBlockId false_block,
                                  LunaSourceSpan span) {
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_BRANCH, span);
    instruction.left = condition;
    instruction.true_block = true_block;
    instruction.false_block = false_block;

    if (!luna_sema_append_instruction(context, &instruction) ||
        !luna_sema_add_predecessor(context, true_block) ||
        !luna_sema_add_predecessor(context, false_block)) {
        return false;
    }

    context->reachable = false;
    return true;
}

static void luna_sema_set_block(LunaSemaContext *context,
                                LunaIrBlockId block_id) {
    context->current_block = block_id;
    const LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    context->reachable =
        block_id == 0U || (block != NULL && block->predecessor_count > 0U);
}

static LunaIrValueId
luna_sema_emit_value_instruction(LunaSemaContext *context,
                                 LunaIrInstruction *instruction,
                                 LunaTypeKind type) {
    instruction->type = luna_sema_ir_type(type);
    instruction->result = luna_sema_add_value(context, type);
    if (instruction->result == LUNA_IR_INVALID_ID ||
        !luna_sema_append_instruction(context, instruction)) {
        return LUNA_IR_INVALID_ID;
    }

    return instruction->result;
}

static LunaSemaFunction *luna_sema_find_function(LunaSemaContext *context,
                                                 LunaStringView name) {
    for (size_t index = 0U; index < context->functions.length; index += 1U) {
        LunaSemaFunction *function = luna_vector_at(&context->functions, index);
        if (luna_string_view_equal(function->syntax->name, name)) {
            return function;
        }
    }

    return NULL;
}

static LunaSemaLocal *luna_sema_find_local(LunaSemaContext *context,
                                           LunaStringView name) {
    for (size_t index = context->locals.length; index > 0U; index -= 1U) {
        LunaSemaLocal *local = luna_vector_at(&context->locals, index - 1U);
        if (luna_string_view_equal(local->name, name)) {
            return local;
        }
    }

    return NULL;
}

static LunaSemaLocal *
luna_sema_find_local_in_current_scope(LunaSemaContext *context,
                                      LunaStringView name) {
    for (size_t index = context->locals.length; index > 0U; index -= 1U) {
        LunaSemaLocal *local = luna_vector_at(&context->locals, index - 1U);
        if (local->scope_depth < context->scope_depth) {
            break;
        }
        if (luna_string_view_equal(local->name, name)) {
            return local;
        }
    }

    return NULL;
}

static bool luna_sema_add_local(LunaSemaContext *context, LunaStringView name,
                                LunaTypeKind type, LunaIrSlotId slot,
                                bool is_mutable) {
    const LunaSemaLocal local = {
        .name = name,
        .type = type,
        .slot = slot,
        .scope_depth = context->scope_depth,
        .is_mutable = is_mutable,
    };

    if (!luna_vector_push(&context->locals, &local)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    return true;
}

static void luna_sema_enter_scope(LunaSemaContext *context) {
    context->scope_depth += 1U;
}

static void luna_sema_leave_scope(LunaSemaContext *context) {
    while (context->locals.length > 0U) {
        const LunaSemaLocal *local =
            luna_vector_at_const(&context->locals, context->locals.length - 1U);
        if (local->scope_depth < context->scope_depth) {
            break;
        }
        context->locals.length -= 1U;
    }

    context->scope_depth -= 1U;
}

static bool luna_sema_require_type(LunaSemaContext *context,
                                   LunaCheckedValue value,
                                   LunaTypeKind expected, LunaSourceSpan span) {
    if (value.type == expected) {
        return true;
    }

    luna_diagnostic_error(context->diagnostics, span, "expected %s, found %s",
                          luna_type_kind_name(expected),
                          luna_type_kind_name(value.type));
    return false;
}

static LunaCheckedValue luna_sema_invalid_value(void) {
    return (LunaCheckedValue){
        .id = LUNA_IR_INVALID_ID,
        .type = LUNA_TYPE_INVALID,
    };
}

static LunaCheckedValue
luna_sema_lower_expression(LunaSemaContext *context,
                           const LunaExpression *expression);

static LunaCheckedValue
luna_sema_lower_logical(LunaSemaContext *context,
                        const LunaExpression *expression) {
    const bool is_and =
        expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_AND;

    LunaCheckedValue left =
        luna_sema_lower_expression(context, expression->as.binary.left);
    if (!luna_sema_require_type(context, left, LUNA_TYPE_BOOL,
                                expression->as.binary.left->span)) {
        return luna_sema_invalid_value();
    }

    const LunaIrSlotId result_slot =
        luna_ir_function_add_slot(context->current_function, LUNA_IR_TYPE_BOOL);
    const LunaIrBlockId right_block = luna_sema_add_block(context);
    const LunaIrBlockId short_block = luna_sema_add_block(context);
    const LunaIrBlockId merge_block = luna_sema_add_block(context);

    if (result_slot == LUNA_IR_INVALID_ID ||
        right_block == LUNA_IR_INVALID_ID ||
        short_block == LUNA_IR_INVALID_ID ||
        merge_block == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return luna_sema_invalid_value();
    }

    if (is_and) {
        (void)luna_sema_emit_branch(context, left.id, right_block, short_block,
                                    expression->span);
    } else {
        (void)luna_sema_emit_branch(context, left.id, short_block, right_block,
                                    expression->span);
    }

    luna_sema_set_block(context, short_block);
    LunaIrInstruction short_value =
        luna_sema_instruction(LUNA_IR_CONST_BOOL, expression->span);
    short_value.immediate = is_and ? 0 : 1;
    const LunaIrValueId short_id =
        luna_sema_emit_value_instruction(context, &short_value, LUNA_TYPE_BOOL);

    LunaIrInstruction short_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    short_store.slot = result_slot;
    short_store.left = short_id;
    (void)luna_sema_append_instruction(context, &short_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, right_block);
    LunaCheckedValue right =
        luna_sema_lower_expression(context, expression->as.binary.right);
    if (!luna_sema_require_type(context, right, LUNA_TYPE_BOOL,
                                expression->as.binary.right->span)) {
        return luna_sema_invalid_value();
    }

    LunaIrInstruction right_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    right_store.slot = result_slot;
    right_store.left = right.id;
    (void)luna_sema_append_instruction(context, &right_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, merge_block);
    LunaIrInstruction load =
        luna_sema_instruction(LUNA_IR_LOAD, expression->span);
    load.slot = result_slot;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, LUNA_TYPE_BOOL);

    return (LunaCheckedValue){
        .id = result,
        .type = LUNA_TYPE_BOOL,
    };
}

static LunaCheckedValue luna_sema_lower_call(LunaSemaContext *context,
                                             const LunaExpression *expression) {
    LunaSemaFunction *callee =
        luna_sema_find_function(context, expression->as.call.name);
    if (callee == NULL) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "unknown function '%.*s'",
                              (int)expression->as.call.name.length,
                              expression->as.call.name.data);
        return luna_sema_invalid_value();
    }

    if (expression->as.call.argument_count != callee->syntax->parameter_count) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "function '%.*s' expects %u arguments, found %u",
                              (int)callee->syntax->name.length,
                              callee->syntax->name.data,
                              callee->syntax->parameter_count,
                              expression->as.call.argument_count);
        return luna_sema_invalid_value();
    }

    if (context->current_function->arguments.length > UINT32_MAX) {
        luna_sema_report_allocation_failure(context);
        return luna_sema_invalid_value();
    }
    const uint32_t first_argument =
        (uint32_t)context->current_function->arguments.length;

    const LunaExpression *argument = expression->as.call.first_argument;
    const LunaParameter *parameter = callee->syntax->first_parameter;
    while (argument != NULL && parameter != NULL) {
        LunaCheckedValue value = luna_sema_lower_expression(context, argument);
        if (!luna_sema_require_type(context, value, parameter->type.kind,
                                    argument->span)) {
            return luna_sema_invalid_value();
        }

        if (!luna_vector_push(&context->current_function->arguments,
                              &value.id)) {
            luna_sema_report_allocation_failure(context);
            return luna_sema_invalid_value();
        }

        argument = argument->next;
        parameter = parameter->next;
    }

    LunaIrInstruction call =
        luna_sema_instruction(LUNA_IR_CALL, expression->span);
    call.callee = callee->ir_id;
    call.first_argument = first_argument;
    call.argument_count = expression->as.call.argument_count;

    const LunaTypeKind return_type = callee->syntax->return_type.kind;
    if (return_type == LUNA_TYPE_VOID) {
        if (!luna_sema_append_instruction(context, &call)) {
            return luna_sema_invalid_value();
        }

        return (LunaCheckedValue){
            .id = LUNA_IR_INVALID_ID,
            .type = LUNA_TYPE_VOID,
        };
    }

    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &call, return_type);
    return (LunaCheckedValue){
        .id = result,
        .type = return_type,
    };
}

static LunaCheckedValue
luna_sema_lower_expression(LunaSemaContext *context,
                           const LunaExpression *expression) {
    if (expression == NULL || !context->reachable) {
        return luna_sema_invalid_value();
    }

    switch (expression->kind) {
    case LUNA_EXPRESSION_INTEGER: {
        if (expression->as.integer < INT32_MIN ||
            expression->as.integer > INT32_MAX) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "integer literal does not fit in i32");
            return luna_sema_invalid_value();
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_I32, expression->span);
        instruction.immediate = (int32_t)expression->as.integer;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, LUNA_TYPE_I32);
        return (LunaCheckedValue){
            .id = result,
            .type = LUNA_TYPE_I32,
        };
    }

    case LUNA_EXPRESSION_BOOLEAN: {
        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_BOOL, expression->span);
        instruction.immediate = expression->as.boolean ? 1 : 0;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, LUNA_TYPE_BOOL);
        return (LunaCheckedValue){
            .id = result,
            .type = LUNA_TYPE_BOOL,
        };
    }

    case LUNA_EXPRESSION_NAME: {
        LunaSemaLocal *local =
            luna_sema_find_local(context, expression->as.name);
        if (local == NULL) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "unknown local variable '%.*s'",
                                  (int)expression->as.name.length,
                                  expression->as.name.data);
            return luna_sema_invalid_value();
        }

        LunaIrInstruction load =
            luna_sema_instruction(LUNA_IR_LOAD, expression->span);
        load.slot = local->slot;
        const LunaIrValueId result =
            luna_sema_emit_value_instruction(context, &load, local->type);
        return (LunaCheckedValue){
            .id = result,
            .type = local->type,
        };
    }

    case LUNA_EXPRESSION_CALL:
        return luna_sema_lower_call(context, expression);

    case LUNA_EXPRESSION_UNARY: {
        LunaCheckedValue operand =
            luna_sema_lower_expression(context, expression->as.unary.operand);
        LunaTypeKind result_type = LUNA_TYPE_I32;
        LunaIrOpcode opcode = LUNA_IR_NEG_I32;

        switch (expression->as.unary.operator_kind) {
        case LUNA_TOKEN_PLUS:
            if (!luna_sema_require_type(context, operand, LUNA_TYPE_I32,
                                        expression->span)) {
                return luna_sema_invalid_value();
            }
            return operand;

        case LUNA_TOKEN_MINUS:
            opcode = LUNA_IR_NEG_I32;
            result_type = LUNA_TYPE_I32;
            break;

        case LUNA_TOKEN_TILDE:
            opcode = LUNA_IR_BIT_NOT_I32;
            result_type = LUNA_TYPE_I32;
            break;

        case LUNA_TOKEN_BANG:
            opcode = LUNA_IR_BOOL_NOT;
            result_type = LUNA_TYPE_BOOL;
            break;

        default:
            return luna_sema_invalid_value();
        }

        if (!luna_sema_require_type(context, operand, result_type,
                                    expression->as.unary.operand->span)) {
            return luna_sema_invalid_value();
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(opcode, expression->span);
        instruction.left = operand.id;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, result_type);
        return (LunaCheckedValue){
            .id = result,
            .type = result_type,
        };
    }

    case LUNA_EXPRESSION_BINARY:
        break;
    }

    if (expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_AND ||
        expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_OR) {
        return luna_sema_lower_logical(context, expression);
    }

    LunaCheckedValue left =
        luna_sema_lower_expression(context, expression->as.binary.left);
    LunaCheckedValue right =
        luna_sema_lower_expression(context, expression->as.binary.right);

    LunaIrOpcode opcode = LUNA_IR_ADD_I32;
    LunaTypeKind operand_type = LUNA_TYPE_I32;
    LunaTypeKind result_type = LUNA_TYPE_I32;

    switch (expression->as.binary.operator_kind) {
    case LUNA_TOKEN_PLUS:
        opcode = LUNA_IR_ADD_I32;
        break;
    case LUNA_TOKEN_MINUS:
        opcode = LUNA_IR_SUB_I32;
        break;
    case LUNA_TOKEN_STAR:
        opcode = LUNA_IR_MUL_I32;
        break;
    case LUNA_TOKEN_SLASH:
        opcode = LUNA_IR_DIV_I32;
        break;
    case LUNA_TOKEN_PERCENT:
        opcode = LUNA_IR_REM_I32;
        break;
    case LUNA_TOKEN_AMPERSAND:
        opcode = LUNA_IR_BIT_AND_I32;
        break;
    case LUNA_TOKEN_PIPE:
        opcode = LUNA_IR_BIT_OR_I32;
        break;
    case LUNA_TOKEN_CARET:
        opcode = LUNA_IR_BIT_XOR_I32;
        break;
    case LUNA_TOKEN_SHIFT_LEFT:
        opcode = LUNA_IR_SHIFT_LEFT_I32;
        break;
    case LUNA_TOKEN_SHIFT_RIGHT:
        opcode = LUNA_IR_SHIFT_RIGHT_I32;
        break;

    case LUNA_TOKEN_EQUAL_EQUAL:
        opcode = LUNA_IR_COMPARE_EQUAL;
        operand_type = left.type;
        result_type = LUNA_TYPE_BOOL;
        break;

    case LUNA_TOKEN_BANG_EQUAL:
        opcode = LUNA_IR_COMPARE_NOT_EQUAL;
        operand_type = left.type;
        result_type = LUNA_TYPE_BOOL;
        break;

    case LUNA_TOKEN_LESS:
        opcode = LUNA_IR_COMPARE_LESS_I32;
        result_type = LUNA_TYPE_BOOL;
        break;

    case LUNA_TOKEN_LESS_EQUAL:
        opcode = LUNA_IR_COMPARE_LESS_EQUAL_I32;
        result_type = LUNA_TYPE_BOOL;
        break;

    case LUNA_TOKEN_GREATER:
        opcode = LUNA_IR_COMPARE_GREATER_I32;
        result_type = LUNA_TYPE_BOOL;
        break;

    case LUNA_TOKEN_GREATER_EQUAL:
        opcode = LUNA_IR_COMPARE_GREATER_EQUAL_I32;
        result_type = LUNA_TYPE_BOOL;
        break;

    default:
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "operator is not implemented in milestone M0");
        return luna_sema_invalid_value();
    }

    if (operand_type == LUNA_TYPE_INVALID ||
        !luna_sema_require_type(context, left, operand_type,
                                expression->as.binary.left->span) ||
        !luna_sema_require_type(context, right, operand_type,
                                expression->as.binary.right->span)) {
        return luna_sema_invalid_value();
    }

    if ((expression->as.binary.operator_kind == LUNA_TOKEN_EQUAL_EQUAL ||
         expression->as.binary.operator_kind == LUNA_TOKEN_BANG_EQUAL) &&
        left.type != LUNA_TYPE_BOOL && left.type != LUNA_TYPE_I32) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "equality is only implemented for bool and i32");
        return luna_sema_invalid_value();
    }

    LunaIrInstruction instruction =
        luna_sema_instruction(opcode, expression->span);
    instruction.left = left.id;
    instruction.right = right.id;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &instruction, result_type);
    return (LunaCheckedValue){
        .id = result,
        .type = result_type,
    };
}

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement);

static void luna_sema_lower_block(LunaSemaContext *context,
                                  const LunaBlock *block, bool create_scope) {
    if (block == NULL) {
        return;
    }

    if (create_scope) {
        luna_sema_enter_scope(context);
    }

    for (const LunaStatement *statement = block->first;
         statement != NULL && context->reachable; statement = statement->next) {
        (void)luna_sema_lower_statement(context, statement);
    }

    if (create_scope) {
        luna_sema_leave_scope(context);
    }
}

static LunaIrOpcode luna_sema_compound_opcode(LunaTokenKind operator_kind) {
    switch (operator_kind) {
    case LUNA_TOKEN_PLUS_EQUAL:
        return LUNA_IR_ADD_I32;
    case LUNA_TOKEN_MINUS_EQUAL:
        return LUNA_IR_SUB_I32;
    case LUNA_TOKEN_STAR_EQUAL:
        return LUNA_IR_MUL_I32;
    case LUNA_TOKEN_SLASH_EQUAL:
        return LUNA_IR_DIV_I32;
    case LUNA_TOKEN_PERCENT_EQUAL:
        return LUNA_IR_REM_I32;
    case LUNA_TOKEN_AMPERSAND_EQUAL:
        return LUNA_IR_BIT_AND_I32;
    case LUNA_TOKEN_PIPE_EQUAL:
        return LUNA_IR_BIT_OR_I32;
    case LUNA_TOKEN_CARET_EQUAL:
        return LUNA_IR_BIT_XOR_I32;
    case LUNA_TOKEN_SHIFT_LEFT_EQUAL:
        return LUNA_IR_SHIFT_LEFT_I32;
    case LUNA_TOKEN_SHIFT_RIGHT_EQUAL:
        return LUNA_IR_SHIFT_RIGHT_I32;
    default:
        return LUNA_IR_ADD_I32;
    }
}

static bool luna_sema_lower_if(LunaSemaContext *context,
                               const LunaStatement *statement) {
    LunaCheckedValue condition = luna_sema_lower_expression(
        context, statement->as.if_statement.condition);
    if (!luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                                statement->as.if_statement.condition->span)) {
        return false;
    }

    const LunaIrBlockId then_block = luna_sema_add_block(context);
    const LunaIrBlockId else_block = luna_sema_add_block(context);
    const LunaIrBlockId merge_block = luna_sema_add_block(context);
    if (then_block == LUNA_IR_INVALID_ID || else_block == LUNA_IR_INVALID_ID ||
        merge_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_branch(context, condition.id, then_block, else_block,
                                statement->span);

    luna_sema_set_block(context, then_block);
    luna_sema_lower_block(context, statement->as.if_statement.then_block, true);
    const bool then_falls_through = context->reachable;
    if (then_falls_through) {
        (void)luna_sema_emit_jump(context, merge_block, statement->span);
    }

    luna_sema_set_block(context, else_block);
    if (statement->as.if_statement.else_branch != NULL) {
        (void)luna_sema_lower_statement(context,
                                        statement->as.if_statement.else_branch);
    }
    const bool else_falls_through = context->reachable;
    if (else_falls_through) {
        (void)luna_sema_emit_jump(context, merge_block, statement->span);
    }

    luna_sema_set_block(context, merge_block);
    context->reachable = then_falls_through || else_falls_through;
    return true;
}

static bool luna_sema_lower_while(LunaSemaContext *context,
                                  const LunaStatement *statement) {
    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (condition_block == LUNA_IR_INVALID_ID ||
        body_block == LUNA_IR_INVALID_ID || exit_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_jump(context, condition_block, statement->span);

    luna_sema_set_block(context, condition_block);
    LunaCheckedValue condition = luna_sema_lower_expression(
        context, statement->as.while_statement.condition);
    if (!luna_sema_require_type(
            context, condition, LUNA_TYPE_BOOL,
            statement->as.while_statement.condition->span)) {
        return false;
    }
    (void)luna_sema_emit_branch(context, condition.id, body_block, exit_block,
                                statement->span);

    const LunaSemaLoop loop = {
        .continue_block = condition_block,
        .break_block = exit_block,
    };
    if (!luna_vector_push(&context->loops, &loop)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.while_statement.body, true);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, condition_block, statement->span);
    }
    context->loops.length -= 1U;

    luna_sema_set_block(context, exit_block);
    context->reachable = true;
    return true;
}

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement) {
    switch (statement->kind) {
    case LUNA_STATEMENT_BLOCK:
        luna_sema_lower_block(context, &statement->as.block, true);
        return true;

    case LUNA_STATEMENT_DECLARATION: {
        if (statement->as.declaration.type.kind == LUNA_TYPE_VOID) {
            luna_diagnostic_error(context->diagnostics,
                                  statement->as.declaration.type.span,
                                  "local variables cannot have type void");
            return false;
        }

        if (luna_sema_find_local_in_current_scope(
                context, statement->as.declaration.name) != NULL) {
            luna_diagnostic_error(context->diagnostics, statement->span,
                                  "duplicate local variable '%.*s'",
                                  (int)statement->as.declaration.name.length,
                                  statement->as.declaration.name.data);
            return false;
        }

        LunaCheckedValue initializer = luna_sema_lower_expression(
            context, statement->as.declaration.initializer);
        if (!luna_sema_require_type(
                context, initializer, statement->as.declaration.type.kind,
                statement->as.declaration.initializer->span)) {
            return false;
        }

        const LunaIrSlotId slot = luna_ir_function_add_slot(
            context->current_function,
            luna_sema_ir_type(statement->as.declaration.type.kind));
        if (slot == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            return false;
        }

        if (!luna_sema_add_local(context, statement->as.declaration.name,
                                 statement->as.declaration.type.kind, slot,
                                 statement->as.declaration.is_mutable)) {
            return false;
        }

        LunaIrInstruction store =
            luna_sema_instruction(LUNA_IR_STORE, statement->span);
        store.slot = slot;
        store.left = initializer.id;
        return luna_sema_append_instruction(context, &store);
    }

    case LUNA_STATEMENT_ASSIGNMENT: {
        LunaSemaLocal *local =
            luna_sema_find_local(context, statement->as.assignment.name);
        if (local == NULL) {
            luna_diagnostic_error(context->diagnostics, statement->span,
                                  "unknown local variable '%.*s'",
                                  (int)statement->as.assignment.name.length,
                                  statement->as.assignment.name.data);
            return false;
        }

        if (!local->is_mutable) {
            luna_diagnostic_error(context->diagnostics, statement->span,
                                  "cannot assign to immutable local '%.*s'",
                                  (int)local->name.length, local->name.data);
            return false;
        }

        LunaCheckedValue value =
            luna_sema_lower_expression(context, statement->as.assignment.value);
        if (!luna_sema_require_type(context, value, local->type,
                                    statement->as.assignment.value->span)) {
            return false;
        }

        if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
            if (local->type != LUNA_TYPE_I32) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "compound assignment requires i32 in milestone M0");
                return false;
            }

            LunaIrInstruction load =
                luna_sema_instruction(LUNA_IR_LOAD, statement->span);
            load.slot = local->slot;
            const LunaIrValueId current =
                luna_sema_emit_value_instruction(context, &load, LUNA_TYPE_I32);

            LunaIrInstruction operation = luna_sema_instruction(
                luna_sema_compound_opcode(
                    statement->as.assignment.operator_kind),
                statement->span);
            operation.left = current;
            operation.right = value.id;
            value.id = luna_sema_emit_value_instruction(context, &operation,
                                                        LUNA_TYPE_I32);
        }

        LunaIrInstruction store =
            luna_sema_instruction(LUNA_IR_STORE, statement->span);
        store.slot = local->slot;
        store.left = value.id;
        return luna_sema_append_instruction(context, &store);
    }

    case LUNA_STATEMENT_EXPRESSION:
        (void)luna_sema_lower_expression(context, statement->as.expression);
        return true;

    case LUNA_STATEMENT_IF:
        return luna_sema_lower_if(context, statement);

    case LUNA_STATEMENT_WHILE:
        return luna_sema_lower_while(context, statement);

    case LUNA_STATEMENT_BREAK:
    case LUNA_STATEMENT_CONTINUE: {
        if (context->loops.length == 0U) {
            luna_diagnostic_error(
                context->diagnostics, statement->span,
                "%s is only valid inside a loop",
                statement->kind == LUNA_STATEMENT_BREAK ? "break" : "continue");
            return false;
        }

        const LunaSemaLoop *loop =
            luna_vector_at_const(&context->loops, context->loops.length - 1U);
        return luna_sema_emit_jump(context,
                                   statement->kind == LUNA_STATEMENT_BREAK
                                       ? loop->break_block
                                       : loop->continue_block,
                                   statement->span);
    }

    case LUNA_STATEMENT_RETURN: {
        const LunaTypeKind expected =
            context->current_syntax_function->return_type.kind;
        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_RETURN, statement->span);

        if (statement->as.return_value == NULL) {
            if (expected != LUNA_TYPE_VOID) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "function returning %s requires a return value",
                    luna_type_kind_name(expected));
                return false;
            }
        } else {
            LunaCheckedValue value =
                luna_sema_lower_expression(context, statement->as.return_value);
            if (!luna_sema_require_type(context, value, expected,
                                        statement->as.return_value->span)) {
                return false;
            }
            instruction.left = value.id;
        }

        if (!luna_sema_append_instruction(context, &instruction)) {
            return false;
        }
        context->reachable = false;
        return true;
    }
    }

    return false;
}

static bool luna_sema_collect_functions(LunaSemaContext *context) {
    for (const LunaFunction *syntax = context->program->first_function;
         syntax != NULL; syntax = syntax->next) {
        if (syntax->is_declaration) {
            luna_diagnostic_error(
                context->diagnostics, syntax->span,
                "separate module interface implementations are scheduled "
                "for milestone M2");
            continue;
        }

        if (luna_sema_find_function(context, syntax->name) != NULL) {
            luna_diagnostic_error(context->diagnostics, syntax->span,
                                  "duplicate function '%.*s'",
                                  (int)syntax->name.length, syntax->name.data);
            continue;
        }

        if (syntax->parameter_count > 6U) {
            luna_diagnostic_error(
                context->diagnostics, syntax->span,
                "milestone M0 supports at most six integer arguments");
            continue;
        }

        const LunaIrFunctionId ir_id = luna_ir_module_add_function(
            context->module, context->program->module_name, syntax->name,
            luna_sema_ir_type(syntax->return_type.kind));
        if (ir_id == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            return false;
        }

        LunaIrFunction *ir_function =
            luna_ir_module_function(context->module, ir_id);
        for (const LunaParameter *parameter = syntax->first_parameter;
             parameter != NULL; parameter = parameter->next) {
            if (parameter->type.kind == LUNA_TYPE_VOID ||
                parameter->type.kind == LUNA_TYPE_INVALID) {
                luna_diagnostic_error(
                    context->diagnostics, parameter->span,
                    "parameter '%.*s' has invalid type %s",
                    (int)parameter->name.length, parameter->name.data,
                    luna_type_kind_name(parameter->type.kind));
                continue;
            }

            const LunaIrType type = luna_sema_ir_type(parameter->type.kind);
            if (!luna_vector_push(&ir_function->parameter_types, &type)) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
            if (luna_ir_function_add_slot(ir_function, type) ==
                LUNA_IR_INVALID_ID) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
        }

        const LunaSemaFunction function = {
            .syntax = syntax,
            .ir_id = ir_id,
        };
        if (!luna_vector_push(&context->functions, &function)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
    }

    return true;
}

static bool luna_sema_find_entry(LunaSemaContext *context) {
    LunaSemaFunction *entry = luna_sema_find_function(
        context, luna_string_view_from_c_string("main"));
    if (entry == NULL) {
        luna_diagnostic_error_plain(context->diagnostics,
                                    "program has no main function");
        return false;
    }

    if (entry->syntax->parameter_count != 0U ||
        entry->syntax->return_type.kind != LUNA_TYPE_I32) {
        luna_diagnostic_error(
            context->diagnostics, entry->syntax->span,
            "milestone M0 entry point must be 'fn main() -> i32'");
        return false;
    }

    context->module->entry_function = entry->ir_id;
    return true;
}

static void luna_sema_lower_function(LunaSemaContext *context,
                                     LunaSemaFunction *function) {
    context->current_function =
        luna_ir_module_function(context->module, function->ir_id);
    context->current_syntax_function = function->syntax;
    context->locals.length = 0U;
    context->loops.length = 0U;
    context->scope_depth = 0U;

    const LunaIrBlockId entry_block = luna_sema_add_block(context);
    if (entry_block == LUNA_IR_INVALID_ID) {
        return;
    }
    context->current_block = entry_block;
    context->reachable = true;

    uint32_t parameter_index = 0U;
    for (const LunaParameter *parameter = function->syntax->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        if (luna_sema_find_local_in_current_scope(context, parameter->name) !=
            NULL) {
            luna_diagnostic_error(context->diagnostics, parameter->span,
                                  "duplicate parameter '%.*s'",
                                  (int)parameter->name.length,
                                  parameter->name.data);
            continue;
        }

        (void)luna_sema_add_local(context, parameter->name,
                                  parameter->type.kind, parameter_index, false);
        parameter_index += 1U;
    }

    luna_sema_lower_block(context, function->syntax->body, true);

    if (context->reachable) {
        if (function->syntax->return_type.kind == LUNA_TYPE_VOID) {
            LunaIrInstruction return_instruction =
                luna_sema_instruction(LUNA_IR_RETURN, function->syntax->span);
            (void)luna_sema_append_instruction(context, &return_instruction);
            context->reachable = false;
        } else {
            luna_diagnostic_error(
                context->diagnostics, function->syntax->span,
                "not every path in function '%.*s' returns %s",
                (int)function->syntax->name.length, function->syntax->name.data,
                luna_type_kind_name(function->syntax->return_type.kind));
        }
    }
}

bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module) {
    LunaSemaContext context = {
        .program = program,
        .diagnostics = diagnostics,
        .module = module,
    };
    luna_vector_init(&context.functions, sizeof(LunaSemaFunction));
    luna_vector_init(&context.locals, sizeof(LunaSemaLocal));
    luna_vector_init(&context.loops, sizeof(LunaSemaLoop));

    if (program->is_interface) {
        luna_diagnostic_error(
            diagnostics, program->module_span,
            "module interface compilation is scheduled for milestone M2");
    }

    if (program->first_import != NULL) {
        luna_diagnostic_error(
            diagnostics, program->first_import->span,
            "cross-module import resolution is scheduled for milestone M2");
    }

    (void)luna_sema_collect_functions(&context);
    (void)luna_sema_find_entry(&context);

    if (luna_diagnostic_error_count(diagnostics) == 0U) {
        for (size_t index = 0U; index < context.functions.length; index += 1U) {
            LunaSemaFunction *function =
                luna_vector_at(&context.functions, index);
            luna_sema_lower_function(&context, function);
        }
    }

    const bool success = luna_diagnostic_error_count(diagnostics) == 0U &&
                         !context.allocation_failed;

    luna_vector_destroy(&context.functions);
    luna_vector_destroy(&context.locals);
    luna_vector_destroy(&context.loops);
    return success;
}
