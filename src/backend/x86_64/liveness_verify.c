#include "liveness_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static bool luna_x86_64_liveness_verify_instruction_shapes(
    const LunaX8664MachineBlock *machine_block,
    const LunaX8664BlockLiveness *block, uint32_t value_count,
    size_t function_index, size_t block_index, FILE *stream) {
    if (block->instructions.element_size !=
            sizeof(LunaX8664InstructionLiveness) ||
        block->instructions.length != machine_block->instructions.length ||
        block->instructions.capacity < block->instructions.length ||
        (block->instructions.length != 0U &&
         block->instructions.data == NULL)) {
        (void)fprintf(stream,
                      "x86-64 liveness verification: function %zu block %zu "
                      "has invalid instruction result storage\n",
                      function_index, block_index);
        return false;
    }
    for (size_t instruction_index = 0U;
         instruction_index < block->instructions.length;
         instruction_index += 1U) {
        const LunaX8664InstructionLiveness *instruction =
            luna_vector_at_const(&block->instructions, instruction_index);
        if (instruction == NULL ||
            !luna_x86_64_live_set_shape_is_valid_internal(
                &instruction->live_before, value_count) ||
            !luna_x86_64_live_set_shape_is_valid_internal(
                &instruction->live_after, value_count)) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: instruction %zu:%zu:%zu has "
                "invalid live-set storage\n",
                function_index, block_index, instruction_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_liveness_verify_block_shapes(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664FunctionLiveness *function, size_t function_index,
    FILE *stream) {
    if ((size_t)function->value_count != machine_function->value_types.length ||
        function->blocks.element_size != sizeof(LunaX8664BlockLiveness) ||
        function->blocks.length != machine_function->blocks.length ||
        function->blocks.capacity < function->blocks.length ||
        (function->blocks.length != 0U && function->blocks.data == NULL) ||
        (function->blocks.length == 0U && function->iteration_count != 0U) ||
        (function->blocks.length != 0U && function->iteration_count == 0U)) {
        (void)fprintf(
            stream,
            "x86-64 liveness verification: function %zu has invalid result "
            "shape or iteration metadata\n",
            function_index);
        return false;
    }
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        const LunaX8664BlockLiveness *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (machine_block == NULL || block == NULL) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: function %zu block %zu "
                "is missing\n",
                function_index, block_index);
            return false;
        }
        if (!luna_x86_64_live_set_shape_is_valid_internal(
                &block->use, function->value_count) ||
            !luna_x86_64_live_set_shape_is_valid_internal(
                &block->definition, function->value_count) ||
            !luna_x86_64_live_set_shape_is_valid_internal(
                &block->live_in, function->value_count) ||
            !luna_x86_64_live_set_shape_is_valid_internal(
                &block->live_out, function->value_count)) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: function %zu block %zu "
                "has invalid block live-set storage\n",
                function_index, block_index);
            return false;
        }
        if (!luna_x86_64_liveness_verify_instruction_shapes(
                machine_block, block, function->value_count, function_index,
                block_index, stream)) {
            return false;
        }
    }
    return true;
}

static void luna_x86_64_liveness_recompute_block_use_definition(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664MachineBlock *machine_block, LunaX8664LiveSet *use,
    LunaX8664LiveSet *definition) {
    luna_x86_64_live_set_clear_internal(use);
    luna_x86_64_live_set_clear_internal(definition);
    for (size_t instruction_index = 0U;
         instruction_index < machine_block->instructions.length;
         instruction_index += 1U) {
        const LunaX8664MachineInstruction *instruction = luna_vector_at_const(
            &machine_block->instructions, instruction_index);
        const uint32_t use_count =
            luna_x86_64_machine_instruction_use_count(instruction);
        for (uint32_t use_index = 0U; use_index < use_count; use_index += 1U) {
            const LunaX8664MachineVirtualRegister virtual_register =
                luna_x86_64_machine_instruction_use(machine_function,
                                                    instruction, use_index);
            if (!luna_x86_64_live_set_contains(definition, virtual_register)) {
                luna_x86_64_live_set_add_internal(use, virtual_register);
            }
        }

        LunaX8664MachineVirtualRegister virtual_register =
            LUNA_X86_64_MACHINE_INVALID_ID;
        if (luna_x86_64_machine_instruction_definition(instruction,
                                                       &virtual_register)) {
            luna_x86_64_live_set_add_internal(definition, virtual_register);
        }
    }
}

