#include "abi_internal.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool luna_x86_64_abi_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool luna_x86_64_abi_align_up(uint64_t value, uint32_t alignment,
                                     uint64_t *aligned) {
    if (aligned == NULL || !luna_x86_64_abi_is_power_of_two(alignment) ||
        value > UINT64_MAX - ((uint64_t)alignment - 1U)) {
        return false;
    }
    *aligned = (value + (uint64_t)alignment - 1U) & ~((uint64_t)alignment - 1U);
    return true;
}

const char *luna_x86_64_abi_class_name(LunaX8664AbiClass abi_class) {
    switch (abi_class) {
    case LUNA_X86_64_ABI_CLASS_NO_CLASS:
        return "no-class";
    case LUNA_X86_64_ABI_CLASS_INTEGER:
        return "integer";
    case LUNA_X86_64_ABI_CLASS_SSE:
        return "sse";
    case LUNA_X86_64_ABI_CLASS_MEMORY:
        return "memory";
    case LUNA_X86_64_ABI_CLASS_INVALID:
        break;
    }
    return "invalid";
}

void luna_x86_64_abi_aggregate_layout_init(LunaX8664AbiAggregateLayout *layout,
                                           uint64_t size_bytes,
                                           uint32_t alignment_bytes) {
    if (layout == NULL) {
        return;
    }
    *layout = (LunaX8664AbiAggregateLayout){
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
    };
    luna_vector_init(&layout->components,
                     sizeof(LunaX8664AbiAggregateComponent));
}

void luna_x86_64_abi_aggregate_layout_destroy(
    LunaX8664AbiAggregateLayout *layout) {
    if (layout == NULL) {
        return;
    }
    luna_vector_destroy(&layout->components);
    layout->size_bytes = 0U;
    layout->alignment_bytes = 0U;
}

bool luna_x86_64_abi_aggregate_layout_add_component(
    LunaX8664AbiAggregateLayout *layout, uint64_t offset_bytes,
    uint64_t size_bytes, uint32_t alignment_bytes,
    LunaX8664AbiClass abi_class) {
    const LunaX8664AbiAggregateComponent component = {
        .offset_bytes = offset_bytes,
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
        .abi_class = abi_class,
    };
    return layout != NULL &&
           layout->components.element_size ==
               sizeof(LunaX8664AbiAggregateComponent) &&
           luna_vector_push(&layout->components, &component);
}

static bool luna_x86_64_abi_aggregate_layout_is_valid(
    const LunaX8664AbiAggregateLayout *layout) {
    if (layout == NULL || layout->size_bytes == 0U ||
        !luna_x86_64_abi_is_power_of_two(layout->alignment_bytes) ||
        layout->alignment_bytes > layout->size_bytes ||
        layout->size_bytes % (uint64_t)layout->alignment_bytes != 0U ||
        layout->components.element_size !=
            sizeof(LunaX8664AbiAggregateComponent) ||
        (layout->size_bytes <=
             (uint64_t)LUNA_X86_64_ABI_MAX_REGISTER_EIGHTBYTES *
                 LUNA_X86_64_ABI_EIGHTBYTE_SIZE &&
         layout->components.length == 0U) ||
        layout->components.capacity < layout->components.length ||
        (layout->components.length != 0U && layout->components.data == NULL)) {
        return false;
    }

    for (size_t index = 0U; index < layout->components.length; index += 1U) {
        const LunaX8664AbiAggregateComponent *component =
            luna_vector_at_const(&layout->components, index);
        if (component == NULL || component->size_bytes == 0U ||
            component->size_bytes > LUNA_X86_64_ABI_EIGHTBYTE_SIZE ||
            component->alignment_bytes > LUNA_X86_64_ABI_EIGHTBYTE_SIZE ||
            component->size_bytes > UINT32_MAX ||
            !luna_x86_64_abi_is_power_of_two((uint32_t)component->size_bytes) ||
            !luna_x86_64_abi_is_power_of_two(component->alignment_bytes) ||
            component->alignment_bytes != component->size_bytes ||
            (component->abi_class != LUNA_X86_64_ABI_CLASS_INTEGER &&
             component->abi_class != LUNA_X86_64_ABI_CLASS_SSE) ||
            (component->abi_class == LUNA_X86_64_ABI_CLASS_SSE &&
             component->size_bytes != 4U && component->size_bytes != 8U) ||
            component->offset_bytes > layout->size_bytes ||
            component->size_bytes >
                layout->size_bytes - component->offset_bytes) {
            return false;
        }
    }
    return true;
}

