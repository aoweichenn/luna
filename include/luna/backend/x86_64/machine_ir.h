#ifndef LUNA_X86_64_MACHINE_IR_H
#define LUNA_X86_64_MACHINE_IR_H

#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/source/source.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
#include "luna/middleend/ir/ir.h"
#include "luna/target/target.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LUNA_X86_64_MACHINE_INVALID_ID UINT32_MAX

typedef uint32_t LunaX8664MachineVirtualRegister;
typedef uint32_t LunaX8664MachineStackSlotId;
typedef uint32_t LunaX8664MachineBlockId;
typedef uint32_t LunaX8664MachineFunctionId;
typedef uint32_t LunaX8664MachineGlobalId;

typedef enum LunaX8664MachineType {
    LUNA_X86_64_MACHINE_TYPE_INVALID,
    LUNA_X86_64_MACHINE_TYPE_VOID,
    LUNA_X86_64_MACHINE_TYPE_BOOL,
    LUNA_X86_64_MACHINE_TYPE_I8,
    LUNA_X86_64_MACHINE_TYPE_I16,
    LUNA_X86_64_MACHINE_TYPE_I32,
    LUNA_X86_64_MACHINE_TYPE_I64,
    LUNA_X86_64_MACHINE_TYPE_U8,
    LUNA_X86_64_MACHINE_TYPE_U16,
    LUNA_X86_64_MACHINE_TYPE_U32,
    LUNA_X86_64_MACHINE_TYPE_U64,
    LUNA_X86_64_MACHINE_TYPE_F32,
    LUNA_X86_64_MACHINE_TYPE_F64,
    LUNA_X86_64_MACHINE_TYPE_POINTER
} LunaX8664MachineType;

typedef enum LunaX8664MachineRegisterClass {
    LUNA_X86_64_MACHINE_REGISTER_NONE,
    LUNA_X86_64_MACHINE_REGISTER_GENERAL,
    LUNA_X86_64_MACHINE_REGISTER_FLOAT
} LunaX8664MachineRegisterClass;

typedef enum LunaX8664MachineFunctionLinkage {
    LUNA_X86_64_MACHINE_LINKAGE_INTERNAL,
    LUNA_X86_64_MACHINE_LINKAGE_MODULE_EXPORT,
    LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT,
    LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C
} LunaX8664MachineFunctionLinkage;

typedef enum LunaX8664MachineModuleKind {
    LUNA_X86_64_MACHINE_MODULE_EXECUTABLE,
    LUNA_X86_64_MACHINE_MODULE_LIBRARY
} LunaX8664MachineModuleKind;

typedef enum LunaX8664MachineOpcode {
    LUNA_X86_64_MACHINE_CONST_INTEGER,
    LUNA_X86_64_MACHINE_CONST_FLOAT,
    LUNA_X86_64_MACHINE_CONST_BOOL,
    LUNA_X86_64_MACHINE_CONST_NULL,
    LUNA_X86_64_MACHINE_LOAD,
    LUNA_X86_64_MACHINE_STORE,
    LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT,
    LUNA_X86_64_MACHINE_MEMBER_ADDRESS,
    LUNA_X86_64_MACHINE_GLOBAL_ADDRESS,
    LUNA_X86_64_MACHINE_ZERO_SLOT,
    LUNA_X86_64_MACHINE_MEMORY_COPY,
    LUNA_X86_64_MACHINE_LOAD_INDIRECT,
    LUNA_X86_64_MACHINE_STORE_INDIRECT,
    LUNA_X86_64_MACHINE_NULL_CHECK,
    LUNA_X86_64_MACHINE_BOUNDS_CHECK,
    LUNA_X86_64_MACHINE_POINTER_OFFSET,
    LUNA_X86_64_MACHINE_NEG_INTEGER,
    LUNA_X86_64_MACHINE_NEG_FLOAT,
    LUNA_X86_64_MACHINE_BIT_NOT_INTEGER,
    LUNA_X86_64_MACHINE_BOOL_NOT,
    LUNA_X86_64_MACHINE_CONVERT_INTEGER,
    LUNA_X86_64_MACHINE_CONVERT_FLOAT,
    LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT,
    LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER,
    LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER,
    LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER,
    LUNA_X86_64_MACHINE_ADD_INTEGER,
    LUNA_X86_64_MACHINE_SUB_INTEGER,
    LUNA_X86_64_MACHINE_MUL_INTEGER,
    LUNA_X86_64_MACHINE_DIV_INTEGER,
    LUNA_X86_64_MACHINE_REM_INTEGER,
    LUNA_X86_64_MACHINE_BIT_AND_INTEGER,
    LUNA_X86_64_MACHINE_BIT_OR_INTEGER,
    LUNA_X86_64_MACHINE_BIT_XOR_INTEGER,
    LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER,
    LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER,
    LUNA_X86_64_MACHINE_ADD_FLOAT,
    LUNA_X86_64_MACHINE_SUB_FLOAT,
    LUNA_X86_64_MACHINE_MUL_FLOAT,
    LUNA_X86_64_MACHINE_DIV_FLOAT,
    LUNA_X86_64_MACHINE_COMPARE_EQUAL,
    LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL,
    LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER,
    LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER,
    LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER,
    LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER,
    LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT,
    LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT,
    LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT,
    LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT,
    LUNA_X86_64_MACHINE_CALL,
    LUNA_X86_64_MACHINE_JUMP,
    LUNA_X86_64_MACHINE_BRANCH,
    LUNA_X86_64_MACHINE_RETURN
} LunaX8664MachineOpcode;

