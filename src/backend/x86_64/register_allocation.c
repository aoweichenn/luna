#include "register_allocation_internal.h"

#include "luna/backend/x86_64/instruction_rewrite.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const LunaX8664PhysicalRegister luna_x86_64_register_allocation_order[] =
    {
        LUNA_X86_64_PHYSICAL_REGISTER_RBX,
        LUNA_X86_64_PHYSICAL_REGISTER_R12,
        LUNA_X86_64_PHYSICAL_REGISTER_R13,
        LUNA_X86_64_PHYSICAL_REGISTER_R14,
        LUNA_X86_64_PHYSICAL_REGISTER_R15,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM8,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM9,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM10,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM11,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM12,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM13,
        LUNA_X86_64_PHYSICAL_REGISTER_XMM14,
};

const char *luna_x86_64_physical_register_name(
    LunaX8664PhysicalRegister physical_register) {
    switch (physical_register) {
    case LUNA_X86_64_PHYSICAL_REGISTER_RAX:
        return "rax";
    case LUNA_X86_64_PHYSICAL_REGISTER_RBX:
        return "rbx";
    case LUNA_X86_64_PHYSICAL_REGISTER_RCX:
        return "rcx";
    case LUNA_X86_64_PHYSICAL_REGISTER_RDX:
        return "rdx";
    case LUNA_X86_64_PHYSICAL_REGISTER_RSI:
        return "rsi";
    case LUNA_X86_64_PHYSICAL_REGISTER_RDI:
        return "rdi";
    case LUNA_X86_64_PHYSICAL_REGISTER_R8:
        return "r8";
    case LUNA_X86_64_PHYSICAL_REGISTER_R9:
        return "r9";
    case LUNA_X86_64_PHYSICAL_REGISTER_R10:
        return "r10";
    case LUNA_X86_64_PHYSICAL_REGISTER_R11:
        return "r11";
    case LUNA_X86_64_PHYSICAL_REGISTER_R12:
        return "r12";
    case LUNA_X86_64_PHYSICAL_REGISTER_R13:
        return "r13";
    case LUNA_X86_64_PHYSICAL_REGISTER_R14:
        return "r14";
    case LUNA_X86_64_PHYSICAL_REGISTER_R15:
        return "r15";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM0:
        return "xmm0";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM1:
        return "xmm1";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM2:
        return "xmm2";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM3:
        return "xmm3";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM4:
        return "xmm4";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM5:
        return "xmm5";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM6:
        return "xmm6";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM7:
        return "xmm7";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM8:
        return "xmm8";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM9:
        return "xmm9";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM10:
        return "xmm10";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM11:
        return "xmm11";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM12:
        return "xmm12";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM13:
        return "xmm13";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM14:
        return "xmm14";
    case LUNA_X86_64_PHYSICAL_REGISTER_XMM15:
        return "xmm15";
    case LUNA_X86_64_PHYSICAL_REGISTER_INVALID:
    case LUNA_X86_64_PHYSICAL_REGISTER_COUNT:
        break;
    }
    return "invalid";
}

LunaX8664MachineRegisterClass luna_x86_64_physical_register_class(
    LunaX8664PhysicalRegister physical_register) {
    if (physical_register >= LUNA_X86_64_PHYSICAL_REGISTER_RAX &&
        physical_register <= LUNA_X86_64_PHYSICAL_REGISTER_R15) {
        return LUNA_X86_64_MACHINE_REGISTER_GENERAL;
    }
    if (physical_register >= LUNA_X86_64_PHYSICAL_REGISTER_XMM0 &&
        physical_register <= LUNA_X86_64_PHYSICAL_REGISTER_XMM15) {
        return LUNA_X86_64_MACHINE_REGISTER_FLOAT;
    }
    return LUNA_X86_64_MACHINE_REGISTER_NONE;
}

bool luna_x86_64_physical_register_is_allocatable(
    LunaX8664PhysicalRegister physical_register) {
    const uint64_t bit =
        luna_x86_64_physical_register_bit_internal(physical_register);
    return luna_x86_64_physical_register_class(physical_register) !=
               LUNA_X86_64_MACHINE_REGISTER_NONE &&
           (bit & luna_x86_64_instruction_rewrite_reserved_register_mask()) ==
               0U;
}

