#include "register_allocation_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool luna_x86_64_register_allocation_verify_shape(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    if (allocation->intervals.element_size != sizeof(LunaX8664LiveInterval) ||
        allocation->allocations.element_size !=
            sizeof(LunaX8664VirtualRegisterAllocation) ||
        allocation->intervals.length != function->value_types.length ||
        allocation->allocations.length != function->value_types.length ||
        allocation->intervals.capacity < allocation->intervals.length ||
        allocation->allocations.capacity < allocation->allocations.length ||
        (allocation->intervals.length != 0U &&
         allocation->intervals.data == NULL) ||
        (allocation->allocations.length != 0U &&
         allocation->allocations.data == NULL)) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu has invalid result storage\n",
                      function_index);
        return false;
    }
    return true;
}

static bool luna_x86_64_register_allocation_verify_intervals(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    LunaX8664FunctionRegisterAllocation expected = {0};
    if (!luna_x86_64_register_allocation_build_intervals_internal(
            function, liveness, &expected)) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu interval reconstruction failed\n",
                      function_index);
        return false;
    }
    bool success = allocation->instruction_count == expected.instruction_count;
    for (size_t value_index = 0U;
         success && value_index < allocation->intervals.length;
         value_index += 1U) {
        const LunaX8664LiveInterval *actual_interval =
            luna_vector_at_const(&allocation->intervals, value_index);
        const LunaX8664LiveInterval *expected_interval =
            luna_vector_at_const(&expected.intervals, value_index);
        if (actual_interval == NULL || expected_interval == NULL ||
            actual_interval->start != expected_interval->start ||
            actual_interval->end != expected_interval->end ||
            actual_interval->crosses_call != expected_interval->crosses_call) {
            success = false;
        }
    }
    luna_x86_64_function_register_allocation_destroy_internal(&expected);
    if (!success) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu has stale or incorrect live intervals\n",
                      function_index);
    }
    return success;
}

static bool luna_x86_64_register_allocation_verify_spill_slots(
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    bool *spill_slot_used = NULL;
    if (allocation->spill_slot_count != 0U) {
        spill_slot_used =
            calloc((size_t)allocation->spill_slot_count, sizeof(bool));
        if (spill_slot_used == NULL) {
            (void)fputs("x86-64 register allocation verification: out of "
                        "memory\n",
                        stream);
            return false;
        }
    }

    bool success = true;
    uint32_t observed_spill_count = 0U;
    for (size_t value_index = 0U;
         success && value_index < allocation->allocations.length;
         value_index += 1U) {
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (location == NULL ||
            location->kind == LUNA_X86_64_ALLOCATION_INVALID) {
            success = false;
            break;
        }
        if (location->kind == LUNA_X86_64_ALLOCATION_SPILL) {
            if (location->physical_register !=
                    LUNA_X86_64_PHYSICAL_REGISTER_INVALID ||
                location->spill_slot >= allocation->spill_slot_count ||
                spill_slot_used == NULL ||
                spill_slot_used[location->spill_slot]) {
                success = false;
                break;
            }
            spill_slot_used[location->spill_slot] = true;
            observed_spill_count += 1U;
        } else if (location->kind == LUNA_X86_64_ALLOCATION_REGISTER) {
            if (location->spill_slot != LUNA_X86_64_MACHINE_INVALID_ID) {
                success = false;
                break;
            }
        } else {
            success = false;
        }
    }
    if (success && observed_spill_count != allocation->spill_slot_count) {
        success = false;
    }
    for (uint32_t spill_slot = 0U;
         success && spill_slot < allocation->spill_slot_count;
         spill_slot += 1U) {
        if (spill_slot_used == NULL || !spill_slot_used[spill_slot]) {
            success = false;
        }
    }
    free(spill_slot_used);
    if (!success) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu has invalid, duplicate or non-dense spill slots\n",
                      function_index);
    }
    return success;
}

