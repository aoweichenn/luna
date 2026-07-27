#include "liveness_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t luna_x86_64_live_set_word_count(uint32_t value_count) {
    return (size_t)(value_count / 64U) + (value_count % 64U == 0U ? 0U : 1U);
}

bool luna_x86_64_live_set_init_internal(LunaX8664LiveSet *set,
                                        uint32_t value_count) {
    if (set == NULL) {
        return false;
    }
    luna_vector_init(&set->words, sizeof(uint64_t));
    set->value_count = value_count;

    const size_t word_count = luna_x86_64_live_set_word_count(value_count);
    if (!luna_vector_reserve(&set->words, word_count)) {
        luna_vector_destroy(&set->words);
        set->value_count = 0U;
        return false;
    }
    const uint64_t empty_word = 0U;
    for (size_t index = 0U; index < word_count; index += 1U) {
        if (!luna_vector_push(&set->words, &empty_word)) {
            luna_vector_destroy(&set->words);
            set->value_count = 0U;
            return false;
        }
    }
    return true;
}

void luna_x86_64_live_set_destroy_internal(LunaX8664LiveSet *set) {
    if (set == NULL) {
        return;
    }
    luna_vector_destroy(&set->words);
    set->value_count = 0U;
}

void luna_x86_64_live_set_clear_internal(LunaX8664LiveSet *set) {
    if (set == NULL || set->words.data == NULL) {
        return;
    }
    memset(set->words.data, 0, set->words.length * sizeof(uint64_t));
}

static uint64_t *
luna_x86_64_live_set_word(LunaX8664LiveSet *set,
                          LunaX8664MachineVirtualRegister virtual_register) {
    if (set == NULL || virtual_register >= set->value_count ||
        !luna_x86_64_live_set_shape_is_valid_internal(set, set->value_count)) {
        return NULL;
    }
    return luna_vector_at(&set->words, (size_t)(virtual_register / 64U));
}

static const uint64_t *luna_x86_64_live_set_word_const(
    const LunaX8664LiveSet *set,
    LunaX8664MachineVirtualRegister virtual_register) {
    if (set == NULL || virtual_register >= set->value_count ||
        !luna_x86_64_live_set_shape_is_valid_internal(set, set->value_count)) {
        return NULL;
    }
    return luna_vector_at_const(&set->words, (size_t)(virtual_register / 64U));
}

void luna_x86_64_live_set_add_internal(
    LunaX8664LiveSet *set, LunaX8664MachineVirtualRegister virtual_register) {
    uint64_t *word = luna_x86_64_live_set_word(set, virtual_register);
    if (word != NULL) {
        *word |= UINT64_C(1) << (virtual_register % 64U);
    }
}

void luna_x86_64_live_set_remove_internal(
    LunaX8664LiveSet *set, LunaX8664MachineVirtualRegister virtual_register) {
    uint64_t *word = luna_x86_64_live_set_word(set, virtual_register);
    if (word != NULL) {
        *word &= ~(UINT64_C(1) << (virtual_register % 64U));
    }
}

void luna_x86_64_live_set_union_internal(LunaX8664LiveSet *destination,
                                         const LunaX8664LiveSet *source) {
    if (destination == NULL || source == NULL ||
        destination->words.length != source->words.length) {
        return;
    }
    for (size_t index = 0U; index < destination->words.length; index += 1U) {
        uint64_t *destination_word = luna_vector_at(&destination->words, index);
        const uint64_t *source_word =
            luna_vector_at_const(&source->words, index);
        if (destination_word != NULL && source_word != NULL) {
            *destination_word |= *source_word;
        }
    }
}

void luna_x86_64_live_set_subtract_internal(LunaX8664LiveSet *destination,
                                            const LunaX8664LiveSet *source) {
    if (destination == NULL || source == NULL ||
        destination->words.length != source->words.length) {
        return;
    }
    for (size_t index = 0U; index < destination->words.length; index += 1U) {
        uint64_t *destination_word = luna_vector_at(&destination->words, index);
        const uint64_t *source_word =
            luna_vector_at_const(&source->words, index);
        if (destination_word != NULL && source_word != NULL) {
            *destination_word &= ~*source_word;
        }
    }
}

