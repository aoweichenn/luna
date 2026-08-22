#include "sema_internal.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement);

static bool luna_sema_lower_dead_expression(LunaSemaContext *context,
                                            LunaIrBlockId block_id,
                                            const LunaExpression *expression,
                                            LunaSemaTypeId expected_type,
                                            LunaSourceSpan span) {
    const bool was_checking_dead_code = context->checking_dead_code;
    context->current_block = block_id;
    context->reachable = true;
    context->checking_dead_code = true;

    LunaCheckedValue value =
        luna_sema_lower_expression_expected(context, expression, expected_type);
    luna_sema_require_type(context, value, expected_type, expression->span);
    const bool success = !context->failed;
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, context->current_block, span);
    }

    context->checking_dead_code = was_checking_dead_code;
    context->reachable = false;
    return success;
}

static bool luna_sema_lower_dead_statement(LunaSemaContext *context,
                                           LunaIrBlockId block_id,
                                           const LunaStatement *statement,
                                           LunaSourceSpan span) {
    const bool was_checking_dead_code = context->checking_dead_code;
    context->current_block = block_id;
    context->reachable = true;
    context->checking_dead_code = true;

    const bool success = luna_sema_lower_statement(context, statement);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, context->current_block, span);
    }

    context->checking_dead_code = was_checking_dead_code;
    context->reachable = false;
    return success;
}

void luna_sema_lower_block(LunaSemaContext *context, const LunaBlock *block,
                           bool create_scope) {
    if (block == NULL) {
        return;
    }

    if (create_scope) {
        luna_sema_enter_scope(context);
    }

    bool owns_dead_region = false;
    for (const LunaStatement *statement = block->first; statement != NULL;
         statement = statement->next) {
        if (!context->reachable) {
            const LunaIrBlockId dead_block = luna_sema_add_block(context);
            if (dead_block == LUNA_IR_INVALID_ID) {
                break;
            }

            context->current_block = dead_block;
            context->reachable = true;
            if (!context->checking_dead_code) {
                context->checking_dead_code = true;
                owns_dead_region = true;
            }
        }

        (void)luna_sema_lower_statement(context, statement);
    }

    if (owns_dead_region) {
        if (context->reachable) {
            (void)luna_sema_emit_jump(context, context->current_block,
                                      block->span);
        }
        context->checking_dead_code = false;
        context->reachable = false;
    }

    if (create_scope) {
        luna_sema_leave_scope(context);
    }
}

static bool luna_sema_lower_if(LunaSemaContext *context,
                               const LunaStatement *statement) {
    LunaCheckedValue condition = luna_sema_lower_expression_expected(
        context, statement->as.if_statement.condition, LUNA_TYPE_BOOL);
    luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                           statement->as.if_statement.condition->span);
    if (context->failed) {
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
    LunaCheckedValue condition = luna_sema_lower_expression_expected(
        context, statement->as.while_statement.condition, LUNA_TYPE_BOOL);
    luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                           statement->as.while_statement.condition->span);
    if (context->failed) {
        return false;
    }
    (void)luna_sema_emit_branch(context, condition.id, body_block, exit_block,
                                statement->span);

    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = condition_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.while_statement.body, true);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, condition_block, statement->span);
    }
    context->control_frames.length -= 1U;

    luna_sema_set_block(context, exit_block);
    context->reachable = true;
    return true;
}

static bool luna_sema_lower_do(LunaSemaContext *context,
                               const LunaStatement *statement) {
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (body_block == LUNA_IR_INVALID_ID ||
        condition_block == LUNA_IR_INVALID_ID ||
        exit_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_jump(context, body_block, statement->span);
    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = condition_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.do_statement.body, true);
    const bool body_falls_through = context->reachable;
    if (body_falls_through) {
        (void)luna_sema_emit_jump(context, condition_block, statement->span);
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool condition_is_live =
        body_falls_through || completed_frame->has_live_continue;
    const bool exit_is_live =
        completed_frame->has_live_break || condition_is_live;
    context->control_frames.length -= 1U;

    if (condition_is_live) {
        luna_sema_set_block(context, condition_block);
        LunaCheckedValue condition = luna_sema_lower_expression_expected(
            context, statement->as.do_statement.condition, LUNA_TYPE_BOOL);
        luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                               statement->as.do_statement.condition->span);
        if (context->failed) {
            return false;
        }
        (void)luna_sema_emit_branch(context, condition.id, body_block,
                                    exit_block, statement->span);
    } else if (!luna_sema_lower_dead_expression(
                   context, condition_block,
                   statement->as.do_statement.condition, LUNA_TYPE_BOOL,
                   statement->span)) {
        return false;
    }

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    return true;
}

