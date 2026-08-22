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

LunaIrInstruction luna_sema_instruction(LunaIrOpcode opcode,
                                        LunaSourceSpan span) {
    return (LunaIrInstruction){
        .opcode = opcode,
        .type = LUNA_IR_TYPE_VOID,
        .memory_type = LUNA_IR_TYPE_VOID,
        .result = LUNA_IR_INVALID_ID,
        .left = LUNA_IR_INVALID_ID,
        .right = LUNA_IR_INVALID_ID,
        .slot = LUNA_IR_INVALID_ID,
        .true_block = LUNA_IR_INVALID_ID,
        .false_block = LUNA_IR_INVALID_ID,
        .callee = LUNA_IR_INVALID_ID,
        .global = LUNA_IR_INVALID_ID,
        .first_argument = 0U,
        .argument_count = 0U,
        .immediate = 0U,
        .span = span,
    };
}

static LunaIrBlock *luna_sema_current_block(LunaSemaContext *context) {
    return luna_ir_function_block(context->current_function,
                                  context->current_block);
}

void luna_sema_append_instruction(LunaSemaContext *context,
                                  const LunaIrInstruction *instruction) {
    if (context->failed) {
        return;
    }
    if (!context->reachable ||
        !luna_ir_block_append(luna_sema_current_block(context), instruction)) {
        luna_sema_report_allocation_failure(context);
    }
}

LunaIrBlockId luna_sema_add_block(LunaSemaContext *context) {
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }
    const LunaIrBlockId block_id =
        luna_ir_function_add_block(context->current_function);
    if (block_id == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return block_id;
}

static LunaIrValueId luna_sema_add_value(LunaSemaContext *context,
                                         LunaSemaTypeId type) {
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }
    const LunaIrValueId value = luna_ir_function_add_value(
        context->current_function, luna_sema_ir_type(context, type));
    if (value == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return value;
}

static void luna_sema_add_predecessor(LunaSemaContext *context,
                                      LunaIrBlockId block_id) {
    if (context->failed) {
        return;
    }
    LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    if (block == NULL || block->predecessor_count == UINT32_MAX) {
        luna_sema_report_allocation_failure(context);
        return;
    }

    block->predecessor_count += 1U;
}

void luna_sema_emit_jump(LunaSemaContext *context, LunaIrBlockId target,
                         LunaSourceSpan span) {
    if (context->failed) {
        return;
    }
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_JUMP, span);
    instruction.true_block = target;

    luna_sema_append_instruction(context, &instruction);
    luna_sema_add_predecessor(context, target);

    if (!context->failed) {
        context->reachable = false;
    }
}

void luna_sema_emit_branch(LunaSemaContext *context, LunaIrValueId condition,
                           LunaIrBlockId true_block, LunaIrBlockId false_block,
                           LunaSourceSpan span) {
    if (context->failed) {
        return;
    }
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_BRANCH, span);
    instruction.left = condition;
    instruction.true_block = true_block;
    instruction.false_block = false_block;

    luna_sema_append_instruction(context, &instruction);
    luna_sema_add_predecessor(context, true_block);
    luna_sema_add_predecessor(context, false_block);

    if (!context->failed) {
        context->reachable = false;
    }
}

void luna_sema_set_block(LunaSemaContext *context, LunaIrBlockId block_id) {
    context->current_block = block_id;
    const LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    context->reachable =
        block_id == 0U || (block != NULL && block->predecessor_count > 0U);
}

LunaIrValueId luna_sema_emit_value_instruction(LunaSemaContext *context,
                                               LunaIrInstruction *instruction,
                                               LunaSemaTypeId type) {
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }
    instruction->type = luna_sema_ir_type(context, type);
    instruction->result = luna_sema_add_value(context, type);
    luna_sema_append_instruction(context, instruction);
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }

    return instruction->result;
}

LunaIrSlotId luna_sema_preserve_value(LunaSemaContext *context,
                                      LunaCheckedValue value,
                                      LunaSourceSpan span) {
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }
    const LunaIrSlotId slot = luna_ir_function_add_slot(
        context->current_function, luna_sema_ir_type(context, value.type));
    if (slot == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return slot;
    }

    LunaIrInstruction store = luna_sema_instruction(LUNA_IR_STORE, span);
    store.slot = slot;
    store.left = value.id;
    luna_sema_append_instruction(context, &store);
    if (context->failed) {
        return LUNA_IR_INVALID_ID;
    }
    return slot;
}

LunaCheckedValue luna_sema_reload_value(LunaSemaContext *context,
                                        LunaIrSlotId slot, LunaSemaTypeId type,
                                        LunaSourceSpan span) {
    LunaIrInstruction load = luna_sema_instruction(LUNA_IR_LOAD, span);
    load.slot = slot;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, type);
    return (LunaCheckedValue){
        .id = result,
        .type = type,
    };
}