void luna_x86_64_live_set_copy_internal(LunaX8664LiveSet *destination,
                                        const LunaX8664LiveSet *source) {
    if (destination == NULL || source == NULL ||
        destination->words.length != source->words.length ||
        destination->words.data == NULL || source->words.data == NULL) {
        return;
    }
    memcpy(destination->words.data, source->words.data,
           source->words.length * sizeof(uint64_t));
}

bool luna_x86_64_live_set_equal_internal(const LunaX8664LiveSet *left,
                                         const LunaX8664LiveSet *right) {
    if (left == NULL || right == NULL ||
        left->value_count != right->value_count ||
        left->words.length != right->words.length) {
        return false;
    }
    if (left->words.length == 0U) {
        return true;
    }
    if (left->words.data == NULL || right->words.data == NULL) {
        return false;
    }
    return memcmp(left->words.data, right->words.data,
                  left->words.length * sizeof(uint64_t)) == 0;
}

bool luna_x86_64_live_set_shape_is_valid_internal(const LunaX8664LiveSet *set,
                                                  uint32_t value_count) {
    if (set == NULL || set->value_count != value_count ||
        set->words.element_size != sizeof(uint64_t) ||
        set->words.length != luna_x86_64_live_set_word_count(value_count) ||
        set->words.capacity < set->words.length ||
        (set->words.length != 0U && set->words.data == NULL)) {
        return false;
    }
    if (value_count == 0U || value_count % 64U == 0U) {
        return true;
    }
    const uint64_t *last_word =
        luna_vector_at_const(&set->words, set->words.length - 1U);
    const uint32_t used_bits = value_count % 64U;
    const uint64_t valid_mask = (UINT64_C(1) << used_bits) - UINT64_C(1);
    return last_word != NULL && (*last_word & ~valid_mask) == 0U;
}

bool luna_x86_64_live_set_contains(
    const LunaX8664LiveSet *set,
    LunaX8664MachineVirtualRegister virtual_register) {
    const uint64_t *word =
        luna_x86_64_live_set_word_const(set, virtual_register);
    return word != NULL &&
           (*word & (UINT64_C(1) << (virtual_register % 64U))) != 0U;
}

uint32_t luna_x86_64_live_set_count(const LunaX8664LiveSet *set) {
    if (set == NULL ||
        !luna_x86_64_live_set_shape_is_valid_internal(set, set->value_count)) {
        return 0U;
    }
    uint32_t count = 0U;
    for (size_t word_index = 0U; word_index < set->words.length;
         word_index += 1U) {
        const uint64_t *stored_word =
            luna_vector_at_const(&set->words, word_index);
        uint64_t word = stored_word == NULL ? 0U : *stored_word;
        while (word != 0U) {
            word &= word - UINT64_C(1);
            count += 1U;
        }
    }
    return count;
}

void luna_x86_64_instruction_liveness_destroy_internal(
    LunaX8664InstructionLiveness *instruction) {
    if (instruction == NULL) {
        return;
    }
    luna_x86_64_live_set_destroy_internal(&instruction->live_after);
    luna_x86_64_live_set_destroy_internal(&instruction->live_before);
}

void luna_x86_64_block_liveness_destroy_internal(
    LunaX8664BlockLiveness *block) {
    if (block == NULL) {
        return;
    }
    for (size_t instruction_index = 0U;
         instruction_index < block->instructions.length;
         instruction_index += 1U) {
        LunaX8664InstructionLiveness *instruction =
            luna_vector_at(&block->instructions, instruction_index);
        luna_x86_64_instruction_liveness_destroy_internal(instruction);
    }
    luna_vector_destroy(&block->instructions);
    luna_x86_64_live_set_destroy_internal(&block->live_out);
    luna_x86_64_live_set_destroy_internal(&block->live_in);
    luna_x86_64_live_set_destroy_internal(&block->definition);
    luna_x86_64_live_set_destroy_internal(&block->use);
}

void luna_x86_64_function_liveness_destroy_internal(
    LunaX8664FunctionLiveness *function) {
    if (function == NULL) {
        return;
    }
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        LunaX8664BlockLiveness *block =
            luna_vector_at(&function->blocks, block_index);
        luna_x86_64_block_liveness_destroy_internal(block);
    }
    luna_vector_destroy(&function->blocks);
    function->iteration_count = 0U;
    function->value_count = 0U;
}

