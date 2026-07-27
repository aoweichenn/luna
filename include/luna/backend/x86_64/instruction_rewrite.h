#ifndef LUNA_X86_64_INSTRUCTION_REWRITE_H
#define LUNA_X86_64_INSTRUCTION_REWRITE_H

#include "luna/backend/x86_64/abi.h"
#include "luna/backend/x86_64/liveness.h"
#include "luna/backend/x86_64/machine_ir.h"
#include "luna/backend/x86_64/register_allocation.h"
#include "luna/frontend/support/buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LunaX8664RewrittenInstruction {
    uint64_t position;
    LunaX8664MachineOpcode opcode;
    uint64_t fixed_input_register_mask;
    uint64_t fixed_output_register_mask;
    uint64_t clobbered_register_mask;
    uint64_t parallel_move_destination_mask;
    uint32_t parallel_move_count;
    bool has_result;
    LunaX8664VirtualRegisterAllocation result_location;
    LunaVector use_locations;
} LunaX8664RewrittenInstruction;

typedef struct LunaX8664FunctionInstructionRewrite {
    uint64_t instruction_count;
    uint32_t spill_slot_count;
    uint64_t used_register_mask;
    uint64_t used_callee_saved_register_mask;
    LunaVector value_locations;
    LunaVector instructions;
} LunaX8664FunctionInstructionRewrite;

typedef struct LunaX8664ModuleInstructionRewrite {
    LunaVector functions;
} LunaX8664ModuleInstructionRewrite;

void luna_x86_64_instruction_rewrite_init(
    LunaX8664ModuleInstructionRewrite *rewrite);
void luna_x86_64_instruction_rewrite_destroy(
    LunaX8664ModuleInstructionRewrite *rewrite);
bool luna_x86_64_instruction_rewrite_build(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    LunaX8664ModuleInstructionRewrite *rewrite, FILE *error_stream);
bool luna_x86_64_instruction_rewrite_verify(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite, FILE *error_stream);
bool luna_x86_64_instruction_rewrite_print(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite,
    LunaStringBuilder *output);

uint64_t luna_x86_64_instruction_rewrite_reserved_register_mask(void);

#endif
