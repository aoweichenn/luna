#include "luna/backend/x86_64/register_allocation.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *luna_x86_64_register_allocation_class_name(
    LunaX8664MachineRegisterClass register_class) {
    switch (register_class) {
    case LUNA_X86_64_MACHINE_REGISTER_GENERAL:
        return "gpr";
    case LUNA_X86_64_MACHINE_REGISTER_FLOAT:
        return "fpr";
    case LUNA_X86_64_MACHINE_REGISTER_NONE:
        break;
    }
    return "none";
}

static bool luna_x86_64_register_allocation_print_function_name(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function) {
    if (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
        (!luna_string_builder_append_view(output, function->module_name) ||
         !luna_string_builder_append_c_string(output, "::"))) {
        return false;
    }
    return luna_string_builder_append_view(output, function->name);
}

static bool
luna_x86_64_register_allocation_print_register_set(LunaStringBuilder *output,
                                                   uint64_t register_mask) {
    if (!luna_string_builder_append_c_string(output, "{")) {
        return false;
    }
    bool has_register = false;
    for (uint32_t register_index = 1U;
         register_index < LUNA_X86_64_PHYSICAL_REGISTER_COUNT;
         register_index += 1U) {
        const uint64_t register_bit = UINT64_C(1) << register_index;
        if ((register_mask & register_bit) == 0U) {
            continue;
        }
        if ((has_register &&
             !luna_string_builder_append_c_string(output, ", ")) ||
            !luna_string_builder_append_format(
                output, "%%%s",
                luna_x86_64_physical_register_name(
                    (LunaX8664PhysicalRegister)register_index))) {
            return false;
        }
        has_register = true;
    }
    return luna_string_builder_append_c_string(output, "}");
}

static bool luna_x86_64_register_allocation_print_value(
    LunaStringBuilder *output, const LunaX8664MachineType *type,
    const LunaX8664LiveInterval *interval,
    const LunaX8664VirtualRegisterAllocation *location, size_t value_index) {
    if (!luna_string_builder_append_format(
            output,
            "  %%v%zu type=%s class=%s interval=[%" PRIu64 ", %" PRIu64
            "] crosses-call=%s location=",
            value_index, luna_x86_64_machine_type_name(*type),
            luna_x86_64_register_allocation_class_name(
                luna_x86_64_machine_type_register_class(*type)),
            interval->start, interval->end,
            interval->crosses_call ? "yes" : "no")) {
        return false;
    }
    if (location->kind == LUNA_X86_64_ALLOCATION_REGISTER) {
        return luna_string_builder_append_format(
            output, "%%%s\n",
            luna_x86_64_physical_register_name(location->physical_register));
    }
    return location->kind == LUNA_X86_64_ALLOCATION_SPILL &&
           luna_string_builder_append_format(output, "spill[%" PRIu32 "]\n",
                                             location->spill_slot);
}

static bool luna_x86_64_register_allocation_print_function(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    size_t function_index) {
    const bool is_declaration =
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (!luna_string_builder_append_format(
            output, "%s @f%zu ", is_declaration ? "declare" : "define",
            function_index) ||
        !luna_x86_64_register_allocation_print_function_name(output,
                                                             function) ||
        !luna_string_builder_append_format(
            output,
            " values=%zu instructions=%" PRIu64 " spills=%" PRIu32 " used=",
            function->value_types.length, allocation->instruction_count,
            allocation->spill_slot_count) ||
        !luna_x86_64_register_allocation_print_register_set(
            output, allocation->used_register_mask) ||
        !luna_string_builder_append_c_string(output, " callee-saved=") ||
        !luna_x86_64_register_allocation_print_register_set(
            output, allocation->used_callee_saved_register_mask) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    for (size_t value_index = 0U; value_index < function->value_types.length;
         value_index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->value_types, value_index);
        const LunaX8664LiveInterval *interval =
            luna_vector_at_const(&allocation->intervals, value_index);
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (type == NULL || interval == NULL || location == NULL ||
            !luna_x86_64_register_allocation_print_value(
                output, type, interval, location, value_index)) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "\n");
}

bool luna_x86_64_register_allocation_print(
    const LunaX8664MachineModule *module,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    LunaStringBuilder *output) {
    if (module == NULL || liveness == NULL || allocation == NULL ||
        output == NULL || output->length != 0U ||
        !luna_x86_64_register_allocation_verify(module, liveness, allocation,
                                                NULL) ||
        !luna_string_builder_append_format(output,
                                           "x86-64-register-allocation\n"
                                           "target-triple \"%s\"\n\n",
                                           module->target->triple)) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionRegisterAllocation *function_allocation =
            luna_vector_at_const(&allocation->functions, function_index);
        if (function == NULL || function_allocation == NULL ||
            !luna_x86_64_register_allocation_print_function(
                output, function, function_allocation, function_index)) {
            return false;
        }
    }
    return true;
}
