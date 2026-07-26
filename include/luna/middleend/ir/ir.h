#ifndef LUNA_IR_H
#define LUNA_IR_H

#include "luna/frontend/source/source.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
#include "luna/target/target.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LUNA_IR_INVALID_ID UINT32_MAX

typedef uint32_t LunaIrValueId;
typedef uint32_t LunaIrSlotId;
typedef uint32_t LunaIrBlockId;
typedef uint32_t LunaIrFunctionId;
typedef uint32_t LunaIrGlobalId;

typedef enum LunaIrType {
    LUNA_IR_TYPE_VOID,
    LUNA_IR_TYPE_BOOL,
    LUNA_IR_TYPE_I8,
    LUNA_IR_TYPE_I16,
    LUNA_IR_TYPE_I32,
    LUNA_IR_TYPE_I64,
    LUNA_IR_TYPE_ISIZE,
    LUNA_IR_TYPE_U8,
    LUNA_IR_TYPE_U16,
    LUNA_IR_TYPE_U32,
    LUNA_IR_TYPE_U64,
    LUNA_IR_TYPE_USIZE,
    LUNA_IR_TYPE_F32,
    LUNA_IR_TYPE_F64,
    LUNA_IR_TYPE_POINTER
} LunaIrType;

typedef enum LunaIrOpcode {
    LUNA_IR_CONST_INTEGER,
    LUNA_IR_CONST_FLOAT,
    LUNA_IR_CONST_BOOL,
    LUNA_IR_CONST_NULL,
    LUNA_IR_LOAD,
    LUNA_IR_STORE,
    LUNA_IR_ADDRESS_OF_SLOT,
    LUNA_IR_GLOBAL_ADDRESS,
    LUNA_IR_ZERO_SLOT,
    LUNA_IR_LOAD_INDIRECT,
    LUNA_IR_STORE_INDIRECT,
    LUNA_IR_NULL_CHECK,
    LUNA_IR_BOUNDS_CHECK,
    LUNA_IR_POINTER_OFFSET,
    LUNA_IR_NEG_INTEGER,
    LUNA_IR_NEG_FLOAT,
    LUNA_IR_BIT_NOT_INTEGER,
    LUNA_IR_BOOL_NOT,
    LUNA_IR_CONVERT_INTEGER,
    LUNA_IR_CONVERT_FLOAT,
    LUNA_IR_CONVERT_INTEGER_TO_FLOAT,
    LUNA_IR_CONVERT_FLOAT_TO_INTEGER,
    LUNA_IR_CONVERT_POINTER_TO_INTEGER,
    LUNA_IR_CONVERT_INTEGER_TO_POINTER,
    LUNA_IR_ADD_INTEGER,
    LUNA_IR_SUB_INTEGER,
    LUNA_IR_MUL_INTEGER,
    LUNA_IR_DIV_INTEGER,
    LUNA_IR_REM_INTEGER,
    LUNA_IR_BIT_AND_INTEGER,
    LUNA_IR_BIT_OR_INTEGER,
    LUNA_IR_BIT_XOR_INTEGER,
    LUNA_IR_SHIFT_LEFT_INTEGER,
    LUNA_IR_SHIFT_RIGHT_INTEGER,
    LUNA_IR_ADD_FLOAT,
    LUNA_IR_SUB_FLOAT,
    LUNA_IR_MUL_FLOAT,
    LUNA_IR_DIV_FLOAT,
    LUNA_IR_COMPARE_EQUAL,
    LUNA_IR_COMPARE_NOT_EQUAL,
    LUNA_IR_COMPARE_LESS_INTEGER,
    LUNA_IR_COMPARE_LESS_EQUAL_INTEGER,
    LUNA_IR_COMPARE_GREATER_INTEGER,
    LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER,
    LUNA_IR_COMPARE_LESS_FLOAT,
    LUNA_IR_COMPARE_LESS_EQUAL_FLOAT,
    LUNA_IR_COMPARE_GREATER_FLOAT,
    LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT,
    LUNA_IR_CALL,
    LUNA_IR_JUMP,
    LUNA_IR_BRANCH,
    LUNA_IR_RETURN
} LunaIrOpcode;