void luna_x86_64_liveness_init(LunaX8664ModuleLiveness *liveness) {
    if (liveness == NULL) {
        return;
    }
    luna_vector_init(&liveness->functions, sizeof(LunaX8664FunctionLiveness));
}

void luna_x86_64_liveness_destroy(LunaX8664ModuleLiveness *liveness) {
    if (liveness == NULL) {
        return;
    }
    for (size_t function_index = 0U;
         function_index < liveness->functions.length; function_index += 1U) {
        LunaX8664FunctionLiveness *function =
            luna_vector_at(&liveness->functions, function_index);
        luna_x86_64_function_liveness_destroy_internal(function);
    }
    luna_vector_destroy(&liveness->functions);
}

uint32_t luna_x86_64_machine_block_successor_count_internal(
    const LunaX8664MachineBlock *block) {
    if (block == NULL || block->instructions.length == 0U) {
        return 0U;
    }
    const LunaX8664MachineInstruction *terminator = luna_vector_at_const(
        &block->instructions, block->instructions.length - 1U);
    if (terminator == NULL) {
        return 0U;
    }
    if (terminator->opcode == LUNA_X86_64_MACHINE_BRANCH) {
        return 2U;
    }
    return terminator->opcode == LUNA_X86_64_MACHINE_JUMP ? 1U : 0U;
}

LunaX8664MachineBlockId
luna_x86_64_machine_block_successor_internal(const LunaX8664MachineBlock *block,
                                             uint32_t successor_index) {
    const uint32_t successor_count =
        luna_x86_64_machine_block_successor_count_internal(block);
    if (successor_index >= successor_count) {
        return LUNA_X86_64_MACHINE_INVALID_ID;
    }
    const LunaX8664MachineInstruction *terminator = luna_vector_at_const(
        &block->instructions, block->instructions.length - 1U);
    if (terminator == NULL) {
        return LUNA_X86_64_MACHINE_INVALID_ID;
    }
    return successor_index == 0U ? terminator->true_block
                                 : terminator->false_block;
}

static bool
luna_x86_64_instruction_liveness_init(LunaX8664InstructionLiveness *instruction,
                                      uint32_t value_count) {
    memset(instruction, 0, sizeof(*instruction));
    if (!luna_x86_64_live_set_init_internal(&instruction->live_before,
                                            value_count)) {
        return false;
    }
    if (!luna_x86_64_live_set_init_internal(&instruction->live_after,
                                            value_count)) {
        luna_x86_64_live_set_destroy_internal(&instruction->live_before);
        return false;
    }
    return true;
}

