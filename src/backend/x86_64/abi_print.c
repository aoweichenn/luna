#include "luna/backend/x86_64/abi.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *luna_x86_64_abi_general_register_name(uint32_t index) {
    static const char *names[LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT] = {
        "rdi", "rsi", "rdx", "rcx", "r8", "r9",
    };
    return index < LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT ? names[index]
                                                          : "invalid";
}

static const char *luna_x86_64_abi_vector_register_name(uint32_t index) {
    static const char *names[LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT] = {
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    };
    return index < LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT ? names[index]
                                                         : "invalid";
}

static bool
luna_x86_64_abi_print_function_name(LunaStringBuilder *output,
                                    const LunaX8664MachineFunction *function) {
    if (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
        (!luna_string_builder_append_view(output, function->module_name) ||
         !luna_string_builder_append_c_string(output, "::"))) {
        return false;
    }
    return luna_string_builder_append_view(output, function->name);
}

static bool
luna_x86_64_abi_print_location(LunaStringBuilder *output,
                               const LunaX8664AbiParameterLocation *location) {
    if (location->kind == LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER) {
        return luna_string_builder_append_format(
            output, "%%%s",
            luna_x86_64_abi_general_register_name(location->register_index));
    }
    if (location->kind == LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER) {
        return luna_string_builder_append_format(
            output, "%%%s",
            luna_x86_64_abi_vector_register_name(location->register_index));
    }
    return location->kind == LUNA_X86_64_ABI_LOCATION_STACK &&
           luna_string_builder_append_format(output, "stack[%" PRIu64 "]",
                                             location->stack_offset_bytes);
}

static bool luna_x86_64_abi_print_function(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664FunctionAbi *abi, size_t function_index) {
    const bool is_declaration =
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (!luna_string_builder_append_format(
            output, "%s @f%zu ", is_declaration ? "declare" : "define",
            function_index) ||
        !luna_x86_64_abi_print_function_name(output, function) ||
        !luna_string_builder_append_format(
            output,
            " parameters=%zu gp=%" PRIu32 " sse=%" PRIu32
            " stack-bytes=%" PRIu64 " call-frame=%" PRIu64 "\n",
            function->parameter_types.length, abi->general_register_count,
            abi->vector_register_count, abi->stack_argument_size_bytes,
            abi->call_frame_size_bytes)) {
        return false;
    }

    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->parameter_types, index);
        const LunaX8664AbiParameterLocation *location =
            luna_vector_at_const(&abi->parameter_locations, index);
        if (type == NULL || location == NULL ||
            !luna_string_builder_append_format(
                output, "  p%zu type=%s class=%s location=", index,
                luna_x86_64_machine_type_name(*type),
                luna_x86_64_abi_class_name(location->abi_class)) ||
            !luna_x86_64_abi_print_location(output, location) ||
            !luna_string_builder_append_c_string(output, "\n")) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "\n");
}

bool luna_x86_64_abi_print(const LunaX8664MachineModule *module,
                           const LunaX8664ModuleAbi *abi,
                           LunaStringBuilder *output) {
    if (module == NULL || abi == NULL || output == NULL ||
        output->length != 0U || !luna_x86_64_abi_verify(module, abi, NULL) ||
        !luna_string_builder_append_format(output,
                                           "x86-64-system-v-abi\n"
                                           "target-triple \"%s\"\n\n",
                                           module->target->triple)) {
        return false;
    }

    for (size_t index = 0U; index < module->functions.length; index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, index);
        const LunaX8664FunctionAbi *function_abi =
            luna_vector_at_const(&abi->functions, index);
        if (function == NULL || function_abi == NULL ||
            !luna_x86_64_abi_print_function(output, function, function_abi,
                                            index)) {
            return false;
        }
    }
    return true;
}
