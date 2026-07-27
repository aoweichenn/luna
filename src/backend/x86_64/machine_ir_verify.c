#include "luna/backend/x86_64/machine_ir.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool luna_x86_64_machine_type_is_value(LunaX8664MachineType type) {
    return luna_x86_64_machine_type_register_class(type) !=
           LUNA_X86_64_MACHINE_REGISTER_NONE;
}

static uint64_t luna_x86_64_machine_integer_mask(LunaX8664MachineType type) {
    const uint32_t width = luna_x86_64_machine_type_bit_width(type);
    if (width == 64U) {
        return UINT64_MAX;
    }
    if (width == 0U) {
        return 0U;
    }
    return (UINT64_C(1) << width) - 1U;
}

static bool luna_x86_64_machine_opcode_is_valid(LunaX8664MachineOpcode opcode) {
    return opcode >= LUNA_X86_64_MACHINE_CONST_INTEGER &&
           opcode <= LUNA_X86_64_MACHINE_RETURN;
}

static bool
luna_x86_64_machine_opcode_is_terminator(LunaX8664MachineOpcode opcode) {
    return opcode == LUNA_X86_64_MACHINE_JUMP ||
           opcode == LUNA_X86_64_MACHINE_BRANCH ||
           opcode == LUNA_X86_64_MACHINE_RETURN;
}

static const LunaX8664MachineType *
luna_x86_64_machine_value_type(const LunaX8664MachineFunction *function,
                               LunaX8664MachineVirtualRegister value) {
    return luna_vector_at_const(&function->value_types, (size_t)value);
}

static const LunaX8664MachineStackSlot *
luna_x86_64_machine_slot(const LunaX8664MachineFunction *function,
                         LunaX8664MachineStackSlotId slot) {
    return luna_vector_at_const(&function->slots, (size_t)slot);
}

static bool
luna_x86_64_machine_types_match(const LunaX8664MachineFunction *function,
                                LunaX8664MachineVirtualRegister left,
                                LunaX8664MachineVirtualRegister right,
                                LunaX8664MachineType expected) {
    const LunaX8664MachineType *left_type =
        luna_x86_64_machine_value_type(function, left);
    const LunaX8664MachineType *right_type =
        luna_x86_64_machine_value_type(function, right);
    return left_type != NULL && right_type != NULL && *left_type == expected &&
           *right_type == expected;
}

static bool
luna_x86_64_machine_verify_call(const LunaX8664MachineModule *module,
                                const LunaX8664MachineFunction *function,
                                const LunaX8664MachineInstruction *instruction,
                                size_t function_index, size_t block_index,
                                size_t instruction_index, bool *argument_used,
                                FILE *stream) {
    const LunaX8664MachineFunction *callee =
        luna_x86_64_machine_module_function_const(module, instruction->callee);
    if (callee == NULL ||
        instruction->argument_count != callee->parameter_types.length ||
        (uint64_t)instruction->first_argument +
                (uint64_t)instruction->argument_count >
            (uint64_t)function->arguments.length) {
        (void)fprintf(
            stream,
            "machine IR verification: call %zu:%zu:%zu has an invalid "
            "callee or argument range\n",
            function_index, block_index, instruction_index);
        return false;
    }
    for (uint32_t argument_index = 0U;
         argument_index < instruction->argument_count; argument_index += 1U) {
        const size_t storage_index =
            (size_t)instruction->first_argument + (size_t)argument_index;
        if (argument_used == NULL || argument_used[storage_index]) {
            (void)fprintf(
                stream,
                "machine IR verification: call %zu:%zu:%zu has overlapping "
                "argument storage\n",
                function_index, block_index, instruction_index);
            return false;
        }
        const LunaX8664MachineVirtualRegister argument =
            luna_x86_64_machine_instruction_use(function, instruction,
                                                argument_index);
        const LunaX8664MachineType *argument_type =
            luna_x86_64_machine_value_type(function, argument);
        const LunaX8664MachineType *parameter_type = luna_vector_at_const(
            &callee->parameter_types, (size_t)argument_index);
        if (argument_type == NULL || parameter_type == NULL ||
            *argument_type != *parameter_type) {
            (void)fprintf(
                stream,
                "machine IR verification: call %zu:%zu:%zu argument %" PRIu32
                " has the wrong target type\n",
                function_index, block_index, instruction_index, argument_index);
            return false;
        }
        argument_used[storage_index] = true;
    }
    if (instruction->type != callee->return_type ||
        (callee->return_type == LUNA_X86_64_MACHINE_TYPE_VOID &&
         instruction->result != LUNA_X86_64_MACHINE_INVALID_ID) ||
        (callee->return_type != LUNA_X86_64_MACHINE_TYPE_VOID &&
         instruction->result == LUNA_X86_64_MACHINE_INVALID_ID)) {
        (void)fprintf(
            stream,
            "machine IR verification: call %zu:%zu:%zu has an invalid "
            "result contract\n",
            function_index, block_index, instruction_index);
        return false;
    }
    return true;
}