static bool
luna_x86_64_block_liveness_init(LunaX8664BlockLiveness *block,
                                const LunaX8664MachineBlock *machine_block,
                                uint32_t value_count) {
    memset(block, 0, sizeof(*block));
    luna_vector_init(&block->instructions,
                     sizeof(LunaX8664InstructionLiveness));
    if (!luna_x86_64_live_set_init_internal(&block->use, value_count) ||
        !luna_x86_64_live_set_init_internal(&block->definition, value_count) ||
        !luna_x86_64_live_set_init_internal(&block->live_in, value_count) ||
        !luna_x86_64_live_set_init_internal(&block->live_out, value_count) ||
        !luna_vector_reserve(&block->instructions,
                             machine_block->instructions.length)) {
        luna_x86_64_block_liveness_destroy_internal(block);
        return false;
    }
    for (size_t instruction_index = 0U;
         instruction_index < machine_block->instructions.length;
         instruction_index += 1U) {
        LunaX8664InstructionLiveness instruction;
        if (!luna_x86_64_instruction_liveness_init(&instruction, value_count)) {
            luna_x86_64_block_liveness_destroy_internal(block);
            return false;
        }
        if (!luna_vector_push(&block->instructions, &instruction)) {
            luna_x86_64_instruction_liveness_destroy_internal(&instruction);
            luna_x86_64_block_liveness_destroy_internal(block);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_function_liveness_init(
    LunaX8664FunctionLiveness *function,
    const LunaX8664MachineFunction *machine_function) {
    memset(function, 0, sizeof(*function));
    luna_vector_init(&function->blocks, sizeof(LunaX8664BlockLiveness));
    if (machine_function->value_types.length > UINT32_MAX ||
        !luna_vector_reserve(&function->blocks,
                             machine_function->blocks.length)) {
        return false;
    }
    function->value_count = (uint32_t)machine_function->value_types.length;
    for (size_t block_index = 0U; block_index < machine_function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        LunaX8664BlockLiveness block;
        if (machine_block == NULL ||
            !luna_x86_64_block_liveness_init(&block, machine_block,
                                             function->value_count)) {
            luna_x86_64_function_liveness_destroy_internal(function);
            return false;
        }
        if (!luna_vector_push(&function->blocks, &block)) {
            luna_x86_64_block_liveness_destroy_internal(&block);
            luna_x86_64_function_liveness_destroy_internal(function);
            return false;
        }
    }
    return true;
}

static void luna_x86_64_liveness_compute_block_sets(
    const LunaX8664MachineFunction *machine_function,
    const LunaX8664MachineBlock *machine_block, LunaX8664BlockLiveness *block) {
    for (size_t instruction_index = 0U;
         instruction_index < machine_block->instructions.length;
         instruction_index += 1U) {
        const LunaX8664MachineInstruction *instruction = luna_vector_at_const(
            &machine_block->instructions, instruction_index);
        const uint32_t use_count =
            luna_x86_64_machine_instruction_use_count(instruction);
        for (uint32_t use_index = 0U; use_index < use_count; use_index += 1U) {
            const LunaX8664MachineVirtualRegister use =
                luna_x86_64_machine_instruction_use(machine_function,
                                                    instruction, use_index);
            if (!luna_x86_64_live_set_contains(&block->definition, use)) {
                luna_x86_64_live_set_add_internal(&block->use, use);
            }
        }

        LunaX8664MachineVirtualRegister definition =
            LUNA_X86_64_MACHINE_INVALID_ID;
        if (luna_x86_64_machine_instruction_definition(instruction,
                                                       &definition)) {
            luna_x86_64_live_set_add_internal(&block->definition, definition);
        }
    }
}

static bool luna_x86_64_liveness_solve_blocks(
    const LunaX8664MachineFunction *machine_function,
    LunaX8664FunctionLiveness *function, LunaX8664LiveSet *scratch_in,
    LunaX8664LiveSet *scratch_out) {
    if (machine_function->blocks.length == 0U) {
        return true;
    }
    bool changed = false;
    do {
        if (function->iteration_count == UINT32_MAX) {
            return false;
        }
        function->iteration_count += 1U;
        changed = false;

        for (size_t reverse_index = machine_function->blocks.length;
             reverse_index > 0U; reverse_index -= 1U) {
            const size_t block_index = reverse_index - 1U;
            const LunaX8664MachineBlock *machine_block =
                luna_vector_at_const(&machine_function->blocks, block_index);
            LunaX8664BlockLiveness *block =
                luna_vector_at(&function->blocks, block_index);
            luna_x86_64_live_set_clear_internal(scratch_out);

            const uint32_t successor_count =
                luna_x86_64_machine_block_successor_count_internal(
                    machine_block);
            for (uint32_t successor_index = 0U;
                 successor_index < successor_count; successor_index += 1U) {
                const LunaX8664MachineBlockId successor_id =
                    luna_x86_64_machine_block_successor_internal(
                        machine_block, successor_index);
                const LunaX8664BlockLiveness *successor = luna_vector_at_const(
                    &function->blocks, (size_t)successor_id);
                luna_x86_64_live_set_union_internal(scratch_out,
                                                    &successor->live_in);
            }

            luna_x86_64_live_set_copy_internal(scratch_in, scratch_out);
            luna_x86_64_live_set_subtract_internal(scratch_in,
                                                   &block->definition);
            luna_x86_64_live_set_union_internal(scratch_in, &block->use);
            if (!luna_x86_64_live_set_equal_internal(scratch_out,
                                                     &block->live_out) ||
                !luna_x86_64_live_set_equal_internal(scratch_in,
                                                     &block->live_in)) {
                luna_x86_64_live_set_copy_internal(&block->live_out,
                                                   scratch_out);
                luna_x86_64_live_set_copy_internal(&block->live_in, scratch_in);
                changed = true;
            }
        }
    } while (changed);
    return true;
}

static void luna_x86_64_liveness_compute_instructions(
    const LunaX8664MachineFunction *machine_function,
    LunaX8664FunctionLiveness *function, LunaX8664LiveSet *scratch) {
    for (size_t block_index = 0U; block_index < machine_function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        LunaX8664BlockLiveness *block =
            luna_vector_at(&function->blocks, block_index);
        luna_x86_64_live_set_copy_internal(scratch, &block->live_out);

        for (size_t reverse_index = machine_block->instructions.length;
             reverse_index > 0U; reverse_index -= 1U) {
            const size_t instruction_index = reverse_index - 1U;
            const LunaX8664MachineInstruction *machine_instruction =
                luna_vector_at_const(&machine_block->instructions,
                                     instruction_index);
            LunaX8664InstructionLiveness *instruction =
                luna_vector_at(&block->instructions, instruction_index);
            luna_x86_64_live_set_copy_internal(&instruction->live_after,
                                               scratch);

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
            luna_x86_64_live_set_copy_internal(&instruction->live_before,
                                               scratch);
        }
    }
}

static bool luna_x86_64_liveness_compute_function(
    const LunaX8664MachineFunction *machine_function,
    LunaX8664FunctionLiveness *function) {
    if (!luna_x86_64_function_liveness_init(function, machine_function)) {
        return false;
    }
    for (size_t block_index = 0U; block_index < machine_function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            luna_vector_at_const(&machine_function->blocks, block_index);
        LunaX8664BlockLiveness *block =
            luna_vector_at(&function->blocks, block_index);
        luna_x86_64_liveness_compute_block_sets(machine_function, machine_block,
                                                block);
    }

    LunaX8664LiveSet scratch_in = {0};
    LunaX8664LiveSet scratch_out = {0};
    const bool scratch_ready =
        luna_x86_64_live_set_init_internal(&scratch_in,
                                           function->value_count) &&
        luna_x86_64_live_set_init_internal(&scratch_out, function->value_count);
    if (!scratch_ready) {
        luna_x86_64_live_set_destroy_internal(&scratch_out);
        luna_x86_64_live_set_destroy_internal(&scratch_in);
        luna_x86_64_function_liveness_destroy_internal(function);
        return false;
    }

    const bool solved = luna_x86_64_liveness_solve_blocks(
        machine_function, function, &scratch_in, &scratch_out);
    if (solved) {
        luna_x86_64_liveness_compute_instructions(machine_function, function,
                                                  &scratch_in);
    }
    luna_x86_64_live_set_destroy_internal(&scratch_out);
    luna_x86_64_live_set_destroy_internal(&scratch_in);
    if (!solved) {
        luna_x86_64_function_liveness_destroy_internal(function);
    }
    return solved;
}

bool luna_x86_64_liveness_analyze(const LunaX8664MachineModule *module,
                                  LunaX8664ModuleLiveness *liveness,
                                  FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || liveness == NULL ||
        liveness->functions.element_size != sizeof(LunaX8664FunctionLiveness) ||
        liveness->functions.length != 0U ||
        !luna_x86_64_machine_verify(module, stream)) {
        (void)fputs("x86-64 liveness analysis: invalid input state\n", stream);
        return false;
    }

    LunaX8664ModuleLiveness computed;
    luna_x86_64_liveness_init(&computed);
    if (!luna_vector_reserve(&computed.functions, module->functions.length)) {
        (void)fputs("x86-64 liveness analysis: out of memory\n", stream);
        luna_x86_64_liveness_destroy(&computed);
        return false;
    }

    bool success = true;
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *machine_function =
            luna_vector_at_const(&module->functions, function_index);
        LunaX8664FunctionLiveness function;
        if (machine_function == NULL || !luna_x86_64_liveness_compute_function(
                                            machine_function, &function)) {
            success = false;
            break;
        }
        if (!luna_vector_push(&computed.functions, &function)) {
            luna_x86_64_function_liveness_destroy_internal(&function);
            success = false;
            break;
        }
    }
    if (!success) {
        (void)fputs("x86-64 liveness analysis: out of memory or iteration "
                    "limit exceeded\n",
                    stream);
        luna_x86_64_liveness_destroy(&computed);
        return false;
    }

    if (!luna_x86_64_liveness_verify(module, &computed, stream)) {
        (void)fputs("x86-64 liveness analysis: result verification failed\n",
                    stream);
        luna_x86_64_liveness_destroy(&computed);
        return false;
    }
    luna_vector_destroy(&liveness->functions);
    *liveness = computed;
    return true;
}