static bool luna_sema_lower_for(LunaSemaContext *context,
                                const LunaStatement *statement) {
    luna_sema_enter_scope(context);
    if (statement->as.for_statement.initializer != NULL &&
        !luna_sema_lower_statement(context,
                                   statement->as.for_statement.initializer)) {
        luna_sema_leave_scope(context);
        return false;
    }

    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId update_block =
        statement->as.for_statement.update == NULL
            ? LUNA_IR_INVALID_ID
            : luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (condition_block == LUNA_IR_INVALID_ID ||
        body_block == LUNA_IR_INVALID_ID ||
        (statement->as.for_statement.update != NULL &&
         update_block == LUNA_IR_INVALID_ID) ||
        exit_block == LUNA_IR_INVALID_ID) {
        luna_sema_leave_scope(context);
        return false;
    }

    (void)luna_sema_emit_jump(context, condition_block, statement->span);
    luna_sema_set_block(context, condition_block);
    const bool condition_can_exit =
        statement->as.for_statement.condition != NULL;
    if (condition_can_exit) {
        LunaCheckedValue condition = luna_sema_lower_expression_expected(
            context, statement->as.for_statement.condition, LUNA_TYPE_BOOL);
        luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                               statement->as.for_statement.condition->span);
        if (context->failed) {
            luna_sema_leave_scope(context);
            return false;
        }
        (void)luna_sema_emit_branch(context, condition.id, body_block,
                                    exit_block, statement->span);
    } else {
        (void)luna_sema_emit_jump(context, body_block, statement->span);
    }

    const LunaIrBlockId continue_block =
        update_block == LUNA_IR_INVALID_ID ? condition_block : update_block;
    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = continue_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        luna_sema_leave_scope(context);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.for_statement.body, true);
    const bool body_falls_through = context->reachable;
    if (body_falls_through) {
        (void)luna_sema_emit_jump(context, continue_block, statement->span);
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool update_is_live =
        body_falls_through || completed_frame->has_live_continue;
    const bool exit_is_live =
        condition_can_exit || completed_frame->has_live_break;
    context->control_frames.length -= 1U;

    if (update_block != LUNA_IR_INVALID_ID) {
        if (update_is_live) {
            luna_sema_set_block(context, update_block);
            if (!luna_sema_lower_statement(
                    context, statement->as.for_statement.update)) {
                luna_sema_leave_scope(context);
                return false;
            }
            if (context->reachable) {
                (void)luna_sema_emit_jump(context, condition_block,
                                          statement->span);
            }
        } else if (!luna_sema_lower_dead_statement(
                       context, update_block,
                       statement->as.for_statement.update, statement->span)) {
            luna_sema_leave_scope(context);
            return false;
        }
    }

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    luna_sema_leave_scope(context);
    return true;
}

