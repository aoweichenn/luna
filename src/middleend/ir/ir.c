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

void luna_ir_aggregate_layout_init(LunaIrAggregateLayout *layout,
                                   bool is_aggregate, uint64_t size_bytes,
                                   uint32_t alignment_bytes) {
    if (layout == NULL) {
        return;
    }
    *layout = (LunaIrAggregateLayout){
        .is_aggregate = is_aggregate,
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
    };
    luna_vector_init(&layout->components, sizeof(LunaIrAggregateComponent));
}

void luna_ir_aggregate_layout_destroy(LunaIrAggregateLayout *layout) {
    if (layout == NULL) {
        return;
    }
    luna_vector_destroy(&layout->components);
    layout->is_aggregate = false;
    layout->size_bytes = 0U;
    layout->alignment_bytes = 0U;
}

bool luna_ir_aggregate_layout_add_component(LunaIrAggregateLayout *layout,
                                            uint64_t offset_bytes,
                                            uint64_t size_bytes,
                                            uint32_t alignment_bytes,
                                            LunaIrType type) {
    const LunaIrAggregateComponent component = {
        .offset_bytes = offset_bytes,
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
        .type = type,
    };
    return layout != NULL && layout->is_aggregate &&
           layout->components.element_size ==
               sizeof(LunaIrAggregateComponent) &&
           luna_vector_push(&layout->components, &component);
}

static void luna_ir_function_destroy(LunaIrFunction *function) {
    for (size_t index = 0U; index < function->blocks.length; index += 1U) {
        LunaIrBlock *block = luna_vector_at(&function->blocks, index);
        luna_vector_destroy(&block->instructions);
    }

    for (size_t index = 0U; index < function->parameter_aggregates.length;
         index += 1U) {
        LunaIrAggregateLayout *layout =
            luna_vector_at(&function->parameter_aggregates, index);
        luna_ir_aggregate_layout_destroy(layout);
    }
    luna_ir_aggregate_layout_destroy(&function->return_aggregate);
    luna_vector_destroy(&function->parameter_aggregates);
    luna_vector_destroy(&function->parameter_types);
    luna_vector_destroy(&function->slots);
    luna_vector_destroy(&function->value_types);
    luna_vector_destroy(&function->arguments);
    luna_vector_destroy(&function->blocks);
}

void luna_ir_module_init(LunaIrModule *module, const LunaTargetInfo *target) {
    module->target = target;
    module->kind = LUNA_IR_MODULE_EXECUTABLE;
    luna_vector_init(&module->globals, sizeof(LunaIrGlobal));
    luna_vector_init(&module->functions, sizeof(LunaIrFunction));
    module->entry_function = LUNA_IR_INVALID_ID;
}

void luna_ir_module_destroy(LunaIrModule *module) {
    for (size_t index = 0U; index < module->globals.length; index += 1U) {
        LunaIrGlobal *global = luna_vector_at(&module->globals, index);
        luna_vector_destroy(&global->bytes);
    }
    for (size_t index = 0U; index < module->functions.length; index += 1U) {
        LunaIrFunction *function = luna_vector_at(&module->functions, index);
        luna_ir_function_destroy(function);
    }

    luna_vector_destroy(&module->globals);
    luna_vector_destroy(&module->functions);
    module->target = NULL;
    module->kind = LUNA_IR_MODULE_EXECUTABLE;
    module->entry_function = LUNA_IR_INVALID_ID;
}

LunaIrFunctionId luna_ir_module_add_function(LunaIrModule *module,
                                             LunaStringView module_name,
                                             LunaStringView name,
                                             LunaIrType return_type,
                                             LunaIrFunctionLinkage linkage) {
    if (module->functions.length >= UINT32_MAX) {
        return LUNA_IR_INVALID_ID;
    }

    LunaIrFunction function = {
        .module_name = module_name,
        .name = name,
        .linkage = linkage,
        .return_type = return_type,
    };
    luna_ir_aggregate_layout_init(&function.return_aggregate, false, 0U, 0U);
    luna_vector_init(&function.parameter_types, sizeof(LunaIrType));
    luna_vector_init(&function.parameter_aggregates,
                     sizeof(LunaIrAggregateLayout));
    luna_vector_init(&function.slots, sizeof(LunaIrSlot));
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

LunaIrGlobalId luna_ir_module_add_global(LunaIrModule *module,
                                         const uint8_t *bytes,
                                         uint64_t byte_count,
                                         uint32_t alignment_bytes,
                                         bool is_read_only) {
    if (module == NULL || bytes == NULL || byte_count == 0U ||
        byte_count > SIZE_MAX || alignment_bytes == 0U ||
        (alignment_bytes & (alignment_bytes - 1U)) != 0U ||
        module->globals.length >= UINT32_MAX) {
        return LUNA_IR_INVALID_ID;
    }

    for (size_t index = 0U; index < module->globals.length; index += 1U) {
        const LunaIrGlobal *existing =
            luna_vector_at_const(&module->globals, index);
        if (existing->is_read_only == is_read_only &&
            existing->alignment_bytes == alignment_bytes &&
            existing->bytes.length == (size_t)byte_count &&
            memcmp(existing->bytes.data, bytes, (size_t)byte_count) == 0) {
            return (LunaIrGlobalId)index;
        }
    }

    LunaIrGlobal global = {
        .alignment_bytes = alignment_bytes,
        .is_read_only = is_read_only,
    };
    luna_vector_init(&global.bytes, sizeof(uint8_t));
    for (uint64_t index = 0U; index < byte_count; index += 1U) {
        if (!luna_vector_push(&global.bytes, &bytes[index])) {
            luna_vector_destroy(&global.bytes);
            return LUNA_IR_INVALID_ID;
        }
    }
    if (!luna_vector_push(&module->globals, &global)) {
        luna_vector_destroy(&global.bytes);
        return LUNA_IR_INVALID_ID;
    }
    return (LunaIrGlobalId)(module->globals.length - 1U);
}

const LunaIrGlobal *luna_ir_module_global(const LunaIrModule *module,
                                          LunaIrGlobalId global_id) {
    if (module == NULL) {
        return NULL;
    }
    return luna_vector_at_const(&module->globals, (size_t)global_id);
}

LunaIrSlotId luna_ir_function_add_slot(LunaIrFunction *function,
                                       LunaIrType type) {
    const LunaIrSlot slot = {
        .type = type,
        .size_bytes = 8U,
        .alignment_bytes = 8U,
        .is_scalar = true,
    };
    if (function->slots.length >= UINT32_MAX ||
        !luna_vector_push(&function->slots, &slot)) {
        return LUNA_IR_INVALID_ID;
    }

    return (LunaIrSlotId)(function->slots.length - 1U);
}

LunaIrSlotId luna_ir_function_add_memory_slot(LunaIrFunction *function,
                                              uint64_t size_bytes,
                                              uint32_t alignment_bytes) {
    const LunaIrSlot slot = {
        .type = LUNA_IR_TYPE_VOID,
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
        .is_scalar = false,
    };
    if (size_bytes == 0U || alignment_bytes == 0U ||
        (alignment_bytes & (alignment_bytes - 1U)) != 0U ||
        function->slots.length >= UINT32_MAX ||
        !luna_vector_push(&function->slots, &slot)) {
        return LUNA_IR_INVALID_ID;
    }
    return (LunaIrSlotId)(function->slots.length - 1U);
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
    case LUNA_IR_TYPE_I8:
        return "i8";
    case LUNA_IR_TYPE_I16:
        return "i16";
    case LUNA_IR_TYPE_I32:
        return "i32";
    case LUNA_IR_TYPE_I64:
        return "i64";
    case LUNA_IR_TYPE_ISIZE:
        return "isize";
    case LUNA_IR_TYPE_U8:
        return "u8";
    case LUNA_IR_TYPE_U16:
        return "u16";
    case LUNA_IR_TYPE_U32:
        return "u32";
    case LUNA_IR_TYPE_U64:
        return "u64";
    case LUNA_IR_TYPE_USIZE:
        return "usize";
    case LUNA_IR_TYPE_F32:
        return "f32";
    case LUNA_IR_TYPE_F64:
        return "f64";
    case LUNA_IR_TYPE_POINTER:
        return "ptr";
    }

    return "<invalid>";
}

