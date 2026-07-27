#ifndef LUNA_X86_64_REGISTER_ALLOCATION_INTERNAL_H
#define LUNA_X86_64_REGISTER_ALLOCATION_INTERNAL_H

#include "luna/backend/x86_64/register_allocation.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LunaX8664SortedInterval {
    LunaX8664MachineVirtualRegister virtual_register;
    uint64_t start;
    uint64_t end;
} LunaX8664SortedInterval;

void luna_x86_64_function_register_allocation_destroy_internal(
    LunaX8664FunctionRegisterAllocation *allocation);
bool luna_x86_64_register_allocation_build_intervals_internal(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    LunaX8664FunctionRegisterAllocation *allocation);
bool luna_x86_64_register_allocation_sort_intervals_internal(
    const LunaX8664FunctionRegisterAllocation *allocation,
    LunaVector *sorted_intervals);
uint64_t luna_x86_64_physical_register_bit_internal(
    LunaX8664PhysicalRegister physical_register);

#endif