typedef struct LunaIrInstruction {
    LunaIrOpcode opcode;
    LunaIrType type;
    LunaIrType memory_type;
    LunaIrValueId result;
    LunaIrValueId left;
    LunaIrValueId right;
    LunaIrSlotId slot;
    LunaIrBlockId true_block;
    LunaIrBlockId false_block;
    LunaIrFunctionId callee;
    LunaIrGlobalId global;
    uint32_t first_argument;
    uint32_t argument_count;
    uint64_t immediate;
    LunaSourceSpan span;
} LunaIrInstruction;

typedef struct LunaIrSlot {
    LunaIrType type;
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    bool is_scalar;
} LunaIrSlot;

typedef struct LunaIrGlobal {
    LunaVector bytes;
    uint32_t alignment_bytes;
    bool is_read_only;
} LunaIrGlobal;

typedef struct LunaIrBlock {
    LunaVector instructions;
    uint32_t predecessor_count;
    bool terminated;
} LunaIrBlock;

typedef struct LunaIrFunction {
    LunaStringView module_name;
    LunaStringView name;
    LunaIrType return_type;
    LunaVector parameter_types;
    LunaVector slots;
    LunaVector value_types;
    LunaVector arguments;
    LunaVector blocks;
} LunaIrFunction;

typedef struct LunaIrModule {
    const LunaTargetInfo *target;
    LunaVector globals;
    LunaVector functions;
    LunaIrFunctionId entry_function;
} LunaIrModule;

void luna_ir_module_init(LunaIrModule *module, const LunaTargetInfo *target);
void luna_ir_module_destroy(LunaIrModule *module);
LunaIrFunctionId luna_ir_module_add_function(LunaIrModule *module,
                                             LunaStringView module_name,
                                             LunaStringView name,
                                             LunaIrType return_type);
LunaIrFunction *luna_ir_module_function(LunaIrModule *module,
                                        LunaIrFunctionId function_id);
const LunaIrFunction *
luna_ir_module_function_const(const LunaIrModule *module,
                              LunaIrFunctionId function_id);
LunaIrGlobalId luna_ir_module_add_global(LunaIrModule *module,
                                         const uint8_t *bytes,
                                         uint64_t byte_count,
                                         uint32_t alignment_bytes,
                                         bool is_read_only);
const LunaIrGlobal *luna_ir_module_global(const LunaIrModule *module,
                                          LunaIrGlobalId global_id);

LunaIrSlotId luna_ir_function_add_slot(LunaIrFunction *function,
                                       LunaIrType type);
LunaIrSlotId luna_ir_function_add_memory_slot(LunaIrFunction *function,
                                              uint64_t size_bytes,
                                              uint32_t alignment_bytes);
LunaIrValueId luna_ir_function_add_value(LunaIrFunction *function,
                                         LunaIrType type);
LunaIrBlockId luna_ir_function_add_block(LunaIrFunction *function);
LunaIrBlock *luna_ir_function_block(LunaIrFunction *function,
                                    LunaIrBlockId block_id);
bool luna_ir_block_append(LunaIrBlock *block,
                          const LunaIrInstruction *instruction);
bool luna_ir_verify(const LunaIrModule *module, FILE *error_stream);
bool luna_ir_print(const LunaIrModule *module, LunaStringBuilder *output);
const char *luna_ir_type_name(LunaIrType type);
bool luna_ir_type_is_integer(LunaIrType type);
bool luna_ir_type_is_signed_integer(LunaIrType type);
bool luna_ir_type_is_float(LunaIrType type);
uint32_t luna_ir_type_bit_width(LunaIrType type,
                                const LunaDataLayout *data_layout);

#endif
