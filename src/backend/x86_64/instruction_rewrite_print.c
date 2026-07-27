#include "luna/backend/x86_64/instruction_rewrite.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool luna_x86_64_instruction_rewrite_print_function_name(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function) {
    if (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
        (!luna_string_builder_append_view(output, function->module_name) ||
         !luna_string_builder_append_c_string(output, "::"))) {
        return false;
    }
    return luna_string_builder_append_view(output, function->name);
}

static bool
luna_x86_64_instruction_rewrite_print_register_set(LunaStringBuilder *output,
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

static bool luna_x86_64_instruction_rewrite_print_location(
    LunaStringBuilder *output,
    const LunaX8664VirtualRegisterAllocation *location) {
    if (location->kind == LUNA_X86_64_ALLOCATION_REGISTER) {
        return luna_string_builder_append_format(
            output, "%%%s",
            luna_x86_64_physical_register_name(location->physical_register));
    }
    return location->kind == LUNA_X86_64_ALLOCATION_SPILL &&
           luna_string_builder_append_format(output, "spill[%" PRIu32 "]",
                                             location->spill_slot);
}

static bool
luna_x86_64_instruction_rewrite_print_locations(LunaStringBuilder *output,
                                                const LunaVector *locations) {
    if (!luna_string_builder_append_c_string(output, "[")) {
        return false;
    }
    for (size_t index = 0U; index < locations->length; index += 1U) {
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(locations, index);
        if (location == NULL ||
            (index != 0U &&
             !luna_string_builder_append_c_string(output, ", ")) ||
            !luna_x86_64_instruction_rewrite_print_location(output, location)) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "]");
}

static bool luna_x86_64_instruction_rewrite_print_instruction(
    LunaStringBuilder *output,
    const LunaX8664RewrittenInstruction *instruction) {
    if (!luna_string_builder_append_format(
            output, "  @%" PRIu64 " %s uses=", instruction->position,
            luna_x86_64_machine_opcode_name(instruction->opcode)) ||
        !luna_x86_64_instruction_rewrite_print_locations(
            output, &instruction->use_locations) ||
        !luna_string_builder_append_c_string(output, " result=")) {
        return false;
    }
    if (instruction->has_result) {
        if (!luna_x86_64_instruction_rewrite_print_location(
                output, &instruction->result_location)) {
            return false;
        }
    } else if (!luna_string_builder_append_c_string(output, "none")) {
        return false;
    }
    if (!luna_string_builder_append_c_string(output, " fixed-in=") ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, instruction->fixed_input_register_mask) ||
        !luna_string_builder_append_c_string(output, " fixed-out=") ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, instruction->fixed_output_register_mask) ||
        !luna_string_builder_append_c_string(output, " clobbers=") ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, instruction->clobbered_register_mask) ||
        !luna_string_builder_append_format(
            output, " parallel-moves=%" PRIu32 " destinations=",
            instruction->parallel_move_count) ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, instruction->parallel_move_destination_mask) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_print_function(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionInstructionRewrite *rewrite, size_t function_index) {
    const bool is_declaration =
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (!luna_string_builder_append_format(
            output, "%s @f%zu ", is_declaration ? "declare" : "define",
            function_index) ||
        !luna_x86_64_instruction_rewrite_print_function_name(output,
                                                             function) ||
        !luna_string_builder_append_format(
            output,
            " values=%zu instructions=%" PRIu64 " spills=%" PRIu32 " used=",
            rewrite->value_locations.length, rewrite->instruction_count,
            rewrite->spill_slot_count) ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, rewrite->used_register_mask) ||
        !luna_string_builder_append_c_string(output, " callee-saved=") ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, rewrite->used_callee_saved_register_mask) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }
    for (size_t instruction_index = 0U;
         instruction_index < rewrite->instructions.length;
         instruction_index += 1U) {
        const LunaX8664RewrittenInstruction *instruction =
            luna_vector_at_const(&rewrite->instructions, instruction_index);
        if (instruction == NULL ||
            !luna_x86_64_instruction_rewrite_print_instruction(output,
                                                               instruction)) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "\n");
}

bool luna_x86_64_instruction_rewrite_print(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite,
    LunaStringBuilder *output) {
    if (module == NULL || abi == NULL || liveness == NULL ||
        allocation == NULL || rewrite == NULL || output == NULL ||
        output->length != 0U ||
        !luna_x86_64_instruction_rewrite_verify(module, abi, liveness,
                                                allocation, rewrite, NULL) ||
        !luna_string_builder_append_format(output,
                                           "x86-64-instruction-rewrite\n"
                                           "target-triple \"%s\"\n"
                                           "reserved=",
                                           module->target->triple) ||
        !luna_x86_64_instruction_rewrite_print_register_set(
            output, luna_x86_64_instruction_rewrite_reserved_register_mask()) ||
        !luna_string_builder_append_c_string(output, "\n\n")) {
        return false;
    }
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionInstructionRewrite *function_rewrite =
            luna_vector_at_const(&rewrite->functions, function_index);
        if (function == NULL || function_rewrite == NULL ||
            !luna_x86_64_instruction_rewrite_print_function(
                output, function, function_rewrite, function_index)) {
            return false;
        }
    }
    return true;
}