bool luna_ir_type_is_integer(LunaIrType type) {
    return type == LUNA_IR_TYPE_I8 || type == LUNA_IR_TYPE_I16 ||
           type == LUNA_IR_TYPE_I32 || type == LUNA_IR_TYPE_I64 ||
           type == LUNA_IR_TYPE_ISIZE || type == LUNA_IR_TYPE_U8 ||
           type == LUNA_IR_TYPE_U16 || type == LUNA_IR_TYPE_U32 ||
           type == LUNA_IR_TYPE_U64 || type == LUNA_IR_TYPE_USIZE;
}

bool luna_ir_type_is_signed_integer(LunaIrType type) {
    return type == LUNA_IR_TYPE_I8 || type == LUNA_IR_TYPE_I16 ||
           type == LUNA_IR_TYPE_I32 || type == LUNA_IR_TYPE_I64 ||
           type == LUNA_IR_TYPE_ISIZE;
}

bool luna_ir_type_is_float(LunaIrType type) {
    return type == LUNA_IR_TYPE_F32 || type == LUNA_IR_TYPE_F64;
}

uint32_t luna_ir_type_bit_width(LunaIrType type,
                                const LunaDataLayout *data_layout) {
    switch (type) {
    case LUNA_IR_TYPE_BOOL:
        return 1U;
    case LUNA_IR_TYPE_I8:
    case LUNA_IR_TYPE_U8:
        return 8U;
    case LUNA_IR_TYPE_I16:
    case LUNA_IR_TYPE_U16:
        return 16U;
    case LUNA_IR_TYPE_I32:
    case LUNA_IR_TYPE_U32:
        return 32U;
    case LUNA_IR_TYPE_I64:
    case LUNA_IR_TYPE_U64:
        return 64U;
    case LUNA_IR_TYPE_ISIZE:
    case LUNA_IR_TYPE_USIZE:
    case LUNA_IR_TYPE_POINTER:
        return data_layout == NULL ? 0U : data_layout->pointer.size_bits;
    case LUNA_IR_TYPE_F32:
        return 32U;
    case LUNA_IR_TYPE_F64:
        return 64U;
    case LUNA_IR_TYPE_VOID:
        return 0U;
    }

    return 0U;
}

static bool luna_ir_type_is_value(LunaIrType type) {
    return type == LUNA_IR_TYPE_BOOL || luna_ir_type_is_integer(type) ||
           luna_ir_type_is_float(type) || type == LUNA_IR_TYPE_POINTER;
}

static bool luna_ir_type_is_return(LunaIrType type) {
    return type == LUNA_IR_TYPE_VOID || luna_ir_type_is_value(type);
}

static uint64_t luna_ir_integer_bit_mask(LunaIrType type,
                                         const LunaDataLayout *data_layout) {
    const uint32_t width = luna_ir_type_bit_width(type, data_layout);
    if (width == 64U) {
        return UINT64_MAX;
    }
    if (width == 0U) {
        return 0U;
    }
    return (UINT64_C(1) << width) - 1U;
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

static const LunaIrInstruction *
luna_ir_find_value_definition(const LunaIrFunction *function,
                              LunaIrValueId value) {
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block == NULL) {
            return NULL;
        }
        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (instruction != NULL && instruction->result == value) {
                return instruction;
            }
        }
    }
    return NULL;
}