bool luna_x86_64_physical_register_is_callee_saved(
    LunaX8664PhysicalRegister physical_register) {
    return physical_register == LUNA_X86_64_PHYSICAL_REGISTER_RBX ||
           (physical_register >= LUNA_X86_64_PHYSICAL_REGISTER_R12 &&
            physical_register <= LUNA_X86_64_PHYSICAL_REGISTER_R15);
}

uint64_t luna_x86_64_physical_register_bit_internal(
    LunaX8664PhysicalRegister physical_register) {
    if (physical_register <= LUNA_X86_64_PHYSICAL_REGISTER_INVALID ||
        physical_register >= LUNA_X86_64_PHYSICAL_REGISTER_COUNT) {
        return 0U;
    }
    return UINT64_C(1) << (uint32_t)physical_register;
}

void luna_x86_64_function_register_allocation_destroy_internal(
    LunaX8664FunctionRegisterAllocation *allocation) {
    if (allocation == NULL) {
        return;
    }
    luna_vector_destroy(&allocation->allocations);
    luna_vector_destroy(&allocation->intervals);
    allocation->used_callee_saved_register_mask = 0U;
    allocation->used_register_mask = 0U;
    allocation->spill_slot_count = 0U;
    allocation->instruction_count = 0U;
}

void luna_x86_64_register_allocation_init(
    LunaX8664ModuleRegisterAllocation *allocation) {
    if (allocation == NULL) {
        return;
    }
    luna_vector_init(&allocation->functions,
                     sizeof(LunaX8664FunctionRegisterAllocation));
}

void luna_x86_64_register_allocation_destroy(
    LunaX8664ModuleRegisterAllocation *allocation) {
    if (allocation == NULL) {
        return;
    }
    for (size_t function_index = 0U;
         function_index < allocation->functions.length; function_index += 1U) {
        LunaX8664FunctionRegisterAllocation *function =
            luna_vector_at(&allocation->functions, function_index);
        luna_x86_64_function_register_allocation_destroy_internal(function);
    }
    luna_vector_destroy(&allocation->functions);
}

