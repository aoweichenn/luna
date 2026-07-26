#ifndef LUNA_IR_H
#define LUNA_IR_H

#include "luna/frontend/source/source.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LUNA_IR_INVALID_ID UINT32_MAX

typedef uint32_t LunaIrValueId;
typedef uint32_t LunaIrSlotId;
typedef uint32_t LunaIrBlockId;
typedef uint32_t LunaIrFunctionId;

typedef enum LunaIrType {
    LUNA_IR_TYPE_VOID,
    LUNA_IR_TYPE_BOOL,
    LUNA_IR_TYPE_I32,
    LUNA_IR_TYPE_I64
} LunaIrType;

typedef enum LunaIrOpcode {
    LUNA_IR_CONST_I32,
    LUNA_IR_CONST_I64,
    LUNA_IR_CONST_BOOL,
    LUNA_IR_LOAD,
    LUNA_IR_STORE,
    LUNA_IR_NEG_I32,
    LUNA_IR_NEG_I64,
    LUNA_IR_BIT_NOT_I32,
    LUNA_IR_BIT_NOT_I64,
    LUNA_IR_BOOL_NOT,
    LUNA_IR_SIGN_EXTEND_I32_TO_I64,
    LUNA_IR_TRUNCATE_I64_TO_I32,
    LUNA_IR_ADD_I32,
    LUNA_IR_SUB_I32,
    LUNA_IR_MUL_I32,
    LUNA_IR_DIV_I32,
    LUNA_IR_REM_I32,
    LUNA_IR_BIT_AND_I32,
    LUNA_IR_BIT_OR_I32,
    LUNA_IR_BIT_XOR_I32,
    LUNA_IR_SHIFT_LEFT_I32,
    LUNA_IR_SHIFT_RIGHT_I32,
    LUNA_IR_ADD_I64,
    LUNA_IR_SUB_I64,
    LUNA_IR_MUL_I64,
    LUNA_IR_DIV_I64,
    LUNA_IR_REM_I64,
    LUNA_IR_BIT_AND_I64,
    LUNA_IR_BIT_OR_I64,
    LUNA_IR_BIT_XOR_I64,
    LUNA_IR_SHIFT_LEFT_I64,
    LUNA_IR_SHIFT_RIGHT_I64,
    LUNA_IR_COMPARE_EQUAL,
    LUNA_IR_COMPARE_NOT_EQUAL,
    LUNA_IR_COMPARE_LESS_I32,
    LUNA_IR_COMPARE_LESS_EQUAL_I32,
    LUNA_IR_COMPARE_GREATER_I32,
    LUNA_IR_COMPARE_GREATER_EQUAL_I32,
    LUNA_IR_COMPARE_LESS_I64,
    LUNA_IR_COMPARE_LESS_EQUAL_I64,
    LUNA_IR_COMPARE_GREATER_I64,
    LUNA_IR_COMPARE_GREATER_EQUAL_I64,
    LUNA_IR_CALL,
    LUNA_IR_JUMP,
    LUNA_IR_BRANCH,
    LUNA_IR_RETURN
} LunaIrOpcode;

typedef struct LunaIrInstruction {
    LunaIrOpcode opcode;
    LunaIrType type;
    LunaIrValueId result;
    LunaIrValueId left;
    LunaIrValueId right;
    LunaIrSlotId slot;
    LunaIrBlockId true_block;
    LunaIrBlockId false_block;
    LunaIrFunctionId callee;
    uint32_t first_argument;
    uint32_t argument_count;
    int64_t immediate;
    LunaSourceSpan span;
} LunaIrInstruction;

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
    LunaVector slot_types;
    LunaVector value_types;
    LunaVector arguments;
    LunaVector blocks;
} LunaIrFunction;

typedef struct LunaIrModule {
    LunaVector functions;
    LunaIrFunctionId entry_function;
} LunaIrModule;

void luna_ir_module_init(LunaIrModule *module);
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

LunaIrSlotId luna_ir_function_add_slot(LunaIrFunction *function,
                                       LunaIrType type);
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

#endif
