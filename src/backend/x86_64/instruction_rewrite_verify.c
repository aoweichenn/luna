#include "instruction_rewrite_internal.h"

#include "register_allocation_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static FILE *luna_x86_64_instruction_rewrite_error_stream(FILE *error_stream) {
    return error_stream == NULL ? stderr : error_stream;
}

static bool luna_x86_64_instruction_rewrite_locations_equal(
    const LunaX8664VirtualRegisterAllocation *left,
    const LunaX8664VirtualRegisterAllocation *right) {
    return left != NULL && right != NULL && left->kind == right->kind &&
           left->physical_register == right->physical_register &&
           left->spill_slot == right->spill_slot;
}

static bool luna_x86_64_instruction_rewrite_verify_vector(
    const LunaVector *vector, size_t element_size, FILE *stream,
    size_t function_index, const char *name) {
    if (vector->element_size != element_size ||
        vector->capacity < vector->length ||
        (vector->length != 0U && vector->data == NULL)) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu has invalid %s storage\n",
                      function_index, name);
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_function_shape(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    FILE *stream) {
    if (!luna_x86_64_instruction_rewrite_verify_vector(
            &rewrite->value_locations,
            sizeof(LunaX8664VirtualRegisterAllocation), stream, function_index,
            "value-location") ||
        !luna_x86_64_instruction_rewrite_verify_vector(
            &rewrite->instructions, sizeof(LunaX8664RewrittenInstruction),
            stream, function_index, "instruction") ||
        rewrite->value_locations.length != function->value_types.length ||
        rewrite->instructions.length != (size_t)allocation->instruction_count ||
        rewrite->instruction_count != allocation->instruction_count ||
        rewrite->spill_slot_count != allocation->spill_slot_count ||
        rewrite->used_register_mask != allocation->used_register_mask ||
        rewrite->used_callee_saved_register_mask !=
            allocation->used_callee_saved_register_mask) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu has stale result shape\n",
                      function_index);
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_value_locations(
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    FILE *stream) {
    const uint64_t reserved_mask =
        luna_x86_64_instruction_rewrite_reserved_register_mask();
    for (size_t value_index = 0U; value_index < rewrite->value_locations.length;
         value_index += 1U) {
        const LunaX8664VirtualRegisterAllocation *expected =
            luna_vector_at_const(&allocation->allocations, value_index);
        const LunaX8664VirtualRegisterAllocation *actual =
            luna_vector_at_const(&rewrite->value_locations, value_index);
        if (!luna_x86_64_instruction_rewrite_locations_equal(expected,
                                                             actual)) {
            (void)fprintf(stream,
                          "x86-64 instruction rewrite verification: function "
                          "%zu value %zu has a stale location\n",
                          function_index, value_index);
            return false;
        }
        if (actual->kind == LUNA_X86_64_ALLOCATION_REGISTER &&
            (luna_x86_64_physical_register_bit_internal(
                 actual->physical_register) &
             reserved_mask) != 0U) {
            (void)fprintf(stream,
                          "x86-64 instruction rewrite verification: function "
                          "%zu value %zu occupies a fixed register\n",
                          function_index, value_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_uses(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664MachineInstruction *source,
    const LunaX8664RewrittenInstruction *instruction, size_t function_index,
    FILE *stream) {
    const uint32_t expected_count =
        luna_x86_64_machine_instruction_use_count(source);
    if (!luna_x86_64_instruction_rewrite_verify_vector(
            &instruction->use_locations,
            sizeof(LunaX8664VirtualRegisterAllocation), stream, function_index,
            "instruction-use") ||
        instruction->use_locations.length != expected_count) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu instruction %" PRIu64 " has invalid use locations\n",
                      function_index, instruction->position);
        return false;
    }
    for (uint32_t use_index = 0U; use_index < expected_count; use_index += 1U) {
        const LunaX8664MachineVirtualRegister virtual_register =
            luna_x86_64_machine_instruction_use(function, source, use_index);
        const LunaX8664VirtualRegisterAllocation *expected =
            luna_vector_at_const(&allocation->allocations,
                                 (size_t)virtual_register);
        const LunaX8664VirtualRegisterAllocation *actual = luna_vector_at_const(
            &instruction->use_locations, (size_t)use_index);
        if (!luna_x86_64_instruction_rewrite_locations_equal(expected,
                                                             actual)) {
            (void)fprintf(stream,
                          "x86-64 instruction rewrite verification: function "
                          "%zu instruction %" PRIu64 " use %" PRIu32
                          " has a stale location\n",
                          function_index, instruction->position, use_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_result(
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664MachineInstruction *source,
    const LunaX8664RewrittenInstruction *instruction, size_t function_index,
    FILE *stream) {
    LunaX8664MachineVirtualRegister definition = LUNA_X86_64_MACHINE_INVALID_ID;
    const bool has_definition =
        luna_x86_64_machine_instruction_definition(source, &definition);
    if (instruction->has_result != has_definition) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu instruction %" PRIu64
                      " has invalid result presence\n",
                      function_index, instruction->position);
        return false;
    }
    if (!has_definition) {
        const LunaX8664VirtualRegisterAllocation invalid = {
            .kind = LUNA_X86_64_ALLOCATION_INVALID,
            .physical_register = LUNA_X86_64_PHYSICAL_REGISTER_INVALID,
            .spill_slot = LUNA_X86_64_MACHINE_INVALID_ID,
        };
        return luna_x86_64_instruction_rewrite_locations_equal(
            &instruction->result_location, &invalid);
    }
    const LunaX8664VirtualRegisterAllocation *expected =
        luna_vector_at_const(&allocation->allocations, (size_t)definition);
    if (!luna_x86_64_instruction_rewrite_locations_equal(
            expected, &instruction->result_location)) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu instruction %" PRIu64
                      " has a stale result location\n",
                      function_index, instruction->position);
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_fixed_registers(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineInstruction *source,
    const LunaX8664RewrittenInstruction *instruction, size_t function_index,
    FILE *stream) {
    LunaX8664InstructionFixedRegisters expected;
    if (!luna_x86_64_instruction_fixed_registers_internal(module, abi, source,
                                                          &expected) ||
        instruction->fixed_input_register_mask != expected.input_mask ||
        instruction->fixed_output_register_mask != expected.output_mask ||
        instruction->clobbered_register_mask != expected.clobbered_mask ||
        instruction->parallel_move_destination_mask !=
            expected.parallel_move_destination_mask ||
        instruction->parallel_move_count != expected.parallel_move_count) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu instruction %" PRIu64
                      " has stale fixed-register constraints\n",
                      function_index, instruction->position);
        return false;
    }
    if ((instruction->fixed_output_register_mask &
         ~instruction->clobbered_register_mask) != 0U ||
        (instruction->parallel_move_destination_mask &
         ~instruction->fixed_input_register_mask) != 0U) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: function "
                      "%zu instruction %" PRIu64
                      " has inconsistent fixed-register masks\n",
                      function_index, instruction->position);
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_clobbers(
    const LunaX8664MachineFunction *function,
    const LunaX8664InstructionLiveness *liveness,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664RewrittenInstruction *instruction, size_t function_index,
    FILE *stream) {
    for (size_t value_index = 0U; value_index < function->value_types.length;
         value_index += 1U) {
        if (!luna_x86_64_live_set_contains(&liveness->live_before,
                                           (uint32_t)value_index) ||
            !luna_x86_64_live_set_contains(&liveness->live_after,
                                           (uint32_t)value_index)) {
            continue;
        }
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (location == NULL) {
            return false;
        }
        if (location->kind == LUNA_X86_64_ALLOCATION_REGISTER &&
            (luna_x86_64_physical_register_bit_internal(
                 location->physical_register) &
             instruction->clobbered_register_mask) != 0U) {
            (void)fprintf(stream,
                          "x86-64 instruction rewrite verification: function "
                          "%zu instruction %" PRIu64
                          " clobbers live-through value %zu\n",
                          function_index, instruction->position, value_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_instruction(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664MachineInstruction *source,
    const LunaX8664InstructionLiveness *liveness,
    const LunaX8664RewrittenInstruction *instruction, uint64_t position,
    size_t function_index, FILE *stream) {
    if (instruction == NULL || source == NULL || liveness == NULL ||
        instruction->position != position ||
        instruction->opcode != source->opcode ||
        !luna_x86_64_instruction_rewrite_verify_uses(function, allocation,
                                                     source, instruction,
                                                     function_index, stream) ||
        !luna_x86_64_instruction_rewrite_verify_result(
            allocation, source, instruction, function_index, stream) ||
        !luna_x86_64_instruction_rewrite_verify_fixed_registers(
            module, abi, source, instruction, function_index, stream) ||
        !luna_x86_64_instruction_rewrite_verify_clobbers(
            function, liveness, allocation, instruction, function_index,
            stream)) {
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_verify_function_instructions(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index,
    FILE *stream) {
    uint64_t position = 0U;
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        const LunaX8664BlockLiveness *block_liveness =
            luna_vector_at_const(&liveness->blocks, block_index);
        if (block == NULL || block_liveness == NULL ||
            block_liveness->instructions.length != block->instructions.length) {
            return false;
        }
        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *source =
                luna_vector_at_const(&block->instructions, instruction_index);
            const LunaX8664InstructionLiveness *instruction_liveness =
                luna_vector_at_const(&block_liveness->instructions,
                                     instruction_index);
            const LunaX8664RewrittenInstruction *instruction =
                luna_vector_at_const(&rewrite->instructions, (size_t)position);
            if (!luna_x86_64_instruction_rewrite_verify_instruction(
                    module, abi, function, allocation, source,
                    instruction_liveness, instruction, position, function_index,
                    stream)) {
                return false;
            }
            position += 1U;
        }
    }
    return position == rewrite->instruction_count;
}

bool luna_x86_64_instruction_rewrite_verify(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite, FILE *error_stream) {
    FILE *stream = luna_x86_64_instruction_rewrite_error_stream(error_stream);
    if (module == NULL || abi == NULL || liveness == NULL ||
        allocation == NULL || rewrite == NULL ||
        !luna_x86_64_abi_verify(module, abi, stream) ||
        !luna_x86_64_liveness_verify(module, liveness, stream) ||
        !luna_x86_64_register_allocation_verify(module, liveness, allocation,
                                                stream) ||
        !luna_x86_64_instruction_rewrite_verify_vector(
            &rewrite->functions, sizeof(LunaX8664FunctionInstructionRewrite),
            stream, 0U, "module-function") ||
        rewrite->functions.length != module->functions.length ||
        allocation->functions.length != module->functions.length ||
        liveness->functions.length != module->functions.length) {
        (void)fprintf(stream,
                      "x86-64 instruction rewrite verification: invalid "
                      "module result shape\n");
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionLiveness *function_liveness =
            luna_vector_at_const(&liveness->functions, function_index);
        const LunaX8664FunctionRegisterAllocation *function_allocation =
            luna_vector_at_const(&allocation->functions, function_index);
        const LunaX8664FunctionInstructionRewrite *function_rewrite =
            luna_vector_at_const(&rewrite->functions, function_index);
        if (function == NULL || function_liveness == NULL ||
            function_allocation == NULL || function_rewrite == NULL ||
            !luna_x86_64_instruction_rewrite_verify_function_shape(
                function, function_allocation, function_rewrite, function_index,
                stream) ||
            !luna_x86_64_instruction_rewrite_verify_value_locations(
                function_allocation, function_rewrite, function_index,
                stream) ||
            !luna_x86_64_instruction_rewrite_verify_function_instructions(
                module, abi, function, function_liveness, function_allocation,
                function_rewrite, function_index, stream)) {
            return false;
        }
    }
    return true;
}