static bool luna_x86_64_register_allocation_verify_locations(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    uint64_t used_register_mask = 0U;
    uint64_t used_callee_saved_register_mask = 0U;
    for (size_t value_index = 0U; value_index < allocation->allocations.length;
         value_index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->value_types, value_index);
        const LunaX8664LiveInterval *interval =
            luna_vector_at_const(&allocation->intervals, value_index);
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (type == NULL || interval == NULL || location == NULL) {
            return false;
        }
        if (location->kind == LUNA_X86_64_ALLOCATION_SPILL) {
            continue;
        }
        const LunaX8664MachineRegisterClass register_class =
            luna_x86_64_machine_type_register_class(*type);
        if (location->kind != LUNA_X86_64_ALLOCATION_REGISTER ||
            !luna_x86_64_physical_register_is_allocatable(
                location->physical_register) ||
            luna_x86_64_physical_register_class(location->physical_register) !=
                register_class ||
            (interval->crosses_call &&
             !luna_x86_64_physical_register_is_callee_saved(
                 location->physical_register))) {
            (void)fprintf(stream,
                          "x86-64 register allocation verification: function "
                          "%zu value %zu has an invalid register constraint\n",
                          function_index, value_index);
            return false;
        }
        const uint64_t register_bit =
            luna_x86_64_physical_register_bit_internal(
                location->physical_register);
        used_register_mask |= register_bit;
        if (luna_x86_64_physical_register_is_callee_saved(
                location->physical_register)) {
            used_callee_saved_register_mask |= register_bit;
        }
    }
    if (allocation->used_register_mask != used_register_mask ||
        allocation->used_callee_saved_register_mask !=
            used_callee_saved_register_mask) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu has incorrect used-register masks\n",
                      function_index);
        return false;
    }
    return true;
}

static bool luna_x86_64_register_allocation_verify_interference(
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    LunaVector sorted_intervals;
    if (!luna_x86_64_register_allocation_sort_intervals_internal(
            allocation, &sorted_intervals)) {
        (void)fputs("x86-64 register allocation verification: out of memory\n",
                    stream);
        return false;
    }
    LunaX8664MachineVirtualRegister owners[LUNA_X86_64_PHYSICAL_REGISTER_COUNT];
    for (uint32_t register_index = 0U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        owners[register_index] = LUNA_X86_64_MACHINE_INVALID_ID;
    }

    bool success = true;
    for (size_t interval_index = 0U;
         success && interval_index < sorted_intervals.length;
         interval_index += 1U) {
        const LunaX8664SortedInterval *sorted_interval =
            luna_vector_at_const(&sorted_intervals, interval_index);
        const LunaX8664VirtualRegisterAllocation *location =
            sorted_interval == NULL
                ? NULL
                : luna_vector_at_const(
                      &allocation->allocations,
                      (size_t)sorted_interval->virtual_register);
        if (sorted_interval == NULL || location == NULL) {
            success = false;
            break;
        }
        if (location->kind != LUNA_X86_64_ALLOCATION_REGISTER) {
            continue;
        }
        const uint32_t register_index = (uint32_t)location->physical_register;
        const LunaX8664MachineVirtualRegister owner = owners[register_index];
        if (owner != LUNA_X86_64_MACHINE_INVALID_ID) {
            const LunaX8664LiveInterval *owner_interval =
                luna_vector_at_const(&allocation->intervals, (size_t)owner);
            if (owner_interval == NULL ||
                owner_interval->end >= sorted_interval->start) {
                success = false;
                break;
            }
        }
        owners[register_index] = sorted_interval->virtual_register;
    }
    luna_vector_destroy(&sorted_intervals);
    if (!success) {
        (void)fprintf(stream,
                      "x86-64 register allocation verification: function "
                      "%zu assigns one register to overlapping intervals\n",
                      function_index);
    }
    return success;
}

static bool luna_x86_64_register_allocation_verify_function(
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionLiveness *liveness,
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index, FILE *stream) {
    return luna_x86_64_register_allocation_verify_shape(
               function, allocation, function_index, stream) &&
           luna_x86_64_register_allocation_verify_intervals(
               function, liveness, allocation, function_index, stream) &&
           luna_x86_64_register_allocation_verify_spill_slots(
               allocation, function_index, stream) &&
           luna_x86_64_register_allocation_verify_locations(
               function, allocation, function_index, stream) &&
           luna_x86_64_register_allocation_verify_interference(
               allocation, function_index, stream);
}

bool luna_x86_64_register_allocation_verify(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation, FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || liveness == NULL || allocation == NULL ||
        !luna_x86_64_liveness_verify(module, liveness, stream) ||
        allocation->functions.element_size !=
            sizeof(LunaX8664FunctionRegisterAllocation) ||
        allocation->functions.length != module->functions.length ||
        allocation->functions.capacity < allocation->functions.length ||
        (allocation->functions.length != 0U &&
         allocation->functions.data == NULL)) {
        (void)fputs("x86-64 register allocation verification: invalid module "
                    "result storage\n",
                    stream);
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
        if (function == NULL || function_liveness == NULL ||
            function_allocation == NULL ||
            !luna_x86_64_register_allocation_verify_function(
                function, function_liveness, function_allocation,
                function_index, stream)) {
            return false;
        }
    }
    return true;
}