static bool luna_sema_switch_label_value(LunaSemaContext *context,
                                         const LunaExpression *expression,
                                         LunaSemaTypeId switch_type,
                                         uint64_t *value) {
    if (expression == NULL) {
        luna_sema_fail_plain(context, "switch case label is missing");
        return false;
    }

    if (luna_sema_is_enum_type(context, switch_type)) {
        LunaSemaTypeId label_type = LUNA_TYPE_INVALID;
        const LunaSemaEnumMember *member =
            luna_sema_scoped_enum_member(context, expression, &label_type);
        if (member == NULL || label_type != switch_type) {
            luna_sema_fail(
                context, expression->span,
                "enum switch case label must be a member of the controlling "
                "enum");
            return false;
        }
        *value = member->value;
        return true;
    }

    bool is_negative = false;
    const LunaExpression *literal = expression;
    if (literal->kind == LUNA_EXPRESSION_UNARY) {
        if (literal->as.unary.operator_kind != LUNA_TOKEN_PLUS &&
            literal->as.unary.operator_kind != LUNA_TOKEN_MINUS) {
            luna_sema_fail(context, expression->span,
                           "expected integer literal as switch case label");
            return false;
        }
        is_negative = literal->as.unary.operator_kind == LUNA_TOKEN_MINUS;
        literal = literal->as.unary.operand;
    }

    if (literal == NULL || literal->kind != LUNA_EXPRESSION_INTEGER) {
        luna_sema_fail(context, expression->span,
                       "expected integer literal as switch case label");
        return false;
    }

    const LunaTypeKind switch_kind = luna_sema_type_kind(context, switch_type);
    const uint32_t width = luna_type_kind_bit_width(
        switch_kind, &context->module->target->data_layout);
    if (width == 0U || width > 64U) {
        luna_sema_fail(context, expression->span,
                       "switch has invalid controlling integer type");
        return false;
    }
    const uint64_t mask =
        width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    const uint64_t magnitude = literal->as.integer;
    uint64_t maximum = luna_sema_integer_maximum(context, switch_type);
    if (is_negative && luna_type_kind_is_signed_integer(switch_kind)) {
        maximum = UINT64_C(1) << (width - 1U);
    }
    if (magnitude > maximum) {
        luna_sema_fail(context, expression->span,
                       "switch case label does not fit in %s",
                       luna_type_kind_name(switch_kind));
        return false;
    }

    *value = is_negative ? (UINT64_C(0) - magnitude) & mask : magnitude;
    return true;
}

