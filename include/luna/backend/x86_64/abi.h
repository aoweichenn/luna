#ifndef LUNA_X86_64_ABI_H
#define LUNA_X86_64_ABI_H

#include "luna/backend/x86_64/machine_ir.h"
#include "luna/frontend/support/buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT = 6,
    LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT = 8,
    LUNA_X86_64_ABI_EIGHTBYTE_SIZE = 8,
    LUNA_X86_64_ABI_STACK_ALIGNMENT = 16,
    LUNA_X86_64_ABI_MAX_REGISTER_EIGHTBYTES = 2
};

typedef enum LunaX8664AbiClass {
    LUNA_X86_64_ABI_CLASS_INVALID,
    LUNA_X86_64_ABI_CLASS_NO_CLASS,
    LUNA_X86_64_ABI_CLASS_INTEGER,
    LUNA_X86_64_ABI_CLASS_SSE,
    LUNA_X86_64_ABI_CLASS_MEMORY
} LunaX8664AbiClass;

typedef enum LunaX8664AbiLocationKind {
    LUNA_X86_64_ABI_LOCATION_INVALID,
    LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER,
    LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER,
    LUNA_X86_64_ABI_LOCATION_STACK
} LunaX8664AbiLocationKind;

typedef struct LunaX8664AbiAggregateComponent {
    uint64_t offset_bytes;
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    LunaX8664AbiClass abi_class;
} LunaX8664AbiAggregateComponent;

typedef struct LunaX8664AbiAggregateLayout {
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    LunaVector components;
} LunaX8664AbiAggregateLayout;

typedef struct LunaX8664AbiAggregateClassification {
    uint32_t eightbyte_count;
    LunaX8664AbiClass eightbytes[LUNA_X86_64_ABI_MAX_REGISTER_EIGHTBYTES];
} LunaX8664AbiAggregateClassification;

typedef struct LunaX8664AbiParameterLocation {
    LunaX8664AbiClass abi_class;
    LunaX8664AbiLocationKind kind;
    uint32_t register_index;
    uint64_t stack_offset_bytes;
} LunaX8664AbiParameterLocation;

typedef struct LunaX8664FunctionAbi {
    uint32_t general_register_count;
    uint32_t vector_register_count;
    uint64_t stack_argument_size_bytes;
    uint64_t call_frame_size_bytes;
    LunaVector parameter_locations;
} LunaX8664FunctionAbi;

typedef struct LunaX8664ModuleAbi {
    LunaVector functions;
} LunaX8664ModuleAbi;

void luna_x86_64_abi_aggregate_layout_init(LunaX8664AbiAggregateLayout *layout,
                                           uint64_t size_bytes,
                                           uint32_t alignment_bytes);
void luna_x86_64_abi_aggregate_layout_destroy(
    LunaX8664AbiAggregateLayout *layout);
bool luna_x86_64_abi_aggregate_layout_add_component(
    LunaX8664AbiAggregateLayout *layout, uint64_t offset_bytes,
    uint64_t size_bytes, uint32_t alignment_bytes, LunaX8664AbiClass abi_class);
bool luna_x86_64_abi_classify_aggregate(
    const LunaX8664AbiAggregateLayout *layout,
    LunaX8664AbiAggregateClassification *classification);
bool luna_x86_64_abi_aggregate_classification_verify(
    const LunaX8664AbiAggregateLayout *layout,
    const LunaX8664AbiAggregateClassification *classification);

void luna_x86_64_abi_init(LunaX8664ModuleAbi *abi);
void luna_x86_64_abi_destroy(LunaX8664ModuleAbi *abi);
bool luna_x86_64_abi_analyze(const LunaX8664MachineModule *module,
                             LunaX8664ModuleAbi *abi, FILE *error_stream);
bool luna_x86_64_abi_verify(const LunaX8664MachineModule *module,
                            const LunaX8664ModuleAbi *abi, FILE *error_stream);
bool luna_x86_64_abi_print(const LunaX8664MachineModule *module,
                           const LunaX8664ModuleAbi *abi,
                           LunaStringBuilder *output);

const char *luna_x86_64_abi_class_name(LunaX8664AbiClass abi_class);

#endif
