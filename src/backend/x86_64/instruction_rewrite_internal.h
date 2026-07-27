#ifndef LUNA_X86_64_INSTRUCTION_REWRITE_INTERNAL_H
#define LUNA_X86_64_INSTRUCTION_REWRITE_INTERNAL_H

#include "luna/backend/x86_64/instruction_rewrite.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LunaX8664InstructionFixedRegisters {
    uint64_t input_mask;
    uint64_t output_mask;
    uint64_t clobbered_mask;
    uint64_t parallel_move_destination_mask;
    uint32_t parallel_move_count;
} LunaX8664InstructionFixedRegisters;

void luna_x86_64_function_instruction_rewrite_destroy_internal(
    LunaX8664FunctionInstructionRewrite *rewrite);
bool luna_x86_64_instruction_fixed_registers_internal(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineInstruction *instruction,
    LunaX8664InstructionFixedRegisters *fixed_registers);
uint64_t luna_x86_64_instruction_rewrite_caller_saved_mask_internal(void);

#endif