static bool luna_ir_value_addresses_exact_memory_slot(
    const LunaIrFunction *function, LunaIrValueId value, uint64_t size_bytes,
    uint32_t alignment_bytes) {
    const LunaIrInstruction *definition =
        luna_ir_find_value_definition(function, value);
    const LunaIrSlot *slot =
        definition == NULL
            ? NULL
            : luna_vector_at_const(&function->slots, definition->slot);
    return definition != NULL &&
           definition->opcode == LUNA_IR_ADDRESS_OF_SLOT && slot != NULL &&
           !slot->is_scalar && slot->type == LUNA_IR_TYPE_VOID &&
           slot->size_bytes == size_bytes &&
           slot->alignment_bytes == alignment_bytes;
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
    if (callee->return_aggregate.is_aggregate) {
        const LunaIrSlot *result_slot =
            luna_vector_at_const(&function->slots, instruction->slot);
        if (result_slot == NULL || result_slot->is_scalar ||
            result_slot->type != LUNA_IR_TYPE_VOID ||
            result_slot->size_bytes != callee->return_aggregate.size_bytes ||
            result_slot->alignment_bytes !=
                callee->return_aggregate.alignment_bytes) {
            return luna_ir_reject(
                reason, "aggregate call has an invalid result memory slot");
        }
    } else if (instruction->slot != LUNA_IR_INVALID_ID) {
        return luna_ir_reject(reason,
                              "scalar call unexpectedly names a result slot");
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
        const LunaIrAggregateLayout *parameter_aggregate =
            luna_vector_at_const(&callee->parameter_aggregates, index);
        if (argument == NULL || parameter_type == NULL ||
            parameter_aggregate == NULL ||
            !luna_ir_verify_value(function, *argument, *parameter_type,
                                  defined_in_block, reason)) {
            return false;
        }
        if (parameter_aggregate->is_aggregate) {
            if (!luna_ir_value_addresses_exact_memory_slot(
                    function, *argument, parameter_aggregate->size_bytes,
                    parameter_aggregate->alignment_bytes)) {
                return luna_ir_reject(
                    reason, "aggregate call argument does not address an exact "
                            "snapshot slot");
            }
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
    case LUNA_IR_CONST_INTEGER:
        if (!luna_ir_type_is_integer(instruction->type)) {
            return luna_ir_reject(reason,
                                  "integer constant has non-integer type");
        }
        if (instruction->immediate >
            luna_ir_integer_bit_mask(instruction->type,
                                     &module->target->data_layout)) {
            return luna_ir_reject(
                reason, "integer constant exceeds its type storage width");
        }
        return luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);

    case LUNA_IR_CONST_FLOAT:
        if (!luna_ir_type_is_float(instruction->type)) {
            return luna_ir_reject(reason,
                                  "floating constant has non-floating type");
        }
        if (instruction->type == LUNA_IR_TYPE_F32 &&
            instruction->immediate > UINT32_MAX) {
            return luna_ir_reject(
                reason, "f32 constant exceeds its type storage width");
        }
        return luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);

    case LUNA_IR_CONST_BOOL:
        if (instruction->immediate != 0U && instruction->immediate != 1U) {
            return luna_ir_reject(reason,
                                  "bool constant must be exactly zero or one");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_BOOL,
                                     reason);

    case LUNA_IR_CONST_NULL:
        if (instruction->immediate != 0U) {
            return luna_ir_reject(reason,
                                  "null constant must have zero address bits");
        }
        return luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_LOAD: {
        if ((size_t)instruction->slot >= function->slots.length) {
            return luna_ir_reject(reason, "load slot id is out of range");
        }
        const LunaIrSlot *slot =
            luna_vector_at_const(&function->slots, (size_t)instruction->slot);
        if (!slot->is_scalar) {
            return luna_ir_reject(reason, "direct load requires a scalar slot");
        }
        return luna_ir_verify_result(function, instruction, slot->type, reason);
    }

    case LUNA_IR_STORE: {
        if ((size_t)instruction->slot >= function->slots.length) {
            return luna_ir_reject(reason, "store slot id is out of range");
        }
        const LunaIrSlot *slot =
            luna_vector_at_const(&function->slots, (size_t)instruction->slot);
        if (!slot->is_scalar) {
            return luna_ir_reject(reason,
                                  "direct store requires a scalar slot");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason) &&
               luna_ir_verify_value(function, instruction->left, slot->type,
                                    defined_in_block, reason);
    }

    case LUNA_IR_ADDRESS_OF_SLOT:
        if ((size_t)instruction->slot >= function->slots.length) {
            return luna_ir_reject(reason, "addressed slot id is out of range");
        }
        return luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_MEMBER_ADDRESS:
        if (instruction->immediate > (uint64_t)INT32_MAX) {
            return luna_ir_reject(
                reason, "member address offset exceeds backend displacement");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_GLOBAL_ADDRESS:
        if ((size_t)instruction->global >= module->globals.length) {
            return luna_ir_reject(reason, "global id is out of range");
        }
        return luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_ZERO_SLOT:
        if ((size_t)instruction->slot >= function->slots.length) {
            return luna_ir_reject(reason, "zeroed slot id is out of range");
        }
        return luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_MEMORY_COPY:
        if (instruction->immediate == 0U ||
            instruction->immediate > (uint64_t)INT32_MAX) {
            return luna_ir_reject(
                reason,
                "memory copy size must fit the positive object-size range");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_value(function, instruction->right,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_LOAD_INDIRECT:
        if (!luna_ir_type_is_value(instruction->type)) {
            return luna_ir_reject(reason,
                                  "indirect load has invalid access type");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);

    case LUNA_IR_STORE_INDIRECT:
        if (!luna_ir_type_is_value(instruction->memory_type)) {
            return luna_ir_reject(reason,
                                  "indirect store has invalid access type");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_value(function, instruction->right,
                                    instruction->memory_type, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_NULL_CHECK:
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_BOUNDS_CHECK:
        if (instruction->immediate == 0U) {
            return luna_ir_reject(reason,
                                  "bounds check requires a positive bound");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_USIZE, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_VOID,
                                     reason);

    case LUNA_IR_POINTER_OFFSET:
        if (instruction->immediate == 0U) {
            return luna_ir_reject(reason,
                                  "pointer offset requires a positive stride");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_value(function, instruction->right,
                                    LUNA_IR_TYPE_USIZE, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_NEG_INTEGER:
    case LUNA_IR_BIT_NOT_INTEGER:
        if (!luna_ir_type_is_integer(instruction->type)) {
            return luna_ir_reject(reason,
                                  "integer unary operation has invalid type");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    instruction->type, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);

    case LUNA_IR_NEG_FLOAT:
        if (!luna_ir_type_is_float(instruction->type)) {
            return luna_ir_reject(reason,
                                  "floating unary operation has invalid type");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    instruction->type, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);

    case LUNA_IR_BOOL_NOT:
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_BOOL, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_BOOL,
                                     reason);

    case LUNA_IR_CONVERT_INTEGER: {
        if (!luna_ir_type_is_integer(instruction->type)) {
            return luna_ir_reject(reason,
                                  "integer conversion has invalid result type");
        }
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "conversion value id is out of range");
        }
        const LunaIrType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_integer(*source_type)) {
            return luna_ir_reject(reason,
                                  "integer conversion has invalid source type");
        }
        if (*source_type == instruction->type) {
            return luna_ir_reject(reason, "integer conversion is redundant");
        }
        return luna_ir_verify_value(function, instruction->left, *source_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);
    }

    case LUNA_IR_CONVERT_FLOAT: {
        if (!luna_ir_type_is_float(instruction->type)) {
            return luna_ir_reject(
                reason, "floating conversion has invalid result type");
        }
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "conversion value id is out of range");
        }
        const LunaIrType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_float(*source_type)) {
            return luna_ir_reject(
                reason, "floating conversion has invalid source type");
        }
        if (*source_type == instruction->type) {
            return luna_ir_reject(reason, "floating conversion is redundant");
        }
        return luna_ir_verify_value(function, instruction->left, *source_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);
    }

    case LUNA_IR_CONVERT_INTEGER_TO_FLOAT: {
        if (!luna_ir_type_is_float(instruction->type)) {
            return luna_ir_reject(
                reason,
                "integer-to-floating conversion has invalid result type");
        }
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "conversion value id is out of range");
        }
        const LunaIrType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_integer(*source_type)) {
            return luna_ir_reject(
                reason,
                "integer-to-floating conversion has invalid source type");
        }
        return luna_ir_verify_value(function, instruction->left, *source_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);
    }

    case LUNA_IR_CONVERT_FLOAT_TO_INTEGER: {
        if (!luna_ir_type_is_integer(instruction->type)) {
            return luna_ir_reject(
                reason,
                "floating-to-integer conversion has invalid result type");
        }
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "conversion value id is out of range");
        }
        const LunaIrType *source_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_float(*source_type)) {
            return luna_ir_reject(
                reason,
                "floating-to-integer conversion has invalid source type");
        }
        return luna_ir_verify_value(function, instruction->left, *source_type,
                                    defined_in_block, reason) &&
               luna_ir_verify_result(function, instruction, instruction->type,
                                     reason);
    }

    case LUNA_IR_CONVERT_POINTER_TO_INTEGER:
        if (instruction->type != LUNA_IR_TYPE_USIZE) {
            return luna_ir_reject(
                reason, "pointer-to-integer conversion must produce usize");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_POINTER, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction, LUNA_IR_TYPE_USIZE,
                                     reason);

    case LUNA_IR_CONVERT_INTEGER_TO_POINTER:
        if (instruction->type != LUNA_IR_TYPE_POINTER) {
            return luna_ir_reject(
                reason, "integer-to-pointer conversion must produce a pointer");
        }
        return luna_ir_verify_value(function, instruction->left,
                                    LUNA_IR_TYPE_USIZE, defined_in_block,
                                    reason) &&
               luna_ir_verify_result(function, instruction,
                                     LUNA_IR_TYPE_POINTER, reason);

    case LUNA_IR_ADD_INTEGER:
    case LUNA_IR_SUB_INTEGER:
    case LUNA_IR_MUL_INTEGER:
    case LUNA_IR_DIV_INTEGER:
    case LUNA_IR_REM_INTEGER:
    case LUNA_IR_BIT_AND_INTEGER:
    case LUNA_IR_BIT_OR_INTEGER:
    case LUNA_IR_BIT_XOR_INTEGER:
    case LUNA_IR_SHIFT_LEFT_INTEGER:
    case LUNA_IR_SHIFT_RIGHT_INTEGER:
        if (!luna_ir_type_is_integer(instruction->type)) {
            return luna_ir_reject(reason,
                                  "integer binary operation has invalid type");
        }
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     instruction->type, instruction->type,
                                     reason);

    case LUNA_IR_ADD_FLOAT:
    case LUNA_IR_SUB_FLOAT:
    case LUNA_IR_MUL_FLOAT:
    case LUNA_IR_DIV_FLOAT:
        if (!luna_ir_type_is_float(instruction->type)) {
            return luna_ir_reject(reason,
                                  "floating binary operation has invalid type");
        }
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     instruction->type, instruction->type,
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

    case LUNA_IR_COMPARE_LESS_INTEGER:
    case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_IR_COMPARE_GREATER_INTEGER:
    case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER: {
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "comparison value id is out of range");
        }
        const LunaIrType *operand_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_integer(*operand_type)) {
            return luna_ir_reject(reason,
                                  "ordering operand type is not integer");
        }
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     *operand_type, LUNA_IR_TYPE_BOOL, reason);
    }

    case LUNA_IR_COMPARE_LESS_FLOAT:
    case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_IR_COMPARE_GREATER_FLOAT:
    case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT: {
        if (instruction->left == LUNA_IR_INVALID_ID ||
            (size_t)instruction->left >= function->value_types.length) {
            return luna_ir_reject(reason,
                                  "comparison value id is out of range");
        }
        const LunaIrType *operand_type = luna_vector_at_const(
            &function->value_types, (size_t)instruction->left);
        if (!luna_ir_type_is_float(*operand_type)) {
            return luna_ir_reject(reason,
                                  "floating ordering operand is not floating");
        }
        return luna_ir_verify_binary(function, instruction, defined_in_block,
                                     *operand_type, LUNA_IR_TYPE_BOOL, reason);
    }

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
        if (!luna_ir_verify_value(function, instruction->left,
                                  function->return_type, defined_in_block,
                                  reason)) {
            return false;
        }
        if (function->return_aggregate.is_aggregate &&
            !luna_ir_value_addresses_exact_memory_slot(
                function, instruction->left,
                function->return_aggregate.size_bytes,
                function->return_aggregate.alignment_bytes)) {
            return luna_ir_reject(
                reason,
                "aggregate return does not address an exact snapshot slot");
        }
        return true;
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