static bool luna_x86_64_register_allocation_init_function(
    const LunaX8664MachineFunction *function,
    LunaX8664FunctionRegisterAllocation *allocation) {
    memset(allocation, 0, sizeof(*allocation));
    luna_vector_init(&allocation->intervals, sizeof(LunaX8664LiveInterval));
    luna_vector_init(&allocation->allocations,
                     sizeof(LunaX8664VirtualRegisterAllocation));
    if (!luna_vector_reserve(&allocation->intervals,
                             function->value_types.length) ||
        !luna_vector_reserve(&allocation->allocations,
                             function->value_types.length)) {
        luna_x86_64_function_register_allocation_destroy_internal(allocation);
        return false;
    }

    for (size_t value_index = 0U; value_index < function->value_types.length;
         value_index += 1U) {
        const LunaX8664LiveInterval interval = {
            .start = UINT64_MAX,
            .end = UINT64_MAX,
            .crosses_call = false,
        };
        const LunaX8664VirtualRegisterAllocation location = {
            .kind = LUNA_X86_64_ALLOCATION_INVALID,
            .physical_register = LUNA_X86_64_PHYSICAL_REGISTER_INVALID,
            .spill_slot = LUNA_X86_64_MACHINE_INVALID_ID,
        };
        if (!luna_vector_push(&allocation->intervals, &interval) ||
            !luna_vector_push(&allocation->allocations, &location)) {
            luna_x86_64_function_register_allocation_destroy_internal(
                allocation);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_register_allocation_record_instruction(
    const LunaX8664MachineFunction *function,
    const LunaX8664MachineInstruction *instruction,
    const LunaX8664InstructionLiveness *instruction_liveness,
    LunaX8664FunctionRegisterAllocation *allocation, uint64_t position) {
    LunaX8664MachineVirtualRegister definition = LUNA_X86_64_MACHINE_INVALID_ID;
    if (luna_x86_64_machine_instruction_definition(instruction, &definition)) {
        LunaX8664LiveInterval *interval =
            luna_vector_at(&allocation->intervals, (size_t)definition);
        if (interval == NULL || interval->start != UINT64_MAX) {
            return false;
        }
        interval->start = position;
        interval->end = position;
    }

    const uint32_t use_count =
        luna_x86_64_machine_instruction_use_count(instruction);
    for (uint32_t use_index = 0U; use_index < use_count; use_index += 1U) {
        const LunaX8664MachineVirtualRegister use =
            luna_x86_64_machine_instruction_use(function, instruction,
                                                use_index);
        LunaX8664LiveInterval *interval =
            luna_vector_at(&allocation->intervals, (size_t)use);
        if (interval == NULL || interval->start == UINT64_MAX ||
            interval->start > position) {
            return false;
        }
        if (interval->end < position) {
            interval->end = position;
        }
    }

    if (instruction->opcode == LUNA_X86_64_MACHINE_CALL) {
        for (uint32_t value = 0U; value < function->value_types.length;
             value += 1U) {
            if (luna_x86_64_live_set_contains(
                    &instruction_liveness->live_before, value) &&
                luna_x86_64_live_set_contains(&instruction_liveness->live_after,
                                              value)) {
                LunaX8664LiveInterval *interval =
                    luna_vector_at(&allocation->intervals, (size_t)value);
                if (interval == NULL) {
                    return false;
                }
                interval->crosses_call = true;
            }
        }
    }
    return true;
}

bool luna_x86_64_register_allocation_build_intervals_internal(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    LunaX8664FunctionRegisterAllocation *allocation) {
    if (function == NULL || liveness == NULL || allocation == NULL ||
        function->value_types.length > UINT32_MAX ||
        !luna_x86_64_register_allocation_init_function(function, allocation)) {
        return false;
    }

    uint64_t position = 0U;
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        const LunaX8664BlockLiveness *block_liveness =
            luna_vector_at_const(&liveness->blocks, block_index);
        if (block == NULL || block_liveness == NULL) {
            luna_x86_64_function_register_allocation_destroy_internal(
                allocation);
            return false;
        }
        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            const LunaX8664InstructionLiveness *instruction_liveness =
                luna_vector_at_const(&block_liveness->instructions,
                                     instruction_index);
            if (instruction == NULL || instruction_liveness == NULL ||
                !luna_x86_64_register_allocation_record_instruction(
                    function, instruction, instruction_liveness, allocation,
                    position) ||
                position == UINT64_MAX) {
                luna_x86_64_function_register_allocation_destroy_internal(
                    allocation);
                return false;
            }
            position += 1U;
        }
    }
    allocation->instruction_count = position;

    for (size_t value_index = 0U; value_index < allocation->intervals.length;
         value_index += 1U) {
        const LunaX8664LiveInterval *interval =
            luna_vector_at_const(&allocation->intervals, value_index);
        if (interval == NULL || interval->start == UINT64_MAX ||
            interval->end == UINT64_MAX || interval->end < interval->start) {
            luna_x86_64_function_register_allocation_destroy_internal(
                allocation);
            return false;
        }
    }
    return true;
}

static int
luna_x86_64_register_allocation_compare_intervals(const void *left_pointer,
                                                  const void *right_pointer) {
    const LunaX8664SortedInterval *left = left_pointer;
    const LunaX8664SortedInterval *right = right_pointer;
    if (left->start < right->start) {
        return -1;
    }
    if (left->start > right->start) {
        return 1;
    }
    if (left->end < right->end) {
        return -1;
    }
    if (left->end > right->end) {
        return 1;
    }
    if (left->virtual_register < right->virtual_register) {
        return -1;
    }
    return left->virtual_register > right->virtual_register ? 1 : 0;
}

bool luna_x86_64_register_allocation_sort_intervals_internal(
    const LunaX8664FunctionRegisterAllocation *allocation,
    LunaVector *sorted_intervals) {
    if (allocation == NULL || sorted_intervals == NULL) {
        return false;
    }
    luna_vector_init(sorted_intervals, sizeof(LunaX8664SortedInterval));
    if (!luna_vector_reserve(sorted_intervals, allocation->intervals.length)) {
        return false;
    }
    for (size_t value_index = 0U; value_index < allocation->intervals.length;
         value_index += 1U) {
        if (value_index > UINT32_MAX) {
            luna_vector_destroy(sorted_intervals);
            return false;
        }
        const LunaX8664LiveInterval *interval =
            luna_vector_at_const(&allocation->intervals, value_index);
        const LunaX8664SortedInterval sorted_interval = {
            .virtual_register = (LunaX8664MachineVirtualRegister)value_index,
            .start = interval == NULL ? UINT64_MAX : interval->start,
            .end = interval == NULL ? UINT64_MAX : interval->end,
        };
        if (interval == NULL ||
            !luna_vector_push(sorted_intervals, &sorted_interval)) {
            luna_vector_destroy(sorted_intervals);
            return false;
        }
    }
    if (sorted_intervals->length > 1U) {
        qsort(sorted_intervals->data, sorted_intervals->length,
              sorted_intervals->element_size,
              luna_x86_64_register_allocation_compare_intervals);
    }
    return true;
}

static bool luna_x86_64_register_allocation_register_is_allowed(
    LunaX8664PhysicalRegister physical_register,
    LunaX8664MachineRegisterClass register_class, bool crosses_call) {
    return luna_x86_64_physical_register_is_allocatable(physical_register) &&
           luna_x86_64_physical_register_class(physical_register) ==
               register_class &&
           (!crosses_call ||
            luna_x86_64_physical_register_is_callee_saved(physical_register));
}

static void luna_x86_64_register_allocation_expire_registers(
    const LunaX8664FunctionRegisterAllocation *allocation, uint64_t start,
    LunaX8664MachineVirtualRegister *owners) {
    for (uint32_t register_index = 1U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        const LunaX8664MachineVirtualRegister owner = owners[register_index];
        if (owner == LUNA_X86_64_MACHINE_INVALID_ID) {
            continue;
        }
        const LunaX8664LiveInterval *owner_interval =
            luna_vector_at_const(&allocation->intervals, (size_t)owner);
        if (owner_interval == NULL || owner_interval->end < start) {
            owners[register_index] = LUNA_X86_64_MACHINE_INVALID_ID;
        }
    }
}

static LunaX8664PhysicalRegister
luna_x86_64_register_allocation_find_free_register(
    LunaX8664MachineRegisterClass register_class, bool crosses_call,
    const LunaX8664MachineVirtualRegister *owners) {
    for (size_t order_index = 0U;
         order_index < sizeof(luna_x86_64_register_allocation_order) /
                           sizeof(luna_x86_64_register_allocation_order[0]);
         order_index += 1U) {
        const LunaX8664PhysicalRegister physical_register =
            luna_x86_64_register_allocation_order[order_index];
        if (luna_x86_64_register_allocation_register_is_allowed(
                physical_register, register_class, crosses_call) &&
            owners[(uint32_t)physical_register] ==
                LUNA_X86_64_MACHINE_INVALID_ID) {
            return physical_register;
        }
    }
    return LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
}

static LunaX8664PhysicalRegister
luna_x86_64_register_allocation_find_spill_victim(
    const LunaX8664FunctionRegisterAllocation *allocation,
    LunaX8664MachineRegisterClass register_class, bool crosses_call,
    const LunaX8664MachineVirtualRegister *owners) {
    LunaX8664PhysicalRegister victim = LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    uint64_t victim_end = 0U;
    for (size_t order_index = 0U;
         order_index < sizeof(luna_x86_64_register_allocation_order) /
                           sizeof(luna_x86_64_register_allocation_order[0]);
         order_index += 1U) {
        const LunaX8664PhysicalRegister physical_register =
            luna_x86_64_register_allocation_order[order_index];
        if (!luna_x86_64_register_allocation_register_is_allowed(
                physical_register, register_class, crosses_call)) {
            continue;
        }
        const LunaX8664MachineVirtualRegister owner =
            owners[(uint32_t)physical_register];
        if (owner == LUNA_X86_64_MACHINE_INVALID_ID) {
            continue;
        }
        const LunaX8664LiveInterval *owner_interval =
            luna_vector_at_const(&allocation->intervals, (size_t)owner);
        if (owner_interval != NULL &&
            (victim == LUNA_X86_64_PHYSICAL_REGISTER_INVALID ||
             owner_interval->end > victim_end)) {
            victim = physical_register;
            victim_end = owner_interval->end;
        }
    }
    return victim;
}

static bool luna_x86_64_register_allocation_spill(
    LunaX8664FunctionRegisterAllocation *allocation,
    LunaX8664MachineVirtualRegister virtual_register) {
    if (allocation->spill_slot_count == UINT32_MAX) {
        return false;
    }
    LunaX8664VirtualRegisterAllocation *location =
        luna_vector_at(&allocation->allocations, (size_t)virtual_register);
    if (location == NULL) {
        return false;
    }
    location->kind = LUNA_X86_64_ALLOCATION_SPILL;
    location->physical_register = LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    location->spill_slot = allocation->spill_slot_count;
    allocation->spill_slot_count += 1U;
    return true;
}

static bool luna_x86_64_register_allocation_assign_interval(
    const LunaX8664MachineFunction *function,
    const LunaX8664SortedInterval *sorted_interval,
    LunaX8664FunctionRegisterAllocation *allocation,
    LunaX8664MachineVirtualRegister *owners) {
    const LunaX8664MachineVirtualRegister virtual_register =
        sorted_interval->virtual_register;
    const LunaX8664MachineType *type =
        luna_vector_at_const(&function->value_types, (size_t)virtual_register);
    const LunaX8664LiveInterval *interval =
        luna_vector_at_const(&allocation->intervals, (size_t)virtual_register);
    LunaX8664VirtualRegisterAllocation *location =
        luna_vector_at(&allocation->allocations, (size_t)virtual_register);
    if (type == NULL || interval == NULL || location == NULL) {
        return false;
    }
    const LunaX8664MachineRegisterClass register_class =
        luna_x86_64_machine_type_register_class(*type);
    luna_x86_64_register_allocation_expire_registers(allocation,
                                                     interval->start, owners);

    LunaX8664PhysicalRegister physical_register =
        luna_x86_64_register_allocation_find_free_register(
            register_class, interval->crosses_call, owners);
    if (physical_register == LUNA_X86_64_PHYSICAL_REGISTER_INVALID) {
        const LunaX8664PhysicalRegister victim_register =
            luna_x86_64_register_allocation_find_spill_victim(
                allocation, register_class, interval->crosses_call, owners);
        const LunaX8664MachineVirtualRegister victim =
            victim_register == LUNA_X86_64_PHYSICAL_REGISTER_INVALID
                ? LUNA_X86_64_MACHINE_INVALID_ID
                : owners[(uint32_t)victim_register];
        const LunaX8664LiveInterval *victim_interval =
            luna_vector_at_const(&allocation->intervals, (size_t)victim);
        if (victim != LUNA_X86_64_MACHINE_INVALID_ID &&
            victim_interval != NULL && victim_interval->end > interval->end) {
            if (!luna_x86_64_register_allocation_spill(allocation, victim)) {
                return false;
            }
            physical_register = victim_register;
        }
    }

    if (physical_register == LUNA_X86_64_PHYSICAL_REGISTER_INVALID) {
        return luna_x86_64_register_allocation_spill(allocation,
                                                     virtual_register);
    }
    location->kind = LUNA_X86_64_ALLOCATION_REGISTER;
    location->physical_register = physical_register;
    location->spill_slot = LUNA_X86_64_MACHINE_INVALID_ID;
    owners[(uint32_t)physical_register] = virtual_register;
    return true;
}

static void luna_x86_64_register_allocation_compute_masks(
    LunaX8664FunctionRegisterAllocation *allocation) {
    allocation->used_register_mask = 0U;
    allocation->used_callee_saved_register_mask = 0U;
    for (size_t value_index = 0U; value_index < allocation->allocations.length;
         value_index += 1U) {
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (location == NULL ||
            location->kind != LUNA_X86_64_ALLOCATION_REGISTER) {
            continue;
        }
        const uint64_t register_bit =
            luna_x86_64_physical_register_bit_internal(
                location->physical_register);
        allocation->used_register_mask |= register_bit;
        if (luna_x86_64_physical_register_is_callee_saved(
                location->physical_register)) {
            allocation->used_callee_saved_register_mask |= register_bit;
        }
    }
}

static bool luna_x86_64_register_allocate_function(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    LunaX8664FunctionRegisterAllocation *allocation) {
    if (!luna_x86_64_register_allocation_build_intervals_internal(
            function, liveness, allocation)) {
        return false;
    }

    LunaVector sorted_intervals;
    if (!luna_x86_64_register_allocation_sort_intervals_internal(
            allocation, &sorted_intervals)) {
        luna_x86_64_function_register_allocation_destroy_internal(allocation);
        return false;
    }
    LunaX8664MachineVirtualRegister owners[LUNA_X86_64_PHYSICAL_REGISTER_COUNT];
    for (uint32_t register_index = 0U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        owners[register_index] = LUNA_X86_64_MACHINE_INVALID_ID;
    }

    bool success = true;
    for (size_t interval_index = 0U; interval_index < sorted_intervals.length;
         interval_index += 1U) {
        const LunaX8664SortedInterval *interval =
            luna_vector_at_const(&sorted_intervals, interval_index);
        if (interval == NULL ||
            !luna_x86_64_register_allocation_assign_interval(
                function, interval, allocation, owners)) {
            success = false;
            break;
        }
    }
    luna_vector_destroy(&sorted_intervals);
    if (!success) {
        luna_x86_64_function_register_allocation_destroy_internal(allocation);
        return false;
    }
    luna_x86_64_register_allocation_compute_masks(allocation);
    return true;
}

bool luna_x86_64_register_allocate(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    LunaX8664ModuleRegisterAllocation *allocation, FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || liveness == NULL || allocation == NULL ||
        allocation->functions.element_size !=
            sizeof(LunaX8664FunctionRegisterAllocation) ||
        allocation->functions.length != 0U ||
        !luna_x86_64_liveness_verify(module, liveness, stream)) {
        (void)fputs("x86-64 register allocation: invalid input state\n",
                    stream);
        return false;
    }

    LunaX8664ModuleRegisterAllocation computed;
    luna_x86_64_register_allocation_init(&computed);
    if (!luna_vector_reserve(&computed.functions, module->functions.length)) {
        (void)fputs("x86-64 register allocation: out of memory\n", stream);
        luna_x86_64_register_allocation_destroy(&computed);
        return false;
    }

    bool success = true;
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionLiveness *function_liveness =
            luna_vector_at_const(&liveness->functions, function_index);
        LunaX8664FunctionRegisterAllocation function_allocation;
        if (function == NULL || function_liveness == NULL ||
            !luna_x86_64_register_allocate_function(function, function_liveness,
                                                    &function_allocation)) {
            success = false;
            break;
        }
        if (!luna_vector_push(&computed.functions, &function_allocation)) {
            luna_x86_64_function_register_allocation_destroy_internal(
                &function_allocation);
            success = false;
            break;
        }
    }
    if (!success) {
        (void)fputs("x86-64 register allocation: out of memory or invalid "
                    "interval state\n",
                    stream);
        luna_x86_64_register_allocation_destroy(&computed);
        return false;
    }
    if (!luna_x86_64_register_allocation_verify(module, liveness, &computed,
                                                stream)) {
        (void)fputs("x86-64 register allocation: result verification failed\n",
                    stream);
        luna_x86_64_register_allocation_destroy(&computed);
        return false;
    }
    luna_vector_destroy(&allocation->functions);
    *allocation = computed;
    return true;
}