static LunaX8664AbiClass
luna_x86_64_abi_merge_classes(LunaX8664AbiClass left, LunaX8664AbiClass right) {
    if (left == right) {
        return left;
    }
    if (left == LUNA_X86_64_ABI_CLASS_NO_CLASS) {
        return right;
    }
    if (right == LUNA_X86_64_ABI_CLASS_NO_CLASS) {
        return left;
    }
    if (left == LUNA_X86_64_ABI_CLASS_MEMORY ||
        right == LUNA_X86_64_ABI_CLASS_MEMORY) {
        return LUNA_X86_64_ABI_CLASS_MEMORY;
    }
    if (left == LUNA_X86_64_ABI_CLASS_INTEGER ||
        right == LUNA_X86_64_ABI_CLASS_INTEGER) {
        return LUNA_X86_64_ABI_CLASS_INTEGER;
    }
    return LUNA_X86_64_ABI_CLASS_SSE;
}

static void luna_x86_64_abi_classification_set_memory(
    LunaX8664AbiAggregateClassification *classification) {
    *classification = (LunaX8664AbiAggregateClassification){
        .eightbyte_count = 1U,
        .eightbytes = {LUNA_X86_64_ABI_CLASS_MEMORY,
                       LUNA_X86_64_ABI_CLASS_NO_CLASS},
    };
}

bool luna_x86_64_abi_classify_aggregate(
    const LunaX8664AbiAggregateLayout *layout,
    LunaX8664AbiAggregateClassification *classification) {
    if (!luna_x86_64_abi_aggregate_layout_is_valid(layout) ||
        classification == NULL) {
        return false;
    }

    if (layout->size_bytes > (uint64_t)LUNA_X86_64_ABI_MAX_REGISTER_EIGHTBYTES *
                                 LUNA_X86_64_ABI_EIGHTBYTE_SIZE) {
        luna_x86_64_abi_classification_set_memory(classification);
        return true;
    }

    const uint32_t eightbyte_count =
        (uint32_t)((layout->size_bytes +
                    (uint64_t)LUNA_X86_64_ABI_EIGHTBYTE_SIZE - 1U) /
                   (uint64_t)LUNA_X86_64_ABI_EIGHTBYTE_SIZE);
    *classification = (LunaX8664AbiAggregateClassification){
        .eightbyte_count = eightbyte_count,
        .eightbytes = {LUNA_X86_64_ABI_CLASS_NO_CLASS,
                       LUNA_X86_64_ABI_CLASS_NO_CLASS},
    };

    for (size_t index = 0U; index < layout->components.length; index += 1U) {
        const LunaX8664AbiAggregateComponent *component =
            luna_vector_at_const(&layout->components, index);
        if (component->offset_bytes % (uint64_t)component->alignment_bytes !=
            0U) {
            luna_x86_64_abi_classification_set_memory(classification);
            return true;
        }
        const uint64_t first =
            component->offset_bytes / LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
        const uint64_t last =
            (component->offset_bytes + component->size_bytes - 1U) /
            LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
        for (uint64_t eightbyte = first; eightbyte <= last; eightbyte += 1U) {
            classification->eightbytes[eightbyte] =
                luna_x86_64_abi_merge_classes(
                    classification->eightbytes[eightbyte],
                    component->abi_class);
        }
    }
    return true;
}

bool luna_x86_64_abi_aggregate_classification_verify(
    const LunaX8664AbiAggregateLayout *layout,
    const LunaX8664AbiAggregateClassification *classification) {
    LunaX8664AbiAggregateClassification expected;
    return classification != NULL &&
           luna_x86_64_abi_classify_aggregate(layout, &expected) &&
           expected.eightbyte_count == classification->eightbyte_count &&
           expected.eightbytes[0] == classification->eightbytes[0] &&
           expected.eightbytes[1] == classification->eightbytes[1];
}

void luna_x86_64_abi_function_destroy_internal(LunaX8664FunctionAbi *abi) {
    if (abi == NULL) {
        return;
    }
    luna_vector_destroy(&abi->parameter_locations);
    abi->general_register_count = 0U;
    abi->vector_register_count = 0U;
    abi->stack_argument_size_bytes = 0U;
    abi->call_frame_size_bytes = 0U;
    memset(&abi->return_location, 0, sizeof(abi->return_location));
}