static bool luna_x86_64_liveness_verify_block_equations(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664FunctionLiveness *function, size_t function_index,
    LunaX8664LiveSet *scratch_in, LunaX8664LiveSet *scratch_out, FILE *stream) {
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        const LunaX8664BlockLiveness *block =
            luna_vector_at_const(&function->blocks, block_index);

        luna_x86_64_liveness_recompute_block_use_definition(
            machine_function, machine_block, scratch_in, scratch_out);
        if (!luna_x86_64_live_set_equal_internal(scratch_in, &block->use) ||
            !luna_x86_64_live_set_equal_internal(scratch_out,
                                                 &block->definition)) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: function %zu block %zu has "
                "incorrect use or definition sets\n",
                function_index, block_index);
            return false;
        }

        luna_x86_64_live_set_clear_internal(scratch_out);
        const uint32_t successor_count =
            luna_x86_64_machine_block_successor_count_internal(machine_block);
        for (uint32_t successor_index = 0U; successor_index < successor_count;
             successor_index += 1U) {
            const LunaX8664MachineBlockId successor_id =
                luna_x86_64_machine_block_successor_internal(machine_block,
                                                             successor_index);
            const LunaX8664BlockLiveness *successor =
                luna_vector_at_const(&function->blocks, (size_t)successor_id);
            luna_x86_64_live_set_union_internal(scratch_out,
                                                &successor->live_in);
        }
        luna_x86_64_live_set_copy_internal(scratch_in, scratch_out);
        luna_x86_64_live_set_subtract_internal(scratch_in, &block->definition);
        luna_x86_64_live_set_union_internal(scratch_in, &block->use);
        if (!luna_x86_64_live_set_equal_internal(scratch_out,
                                                 &block->live_out) ||
            !luna_x86_64_live_set_equal_internal(scratch_in, &block->live_in)) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: function %zu block %zu does "
                "not satisfy the data-flow equations\n",
                function_index, block_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_liveness_verify_instruction_equations(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664FunctionLiveness *function, size_t function_index,
    LunaX8664LiveSet *scratch, FILE *stream) {
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        const LunaX8664BlockLiveness *block =
            luna_vector_at_const(&function->blocks, block_index);
        luna_x86_64_live_set_copy_internal(scratch, &block->live_out);

        for (size_t reverse_index = machine_block->instructions.length;
             reverse_index > 0U; reverse_index -= 1U) {
            const size_t instruction_index = reverse_index - 1U;
            const LunaX8664MachineInstruction *machine_instruction =
                luna_vector_at_const(&machine_block->instructions,
                                     instruction_index);
            const LunaX8664InstructionLiveness *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (!luna_x86_64_live_set_equal_internal(
                    scratch, &instruction->live_after)) {
                (void)fprintf(
                    stream,
                    "x86-64 liveness verification: instruction %zu:%zu:%zu "
                    "has an incorrect live-after set\n",
                    function_index, block_index, instruction_index);
                return false;
            }

            LunaX8664MachineVirtualRegister definition =
                LUNA_X86_64_MACHINE_INVALID_ID;
            if (luna_x86_64_machine_instruction_definition(machine_instruction,
                                                           &definition)) {
                luna_x86_64_live_set_remove_internal(scratch, definition);
            }
            const uint32_t use_count =
                luna_x86_64_machine_instruction_use_count(machine_instruction);
            for (uint32_t use_index = 0U; use_index < use_count;
                 use_index += 1U) {
                const LunaX8664MachineVirtualRegister use =
                    luna_x86_64_machine_instruction_use(
                        machine_function, machine_instruction, use_index);
                luna_x86_64_live_set_add_internal(scratch, use);
            }
            if (!luna_x86_64_live_set_equal_internal(
                    scratch, &instruction->live_before)) {
                (void)fprintf(
                    stream,
                    "x86-64 liveness verification: instruction %zu:%zu:%zu "
                    "has an incorrect live-before set\n",
                    function_index, block_index, instruction_index);
                return false;
            }
        }
        if (!luna_x86_64_live_set_equal_internal(scratch, &block->live_in)) {
            (void)fprintf(
                stream,
                "x86-64 liveness verification: function %zu block %zu "
                "instruction transfer does not reach live-in\n",
                function_index, block_index);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_liveness_verify_function(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664FunctionLiveness *function, size_t function_index,
    FILE *stream) {
    if (!luna_x86_64_liveness_verify_block_shapes(machine_function, function,
                                                  function_index, stream)) {
        return false;
    }

    LunaX8664LiveSet scratch_in = {0};
    LunaX8664LiveSet scratch_out = {0};
    if (!luna_x86_64_live_set_init_internal(&scratch_in,
                                            function->value_count) ||
        !luna_x86_64_live_set_init_internal(&scratch_out,
                                            function->value_count)) {
        (void)fputs("x86-64 liveness verification: out of memory\n", stream);
        luna_x86_64_live_set_destroy_internal(&scratch_out);
        luna_x86_64_live_set_destroy_internal(&scratch_in);
        return false;
    }

    const bool success =
        luna_x86_64_liveness_verify_block_equations(machine_function, function,
                                                    function_index, &scratch_in,
                                                    &scratch_out, stream) &&
        luna_x86_64_liveness_verify_instruction_equations(
            machine_function, function, function_index, &scratch_in, stream);
    luna_x86_64_live_set_destroy_internal(&scratch_out);
    luna_x86_64_live_set_destroy_internal(&scratch_in);
    return success;
}

bool luna_x86_64_liveness_verify(const LunaX8664MachineModule *module,
                                 const LunaX8664ModuleLiveness *liveness,
                                 FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || liveness == NULL ||
        !luna_x86_64_machine_verify(module, stream) ||
        liveness->functions.element_size != sizeof(LunaX8664FunctionLiveness) ||
        liveness->functions.length != module->functions.length ||
        liveness->functions.capacity < liveness->functions.length ||
        (liveness->functions.length != 0U &&
         liveness->functions.data == NULL)) {
        (void)fputs("x86-64 liveness verification: invalid module result "
                    "storage\n",
                    stream);
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *machine_function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionLiveness *function =
            luna_vector_at_const(&liveness->functions, function_index);
        if (machine_function == NULL || function == NULL ||
            !luna_x86_64_liveness_verify_function(machine_function, function,
                                                  function_index, stream)) {
            return false;
        }
    }
    return true;
}