static bool luna_x86_64_machine_verify_instruction_types(
    const LunaX8664MachineModule *module,
    const LunaX8664MachineFunction *function,
    const LunaX8664MachineInstruction *instruction, size_t function_index,
    size_t block_index, size_t instruction_index, bool *argument_used,
    FILE *stream) {
    const LunaX8664MachineType *left_type =
        luna_x86_64_machine_value_type(function, instruction->left);
    const LunaX8664MachineType *right_type =
        luna_x86_64_machine_value_type(function, instruction->right);
    const LunaX8664MachineStackSlot *slot =
        luna_x86_64_machine_slot(function, instruction->slot);

    bool valid = false;
    switch (instruction->opcode) {
    case LUNA_X86_64_MACHINE_CONST_INTEGER:
        valid = luna_x86_64_machine_type_is_integer(instruction->type) &&
                instruction->immediate <=
                    luna_x86_64_machine_integer_mask(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_CONST_FLOAT:
        valid = luna_x86_64_machine_type_is_float(instruction->type) &&
                (instruction->type != LUNA_X86_64_MACHINE_TYPE_F32 ||
                 instruction->immediate <= UINT32_MAX);
        break;
    case LUNA_X86_64_MACHINE_CONST_BOOL:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                instruction->immediate <= 1U;
        break;
    case LUNA_X86_64_MACHINE_CONST_NULL:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                instruction->immediate == 0U;
        break;
    case LUNA_X86_64_MACHINE_LOAD:
        valid =
            slot != NULL && slot->is_scalar && slot->type == instruction->type;
        break;
    case LUNA_X86_64_MACHINE_STORE:
        valid = slot != NULL && slot->is_scalar && left_type != NULL &&
                slot->type == *left_type &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
        valid = slot != NULL &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER;
        break;
    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                instruction->immediate <= (uint64_t)INT32_MAX &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER;
        break;
    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
        valid = instruction->global < module->globals.length &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER;
        break;
    case LUNA_X86_64_MACHINE_ZERO_SLOT:
        valid =
            slot != NULL && instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_MEMORY_COPY:
        valid = left_type != NULL && right_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                *right_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                instruction->immediate > 0U &&
                instruction->immediate <= (uint64_t)INT32_MAX &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                luna_x86_64_machine_type_is_value(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
        valid = left_type != NULL && right_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                *right_type == instruction->memory_type &&
                luna_x86_64_machine_type_is_value(instruction->memory_type) &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_NULL_CHECK:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_U64 &&
                instruction->immediate > 0U &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
        valid = left_type != NULL && right_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                *right_type == LUNA_X86_64_MACHINE_TYPE_U64 &&
                instruction->immediate > 0U &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER;
        break;
    case LUNA_X86_64_MACHINE_NEG_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
        valid = left_type != NULL && *left_type == instruction->type &&
                luna_x86_64_machine_type_is_integer(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_NEG_FLOAT:
        valid = left_type != NULL && *left_type == instruction->type &&
                luna_x86_64_machine_type_is_float(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_BOOL_NOT:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER:
        valid = left_type != NULL &&
                luna_x86_64_machine_type_is_integer(*left_type) &&
                luna_x86_64_machine_type_is_integer(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
        valid = left_type != NULL &&
                luna_x86_64_machine_type_is_float(*left_type) &&
                luna_x86_64_machine_type_is_float(instruction->type) &&
                *left_type != instruction->type;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
        valid = left_type != NULL &&
                luna_x86_64_machine_type_is_integer(*left_type) &&
                luna_x86_64_machine_type_is_float(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
        valid = left_type != NULL &&
                luna_x86_64_machine_type_is_float(*left_type) &&
                luna_x86_64_machine_type_is_integer(instruction->type);
        break;
    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_POINTER &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_U64;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_U64 &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_POINTER;
        break;
    case LUNA_X86_64_MACHINE_ADD_INTEGER:
    case LUNA_X86_64_MACHINE_SUB_INTEGER:
    case LUNA_X86_64_MACHINE_MUL_INTEGER:
    case LUNA_X86_64_MACHINE_DIV_INTEGER:
    case LUNA_X86_64_MACHINE_REM_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER:
        valid = luna_x86_64_machine_type_is_integer(instruction->type) &&
                luna_x86_64_machine_types_match(function, instruction->left,
                                                instruction->right,
                                                instruction->type);
        break;
    case LUNA_X86_64_MACHINE_ADD_FLOAT:
    case LUNA_X86_64_MACHINE_SUB_FLOAT:
    case LUNA_X86_64_MACHINE_MUL_FLOAT:
    case LUNA_X86_64_MACHINE_DIV_FLOAT:
        valid = luna_x86_64_machine_type_is_float(instruction->type) &&
                luna_x86_64_machine_types_match(function, instruction->left,
                                                instruction->right,
                                                instruction->type);
        break;
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                left_type != NULL && right_type != NULL &&
                *left_type == *right_type &&
                luna_x86_64_machine_type_is_value(*left_type);
        break;
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                left_type != NULL &&
                luna_x86_64_machine_type_is_integer(*left_type) &&
                right_type != NULL && *left_type == *right_type;
        break;
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                left_type != NULL &&
                luna_x86_64_machine_type_is_float(*left_type) &&
                right_type != NULL && *left_type == *right_type;
        break;
    case LUNA_X86_64_MACHINE_CALL:
        return luna_x86_64_machine_verify_call(
            module, function, instruction, function_index, block_index,
            instruction_index, argument_used, stream);
    case LUNA_X86_64_MACHINE_JUMP:
        valid = instruction->true_block < function->blocks.length &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_BRANCH:
        valid = left_type != NULL &&
                *left_type == LUNA_X86_64_MACHINE_TYPE_BOOL &&
                instruction->true_block < function->blocks.length &&
                instruction->false_block < function->blocks.length &&
                instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID;
        break;
    case LUNA_X86_64_MACHINE_RETURN:
        valid = instruction->type == LUNA_X86_64_MACHINE_TYPE_VOID &&
                ((function->return_type == LUNA_X86_64_MACHINE_TYPE_VOID &&
                  instruction->left == LUNA_X86_64_MACHINE_INVALID_ID) ||
                 (function->return_type != LUNA_X86_64_MACHINE_TYPE_VOID &&
                  left_type != NULL && *left_type == function->return_type));
        break;
    }

    if (!valid) {
        (void)fprintf(
            stream,
            "machine IR verification: instruction %zu:%zu:%zu has an invalid "
            "target type or operand contract (opcode=%d, type=%d, memory=%d, "
            "left-type=%d, right-type=%d, slot=%" PRIu32 ", immediate=%" PRIu64
            ")\n",
            function_index, block_index, instruction_index,
            (int)instruction->opcode, (int)instruction->type,
            (int)instruction->memory_type,
            left_type == NULL ? -1 : (int)*left_type,
            right_type == NULL ? -1 : (int)*right_type, instruction->slot,
            instruction->immediate);
    }
    return valid;
}

static bool
luna_x86_64_machine_verify_function(const LunaX8664MachineModule *module,
                                    const LunaX8664MachineFunction *function,
                                    size_t function_index, FILE *stream) {
    if (function->module_name.data == NULL ||
        function->module_name.length == 0U || function->name.data == NULL ||
        function->name.length == 0U ||
        function->linkage < LUNA_X86_64_MACHINE_LINKAGE_INTERNAL ||
        function->linkage > LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C ||
        function->return_type == LUNA_X86_64_MACHINE_TYPE_INVALID) {
        (void)fprintf(stream,
                      "machine IR verification: function %zu has an invalid "
                      "identity or signature\n",
                      function_index);
        return false;
    }
    if (function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT &&
        !function->has_module_metadata_hash) {
        (void)fprintf(stream,
                      "machine IR verification: imported function %zu has no "
                      "metadata identity\n",
                      function_index);
        return false;
    }
    if ((function->linkage == LUNA_X86_64_MACHINE_LINKAGE_INTERNAL ||
         function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C) &&
        function->has_module_metadata_hash) {
        (void)fprintf(stream,
                      "machine IR verification: function %zu has an "
                      "unexpected metadata identity\n",
                      function_index);
        return false;
    }

    if (function->parameter_types.element_size !=
            sizeof(LunaX8664MachineType) ||
        function->slots.element_size != sizeof(LunaX8664MachineStackSlot) ||
        function->value_types.element_size != sizeof(LunaX8664MachineType) ||
        function->arguments.element_size !=
            sizeof(LunaX8664MachineVirtualRegister) ||
        function->blocks.element_size != sizeof(LunaX8664MachineBlock)) {
        (void)fprintf(stream,
                      "machine IR verification: function %zu has malformed "
                      "vector storage\n",
                      function_index);
        return false;
    }

    uint32_t general_parameter_count = 0U;
    uint32_t float_parameter_count = 0U;
    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->parameter_types, index);
        if (type == NULL || !luna_x86_64_machine_type_is_value(*type)) {
            (void)fprintf(
                stream,
                "machine IR verification: function %zu parameter %zu has an "
                "invalid target type\n",
                function_index, index);
            return false;
        }
        if (luna_x86_64_machine_type_is_float(*type)) {
            float_parameter_count += 1U;
        } else {
            general_parameter_count += 1U;
        }
    }
    if (general_parameter_count > 6U || float_parameter_count > 8U) {
        (void)fprintf(stream,
                      "machine IR verification: function %zu exceeds the "
                      "implemented register argument ABI\n",
                      function_index);
        return false;
    }

    const bool is_declaration =
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (is_declaration) {
        if (function->slots.length != 0U ||
            function->value_types.length != 0U ||
            function->arguments.length != 0U || function->blocks.length != 0U) {
            (void)fprintf(
                stream, "machine IR verification: declaration %zu has a body\n",
                function_index);
            return false;
        }
        return true;
    }
    if (function->blocks.length == 0U ||
        function->slots.length < function->parameter_types.length) {
        (void)fprintf(stream,
                      "machine IR verification: definition %zu has no valid "
                      "body or parameter homes\n",
                      function_index);
        return false;
    }

    for (size_t slot_index = 0U; slot_index < function->slots.length;
         slot_index += 1U) {
        const LunaX8664MachineStackSlot *slot =
            luna_vector_at_const(&function->slots, slot_index);
        if (slot == NULL || slot->size_bytes == 0U ||
            slot->alignment_bytes == 0U ||
            (slot->alignment_bytes & (slot->alignment_bytes - 1U)) != 0U ||
            (slot->is_scalar &&
             (!luna_x86_64_machine_type_is_value(slot->type) ||
              slot->size_bytes != 8U || slot->alignment_bytes != 8U)) ||
            (!slot->is_scalar && slot->type != LUNA_X86_64_MACHINE_TYPE_VOID)) {
            (void)fprintf(stream,
                          "machine IR verification: function %zu slot %zu has "
                          "an invalid layout\n",
                          function_index, slot_index);
            return false;
        }
        if (slot_index < function->parameter_types.length) {
            const LunaX8664MachineType *parameter_type =
                luna_vector_at_const(&function->parameter_types, slot_index);
            if (!slot->is_scalar || parameter_type == NULL ||
                slot->type != *parameter_type) {
                (void)fprintf(
                    stream,
                    "machine IR verification: function %zu parameter home %zu "
                    "has the wrong target type\n",
                    function_index, slot_index);
                return false;
            }
        }
    }

    for (size_t value_index = 0U; value_index < function->value_types.length;
         value_index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->value_types, value_index);
        if (type == NULL || !luna_x86_64_machine_type_is_value(*type)) {
            (void)fprintf(
                stream,
                "machine IR verification: function %zu virtual register %zu "
                "has an invalid target type\n",
                function_index, value_index);
            return false;
        }
    }

    uint8_t *definition_counts =
        calloc(function->value_types.length, sizeof(uint8_t));
    bool *defined_in_block = calloc(function->value_types.length, sizeof(bool));
    bool *argument_used = calloc(function->arguments.length, sizeof(bool));
    uint32_t *computed_predecessors =
        calloc(function->blocks.length, sizeof(uint32_t));
    if ((function->value_types.length > 0U &&
         (definition_counts == NULL || defined_in_block == NULL)) ||
        (function->arguments.length > 0U && argument_used == NULL) ||
        computed_predecessors == NULL) {
        free(computed_predecessors);
        free(argument_used);
        free(defined_in_block);
        free(definition_counts);
        (void)fputs(
            "machine IR verification: out of memory during verification\n",
            stream);
        return false;
    }

    bool success = true;
    for (size_t block_index = 0U;
         success && block_index < function->blocks.length; block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (function->value_types.length > 0U) {
            memset(defined_in_block, 0,
                   function->value_types.length * sizeof(bool));
        }
        if (block == NULL ||
            block->instructions.element_size !=
                sizeof(LunaX8664MachineInstruction) ||
            (block->instructions.length == 0U
                 ? block_index == 0U || block->predecessor_count != 0U
                 : !block->terminated)) {
            (void)fprintf(stream,
                          "machine IR verification: function %zu block %zu "
                          "has invalid storage or termination metadata\n",
                          function_index, block_index);
            success = false;
            break;
        }

        for (size_t instruction_index = 0U;
             success && instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (instruction == NULL ||
                !luna_x86_64_machine_opcode_is_valid(instruction->opcode)) {
                (void)fprintf(
                    stream,
                    "machine IR verification: instruction %zu:%zu:%zu has an "
                    "invalid opcode\n",
                    function_index, block_index, instruction_index);
                success = false;
                break;
            }

            const bool is_last =
                instruction_index + 1U == block->instructions.length;
            if (luna_x86_64_machine_opcode_is_terminator(instruction->opcode) !=
                is_last) {
                (void)fprintf(
                    stream,
                    "machine IR verification: instruction %zu:%zu:%zu has an "
                    "invalid terminator position\n",
                    function_index, block_index, instruction_index);
                success = false;
                break;
            }

            LunaX8664MachineVirtualRegister definition =
                LUNA_X86_64_MACHINE_INVALID_ID;
            const bool has_definition =
                luna_x86_64_machine_instruction_definition(instruction,
                                                           &definition);
            LunaX8664MachineInstruction definition_shape = *instruction;
            definition_shape.result = 0U;
            LunaX8664MachineVirtualRegister ignored_definition =
                LUNA_X86_64_MACHINE_INVALID_ID;
            const bool opcode_can_define =
                luna_x86_64_machine_instruction_definition(&definition_shape,
                                                           &ignored_definition);
            const bool requires_definition =
                instruction->opcode == LUNA_X86_64_MACHINE_CALL
                    ? instruction->type != LUNA_X86_64_MACHINE_TYPE_VOID
                    : opcode_can_define;
            if ((requires_definition && !has_definition) ||
                (!requires_definition &&
                 instruction->opcode != LUNA_X86_64_MACHINE_CALL &&
                 instruction->result != LUNA_X86_64_MACHINE_INVALID_ID) ||
                (has_definition &&
                 (definition >= function->value_types.length ||
                  definition_counts[definition] != 0U))) {
                (void)fprintf(
                    stream,
                    "machine IR verification: instruction %zu:%zu:%zu has an "
                    "invalid or duplicate definition\n",
                    function_index, block_index, instruction_index);
                success = false;
                break;
            }
            if (has_definition) {
                const LunaX8664MachineType *definition_type =
                    luna_x86_64_machine_value_type(function, definition);
                if (definition_type == NULL ||
                    *definition_type != instruction->type) {
                    (void)fprintf(
                        stream,
                        "machine IR verification: instruction %zu:%zu:%zu "
                        "definition type does not match its virtual register\n",
                        function_index, block_index, instruction_index);
                    success = false;
                    break;
                }
            }

            const uint32_t use_count =
                luna_x86_64_machine_instruction_use_count(instruction);
            for (uint32_t use_index = 0U; use_index < use_count;
                 use_index += 1U) {
                const LunaX8664MachineVirtualRegister use =
                    luna_x86_64_machine_instruction_use(function, instruction,
                                                        use_index);
                if (use >= function->value_types.length ||
                    defined_in_block == NULL || !defined_in_block[use]) {
                    (void)fprintf(
                        stream,
                        "machine IR verification: instruction %zu:%zu:%zu use "
                        "%" PRIu32
                        " is invalid, used before its definition or crosses a "
                        "block boundary\n",
                        function_index, block_index, instruction_index,
                        use_index);
                    success = false;
                    break;
                }
            }
            if (!success ||
                !luna_x86_64_machine_verify_instruction_types(
                    module, function, instruction, function_index, block_index,
                    instruction_index, argument_used, stream)) {
                success = false;
                break;
            }
            if (has_definition) {
                definition_counts[definition] = 1U;
                defined_in_block[definition] = true;
            }

            if (instruction->opcode == LUNA_X86_64_MACHINE_JUMP ||
                instruction->opcode == LUNA_X86_64_MACHINE_BRANCH) {
                if (computed_predecessors[instruction->true_block] ==
                    UINT32_MAX) {
                    success = false;
                    break;
                }
                computed_predecessors[instruction->true_block] += 1U;
            }
            if (instruction->opcode == LUNA_X86_64_MACHINE_BRANCH) {
                if (computed_predecessors[instruction->false_block] ==
                    UINT32_MAX) {
                    success = false;
                    break;
                }
                computed_predecessors[instruction->false_block] += 1U;
            }
        }
    }

    for (size_t value_index = 0U;
         success && value_index < function->value_types.length;
         value_index += 1U) {
        if (definition_counts[value_index] != 1U) {
            (void)fprintf(stream,
                          "machine IR verification: function %zu virtual "
                          "register %zu is not defined exactly once\n",
                          function_index, value_index);
            success = false;
        }
    }
    for (size_t argument_index = 0U;
         success && argument_index < function->arguments.length;
         argument_index += 1U) {
        if (argument_used == NULL || !argument_used[argument_index]) {
            (void)fprintf(
                stream,
                "machine IR verification: function %zu call argument %zu is "
                "not owned by exactly one call\n",
                function_index, argument_index);
            success = false;
        }
    }
    for (size_t block_index = 0U;
         success && block_index < function->blocks.length; block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block == NULL ||
            block->predecessor_count != computed_predecessors[block_index]) {
            (void)fprintf(stream,
                          "machine IR verification: function %zu block %zu "
                          "has incorrect predecessor metadata\n",
                          function_index, block_index);
            success = false;
        }
    }

    free(computed_predecessors);
    free(argument_used);
    free(defined_in_block);
    free(definition_counts);
    return success;
}

bool luna_x86_64_machine_verify(const LunaX8664MachineModule *module,
                                FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL) {
        (void)fputs("machine IR verification: module is null\n", stream);
        return false;
    }
    if (!luna_target_info_is_supported(module->target) ||
        module->globals.element_size != sizeof(LunaX8664MachineGlobal) ||
        module->functions.element_size != sizeof(LunaX8664MachineFunction) ||
        (module->kind != LUNA_X86_64_MACHINE_MODULE_EXECUTABLE &&
         module->kind != LUNA_X86_64_MACHINE_MODULE_LIBRARY)) {
        (void)fputs(
            "machine IR verification: target or module storage is invalid\n",
            stream);
        return false;
    }
    for (size_t global_index = 0U; global_index < module->globals.length;
         global_index += 1U) {
        const LunaX8664MachineGlobal *global =
            luna_vector_at_const(&module->globals, global_index);
        if (global == NULL || global->bytes.element_size != sizeof(uint8_t) ||
            global->bytes.length == 0U || global->alignment_bytes == 0U ||
            (global->alignment_bytes & (global->alignment_bytes - 1U)) != 0U) {
            (void)fprintf(stream,
                          "machine IR verification: global %zu has an invalid "
                          "layout\n",
                          global_index);
            return false;
        }
    }

    if (module->kind == LUNA_X86_64_MACHINE_MODULE_EXECUTABLE) {
        const LunaX8664MachineFunction *entry =
            luna_x86_64_machine_module_function_const(module,
                                                      module->entry_function);
        if (entry == NULL ||
            entry->linkage != LUNA_X86_64_MACHINE_LINKAGE_INTERNAL ||
            entry->return_type != LUNA_X86_64_MACHINE_TYPE_I32 ||
            entry->parameter_types.length != 0U) {
            (void)fputs("machine IR verification: executable entry must be an "
                        "internal fn() -> i32\n",
                        stream);
            return false;
        }
    } else if (module->entry_function != LUNA_X86_64_MACHINE_INVALID_ID) {
        (void)fputs("machine IR verification: library must not have an entry\n",
                    stream);
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (function == NULL || !luna_x86_64_machine_verify_function(
                                    module, function, function_index, stream)) {
            return false;
        }
    }
    return true;
}