typedef struct LunaX8664MachineInstruction {
    LunaX8664MachineOpcode opcode;
    LunaX8664MachineType type;
    LunaX8664MachineType memory_type;
    LunaX8664MachineVirtualRegister result;
    LunaX8664MachineVirtualRegister left;
    LunaX8664MachineVirtualRegister right;
    LunaX8664MachineStackSlotId slot;
    LunaX8664MachineBlockId true_block;
    LunaX8664MachineBlockId false_block;
    LunaX8664MachineFunctionId callee;
    LunaX8664MachineGlobalId global;
    uint32_t first_argument;
    uint32_t argument_count;
    uint64_t immediate;
    LunaSourceSpan span;
} LunaX8664MachineInstruction;

typedef struct LunaX8664MachineStackSlot {
    LunaX8664MachineType type;
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    bool is_scalar;
} LunaX8664MachineStackSlot;

typedef struct LunaX8664MachineGlobal {
    LunaVector bytes;
    uint32_t alignment_bytes;
    bool is_read_only;
} LunaX8664MachineGlobal;

typedef struct LunaX8664MachineBlock {
    LunaVector instructions;
    uint32_t predecessor_count;
    bool terminated;
} LunaX8664MachineBlock;

typedef struct LunaX8664MachineFunction {
    LunaStringView module_name;
    LunaStringView name;
    LunaX8664MachineFunctionLinkage linkage;
    bool has_module_metadata_hash;
    uint64_t module_metadata_hash;
    LunaX8664MachineType return_type;
    LunaVector parameter_types;
    LunaVector slots;
    LunaVector value_types;
    LunaVector arguments;
    LunaVector blocks;
} LunaX8664MachineFunction;

typedef struct LunaX8664MachineModule {
    const LunaTargetInfo *target;
    LunaX8664MachineModuleKind kind;
    LunaVector globals;
    LunaVector functions;
    LunaX8664MachineFunctionId entry_function;
} LunaX8664MachineModule;

void luna_x86_64_machine_module_init(LunaX8664MachineModule *module,
                                     const LunaTargetInfo *target);
void luna_x86_64_machine_module_destroy(LunaX8664MachineModule *module);
bool luna_x86_64_machine_lower(const LunaIrModule *source,
                               LunaDiagnosticEngine *diagnostics,
                               LunaX8664MachineModule *module);
LunaX8664MachineFunction *
luna_x86_64_machine_module_function(LunaX8664MachineModule *module,
                                    LunaX8664MachineFunctionId function_id);
const LunaX8664MachineFunction *luna_x86_64_machine_module_function_const(
    const LunaX8664MachineModule *module,
    LunaX8664MachineFunctionId function_id);
bool luna_x86_64_machine_verify(const LunaX8664MachineModule *module,
                                FILE *error_stream);
bool luna_x86_64_machine_print(const LunaX8664MachineModule *module,
                               LunaStringBuilder *output);

const char *luna_x86_64_machine_type_name(LunaX8664MachineType type);
uint32_t luna_x86_64_machine_type_bit_width(LunaX8664MachineType type);
bool luna_x86_64_machine_type_is_integer(LunaX8664MachineType type);
bool luna_x86_64_machine_type_is_signed_integer(LunaX8664MachineType type);
bool luna_x86_64_machine_type_is_float(LunaX8664MachineType type);
LunaX8664MachineRegisterClass
luna_x86_64_machine_type_register_class(LunaX8664MachineType type);

bool luna_x86_64_machine_instruction_definition(
    const LunaX8664MachineInstruction *instruction,
    LunaX8664MachineVirtualRegister *definition);
uint32_t luna_x86_64_machine_instruction_use_count(
    const LunaX8664MachineInstruction *instruction);
LunaX8664MachineVirtualRegister luna_x86_64_machine_instruction_use(
    const LunaX8664MachineFunction *function,
    const LunaX8664MachineInstruction *instruction, uint32_t use_index);

#endif
