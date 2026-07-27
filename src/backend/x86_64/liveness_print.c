#include "luna/backend/x86_64/liveness.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool luna_x86_64_liveness_print_set(LunaStringBuilder *output,
                                           const LunaX8664LiveSet *set) {
    if (!luna_string_builder_append_c_string(output, "{")) {
        return false;
    }
    bool has_value = false;
    for (uint32_t virtual_register = 0U; virtual_register < set->value_count;
         virtual_register += 1U) {
        if (!luna_x86_64_live_set_contains(set, virtual_register)) {
            continue;
        }
        if ((has_value && !luna_string_builder_append_c_string(output, ", ")) ||
            !luna_string_builder_append_format(output, "%%v%" PRIu32,
                                               virtual_register)) {
            return false;
        }
        has_value = true;
    }
    return luna_string_builder_append_c_string(output, "}");
}

static bool luna_x86_64_liveness_print_function_name(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function) {
    if (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
        (!luna_string_builder_append_view(output, function->module_name) ||
         !luna_string_builder_append_c_string(output, "::"))) {
        return false;
    }
    return luna_string_builder_append_view(output, function->name);
}

static bool luna_x86_64_liveness_print_instruction(
    LunaStringBuilder *output, const LunaX8664InstructionLiveness *instruction,
    size_t instruction_index) {
    return luna_string_builder_append_format(
               output, "    i%zu before=", instruction_index) &&
           luna_x86_64_liveness_print_set(output, &instruction->live_before) &&
           luna_string_builder_append_c_string(output, " after=") &&
           luna_x86_64_liveness_print_set(output, &instruction->live_after) &&
           luna_string_builder_append_c_string(output, "\n");
}

static bool
luna_x86_64_liveness_print_block(LunaStringBuilder *output,
                                 const LunaX8664BlockLiveness *block,
                                 size_t block_index) {
    if (!luna_string_builder_append_format(output,
                                           "  bb%zu use=", block_index) ||
        !luna_x86_64_liveness_print_set(output, &block->use) ||
        !luna_string_builder_append_c_string(output, " def=") ||
        !luna_x86_64_liveness_print_set(output, &block->definition) ||
        !luna_string_builder_append_c_string(output, " live-in=") ||
        !luna_x86_64_liveness_print_set(output, &block->live_in) ||
        !luna_string_builder_append_c_string(output, " live-out=") ||
        !luna_x86_64_liveness_print_set(output, &block->live_out) ||
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    for (size_t instruction_index = 0U;
         instruction_index < block->instructions.length;
         instruction_index += 1U) {
        const LunaX8664InstructionLiveness *instruction =
            luna_vector_at_const(&block->instructions, instruction_index);
        if (instruction == NULL ||
            !luna_x86_64_liveness_print_instruction(output, instruction,
                                                    instruction_index)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_liveness_print_function(
    LunaStringBuilder *output, const LunaX8664MachineFunction *machine_function,
    const LunaX8664FunctionLiveness *function, size_t function_index) {
    const bool is_declaration =
        machine_function->linkage ==
            LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        machine_function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (!luna_string_builder_append_format(
            output, "%s @f%zu ", is_declaration ? "declare" : "define",
            function_index) ||
        !luna_x86_64_liveness_print_function_name(output, machine_function) ||
        !luna_string_builder_append_format(
            output, " values=%" PRIu32 " iterations=%" PRIu32 "\n",
            function->value_count, function->iteration_count)) {
        return false;
    }
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664BlockLiveness *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block == NULL ||
            !luna_x86_64_liveness_print_block(output, block, block_index)) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "\n");
}

bool luna_x86_64_liveness_print(const LunaX8664MachineModule *module,
                                const LunaX8664ModuleLiveness *liveness,
                                LunaStringBuilder *output) {
    if (module == NULL || liveness == NULL || output == NULL ||
        output->length != 0U ||
        !luna_x86_64_liveness_verify(module, liveness, NULL) ||
        !luna_string_builder_append_format(output,
                                           "x86-64-liveness\n"
                                           "target-triple \"%s\"\n\n",
                                           module->target->triple)) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *machine_function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionLiveness *function =
            luna_vector_at_const(&liveness->functions, function_index);
        if (machine_function == NULL || function == NULL ||
            !luna_x86_64_liveness_print_function(output, machine_function,
                                                 function, function_index)) {
            return false;
        }
    }
    return true;
}
