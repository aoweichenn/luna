#ifndef LUNA_X86_64_LIVENESS_H
#define LUNA_X86_64_LIVENESS_H

#include "luna/backend/x86_64/machine_ir.h"
#include "luna/frontend/support/buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LunaX8664LiveSet {
    LunaVector words;
    uint32_t value_count;
} LunaX8664LiveSet;

typedef struct LunaX8664InstructionLiveness {
    LunaX8664LiveSet live_before;
    LunaX8664LiveSet live_after;
} LunaX8664InstructionLiveness;

typedef struct LunaX8664BlockLiveness {
    LunaX8664LiveSet use;
    LunaX8664LiveSet definition;
    LunaX8664LiveSet live_in;
    LunaX8664LiveSet live_out;
    LunaVector instructions;
} LunaX8664BlockLiveness;

typedef struct LunaX8664FunctionLiveness {
    uint32_t value_count;
    uint32_t iteration_count;
    LunaVector blocks;
} LunaX8664FunctionLiveness;

typedef struct LunaX8664ModuleLiveness {
    LunaVector functions;
} LunaX8664ModuleLiveness;

void luna_x86_64_liveness_init(LunaX8664ModuleLiveness *liveness);
void luna_x86_64_liveness_destroy(LunaX8664ModuleLiveness *liveness);
bool luna_x86_64_liveness_analyze(const LunaX8664MachineModule *module,
                                  LunaX8664ModuleLiveness *liveness,
                                  FILE *error_stream);
bool luna_x86_64_liveness_verify(const LunaX8664MachineModule *module,
                                 const LunaX8664ModuleLiveness *liveness,
                                 FILE *error_stream);
bool luna_x86_64_liveness_print(const LunaX8664MachineModule *module,
                                const LunaX8664ModuleLiveness *liveness,
                                LunaStringBuilder *output);

bool luna_x86_64_live_set_contains(
    const LunaX8664LiveSet *set,
    LunaX8664MachineVirtualRegister virtual_register);
uint32_t luna_x86_64_live_set_count(const LunaX8664LiveSet *set);

#endif