static bool
luna_ir_aggregate_layout_is_valid(const LunaIrAggregateLayout *layout,
                                  const LunaDataLayout *data_layout) {
    if (layout == NULL ||
        layout->components.element_size != sizeof(LunaIrAggregateComponent) ||
        layout->components.capacity < layout->components.length ||
        (layout->components.length != 0U && layout->components.data == NULL)) {
        return false;
    }
    if (!layout->is_aggregate) {
        return layout->size_bytes == 0U && layout->alignment_bytes == 0U &&
               layout->components.length == 0U;
    }
    if (layout->size_bytes == 0U || layout->alignment_bytes == 0U ||
        (layout->alignment_bytes & (layout->alignment_bytes - 1U)) != 0U ||
        layout->size_bytes % (uint64_t)layout->alignment_bytes != 0U ||
        (layout->size_bytes <= 16U && layout->components.length == 0U)) {
        return false;
    }
    for (size_t index = 0U; index < layout->components.length; index += 1U) {
        const LunaIrAggregateComponent *component =
            luna_vector_at_const(&layout->components, index);
        const uint32_t bit_width =
            component == NULL
                ? 0U
                : luna_ir_type_bit_width(component->type, data_layout);
        const uint64_t expected_size = (uint64_t)bit_width / 8U;
        if (component == NULL || bit_width == 0U || bit_width % 8U != 0U ||
            component->size_bytes != expected_size ||
            component->alignment_bytes != expected_size ||
            component->offset_bytes > layout->size_bytes ||
            component->size_bytes >
                layout->size_bytes - component->offset_bytes) {
            return false;
        }
    }
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

    if (!luna_ir_type_is_return(function->return_type) ||
        !luna_ir_aggregate_layout_is_valid(&function->return_aggregate,
                                           &module->target->data_layout) ||
        (function->return_aggregate.is_aggregate &&
         function->return_type != LUNA_IR_TYPE_POINTER) ||
        function->parameter_aggregates.element_size !=
            sizeof(LunaIrAggregateLayout) ||
        function->parameter_aggregates.length !=
            function->parameter_types.length ||
        function->parameter_aggregates.capacity <
            function->parameter_aggregates.length ||
        (function->parameter_aggregates.length != 0U &&
         function->parameter_aggregates.data == NULL)) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has invalid signature "
                      "layout\n",
                      function_index);
        goto cleanup;
    }

    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaIrType *parameter_type =
            luna_vector_at_const(&function->parameter_types, index);
        const LunaIrAggregateLayout *parameter_aggregate =
            luna_vector_at_const(&function->parameter_aggregates, index);
        if (parameter_type == NULL || parameter_aggregate == NULL ||
            !luna_ir_aggregate_layout_is_valid(parameter_aggregate,
                                               &module->target->data_layout) ||
            (parameter_aggregate->is_aggregate &&
             *parameter_type != LUNA_IR_TYPE_POINTER)) {
            (void)fprintf(
                error_stream,
                "IR verification: function %zu parameter %zu has an invalid "
                "signature layout\n",
                function_index, index);
            goto cleanup;
        }
    }

    if (function->linkage != LUNA_IR_LINKAGE_INTERNAL &&
        function->linkage != LUNA_IR_LINKAGE_MODULE_EXPORT &&
        function->linkage != LUNA_IR_LINKAGE_MODULE_IMPORT &&
        function->linkage != LUNA_IR_LINKAGE_EXTERNAL_C) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has invalid linkage\n",
                      function_index);
        goto cleanup;
    }

    if (function->linkage == LUNA_IR_LINKAGE_MODULE_IMPORT &&
        !function->has_module_metadata_hash) {
        (void)fprintf(error_stream,
                      "IR verification: imported function %zu has no module "
                      "metadata identity\n",
                      function_index);
        goto cleanup;
    }
    if ((function->linkage == LUNA_IR_LINKAGE_INTERNAL ||
         function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C) &&
        function->has_module_metadata_hash) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has an unexpected module "
                      "metadata identity\n",
                      function_index);
        goto cleanup;
    }

    if (function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C ||
        function->linkage == LUNA_IR_LINKAGE_MODULE_IMPORT) {
        for (size_t index = 0U; index < function->parameter_types.length;
             index += 1U) {
            const LunaIrType *parameter_type =
                luna_vector_at_const(&function->parameter_types, index);
            if (parameter_type == NULL ||
                !luna_ir_type_is_value(*parameter_type)) {
                (void)fprintf(
                    error_stream,
                    "IR verification: invalid declaration parameter %zu in "
                    "function %zu\n",
                    index, function_index);
                goto cleanup;
            }
        }
        if (function->slots.length != 0U ||
            function->value_types.length != 0U ||
            function->arguments.length != 0U || function->blocks.length != 0U) {
            (void)fprintf(error_stream,
                          function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C
                              ? "IR verification: external function %zu has "
                                "a definition\n"
                              : "IR verification: imported function %zu has "
                                "a definition\n",
                          function_index);
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }

    if (function->blocks.length == 0U) {
        (void)fprintf(error_stream,
                      "IR verification: function %zu has no blocks\n",
                      function_index);
        goto cleanup;
    }

    if (function->parameter_types.length > function->slots.length) {
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
        const LunaIrAggregateLayout *parameter_aggregate =
            luna_vector_at_const(&function->parameter_aggregates, index);
        const LunaIrSlot *slot = luna_vector_at_const(&function->slots, index);
        const bool valid_scalar = !parameter_aggregate->is_aggregate &&
                                  luna_ir_type_is_value(*parameter_type) &&
                                  slot->is_scalar &&
                                  slot->type == *parameter_type;
        const bool valid_aggregate =
            parameter_aggregate->is_aggregate &&
            *parameter_type == LUNA_IR_TYPE_POINTER && !slot->is_scalar &&
            slot->type == LUNA_IR_TYPE_VOID &&
            slot->size_bytes == parameter_aggregate->size_bytes &&
            slot->alignment_bytes == parameter_aggregate->alignment_bytes;
        if (!valid_scalar && !valid_aggregate) {
            (void)fprintf(error_stream,
                          "IR verification: invalid parameter %zu in function "
                          "%zu\n",
                          index, function_index);
            goto cleanup;
        }
    }

    for (size_t index = 0U; index < function->slots.length; index += 1U) {
        const LunaIrSlot *slot = luna_vector_at_const(&function->slots, index);
        const bool valid_alignment =
            slot->alignment_bytes != 0U &&
            (slot->alignment_bytes & (slot->alignment_bytes - 1U)) == 0U;
        const bool valid_scalar =
            slot->is_scalar && luna_ir_type_is_value(slot->type) &&
            slot->size_bytes == 8U && slot->alignment_bytes == 8U;
        const bool valid_memory = !slot->is_scalar &&
                                  slot->type == LUNA_IR_TYPE_VOID &&
                                  slot->size_bytes != 0U && valid_alignment;
        if (!valid_scalar && !valid_memory) {
            (void)fprintf(error_stream,
                          "IR verification: invalid slot layout at %zu in "
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

    if (module->target == NULL || module->target->triple == NULL ||
        module->target->triple[0] == '\0' ||
        !luna_data_layout_is_valid(&module->target->data_layout)) {
        (void)fputs(
            "IR verification: target data layout is missing or invalid\n",
            stream);
        return false;
    }
    if (module->kind != LUNA_IR_MODULE_EXECUTABLE &&
        module->kind != LUNA_IR_MODULE_LIBRARY) {
        (void)fputs("IR verification: module kind is invalid\n", stream);
        return false;
    }

    for (size_t index = 0U; index < module->globals.length; index += 1U) {
        const LunaIrGlobal *global =
            luna_vector_at_const(&module->globals, index);
        if (global->bytes.element_size != sizeof(uint8_t) ||
            global->bytes.length == 0U || global->alignment_bytes == 0U ||
            (global->alignment_bytes & (global->alignment_bytes - 1U)) != 0U) {
            (void)fprintf(stream,
                          "IR verification: global %zu has invalid layout\n",
                          index);
            return false;
        }
    }

    if (module->kind == LUNA_IR_MODULE_EXECUTABLE) {
        if (module->entry_function == LUNA_IR_INVALID_ID ||
            (size_t)module->entry_function >= module->functions.length) {
            (void)fputs("IR verification: missing entry function\n", stream);
            return false;
        }

        const LunaIrFunction *entry =
            luna_ir_module_function_const(module, module->entry_function);
        bool entry_parameters_are_valid = entry->parameter_types.length == 0U;
        if (entry->parameter_types.length == 2U) {
            const LunaIrType *argument_count_type =
                luna_vector_at_const(&entry->parameter_types, 0U);
            const LunaIrType *argument_vector_type =
                luna_vector_at_const(&entry->parameter_types, 1U);
            entry_parameters_are_valid =
                argument_count_type != NULL &&
                *argument_count_type == LUNA_IR_TYPE_USIZE &&
                argument_vector_type != NULL &&
                *argument_vector_type == LUNA_IR_TYPE_POINTER;
        }
        if (entry->linkage != LUNA_IR_LINKAGE_INTERNAL ||
            entry->return_type != LUNA_IR_TYPE_I32 ||
            entry->return_aggregate.is_aggregate ||
            !entry_parameters_are_valid) {
            (void)fputs(
                "IR verification: entry function must be an internal fn() -> "
                "i32 or fn(usize, pointer) -> i32\n",
                stream);
            return false;
        }
    } else if (module->entry_function != LUNA_IR_INVALID_ID) {
        (void)fputs(
            "IR verification: library module must not have an entry function\n",
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

static bool luna_ir_print_function_name(const LunaIrModule *module,
                                        const LunaIrFunction *function,
                                        LunaStringBuilder *output) {
    bool needs_module_name = function->linkage == LUNA_IR_LINKAGE_MODULE_IMPORT;
    if (function->linkage == LUNA_IR_LINKAGE_INTERNAL ||
        function->linkage == LUNA_IR_LINKAGE_MODULE_EXPORT) {
        for (size_t index = 0U; index < module->functions.length; index += 1U) {
            const LunaIrFunction *candidate =
                luna_vector_at_const(&module->functions, index);
            if (candidate != function &&
                candidate->linkage != LUNA_IR_LINKAGE_EXTERNAL_C &&
                luna_string_view_equal(candidate->name, function->name)) {
                needs_module_name = true;
                break;
            }
        }
    }

    return (!needs_module_name ||
            (luna_string_builder_append_view(output, function->module_name) &&
             luna_string_builder_append_c_string(output, "::"))) &&
           luna_string_builder_append_view(output, function->name);
}

static bool luna_ir_print_function_metadata(const LunaIrFunction *function,
                                            LunaStringBuilder *output) {
    return !function->has_module_metadata_hash ||
           luna_string_builder_append_format(output,
                                             " [metadata 0x%016" PRIx64 "]",
                                             function->module_metadata_hash);
}

static bool
luna_ir_print_signature_type(LunaStringBuilder *output, LunaIrType type,
                             const LunaIrAggregateLayout *aggregate) {
    if (aggregate != NULL && aggregate->is_aggregate) {
        return luna_string_builder_append_format(
            output, "aggregate[%" PRIu64 ",%" PRIu32 "]", aggregate->size_bytes,
            aggregate->alignment_bytes);
    }
    return luna_string_builder_append_c_string(output, luna_ir_type_name(type));
}

static int64_t luna_ir_signed_immediate(LunaIrType type, uint64_t bits,
                                        const LunaDataLayout *data_layout) {
    const uint64_t maximum_bits = luna_ir_integer_bit_mask(type, data_layout);
    const uint64_t maximum_positive = maximum_bits >> 1U;
    if (bits <= maximum_positive) {
        return (int64_t)bits;
    }

    const uint64_t magnitude = maximum_bits - bits + 1U;
    if (magnitude == (UINT64_C(1) << 63U)) {
        return INT64_MIN;
    }
    return -(int64_t)magnitude;
}

static LunaIrType
luna_ir_instruction_operand_type(const LunaIrFunction *function,
                                 const LunaIrInstruction *instruction) {
    const LunaIrType *type =
        luna_vector_at_const(&function->value_types, (size_t)instruction->left);
    return type == NULL ? LUNA_IR_TYPE_VOID : *type;
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
    case LUNA_IR_CONST_INTEGER:
        if (luna_ir_type_is_signed_integer(instruction->type)) {
            return luna_string_builder_append_format(
                output, "const.%s %" PRId64 "\n",
                luna_ir_type_name(instruction->type),
                luna_ir_signed_immediate(instruction->type,
                                         instruction->immediate,
                                         &module->target->data_layout));
        }
        return luna_string_builder_append_format(
            output, "const.%s %" PRIu64 "\n",
            luna_ir_type_name(instruction->type), instruction->immediate);

    case LUNA_IR_CONST_FLOAT:
        if (instruction->type == LUNA_IR_TYPE_F32) {
            return luna_string_builder_append_format(
                output, "const.f32 0x%08" PRIx32 "\n",
                (uint32_t)instruction->immediate);
        }
        return luna_string_builder_append_format(
            output, "const.f64 0x%016" PRIx64 "\n", instruction->immediate);

    case LUNA_IR_CONST_BOOL:
        return luna_string_builder_append_format(
            output, "const.bool %s\n",
            instruction->immediate == 0U ? "false" : "true");

    case LUNA_IR_CONST_NULL:
        return luna_string_builder_append_c_string(output, "const.ptr null\n");

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

    case LUNA_IR_ADDRESS_OF_SLOT:
        return luna_string_builder_append_format(output, "address $%u\n",
                                                 instruction->slot);

    case LUNA_IR_MEMBER_ADDRESS:
        if (!luna_string_builder_append_format(output,
                                               "member_address %" PRIu64 ", ",
                                               instruction->immediate) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_GLOBAL_ADDRESS:
        return luna_string_builder_append_format(
            output, "global_address @g%u\n", instruction->global);

    case LUNA_IR_ZERO_SLOT:
        return luna_string_builder_append_format(output, "zero $%u\n",
                                                 instruction->slot);

    case LUNA_IR_MEMORY_COPY:
        if (!luna_string_builder_append_format(
                output, "memory_copy %" PRIu64 ", ", instruction->immediate) ||
            !luna_ir_print_value(output, instruction->left) ||
            !luna_string_builder_append_c_string(output, ", ") ||
            !luna_ir_print_value(output, instruction->right)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_LOAD_INDIRECT:
        if (!luna_string_builder_append_format(
                output, "load_indirect.%s ",
                luna_ir_type_name(instruction->type)) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_STORE_INDIRECT:
        if (!luna_string_builder_append_format(
                output, "store_indirect.%s ",
                luna_ir_type_name(instruction->memory_type)) ||
            !luna_ir_print_value(output, instruction->left) ||
            !luna_string_builder_append_c_string(output, ", ") ||
            !luna_ir_print_value(output, instruction->right)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_NULL_CHECK:
        if (!luna_string_builder_append_c_string(output, "null_check ") ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_BOUNDS_CHECK:
        if (!luna_string_builder_append_format(
                output, "bounds_check %" PRIu64 ", ", instruction->immediate) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_POINTER_OFFSET:
        if (!luna_string_builder_append_format(output,
                                               "pointer_offset %" PRIu64 ", ",
                                               instruction->immediate) ||
            !luna_ir_print_value(output, instruction->left) ||
            !luna_string_builder_append_c_string(output, ", ") ||
            !luna_ir_print_value(output, instruction->right)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_NEG_INTEGER:
    case LUNA_IR_NEG_FLOAT:
    case LUNA_IR_BIT_NOT_INTEGER:
    case LUNA_IR_BOOL_NOT: {
        if (instruction->opcode == LUNA_IR_BOOL_NOT) {
            if (!luna_string_builder_append_c_string(output, "not.bool ")) {
                return false;
            }
        } else if (!luna_string_builder_append_format(
                       output, "%s.%s ",
                       instruction->opcode == LUNA_IR_NEG_INTEGER ||
                               instruction->opcode == LUNA_IR_NEG_FLOAT
                           ? "neg"
                           : "bit_not",
                       luna_ir_type_name(instruction->type))) {
            return false;
        }
        if (!luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CONVERT_INTEGER: {
        const LunaIrType source_type =
            luna_ir_instruction_operand_type(function, instruction);
        const uint32_t source_width =
            luna_ir_type_bit_width(source_type, &module->target->data_layout);
        const uint32_t target_width = luna_ir_type_bit_width(
            instruction->type, &module->target->data_layout);
        const char *name = "bitcast";
        if (source_width > target_width) {
            name = "trunc";
        } else if (source_width < target_width) {
            name =
                luna_ir_type_is_signed_integer(source_type) ? "sext" : "zext";
        }

        if (!luna_string_builder_append_format(
                output, "%s.%s.%s ", name, luna_ir_type_name(source_type),
                luna_ir_type_name(instruction->type)) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CONVERT_FLOAT: {
        const LunaIrType source_type =
            luna_ir_instruction_operand_type(function, instruction);
        const char *name =
            source_type == LUNA_IR_TYPE_F32 ? "fpext" : "fptrunc";
        if (!luna_string_builder_append_format(
                output, "%s.%s.%s ", name, luna_ir_type_name(source_type),
                luna_ir_type_name(instruction->type)) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CONVERT_INTEGER_TO_FLOAT: {
        const LunaIrType source_type =
            luna_ir_instruction_operand_type(function, instruction);
        const char *name =
            luna_ir_type_is_signed_integer(source_type) ? "sitofp" : "uitofp";
        if (!luna_string_builder_append_format(
                output, "%s.%s.%s ", name, luna_ir_type_name(source_type),
                luna_ir_type_name(instruction->type)) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CONVERT_FLOAT_TO_INTEGER: {
        const LunaIrType source_type =
            luna_ir_instruction_operand_type(function, instruction);
        const char *name = luna_ir_type_is_signed_integer(instruction->type)
                               ? "fptosi"
                               : "fptoui";
        if (!luna_string_builder_append_format(
                output, "%s.%s.%s ", name, luna_ir_type_name(source_type),
                luna_ir_type_name(instruction->type)) ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
    }

    case LUNA_IR_CONVERT_POINTER_TO_INTEGER:
        if (!luna_string_builder_append_c_string(output, "ptrtoint.usize ") ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_CONVERT_INTEGER_TO_POINTER:
        if (!luna_string_builder_append_c_string(output, "inttoptr.usize ") ||
            !luna_ir_print_value(output, instruction->left)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");

    case LUNA_IR_ADD_INTEGER:
    case LUNA_IR_SUB_INTEGER:
    case LUNA_IR_MUL_INTEGER:
    case LUNA_IR_DIV_INTEGER:
    case LUNA_IR_REM_INTEGER:
    case LUNA_IR_BIT_AND_INTEGER:
    case LUNA_IR_BIT_OR_INTEGER:
    case LUNA_IR_BIT_XOR_INTEGER:
    case LUNA_IR_SHIFT_LEFT_INTEGER:
    case LUNA_IR_SHIFT_RIGHT_INTEGER:
    case LUNA_IR_ADD_FLOAT:
    case LUNA_IR_SUB_FLOAT:
    case LUNA_IR_MUL_FLOAT:
    case LUNA_IR_DIV_FLOAT:
    case LUNA_IR_COMPARE_EQUAL:
    case LUNA_IR_COMPARE_NOT_EQUAL:
    case LUNA_IR_COMPARE_LESS_INTEGER:
    case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_IR_COMPARE_GREATER_INTEGER:
    case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER:
    case LUNA_IR_COMPARE_LESS_FLOAT:
    case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_IR_COMPARE_GREATER_FLOAT:
    case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT: {
        const char *name = "<invalid>";
        switch (instruction->opcode) {
        case LUNA_IR_ADD_INTEGER:
        case LUNA_IR_ADD_FLOAT:
            name = "add";
            break;
        case LUNA_IR_SUB_INTEGER:
        case LUNA_IR_SUB_FLOAT:
            name = "sub";
            break;
        case LUNA_IR_MUL_INTEGER:
        case LUNA_IR_MUL_FLOAT:
            name = "mul";
            break;
        case LUNA_IR_DIV_INTEGER:
        case LUNA_IR_DIV_FLOAT:
            name = "div";
            break;
        case LUNA_IR_REM_INTEGER:
            name = "rem";
            break;
        case LUNA_IR_BIT_AND_INTEGER:
            name = "and";
            break;
        case LUNA_IR_BIT_OR_INTEGER:
            name = "or";
            break;
        case LUNA_IR_BIT_XOR_INTEGER:
            name = "xor";
            break;
        case LUNA_IR_SHIFT_LEFT_INTEGER:
            name = "shl";
            break;
        case LUNA_IR_SHIFT_RIGHT_INTEGER:
            name = "shr";
            break;
        case LUNA_IR_COMPARE_EQUAL:
            name = "eq";
            break;
        case LUNA_IR_COMPARE_NOT_EQUAL:
            name = "ne";
            break;
        case LUNA_IR_COMPARE_LESS_INTEGER:
        case LUNA_IR_COMPARE_LESS_FLOAT:
            name = "lt";
            break;
        case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
        case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
            name = "le";
            break;
        case LUNA_IR_COMPARE_GREATER_INTEGER:
        case LUNA_IR_COMPARE_GREATER_FLOAT:
            name = "gt";
            break;
        case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER:
        case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT:
            name = "ge";
            break;
        default:
            break;
        }

        const bool has_type_suffix =
            instruction->opcode != LUNA_IR_COMPARE_EQUAL &&
            instruction->opcode != LUNA_IR_COMPARE_NOT_EQUAL;
        const LunaIrType operation_type =
            instruction->type == LUNA_IR_TYPE_BOOL
                ? luna_ir_instruction_operand_type(function, instruction)
                : instruction->type;
        if (!luna_string_builder_append_c_string(output, name) ||
            (has_type_suffix &&
             !luna_string_builder_append_format(
                 output, ".%s", luna_ir_type_name(operation_type))) ||
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
            !luna_ir_print_function_name(module, callee, output) ||
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

        if (!luna_string_builder_append_c_string(output, ")")) {
            return false;
        }
        if (callee->return_aggregate.is_aggregate &&
            !luna_string_builder_append_format(output, " result=$%" PRIu32,
                                               instruction->slot)) {
            return false;
        }
        return luna_string_builder_append_c_string(output, "\n");
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
    if (module == NULL || module->target == NULL ||
        module->target->triple == NULL ||
        !luna_string_builder_append_format(
            output, "ir luna.v0\ntarget \"%s\"\n\n", module->target->triple)) {
        return false;
    }

    for (size_t global_index = 0U; global_index < module->globals.length;
         global_index += 1U) {
        const LunaIrGlobal *global =
            luna_vector_at_const(&module->globals, global_index);
        if (!luna_string_builder_append_format(
                output, "global @g%zu %s align %u [", global_index,
                global->is_read_only ? "readonly" : "mutable",
                global->alignment_bytes)) {
            return false;
        }
        for (size_t byte_index = 0U; byte_index < global->bytes.length;
             byte_index += 1U) {
            const uint8_t *byte =
                luna_vector_at_const(&global->bytes, byte_index);
            if ((byte_index > 0U &&
                 !luna_string_builder_append_c_string(output, ", ")) ||
                !luna_string_builder_append_format(output, "0x%02" PRIx8,
                                                   *byte)) {
                return false;
            }
        }
        if (!luna_string_builder_append_c_string(output, "]\n")) {
            return false;
        }
    }
    if (module->globals.length > 0U &&
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaIrFunction *function =
            luna_vector_at_const(&module->functions, function_index);

        if (function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C ||
            function->linkage == LUNA_IR_LINKAGE_MODULE_IMPORT) {
            const char *declaration_prefix =
                function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C ? "extern fn @"
                                                                : "import fn @";
            if (!luna_string_builder_append_c_string(output,
                                                     declaration_prefix) ||
                !luna_ir_print_function_name(module, function, output) ||
                !luna_string_builder_append_c_string(output, "(")) {
                return false;
            }
            for (size_t parameter_index = 0U;
                 parameter_index < function->parameter_types.length;
                 parameter_index += 1U) {
                const LunaIrType *type = luna_vector_at_const(
                    &function->parameter_types, parameter_index);
                const LunaIrAggregateLayout *aggregate = luna_vector_at_const(
                    &function->parameter_aggregates, parameter_index);
                if ((parameter_index > 0U &&
                     !luna_string_builder_append_c_string(output, ", ")) ||
                    type == NULL || aggregate == NULL ||
                    !luna_ir_print_signature_type(output, *type, aggregate)) {
                    return false;
                }
            }
            if (!luna_string_builder_append_c_string(output, ") -> ") ||
                !luna_ir_print_signature_type(output, function->return_type,
                                              &function->return_aggregate) ||
                !luna_ir_print_function_metadata(function, output) ||
                !luna_string_builder_append_c_string(output, "\n\n")) {
                return false;
            }
            continue;
        }

        const char *definition_prefix =
            function->linkage == LUNA_IR_LINKAGE_MODULE_EXPORT ? "export fn @"
                                                               : "fn @";
        if (!luna_string_builder_append_c_string(output, definition_prefix) ||
            !luna_ir_print_function_name(module, function, output) ||
            !luna_string_builder_append_c_string(output, "(")) {
            return false;
        }

        for (size_t parameter_index = 0U;
             parameter_index < function->parameter_types.length;
             parameter_index += 1U) {
            const LunaIrType *type = luna_vector_at_const(
                &function->parameter_types, parameter_index);
            const LunaIrAggregateLayout *aggregate = luna_vector_at_const(
                &function->parameter_aggregates, parameter_index);
            if ((parameter_index > 0U &&
                 !luna_string_builder_append_c_string(output, ", ")) ||
                !luna_string_builder_append_format(output,
                                                   "$%zu: ", parameter_index) ||
                type == NULL || aggregate == NULL ||
                !luna_ir_print_signature_type(output, *type, aggregate)) {
                return false;
            }
        }

        if (!luna_string_builder_append_c_string(output, ") -> ") ||
            !luna_ir_print_signature_type(output, function->return_type,
                                          &function->return_aggregate) ||
            !luna_ir_print_function_metadata(function, output) ||
            !luna_string_builder_append_c_string(output, " {\n")) {
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
