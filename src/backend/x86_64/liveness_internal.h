#ifndef LUNA_X86_64_LIVENESS_INTERNAL_H
#define LUNA_X86_64_LIVENESS_INTERNAL_H

#include "luna/backend/x86_64/liveness.h"

#include <stdbool.h>
#include <stdint.h>

bool luna_x86_64_live_set_init_internal(LunaX8664LiveSet *set,
                                        uint32_t value_count);
void luna_x86_64_live_set_destroy_internal(LunaX8664LiveSet *set);
void luna_x86_64_live_set_clear_internal(LunaX8664LiveSet *set);
void luna_x86_64_live_set_add_internal(
    LunaX8664LiveSet *set, LunaX8664MachineVirtualRegister virtual_register);
void luna_x86_64_live_set_remove_internal(
    LunaX8664LiveSet *set, LunaX8664MachineVirtualRegister virtual_register);
void luna_x86_64_live_set_union_internal(LunaX8664LiveSet *destination,
                                         const LunaX8664LiveSet *source);
void luna_x86_64_live_set_subtract_internal(LunaX8664LiveSet *destination,
                                            const LunaX8664LiveSet *source);
void luna_x86_64_live_set_copy_internal(LunaX8664LiveSet *destination,
                                        const LunaX8664LiveSet *source);
bool luna_x86_64_live_set_equal_internal(const LunaX8664LiveSet *left,
                                         const LunaX8664LiveSet *right);
bool luna_x86_64_live_set_shape_is_valid_internal(const LunaX8664LiveSet *set,
                                                  uint32_t value_count);

void luna_x86_64_instruction_liveness_destroy_internal(
    LunaX8664InstructionLiveness *instruction);
void luna_x86_64_block_liveness_destroy_internal(LunaX8664BlockLiveness *block);
void luna_x86_64_function_liveness_destroy_internal(
    LunaX8664FunctionLiveness *function);

uint32_t luna_x86_64_machine_block_successor_count_internal(
    const LunaX8664MachineBlock *block);
LunaX8664MachineBlockId
luna_x86_64_machine_block_successor_internal(const LunaX8664MachineBlock *block,
                                             uint32_t successor_index);

#endif
