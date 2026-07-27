#ifndef LUNA_X86_64_REGISTER_ALLOCATION_H
#define LUNA_X86_64_REGISTER_ALLOCATION_H

#include "luna/backend/x86_64/liveness.h"
#include "luna/backend/x86_64/machine_ir.h"
#include "luna/frontend/support/buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum LunaX8664PhysicalRegister {
    LUNA_X86_64_PHYSICAL_REGISTER_INVALID,
    LUNA_X86_64_PHYSICAL_REGISTER_RAX,
    LUNA_X86_64_PHYSICAL_REGISTER_RBX,
    LUNA_X86_64_PHYSICAL_REGISTER_RCX,
    LUNA_X86_64_PHYSICAL_REGISTER_RDX,
    LUNA_X86_64_PHYSICAL_REGISTER_RSI,
    LUNA_X86_64_PHYSICAL_REGISTER_RDI,
    LUNA_X86_64_PHYSICAL_REGISTER_R8,
    LUNA_X86_64_PHYSICAL_REGISTER_R9,
    LUNA_X86_64_PHYSICAL_REGISTER_R10,
    LUNA_X86_64_PHYSICAL_REGISTER_R11,
    LUNA_X86_64_PHYSICAL_REGISTER_R12,
    LUNA_X86_64_PHYSICAL_REGISTER_R13,
    LUNA_X86_64_PHYSICAL_REGISTER_R14,
    LUNA_X86_64_PHYSICAL_REGISTER_R15,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM0,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM1,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM2,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM3,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM4,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM5,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM6,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM7,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM8,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM9,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM10,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM11,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM12,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM13,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM14,
    LUNA_X86_64_PHYSICAL_REGISTER_XMM15,
    LUNA_X86_64_PHYSICAL_REGISTER_COUNT
} LunaX8664PhysicalRegister;

typedef enum LunaX8664AllocationKind {
    LUNA_X86_64_ALLOCATION_INVALID,
    LUNA_X86_64_ALLOCATION_REGISTER,
    LUNA_X86_64_ALLOCATION_SPILL
} LunaX8664AllocationKind;

typedef struct LunaX8664LiveInterval {
    uint64_t start;
    uint64_t end;
    bool crosses_call;
} LunaX8664LiveInterval;

typedef struct LunaX8664VirtualRegisterAllocation {
    LunaX8664AllocationKind kind;
    LunaX8664PhysicalRegister physical_register;
    uint32_t spill_slot;
} LunaX8664VirtualRegisterAllocation;

typedef struct LunaX8664FunctionRegisterAllocation {
    uint64_t instruction_count;
    uint32_t spill_slot_count;
    uint64_t used_register_mask;
    uint64_t used_callee_saved_register_mask;
    LunaVector intervals;
    LunaVector allocations;
} LunaX8664FunctionRegisterAllocation;

typedef struct LunaX8664ModuleRegisterAllocation {
    LunaVector functions;
} LunaX8664ModuleRegisterAllocation;

void luna_x86_64_register_allocation_init(
    LunaX8664ModuleRegisterAllocation *allocation);
void luna_x86_64_register_allocation_destroy(
    LunaX8664ModuleRegisterAllocation *allocation);
bool luna_x86_64_register_allocate(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    LunaX8664ModuleRegisterAllocation *allocation, FILE *error_stream);
bool luna_x86_64_register_allocation_verify(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation, FILE *error_stream);
bool luna_x86_64_register_allocation_print(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    LunaStringBuilder *output);

const char *
luna_x86_64_physical_register_name(LunaX8664PhysicalRegister physical_register);
LunaX8664MachineRegisterClass luna_x86_64_physical_register_class(
    LunaX8664PhysicalRegister physical_register);
bool luna_x86_64_physical_register_is_allocatable(
    LunaX8664PhysicalRegister physical_register);
bool luna_x86_64_physical_register_is_callee_saved(
    LunaX8664PhysicalRegister physical_register);

#endif
