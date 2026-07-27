#include "abi_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static bool luna_x86_64_abi_return_locations_equal(
    const LunaX8664AbiReturnLocation *left,
    const LunaX8664AbiReturnLocation *right) {
    if (left->is_aggregate != right->is_aggregate ||
        left->uses_hidden_pointer != right->uses_hidden_pointer ||
        left->size_bytes != right->size_bytes ||
        left->alignment_bytes != right->alignment_bytes ||
        left->piece_count != right->piece_count) {
        return false;
    }
    for (uint32_t index = 0U; index < left->piece_count; index += 1U) {
        if (left->pieces[index].abi_class != right->pieces[index].abi_class ||
            left->pieces[index].register_index !=
                right->pieces[index].register_index ||
            left->pieces[index].value_offset_bytes !=
                right->pieces[index].value_offset_bytes ||
            left->pieces[index].size_bytes != right->pieces[index].size_bytes) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_abi_parameter_locations_equal(
    const LunaX8664AbiParameterLocation *left,
    const LunaX8664AbiParameterLocation *right) {
    if (left->abi_class != right->abi_class || left->kind != right->kind ||
        left->register_index != right->register_index ||
        left->stack_offset_bytes != right->stack_offset_bytes ||
        left->is_aggregate != right->is_aggregate ||
        left->size_bytes != right->size_bytes ||
        left->alignment_bytes != right->alignment_bytes ||
        left->stack_size_bytes != right->stack_size_bytes ||
        left->piece_count != right->piece_count) {
        return false;
    }
    for (uint32_t index = 0U; index < left->piece_count; index += 1U) {
        if (left->pieces[index].abi_class != right->pieces[index].abi_class ||
            left->pieces[index].kind != right->pieces[index].kind ||
            left->pieces[index].register_index !=
                right->pieces[index].register_index ||
            left->pieces[index].value_offset_bytes !=
                right->pieces[index].value_offset_bytes ||
            left->pieces[index].size_bytes != right->pieces[index].size_bytes) {
            return false;
        }
    }
    return true;
}

static bool
luna_x86_64_abi_verify_function(const LunaX8664MachineFunction *function,
                                const LunaX8664FunctionAbi *actual,
                                size_t function_index, FILE *stream) {
    if (actual->parameter_locations.element_size !=
            sizeof(LunaX8664AbiParameterLocation) ||
        actual->parameter_locations.length !=
            function->parameter_types.length ||
        actual->parameter_locations.capacity <
            actual->parameter_locations.length ||
        (actual->parameter_locations.length != 0U &&
         actual->parameter_locations.data == NULL)) {
        (void)fprintf(stream,
                      "x86-64 ABI verification: function %zu has malformed "
                      "location storage\n",
                      function_index);
        return false;
    }

    LunaX8664FunctionAbi expected;
    if (!luna_x86_64_abi_classify_function_internal(function, &expected)) {
        (void)fprintf(stream,
                      "x86-64 ABI verification: function %zu cannot be "
                      "classified\n",
                      function_index);
        return false;
    }
    bool success =
        actual->general_register_count == expected.general_register_count &&
        actual->vector_register_count == expected.vector_register_count &&
        actual->stack_argument_size_bytes ==
            expected.stack_argument_size_bytes &&
        actual->call_frame_size_bytes == expected.call_frame_size_bytes &&
        luna_x86_64_abi_return_locations_equal(&actual->return_location,
                                               &expected.return_location);
    for (size_t index = 0U;
         success && index < actual->parameter_locations.length; index += 1U) {
        const LunaX8664AbiParameterLocation *actual_location =
            luna_vector_at_const(&actual->parameter_locations, index);
        const LunaX8664AbiParameterLocation *expected_location =
            luna_vector_at_const(&expected.parameter_locations, index);
        if (actual_location == NULL || expected_location == NULL ||
            !luna_x86_64_abi_parameter_locations_equal(actual_location,
                                                       expected_location)) {
            success = false;
        }
    }
    luna_x86_64_abi_function_destroy_internal(&expected);
    if (!success) {
        (void)fprintf(stream,
                      "x86-64 ABI verification: function %zu has stale or "
                      "incorrect parameter locations\n",
                      function_index);
    }
    return success;
}

bool luna_x86_64_abi_verify(const LunaX8664MachineModule *module,
                            const LunaX8664ModuleAbi *abi, FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || abi == NULL ||
        !luna_x86_64_machine_verify(module, stream) ||
        abi->functions.element_size != sizeof(LunaX8664FunctionAbi) ||
        abi->functions.length != module->functions.length ||
        abi->functions.capacity < abi->functions.length ||
        (abi->functions.length != 0U && abi->functions.data == NULL)) {
        (void)fputs("x86-64 ABI verification: invalid module result storage\n",
                    stream);
        return false;
    }

    for (size_t index = 0U; index < module->functions.length; index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, index);
        const LunaX8664FunctionAbi *function_abi =
            luna_vector_at_const(&abi->functions, index);
        if (function == NULL || function_abi == NULL ||
            !luna_x86_64_abi_verify_function(function, function_abi, index,
                                             stream)) {
            return false;
        }
    }
    return true;
}