static bool luna_sema_lower_switch(LunaSemaContext *context,
                                   const LunaStatement *statement) {
    LunaCheckedValue controlling = luna_sema_lower_expression(
        context, statement->as.switch_statement.expression);
    if (!luna_sema_is_integer_type(controlling.type) &&
        !luna_sema_is_enum_type(context, controlling.type)) {
        luna_sema_fail(context, statement->as.switch_statement.expression->span,
                       "switch expression requires an integer type or "
                       "scoped enum");
        return false;
    }

    const LunaIrSlotId controlling_slot =
        luna_ir_function_add_slot(context->current_function,
                                  luna_sema_ir_type(context, controlling.type));
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (controlling_slot == LUNA_IR_INVALID_ID ||
        exit_block == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    LunaIrInstruction controlling_store =
        luna_sema_instruction(LUNA_IR_STORE, statement->span);
    controlling_store.slot = controlling_slot;
    controlling_store.left = controlling.id;
    luna_sema_append_instruction(context, &controlling_store);
    if (context->failed) {
        return false;
    }

    LunaVector arms;
    LunaVector labels;
    luna_vector_init(&arms, sizeof(LunaSemaSwitchArm));
    luna_vector_init(&labels, sizeof(LunaSemaSwitchLabel));
    LunaIrBlockId default_block = LUNA_IR_INVALID_ID;
    bool valid = true;

    for (const LunaSwitchArm *arm = statement->as.switch_statement.first_arm;
         arm != NULL; arm = arm->next) {
        const LunaIrBlockId body_block = luna_sema_add_block(context);
        if (body_block == LUNA_IR_INVALID_ID) {
            valid = false;
            break;
        }
        const LunaSemaSwitchArm lowered_arm = {
            .syntax = arm,
            .body_block = body_block,
        };
        if (!luna_vector_push(&arms, &lowered_arm)) {
            luna_sema_report_allocation_failure(context);
            valid = false;
            break;
        }

        if (arm->is_default) {
            if (default_block != LUNA_IR_INVALID_ID) {
                luna_sema_fail(context, arm->span,
                               "switch has more than one default arm");
                valid = false;
                continue;
            }
            default_block = body_block;
            continue;
        }

        if (arm->first_label == NULL) {
            luna_sema_fail(context, arm->span,
                           "switch case requires at least one label");
            valid = false;
            continue;
        }

        for (const LunaExpression *label = arm->first_label; label != NULL;
             label = label->next) {
            LunaSemaSwitchLabel lowered_label = {
                .span = label->span,
                .body_block = body_block,
            };
            if (!luna_sema_switch_label_value(context, label, controlling.type,
                                              &lowered_label.value)) {
                valid = false;
                continue;
            }

            for (size_t index = 0U; index < labels.length; index += 1U) {
                const LunaSemaSwitchLabel *existing =
                    luna_vector_at_const(&labels, index);
                if (existing->value == lowered_label.value) {
                    luna_sema_fail(context, label->span,
                                   "duplicate switch case value for type %s",
                                   luna_type_kind_name(luna_sema_type_kind(
                                       context, controlling.type)));
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }
            if (!luna_vector_push(&labels, &lowered_label)) {
                luna_sema_report_allocation_failure(context);
                valid = false;
                break;
            }
        }
    }

    if (!valid) {
        luna_vector_destroy(&labels);
        luna_vector_destroy(&arms);
        return false;
    }

    const LunaIrBlockId no_match_block =
        default_block == LUNA_IR_INVALID_ID ? exit_block : default_block;
    if (labels.length == 0U) {
        (void)luna_sema_emit_jump(context, no_match_block, statement->span);
    } else {
        for (size_t index = 0U; index < labels.length; index += 1U) {
            const LunaSemaSwitchLabel *label =
                luna_vector_at_const(&labels, index);
            const bool is_last = index + 1U == labels.length;
            const LunaIrBlockId next_block =
                is_last ? no_match_block : luna_sema_add_block(context);
            if (next_block == LUNA_IR_INVALID_ID) {
                luna_vector_destroy(&labels);
                luna_vector_destroy(&arms);
                return false;
            }

            LunaIrInstruction load =
                luna_sema_instruction(LUNA_IR_LOAD, label->span);
            load.slot = controlling_slot;
            const LunaIrValueId loaded = luna_sema_emit_value_instruction(
                context, &load, controlling.type);

            LunaIrInstruction constant =
                luna_sema_instruction(LUNA_IR_CONST_INTEGER, label->span);
            constant.immediate = label->value;
            const LunaIrValueId expected = luna_sema_emit_value_instruction(
                context, &constant, controlling.type);

            LunaIrInstruction comparison =
                luna_sema_instruction(LUNA_IR_COMPARE_EQUAL, label->span);
            comparison.left = loaded;
            comparison.right = expected;
            const LunaIrValueId matches = luna_sema_emit_value_instruction(
                context, &comparison, LUNA_TYPE_BOOL);
            (void)luna_sema_emit_branch(context, matches, label->body_block,
                                        next_block, label->span);
            if (!is_last) {
                luna_sema_set_block(context, next_block);
            }
        }
    }

    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = LUNA_IR_INVALID_ID,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        luna_vector_destroy(&labels);
        luna_vector_destroy(&arms);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;
    bool arm_falls_through = false;
    for (size_t index = 0U; index < arms.length; index += 1U) {
        const LunaSemaSwitchArm *arm = luna_vector_at_const(&arms, index);
        luna_sema_set_block(context, arm->body_block);
        luna_sema_lower_block(context, arm->syntax->body, true);
        if (context->reachable) {
            arm_falls_through = true;
            (void)luna_sema_emit_jump(context, exit_block, arm->syntax->span);
        }
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool exit_is_live = default_block == LUNA_IR_INVALID_ID ||
                              arm_falls_through ||
                              completed_frame->has_live_break;
    context->control_frames.length -= 1U;
    luna_vector_destroy(&labels);
    luna_vector_destroy(&arms);

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    return true;
}

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement) {
    switch (statement->kind) {
    case LUNA_STATEMENT_BLOCK:
        luna_sema_lower_block(context, &statement->as.block, true);
        return true;

    case LUNA_STATEMENT_DECLARATION: {
        const LunaSemaTypeId declared_type =
            luna_sema_resolve_type(context, &statement->as.declaration.type);
        if (declared_type == LUNA_TYPE_INVALID) {
            return false;
        }
        if (declared_type == LUNA_TYPE_VOID) {
            luna_sema_fail(context, statement->as.declaration.type.span,
                           "local variables cannot have type void");
            return false;
        }

        if (luna_sema_find_local_in_current_scope(
                context, statement->as.declaration.name) != NULL) {
            luna_sema_fail(context, statement->span,
                           "duplicate local variable '%.*s'",
                           (int)statement->as.declaration.name.length,
                           statement->as.declaration.name.data);
            return false;
        }

        if (luna_sema_is_memory_type(context, declared_type)) {
            const LunaExpression *initializer =
                statement->as.declaration.initializer;
            const LunaIrSlotId slot = luna_sema_allocate_memory_slot(
                context, declared_type, statement->as.declaration.type.span);
            if (slot == LUNA_IR_INVALID_ID) {
                return false;
            }

            if (luna_sema_is_brace_initializer(initializer)) {
                luna_sema_zero_memory_slot(context, slot, initializer->span);
                if (!luna_sema_initialize_slot_value(
                        context, slot, declared_type, declared_type, 0U,
                        initializer)) {
                    return false;
                }
            } else {
                const LunaCheckedLvalue source = luna_sema_lower_memory_source(
                    context, initializer, declared_type);
                if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
                    return false;
                }
                const LunaCheckedLvalue destination = {
                    .type = declared_type,
                    .storage = LUNA_SEMA_LVALUE_SLOT,
                    .slot = slot,
                    .address = LUNA_IR_INVALID_ID,
                    .is_mutable = true,
                };
                luna_sema_emit_memory_copy(context, &destination, &source,
                                           initializer->span);
                if (context->failed) {
                    return false;
                }
            }

            return luna_sema_add_local(context, statement->as.declaration.name,
                                       declared_type, slot,
                                       statement->as.declaration.is_mutable);
        }

        LunaCheckedValue initializer = luna_sema_lower_expression_expected(
            context, statement->as.declaration.initializer, declared_type);
        luna_sema_require_type(context, initializer, declared_type,
                               statement->as.declaration.initializer->span);
        if (context->failed) {
            return false;
        }

        const LunaIrSlotId slot = luna_ir_function_add_slot(
            context->current_function,
            luna_sema_ir_type(context, declared_type));
        if (slot == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            return false;
        }

        if (!luna_sema_add_local(context, statement->as.declaration.name,
                                 declared_type, slot,
                                 statement->as.declaration.is_mutable)) {
            return false;
        }

        LunaIrInstruction store =
            luna_sema_instruction(LUNA_IR_STORE, statement->span);
        store.slot = slot;
        store.left = initializer.id;
        luna_sema_append_instruction(context, &store);
        return !context->failed;
    }

    case LUNA_STATEMENT_ASSIGNMENT: {
        LunaCheckedLvalue lvalue =
            luna_sema_lower_lvalue(context, statement->as.assignment.target);
        if (lvalue.storage == LUNA_SEMA_LVALUE_INVALID) {
            return false;
        }
        if (!lvalue.is_mutable) {
            if (statement->as.assignment.target->kind == LUNA_EXPRESSION_NAME) {
                const LunaStringView name =
                    statement->as.assignment.target->as.name;
                luna_sema_fail(context, statement->span,
                               "cannot assign to immutable local '%.*s'",
                               (int)name.length, name.data);
            } else {
                luna_sema_fail(context, statement->span,
                               "cannot assign through an immutable lvalue");
            }
            return false;
        }
        if (luna_sema_is_memory_type(context, lvalue.type)) {
            if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
                luna_sema_fail(
                    context, statement->span,
                    "compound assignment requires a scalar numeric type");
                return false;
            }

            LunaIrSlotId preserved_address = LUNA_IR_INVALID_ID;
            if (lvalue.storage == LUNA_SEMA_LVALUE_ADDRESS &&
                luna_sema_expression_can_branch(
                    statement->as.assignment.value)) {
                const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                    context, lvalue.type, !lvalue.is_mutable);
                preserved_address = luna_sema_preserve_value(
                    context,
                    (LunaCheckedValue){
                        .id = lvalue.address,
                        .type = pointer_type,
                    },
                    statement->as.assignment.target->span);
                if (preserved_address == LUNA_IR_INVALID_ID) {
                    return false;
                }
            }

            const LunaCheckedLvalue source =
                luna_sema_lower_memory_assignment_value(
                    context, statement->as.assignment.value, lvalue.type);
            if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
                return false;
            }

            if (preserved_address != LUNA_IR_INVALID_ID) {
                const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                    context, lvalue.type, !lvalue.is_mutable);
                lvalue.address = luna_sema_reload_value(
                                     context, preserved_address, pointer_type,
                                     statement->as.assignment.target->span)
                                     .id;
            }
            luna_sema_emit_memory_copy(context, &lvalue, &source,
                                       statement->span);
            return !context->failed;
        }

        LunaIrSlotId preserved_address = LUNA_IR_INVALID_ID;
        if (lvalue.storage == LUNA_SEMA_LVALUE_ADDRESS &&
            luna_sema_expression_can_branch(statement->as.assignment.value)) {
            const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                context, lvalue.type, !lvalue.is_mutable);
            preserved_address =
                luna_sema_preserve_value(context,
                                         (LunaCheckedValue){
                                             .id = lvalue.address,
                                             .type = pointer_type,
                                         },
                                         statement->as.assignment.target->span);
            if (preserved_address == LUNA_IR_INVALID_ID) {
                return false;
            }
        }

        LunaCheckedValue current = luna_sema_invalid_value();
        LunaIrSlotId current_slot = LUNA_IR_INVALID_ID;
        if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
            if (!luna_sema_is_numeric_type(lvalue.type)) {
                luna_sema_fail(context, statement->span,
                               "compound assignment requires a numeric type");
                return false;
            }

            current = luna_sema_load_lvalue(context, &lvalue, statement->span);
            if (current.type == LUNA_TYPE_INVALID) {
                return false;
            }
            lvalue.requires_null_check = false;

            current_slot = luna_ir_function_add_slot(
                context->current_function,
                luna_sema_ir_type(context, lvalue.type));
            if (current_slot == LUNA_IR_INVALID_ID) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
            LunaIrInstruction preserve =
                luna_sema_instruction(LUNA_IR_STORE, statement->span);
            preserve.slot = current_slot;
            preserve.left = current.id;
            luna_sema_append_instruction(context, &preserve);
            if (context->failed) {
                return false;
            }
        }

        LunaCheckedValue value = luna_sema_lower_expression_expected(
            context, statement->as.assignment.value, lvalue.type);
        luna_sema_require_type(context, value, lvalue.type,
                               statement->as.assignment.value->span);
        if (context->failed) {
            return false;
        }

        if (preserved_address != LUNA_IR_INVALID_ID) {
            const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                context, lvalue.type, !lvalue.is_mutable);
            lvalue.address =
                luna_sema_reload_value(context, preserved_address, pointer_type,
                                       statement->as.assignment.target->span)
                    .id;
        }

        if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
            LunaIrInstruction reload =
                luna_sema_instruction(LUNA_IR_LOAD, statement->span);
            reload.slot = current_slot;
            current.id =
                luna_sema_emit_value_instruction(context, &reload, lvalue.type);

            LunaIrOpcode opcode = LUNA_IR_ADD_INTEGER;
            if (luna_sema_is_float_type(lvalue.type) &&
                !luna_sema_binary_float_opcode(
                    statement->as.assignment.operator_kind, &opcode)) {
                luna_sema_fail(
                    context, statement->span,
                    "operator %s is not defined for floating-point operands",
                    luna_token_kind_name(
                        statement->as.assignment.operator_kind));
                return false;
            }
            if (luna_sema_is_integer_type(lvalue.type)) {
                opcode = luna_sema_binary_integer_opcode(
                    statement->as.assignment.operator_kind);
            }

            LunaIrInstruction operation =
                luna_sema_instruction(opcode, statement->span);
            operation.left = current.id;
            operation.right = value.id;
            value.id = luna_sema_emit_value_instruction(context, &operation,
                                                        lvalue.type);
        }

        luna_sema_store_lvalue(context, &lvalue, value, statement->span);
        return !context->failed;
    }

    case LUNA_STATEMENT_EXPRESSION:
        (void)luna_sema_lower_expression(context, statement->as.expression);
        return true;

    case LUNA_STATEMENT_IF:
        return luna_sema_lower_if(context, statement);

    case LUNA_STATEMENT_WHILE:
        return luna_sema_lower_while(context, statement);

    case LUNA_STATEMENT_DO:
        return luna_sema_lower_do(context, statement);

    case LUNA_STATEMENT_FOR:
        return luna_sema_lower_for(context, statement);

    case LUNA_STATEMENT_SWITCH:
        return luna_sema_lower_switch(context, statement);

    case LUNA_STATEMENT_BREAK:
    case LUNA_STATEMENT_CONTINUE: {
        if (statement->kind == LUNA_STATEMENT_BREAK) {
            if (context->control_frames.length == 0U) {
                luna_sema_fail(context, statement->span,
                               "break is only valid inside a loop or switch");
                return false;
            }

            LunaSemaControlFrame *frame = luna_vector_at(
                &context->control_frames, context->control_frames.length - 1U);
            if (!context->checking_dead_code) {
                frame->has_live_break = true;
            }
            luna_sema_emit_jump(context, frame->break_block, statement->span);
            return !context->failed;
        }

        for (size_t index = context->control_frames.length; index > 0U;
             index -= 1U) {
            LunaSemaControlFrame *frame =
                luna_vector_at(&context->control_frames, index - 1U);
            if (frame->continue_block == LUNA_IR_INVALID_ID) {
                continue;
            }
            if (!context->checking_dead_code) {
                frame->has_live_continue = true;
            }
            luna_sema_emit_jump(context, frame->continue_block,
                                statement->span);
            return !context->failed;
        }

        luna_sema_fail(context, statement->span,
                       "continue is only valid inside a loop");
        return false;
    }

    case LUNA_STATEMENT_RETURN: {
        const LunaSemaTypeId expected =
            context->current_semantic_function->return_type;
        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_RETURN, statement->span);

        if (statement->as.return_value == NULL) {
            if (expected != LUNA_TYPE_VOID) {
                luna_sema_fail(context, statement->span,
                               "non-void function requires a return value");
                return false;
            }
        } else if (luna_sema_is_memory_type(context, expected)) {
            const LunaCheckedLvalue source =
                luna_sema_lower_memory_assignment_value(
                    context, statement->as.return_value, expected);
            const LunaIrSlotId snapshot_slot = luna_sema_allocate_memory_slot(
                context, expected, statement->as.return_value->span);
            const LunaCheckedLvalue snapshot = {
                .type = expected,
                .storage = LUNA_SEMA_LVALUE_SLOT,
                .slot = snapshot_slot,
                .address = LUNA_IR_INVALID_ID,
                .is_mutable = true,
            };
            if (source.storage == LUNA_SEMA_LVALUE_INVALID ||
                snapshot_slot == LUNA_IR_INVALID_ID) {
                return false;
            }
            luna_sema_emit_memory_copy(context, &snapshot, &source,
                                       statement->as.return_value->span);
            if (context->failed) {
                return false;
            }
            instruction.left = luna_sema_lvalue_address(
                context, &snapshot, statement->as.return_value->span);
            if (instruction.left == LUNA_IR_INVALID_ID) {
                return false;
            }
        } else {
            LunaCheckedValue value = luna_sema_lower_expression_expected(
                context, statement->as.return_value, expected);
            luna_sema_require_type(context, value, expected,
                                   statement->as.return_value->span);
            if (context->failed) {
                return false;
            }
            instruction.left = value.id;
        }

        luna_sema_append_instruction(context, &instruction);
        if (context->failed) {
            return false;
        }
        context->reachable = false;
        return true;
    }
    }

    return false;
}