static bool luna_x86_64_abi_build_aggregate_layout(
    const LunaX8664MachineAggregateLayout *source,
    LunaX8664AbiAggregateLayout *destination) {
    if (source == NULL || destination == NULL || !source->is_aggregate) {
        return false;
    }
    luna_x86_64_abi_aggregate_layout_init(destination, source->size_bytes,
                                          source->alignment_bytes);
    for (size_t index = 0U; index < source->components.length; index += 1U) {
        const LunaX8664MachineAggregateComponent *component =
            luna_vector_at_const(&source->components, index);
        const LunaX8664AbiClass abi_class =
            component != NULL &&
                    luna_x86_64_machine_type_is_float(component->type)
                ? LUNA_X86_64_ABI_CLASS_SSE
                : LUNA_X86_64_ABI_CLASS_INTEGER;
        if (component == NULL ||
            luna_x86_64_machine_type_register_class(component->type) ==
                LUNA_X86_64_MACHINE_REGISTER_NONE ||
            !luna_x86_64_abi_aggregate_layout_add_component(
                destination, component->offset_bytes, component->size_bytes,
                component->alignment_bytes, abi_class)) {
            luna_x86_64_abi_aggregate_layout_destroy(destination);
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_abi_classify_machine_aggregate(
    const LunaX8664MachineAggregateLayout *source,
    LunaX8664AbiAggregateClassification *classification) {
    LunaX8664AbiAggregateLayout layout;
    if (!luna_x86_64_abi_build_aggregate_layout(source, &layout)) {
        return false;
    }
    const bool success =
        luna_x86_64_abi_classify_aggregate(&layout, classification);
    luna_x86_64_abi_aggregate_layout_destroy(&layout);
    return success;
}

static bool luna_x86_64_abi_add_stack_area(LunaX8664FunctionAbi *abi,
                                           uint64_t size_bytes,
                                           uint32_t alignment_bytes,
                                           uint64_t *offset_bytes,
                                           uint64_t *area_size_bytes) {
    uint64_t aligned_offset = 0U;
    uint64_t aligned_size = 0U;
    const uint32_t stack_alignment =
        alignment_bytes > LUNA_X86_64_ABI_EIGHTBYTE_SIZE
            ? alignment_bytes
            : LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
    if (!luna_x86_64_abi_align_up(abi->stack_argument_size_bytes,
                                  stack_alignment, &aligned_offset) ||
        !luna_x86_64_abi_align_up(size_bytes, LUNA_X86_64_ABI_EIGHTBYTE_SIZE,
                                  &aligned_size) ||
        aligned_offset > (uint64_t)INT32_MAX ||
        aligned_size > (uint64_t)INT32_MAX - aligned_offset) {
        return false;
    }
    *offset_bytes = aligned_offset;
    *area_size_bytes = aligned_size;
    abi->stack_argument_size_bytes = aligned_offset + aligned_size;
    return true;
}

static bool luna_x86_64_abi_add_parameter_location(LunaX8664FunctionAbi *abi,
                                                   LunaX8664AbiClass abi_class,
                                                   uint64_t size_bytes,
                                                   uint32_t alignment_bytes) {
    LunaX8664AbiParameterLocation location = {
        .abi_class = abi_class,
        .kind = LUNA_X86_64_ABI_LOCATION_INVALID,
        .register_index = UINT32_MAX,
        .stack_offset_bytes = UINT64_MAX,
        .size_bytes = size_bytes,
        .alignment_bytes = alignment_bytes,
    };
    if (abi_class == LUNA_X86_64_ABI_CLASS_INTEGER &&
        abi->general_register_count < LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT) {
        location.kind = LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER;
        location.register_index = abi->general_register_count;
        abi->general_register_count += 1U;
    } else if (abi_class == LUNA_X86_64_ABI_CLASS_SSE &&
               abi->vector_register_count <
                   LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT) {
        location.kind = LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER;
        location.register_index = abi->vector_register_count;
        abi->vector_register_count += 1U;
    } else {
        if (!luna_x86_64_abi_add_stack_area(abi, LUNA_X86_64_ABI_EIGHTBYTE_SIZE,
                                            LUNA_X86_64_ABI_EIGHTBYTE_SIZE,
                                            &location.stack_offset_bytes,
                                            &location.stack_size_bytes)) {
            return false;
        }
        location.kind = LUNA_X86_64_ABI_LOCATION_STACK;
    }
    return luna_vector_push(&abi->parameter_locations, &location);
}

static bool luna_x86_64_abi_classify_aggregate_return(
    const LunaX8664MachineAggregateLayout *aggregate,
    LunaX8664FunctionAbi *abi) {
    if (!aggregate->is_aggregate) {
        return true;
    }
    LunaX8664AbiAggregateClassification classification;
    if (!luna_x86_64_abi_classify_machine_aggregate(aggregate,
                                                    &classification)) {
        return false;
    }
    abi->return_location.is_aggregate = true;
    abi->return_location.size_bytes = aggregate->size_bytes;
    abi->return_location.alignment_bytes = aggregate->alignment_bytes;
    if (classification.eightbytes[0] == LUNA_X86_64_ABI_CLASS_MEMORY) {
        abi->return_location.uses_hidden_pointer = true;
        abi->general_register_count = 1U;
        return true;
    }

    uint32_t general_index = 0U;
    uint32_t vector_index = 0U;
    abi->return_location.piece_count = classification.eightbyte_count;
    for (uint32_t index = 0U; index < classification.eightbyte_count;
         index += 1U) {
        const uint64_t offset =
            (uint64_t)index * LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
        const uint64_t remaining = aggregate->size_bytes - offset;
        abi->return_location.pieces[index].abi_class =
            classification.eightbytes[index];
        abi->return_location.pieces[index].register_index =
            classification.eightbytes[index] == LUNA_X86_64_ABI_CLASS_INTEGER
                ? general_index++
                : vector_index++;
        abi->return_location.pieces[index].value_offset_bytes = offset;
        abi->return_location.pieces[index].size_bytes =
            remaining < LUNA_X86_64_ABI_EIGHTBYTE_SIZE
                ? remaining
                : LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
    }
    return true;
}

static bool luna_x86_64_abi_add_aggregate_parameter(
    LunaX8664FunctionAbi *abi,
    const LunaX8664MachineAggregateLayout *aggregate) {
    LunaX8664AbiAggregateClassification classification;
    if (!luna_x86_64_abi_classify_machine_aggregate(aggregate,
                                                    &classification)) {
        return false;
    }
    LunaX8664AbiParameterLocation location = {
        .abi_class = LUNA_X86_64_ABI_CLASS_NO_CLASS,
        .kind = LUNA_X86_64_ABI_LOCATION_INVALID,
        .register_index = UINT32_MAX,
        .stack_offset_bytes = UINT64_MAX,
        .is_aggregate = true,
        .size_bytes = aggregate->size_bytes,
        .alignment_bytes = aggregate->alignment_bytes,
    };

    uint32_t needed_general = 0U;
    uint32_t needed_vector = 0U;
    for (uint32_t index = 0U; index < classification.eightbyte_count;
         index += 1U) {
        if (classification.eightbytes[index] == LUNA_X86_64_ABI_CLASS_INTEGER) {
            needed_general += 1U;
        } else if (classification.eightbytes[index] ==
                   LUNA_X86_64_ABI_CLASS_SSE) {
            needed_vector += 1U;
        }
    }

    const bool must_use_memory =
        classification.eightbytes[0] == LUNA_X86_64_ABI_CLASS_MEMORY ||
        needed_general > LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT -
                             abi->general_register_count ||
        needed_vector >
            LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT - abi->vector_register_count;
    if (must_use_memory) {
        location.abi_class = LUNA_X86_64_ABI_CLASS_MEMORY;
        location.kind = LUNA_X86_64_ABI_LOCATION_STACK;
        if (!luna_x86_64_abi_add_stack_area(
                abi, aggregate->size_bytes, aggregate->alignment_bytes,
                &location.stack_offset_bytes, &location.stack_size_bytes)) {
            return false;
        }
        return luna_vector_push(&abi->parameter_locations, &location);
    }

    location.piece_count = classification.eightbyte_count;
    for (uint32_t index = 0U; index < classification.eightbyte_count;
         index += 1U) {
        const LunaX8664AbiClass abi_class = classification.eightbytes[index];
        const uint64_t offset =
            (uint64_t)index * LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
        const uint64_t remaining = aggregate->size_bytes - offset;
        location.pieces[index].abi_class = abi_class;
        location.pieces[index].kind =
            abi_class == LUNA_X86_64_ABI_CLASS_INTEGER
                ? LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER
                : LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER;
        location.pieces[index].register_index =
            abi_class == LUNA_X86_64_ABI_CLASS_INTEGER
                ? abi->general_register_count++
                : abi->vector_register_count++;
        location.pieces[index].value_offset_bytes = offset;
        location.pieces[index].size_bytes =
            remaining < LUNA_X86_64_ABI_EIGHTBYTE_SIZE
                ? remaining
                : LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
    }
    return luna_vector_push(&abi->parameter_locations, &location);
}

bool luna_x86_64_abi_classify_function_internal(
    const LunaX8664MachineFunction *function, LunaX8664FunctionAbi *abi) {
    if (function == NULL || abi == NULL) {
        return false;
    }
    memset(abi, 0, sizeof(*abi));
    luna_vector_init(&abi->parameter_locations,
                     sizeof(LunaX8664AbiParameterLocation));
    if (!luna_vector_reserve(&abi->parameter_locations,
                             function->parameter_types.length)) {
        luna_x86_64_abi_function_destroy_internal(abi);
        return false;
    }
    if (!luna_x86_64_abi_classify_aggregate_return(&function->return_aggregate,
                                                   abi)) {
        luna_x86_64_abi_function_destroy_internal(abi);
        return false;
    }

    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->parameter_types, index);
        const LunaX8664MachineAggregateLayout *aggregate =
            luna_vector_at_const(&function->parameter_aggregates, index);
        if (aggregate != NULL && aggregate->is_aggregate) {
            if (!luna_x86_64_abi_add_aggregate_parameter(abi, aggregate)) {
                luna_x86_64_abi_function_destroy_internal(abi);
                return false;
            }
            continue;
        }
        const LunaX8664AbiClass abi_class =
            type != NULL && luna_x86_64_machine_type_is_float(*type)
                ? LUNA_X86_64_ABI_CLASS_SSE
                : LUNA_X86_64_ABI_CLASS_INTEGER;
        const uint64_t size_bytes =
            type == NULL
                ? 0U
                : (uint64_t)luna_x86_64_machine_type_bit_width(*type) / 8U;
        if (type == NULL || aggregate == NULL || aggregate->is_aggregate ||
            luna_x86_64_machine_type_register_class(*type) ==
                LUNA_X86_64_MACHINE_REGISTER_NONE ||
            size_bytes == 0U ||
            !luna_x86_64_abi_add_parameter_location(abi, abi_class, size_bytes,
                                                    (uint32_t)size_bytes)) {
            luna_x86_64_abi_function_destroy_internal(abi);
            return false;
        }
    }

    if (!luna_x86_64_abi_align_up(abi->stack_argument_size_bytes,
                                  LUNA_X86_64_ABI_STACK_ALIGNMENT,
                                  &abi->call_frame_size_bytes) ||
        abi->call_frame_size_bytes > (uint64_t)INT32_MAX) {
        luna_x86_64_abi_function_destroy_internal(abi);
        return false;
    }
    return true;
}

void luna_x86_64_abi_init(LunaX8664ModuleAbi *abi) {
    if (abi == NULL) {
        return;
    }
    luna_vector_init(&abi->functions, sizeof(LunaX8664FunctionAbi));
}

void luna_x86_64_abi_destroy(LunaX8664ModuleAbi *abi) {
    if (abi == NULL) {
        return;
    }
    for (size_t index = 0U; index < abi->functions.length; index += 1U) {
        LunaX8664FunctionAbi *function = luna_vector_at(&abi->functions, index);
        luna_x86_64_abi_function_destroy_internal(function);
    }
    luna_vector_destroy(&abi->functions);
}

bool luna_x86_64_abi_analyze(const LunaX8664MachineModule *module,
                             LunaX8664ModuleAbi *abi, FILE *error_stream) {
    FILE *stream = error_stream == NULL ? stderr : error_stream;
    if (module == NULL || abi == NULL ||
        abi->functions.element_size != sizeof(LunaX8664FunctionAbi) ||
        abi->functions.length != 0U ||
        !luna_x86_64_machine_verify(module, stream)) {
        (void)fputs("x86-64 ABI analysis: invalid input state\n", stream);
        return false;
    }

    LunaX8664ModuleAbi computed;
    luna_x86_64_abi_init(&computed);
    if (!luna_vector_reserve(&computed.functions, module->functions.length)) {
        luna_x86_64_abi_destroy(&computed);
        (void)fputs("x86-64 ABI analysis: out of memory\n", stream);
        return false;
    }
    for (size_t index = 0U; index < module->functions.length; index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, index);
        LunaX8664FunctionAbi function_abi;
        if (function == NULL || !luna_x86_64_abi_classify_function_internal(
                                    function, &function_abi)) {
            (void)fputs("x86-64 ABI analysis: function classification "
                        "failed\n",
                        stream);
            luna_x86_64_abi_destroy(&computed);
            return false;
        }
        if (!luna_vector_push(&computed.functions, &function_abi)) {
            luna_x86_64_abi_function_destroy_internal(&function_abi);
            luna_x86_64_abi_destroy(&computed);
            (void)fputs("x86-64 ABI analysis: out of memory\n", stream);
            return false;
        }
    }
    if (!luna_x86_64_abi_verify(module, &computed, stream)) {
        luna_x86_64_abi_destroy(&computed);
        return false;
    }
    luna_vector_destroy(&abi->functions);
    *abi = computed;
    return true;
}
