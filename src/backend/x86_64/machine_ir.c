#include "luna/backend/x86_64/machine_ir.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static LunaX8664MachineType luna_x86_64_machine_lower_type(LunaIrType type) {
    switch (type) {
    case LUNA_IR_TYPE_VOID:
        return LUNA_X86_64_MACHINE_TYPE_VOID;
    case LUNA_IR_TYPE_BOOL:
        return LUNA_X86_64_MACHINE_TYPE_BOOL;
    case LUNA_IR_TYPE_I8:
        return LUNA_X86_64_MACHINE_TYPE_I8;
    case LUNA_IR_TYPE_I16:
        return LUNA_X86_64_MACHINE_TYPE_I16;
    case LUNA_IR_TYPE_I32:
        return LUNA_X86_64_MACHINE_TYPE_I32;
    case LUNA_IR_TYPE_I64:
    case LUNA_IR_TYPE_ISIZE:
        return LUNA_X86_64_MACHINE_TYPE_I64;
    case LUNA_IR_TYPE_U8:
        return LUNA_X86_64_MACHINE_TYPE_U8;
    case LUNA_IR_TYPE_U16:
        return LUNA_X86_64_MACHINE_TYPE_U16;
    case LUNA_IR_TYPE_U32:
        return LUNA_X86_64_MACHINE_TYPE_U32;
    case LUNA_IR_TYPE_U64:
    case LUNA_IR_TYPE_USIZE:
        return LUNA_X86_64_MACHINE_TYPE_U64;
    case LUNA_IR_TYPE_F32:
        return LUNA_X86_64_MACHINE_TYPE_F32;
    case LUNA_IR_TYPE_F64:
        return LUNA_X86_64_MACHINE_TYPE_F64;
    case LUNA_IR_TYPE_POINTER:
        return LUNA_X86_64_MACHINE_TYPE_POINTER;
    }
    return LUNA_X86_64_MACHINE_TYPE_INVALID;
}

static LunaX8664MachineOpcode
luna_x86_64_machine_lower_opcode(LunaIrOpcode opcode) {
    switch (opcode) {
    case LUNA_IR_CONST_INTEGER:
        return LUNA_X86_64_MACHINE_CONST_INTEGER;
    case LUNA_IR_CONST_FLOAT:
        return LUNA_X86_64_MACHINE_CONST_FLOAT;
    case LUNA_IR_CONST_BOOL:
        return LUNA_X86_64_MACHINE_CONST_BOOL;
    case LUNA_IR_CONST_NULL:
        return LUNA_X86_64_MACHINE_CONST_NULL;
    case LUNA_IR_LOAD:
        return LUNA_X86_64_MACHINE_LOAD;
    case LUNA_IR_STORE:
        return LUNA_X86_64_MACHINE_STORE;
    case LUNA_IR_ADDRESS_OF_SLOT:
        return LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT;
    case LUNA_IR_MEMBER_ADDRESS:
        return LUNA_X86_64_MACHINE_MEMBER_ADDRESS;
    case LUNA_IR_GLOBAL_ADDRESS:
        return LUNA_X86_64_MACHINE_GLOBAL_ADDRESS;
    case LUNA_IR_ZERO_SLOT:
        return LUNA_X86_64_MACHINE_ZERO_SLOT;
    case LUNA_IR_MEMORY_COPY:
        return LUNA_X86_64_MACHINE_MEMORY_COPY;
    case LUNA_IR_LOAD_INDIRECT:
        return LUNA_X86_64_MACHINE_LOAD_INDIRECT;
    case LUNA_IR_STORE_INDIRECT:
        return LUNA_X86_64_MACHINE_STORE_INDIRECT;
    case LUNA_IR_NULL_CHECK:
        return LUNA_X86_64_MACHINE_NULL_CHECK;
    case LUNA_IR_BOUNDS_CHECK:
        return LUNA_X86_64_MACHINE_BOUNDS_CHECK;
    case LUNA_IR_POINTER_OFFSET:
        return LUNA_X86_64_MACHINE_POINTER_OFFSET;
    case LUNA_IR_NEG_INTEGER:
        return LUNA_X86_64_MACHINE_NEG_INTEGER;
    case LUNA_IR_NEG_FLOAT:
        return LUNA_X86_64_MACHINE_NEG_FLOAT;
    case LUNA_IR_BIT_NOT_INTEGER:
        return LUNA_X86_64_MACHINE_BIT_NOT_INTEGER;
    case LUNA_IR_BOOL_NOT:
        return LUNA_X86_64_MACHINE_BOOL_NOT;
    case LUNA_IR_CONVERT_INTEGER:
        return LUNA_X86_64_MACHINE_CONVERT_INTEGER;
    case LUNA_IR_CONVERT_FLOAT:
        return LUNA_X86_64_MACHINE_CONVERT_FLOAT;
    case LUNA_IR_CONVERT_INTEGER_TO_FLOAT:
        return LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT;
    case LUNA_IR_CONVERT_FLOAT_TO_INTEGER:
        return LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER;
    case LUNA_IR_CONVERT_POINTER_TO_INTEGER:
        return LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER;
    case LUNA_IR_CONVERT_INTEGER_TO_POINTER:
        return LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER;
    case LUNA_IR_ADD_INTEGER:
        return LUNA_X86_64_MACHINE_ADD_INTEGER;
    case LUNA_IR_SUB_INTEGER:
        return LUNA_X86_64_MACHINE_SUB_INTEGER;
    case LUNA_IR_MUL_INTEGER:
        return LUNA_X86_64_MACHINE_MUL_INTEGER;
    case LUNA_IR_DIV_INTEGER:
        return LUNA_X86_64_MACHINE_DIV_INTEGER;
    case LUNA_IR_REM_INTEGER:
        return LUNA_X86_64_MACHINE_REM_INTEGER;
    case LUNA_IR_BIT_AND_INTEGER:
        return LUNA_X86_64_MACHINE_BIT_AND_INTEGER;
    case LUNA_IR_BIT_OR_INTEGER:
        return LUNA_X86_64_MACHINE_BIT_OR_INTEGER;
    case LUNA_IR_BIT_XOR_INTEGER:
        return LUNA_X86_64_MACHINE_BIT_XOR_INTEGER;
    case LUNA_IR_SHIFT_LEFT_INTEGER:
        return LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER;
    case LUNA_IR_SHIFT_RIGHT_INTEGER:
        return LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER;
    case LUNA_IR_ADD_FLOAT:
        return LUNA_X86_64_MACHINE_ADD_FLOAT;
    case LUNA_IR_SUB_FLOAT:
        return LUNA_X86_64_MACHINE_SUB_FLOAT;
    case LUNA_IR_MUL_FLOAT:
        return LUNA_X86_64_MACHINE_MUL_FLOAT;
    case LUNA_IR_DIV_FLOAT:
        return LUNA_X86_64_MACHINE_DIV_FLOAT;
    case LUNA_IR_COMPARE_EQUAL:
        return LUNA_X86_64_MACHINE_COMPARE_EQUAL;
    case LUNA_IR_COMPARE_NOT_EQUAL:
        return LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL;
    case LUNA_IR_COMPARE_LESS_INTEGER:
        return LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER;
    case LUNA_IR_COMPARE_LESS_EQUAL_INTEGER:
        return LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER;
    case LUNA_IR_COMPARE_GREATER_INTEGER:
        return LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER;
    case LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER:
        return LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER;
    case LUNA_IR_COMPARE_LESS_FLOAT:
        return LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT;
    case LUNA_IR_COMPARE_LESS_EQUAL_FLOAT:
        return LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT;
    case LUNA_IR_COMPARE_GREATER_FLOAT:
        return LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT;
    case LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT:
        return LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT;
    case LUNA_IR_CALL:
        return LUNA_X86_64_MACHINE_CALL;
    case LUNA_IR_JUMP:
        return LUNA_X86_64_MACHINE_JUMP;
    case LUNA_IR_BRANCH:
        return LUNA_X86_64_MACHINE_BRANCH;
    case LUNA_IR_RETURN:
        return LUNA_X86_64_MACHINE_RETURN;
    }
    return (LunaX8664MachineOpcode)UINT32_MAX;
}

static LunaX8664MachineFunctionLinkage
luna_x86_64_machine_lower_linkage(LunaIrFunctionLinkage linkage) {
    switch (linkage) {
    case LUNA_IR_LINKAGE_INTERNAL:
        return LUNA_X86_64_MACHINE_LINKAGE_INTERNAL;
    case LUNA_IR_LINKAGE_MODULE_EXPORT:
        return LUNA_X86_64_MACHINE_LINKAGE_MODULE_EXPORT;
    case LUNA_IR_LINKAGE_MODULE_IMPORT:
        return LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT;
    case LUNA_IR_LINKAGE_EXTERNAL_C:
        return LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    }
    return (LunaX8664MachineFunctionLinkage)UINT32_MAX;
}

static void
luna_x86_64_machine_function_init(LunaX8664MachineFunction *function) {
    memset(function, 0, sizeof(*function));
    luna_vector_init(&function->parameter_types, sizeof(LunaX8664MachineType));
    luna_vector_init(&function->slots, sizeof(LunaX8664MachineStackSlot));
    luna_vector_init(&function->value_types, sizeof(LunaX8664MachineType));
    luna_vector_init(&function->arguments,
                     sizeof(LunaX8664MachineVirtualRegister));
    luna_vector_init(&function->blocks, sizeof(LunaX8664MachineBlock));
}

static void
luna_x86_64_machine_function_destroy(LunaX8664MachineFunction *function) {
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        LunaX8664MachineBlock *block =
            luna_vector_at(&function->blocks, block_index);
        if (block != NULL) {
            luna_vector_destroy(&block->instructions);
        }
    }
    luna_vector_destroy(&function->blocks);
    luna_vector_destroy(&function->arguments);
    luna_vector_destroy(&function->value_types);
    luna_vector_destroy(&function->slots);
    luna_vector_destroy(&function->parameter_types);
}

void luna_x86_64_machine_module_init(LunaX8664MachineModule *module,
                                     const LunaTargetInfo *target) {
    module->target = target;
    module->kind = LUNA_X86_64_MACHINE_MODULE_EXECUTABLE;
    luna_vector_init(&module->globals, sizeof(LunaX8664MachineGlobal));
    luna_vector_init(&module->functions, sizeof(LunaX8664MachineFunction));
    module->entry_function = LUNA_X86_64_MACHINE_INVALID_ID;
}

void luna_x86_64_machine_module_destroy(LunaX8664MachineModule *module) {
    if (module == NULL) {
        return;
    }
    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        LunaX8664MachineFunction *function =
            luna_vector_at(&module->functions, function_index);
        if (function != NULL) {
            luna_x86_64_machine_function_destroy(function);
        }
    }
    for (size_t global_index = 0U; global_index < module->globals.length;
         global_index += 1U) {
        LunaX8664MachineGlobal *global =
            luna_vector_at(&module->globals, global_index);
        if (global != NULL) {
            luna_vector_destroy(&global->bytes);
        }
    }
    luna_vector_destroy(&module->functions);
    luna_vector_destroy(&module->globals);
    module->target = NULL;
    module->kind = LUNA_X86_64_MACHINE_MODULE_EXECUTABLE;
    module->entry_function = LUNA_X86_64_MACHINE_INVALID_ID;
}

LunaX8664MachineFunction *
luna_x86_64_machine_module_function(LunaX8664MachineModule *module,
                                    LunaX8664MachineFunctionId function_id) {
    if (module == NULL) {
        return NULL;
    }
    return luna_vector_at(&module->functions, (size_t)function_id);
}

const LunaX8664MachineFunction *luna_x86_64_machine_module_function_const(
    const LunaX8664MachineModule *module,
    LunaX8664MachineFunctionId function_id) {
    if (module == NULL) {
        return NULL;
    }
    return luna_vector_at_const(&module->functions, (size_t)function_id);
}

const char *luna_x86_64_machine_type_name(LunaX8664MachineType type) {
    switch (type) {
    case LUNA_X86_64_MACHINE_TYPE_VOID:
        return "void";
    case LUNA_X86_64_MACHINE_TYPE_BOOL:
        return "bool";
    case LUNA_X86_64_MACHINE_TYPE_I8:
        return "i8";
    case LUNA_X86_64_MACHINE_TYPE_I16:
        return "i16";
    case LUNA_X86_64_MACHINE_TYPE_I32:
        return "i32";
    case LUNA_X86_64_MACHINE_TYPE_I64:
        return "i64";
    case LUNA_X86_64_MACHINE_TYPE_U8:
        return "u8";
    case LUNA_X86_64_MACHINE_TYPE_U16:
        return "u16";
    case LUNA_X86_64_MACHINE_TYPE_U32:
        return "u32";
    case LUNA_X86_64_MACHINE_TYPE_U64:
        return "u64";
    case LUNA_X86_64_MACHINE_TYPE_F32:
        return "f32";
    case LUNA_X86_64_MACHINE_TYPE_F64:
        return "f64";
    case LUNA_X86_64_MACHINE_TYPE_POINTER:
        return "ptr64";
    case LUNA_X86_64_MACHINE_TYPE_INVALID:
        break;
    }
    return "invalid";
}

uint32_t luna_x86_64_machine_type_bit_width(LunaX8664MachineType type) {
    switch (type) {
    case LUNA_X86_64_MACHINE_TYPE_BOOL:
    case LUNA_X86_64_MACHINE_TYPE_I8:
    case LUNA_X86_64_MACHINE_TYPE_U8:
        return 8U;
    case LUNA_X86_64_MACHINE_TYPE_I16:
    case LUNA_X86_64_MACHINE_TYPE_U16:
        return 16U;
    case LUNA_X86_64_MACHINE_TYPE_I32:
    case LUNA_X86_64_MACHINE_TYPE_U32:
    case LUNA_X86_64_MACHINE_TYPE_F32:
        return 32U;
    case LUNA_X86_64_MACHINE_TYPE_I64:
    case LUNA_X86_64_MACHINE_TYPE_U64:
    case LUNA_X86_64_MACHINE_TYPE_F64:
    case LUNA_X86_64_MACHINE_TYPE_POINTER:
        return 64U;
    case LUNA_X86_64_MACHINE_TYPE_INVALID:
    case LUNA_X86_64_MACHINE_TYPE_VOID:
        return 0U;
    }
    return 0U;
}

bool luna_x86_64_machine_type_is_integer(LunaX8664MachineType type) {
    return type >= LUNA_X86_64_MACHINE_TYPE_I8 &&
           type <= LUNA_X86_64_MACHINE_TYPE_U64;
}

bool luna_x86_64_machine_type_is_signed_integer(LunaX8664MachineType type) {
    return type >= LUNA_X86_64_MACHINE_TYPE_I8 &&
           type <= LUNA_X86_64_MACHINE_TYPE_I64;
}

bool luna_x86_64_machine_type_is_float(LunaX8664MachineType type) {
    return type == LUNA_X86_64_MACHINE_TYPE_F32 ||
           type == LUNA_X86_64_MACHINE_TYPE_F64;
}

LunaX8664MachineRegisterClass
luna_x86_64_machine_type_register_class(LunaX8664MachineType type) {
    if (luna_x86_64_machine_type_is_float(type)) {
        return LUNA_X86_64_MACHINE_REGISTER_FLOAT;
    }
    if (type == LUNA_X86_64_MACHINE_TYPE_BOOL ||
        luna_x86_64_machine_type_is_integer(type) ||
        type == LUNA_X86_64_MACHINE_TYPE_POINTER) {
        return LUNA_X86_64_MACHINE_REGISTER_GENERAL;
    }
    return LUNA_X86_64_MACHINE_REGISTER_NONE;
}

static bool
luna_x86_64_machine_opcode_defines_result(LunaX8664MachineOpcode opcode) {
    switch (opcode) {
    case LUNA_X86_64_MACHINE_CONST_INTEGER:
    case LUNA_X86_64_MACHINE_CONST_FLOAT:
    case LUNA_X86_64_MACHINE_CONST_BOOL:
    case LUNA_X86_64_MACHINE_CONST_NULL:
    case LUNA_X86_64_MACHINE_LOAD:
    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
    case LUNA_X86_64_MACHINE_NEG_INTEGER:
    case LUNA_X86_64_MACHINE_NEG_FLOAT:
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
    case LUNA_X86_64_MACHINE_BOOL_NOT:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
    case LUNA_X86_64_MACHINE_ADD_INTEGER:
    case LUNA_X86_64_MACHINE_SUB_INTEGER:
    case LUNA_X86_64_MACHINE_MUL_INTEGER:
    case LUNA_X86_64_MACHINE_DIV_INTEGER:
    case LUNA_X86_64_MACHINE_REM_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER:
    case LUNA_X86_64_MACHINE_ADD_FLOAT:
    case LUNA_X86_64_MACHINE_SUB_FLOAT:
    case LUNA_X86_64_MACHINE_MUL_FLOAT:
    case LUNA_X86_64_MACHINE_DIV_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        return true;
    case LUNA_X86_64_MACHINE_CALL:
    case LUNA_X86_64_MACHINE_STORE:
    case LUNA_X86_64_MACHINE_ZERO_SLOT:
    case LUNA_X86_64_MACHINE_MEMORY_COPY:
    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
    case LUNA_X86_64_MACHINE_NULL_CHECK:
    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
    case LUNA_X86_64_MACHINE_JUMP:
    case LUNA_X86_64_MACHINE_BRANCH:
    case LUNA_X86_64_MACHINE_RETURN:
        return false;
    }
    return false;
}

bool luna_x86_64_machine_instruction_definition(
    const LunaX8664MachineInstruction *instruction,
    LunaX8664MachineVirtualRegister *definition) {
    if (instruction == NULL || definition == NULL ||
        instruction->result == LUNA_X86_64_MACHINE_INVALID_ID ||
        (!luna_x86_64_machine_opcode_defines_result(instruction->opcode) &&
         instruction->opcode != LUNA_X86_64_MACHINE_CALL)) {
        return false;
    }
    *definition = instruction->result;
    return true;
}

uint32_t luna_x86_64_machine_instruction_use_count(
    const LunaX8664MachineInstruction *instruction) {
    if (instruction == NULL) {
        return 0U;
    }
    switch (instruction->opcode) {
    case LUNA_X86_64_MACHINE_STORE:
    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
    case LUNA_X86_64_MACHINE_NULL_CHECK:
    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
    case LUNA_X86_64_MACHINE_NEG_INTEGER:
    case LUNA_X86_64_MACHINE_NEG_FLOAT:
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
    case LUNA_X86_64_MACHINE_BOOL_NOT:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
    case LUNA_X86_64_MACHINE_BRANCH:
        return 1U;
    case LUNA_X86_64_MACHINE_MEMORY_COPY:
    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
    case LUNA_X86_64_MACHINE_ADD_INTEGER:
    case LUNA_X86_64_MACHINE_SUB_INTEGER:
    case LUNA_X86_64_MACHINE_MUL_INTEGER:
    case LUNA_X86_64_MACHINE_DIV_INTEGER:
    case LUNA_X86_64_MACHINE_REM_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER:
    case LUNA_X86_64_MACHINE_ADD_FLOAT:
    case LUNA_X86_64_MACHINE_SUB_FLOAT:
    case LUNA_X86_64_MACHINE_MUL_FLOAT:
    case LUNA_X86_64_MACHINE_DIV_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        return 2U;
    case LUNA_X86_64_MACHINE_CALL:
        return instruction->argument_count;
    case LUNA_X86_64_MACHINE_RETURN:
        return instruction->left == LUNA_X86_64_MACHINE_INVALID_ID ? 0U : 1U;
    case LUNA_X86_64_MACHINE_CONST_INTEGER:
    case LUNA_X86_64_MACHINE_CONST_FLOAT:
    case LUNA_X86_64_MACHINE_CONST_BOOL:
    case LUNA_X86_64_MACHINE_CONST_NULL:
    case LUNA_X86_64_MACHINE_LOAD:
    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
    case LUNA_X86_64_MACHINE_ZERO_SLOT:
    case LUNA_X86_64_MACHINE_JUMP:
        return 0U;
    }
    return 0U;
}

LunaX8664MachineVirtualRegister luna_x86_64_machine_instruction_use(
    const LunaX8664MachineFunction *function,
    const LunaX8664MachineInstruction *instruction, uint32_t use_index) {
    const uint32_t use_count =
        luna_x86_64_machine_instruction_use_count(instruction);
    if (instruction == NULL || use_index >= use_count) {
        return LUNA_X86_64_MACHINE_INVALID_ID;
    }
    if (instruction->opcode == LUNA_X86_64_MACHINE_CALL) {
        if (function == NULL ||
            (uint64_t)instruction->first_argument + (uint64_t)use_index >=
                (uint64_t)function->arguments.length) {
            return LUNA_X86_64_MACHINE_INVALID_ID;
        }
        const LunaX8664MachineVirtualRegister *argument = luna_vector_at_const(
            &function->arguments,
            (size_t)instruction->first_argument + (size_t)use_index);
        return argument == NULL ? LUNA_X86_64_MACHINE_INVALID_ID : *argument;
    }
    return use_index == 0U ? instruction->left : instruction->right;
}

static bool luna_x86_64_machine_copy_types(const LunaVector *source,
                                           LunaVector *destination) {
    if (source->element_size != sizeof(LunaIrType) ||
        !luna_vector_reserve(destination, source->length)) {
        return false;
    }
    for (size_t index = 0U; index < source->length; index += 1U) {
        const LunaIrType *source_type = luna_vector_at_const(source, index);
        const LunaX8664MachineType target_type =
            source_type == NULL ? LUNA_X86_64_MACHINE_TYPE_INVALID
                                : luna_x86_64_machine_lower_type(*source_type);
        if (target_type == LUNA_X86_64_MACHINE_TYPE_INVALID ||
            !luna_vector_push(destination, &target_type)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_machine_copy_arguments(const LunaVector *source,
                                               LunaVector *destination) {
    if (source->element_size != sizeof(LunaIrValueId) ||
        !luna_vector_reserve(destination, source->length)) {
        return false;
    }
    for (size_t index = 0U; index < source->length; index += 1U) {
        const LunaIrValueId *source_argument =
            luna_vector_at_const(source, index);
        if (source_argument == NULL ||
            !luna_vector_push(destination, source_argument)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_machine_copy_slots(const LunaVector *source,
                                           LunaVector *destination) {
    if (source->element_size != sizeof(LunaIrSlot) ||
        !luna_vector_reserve(destination, source->length)) {
        return false;
    }
    for (size_t index = 0U; index < source->length; index += 1U) {
        const LunaIrSlot *source_slot = luna_vector_at_const(source, index);
        if (source_slot == NULL) {
            return false;
        }
        const LunaX8664MachineStackSlot target_slot = {
            .type = luna_x86_64_machine_lower_type(source_slot->type),
            .size_bytes = source_slot->size_bytes,
            .alignment_bytes = source_slot->alignment_bytes,
            .is_scalar = source_slot->is_scalar,
        };
        if (target_slot.type == LUNA_X86_64_MACHINE_TYPE_INVALID ||
            !luna_vector_push(destination, &target_slot)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_machine_lower_instruction(
    const LunaIrInstruction *source, LunaX8664MachineInstruction *destination) {
    destination->opcode = luna_x86_64_machine_lower_opcode(source->opcode);
    destination->type = luna_x86_64_machine_lower_type(source->type);
    destination->memory_type =
        luna_x86_64_machine_lower_type(source->memory_type);
    destination->result = source->result;
    destination->left = source->left;
    destination->right = source->right;
    destination->slot = source->slot;
    destination->true_block = source->true_block;
    destination->false_block = source->false_block;
    destination->callee = source->callee;
    destination->global = source->global;
    destination->first_argument = source->first_argument;
    destination->argument_count = source->argument_count;
    destination->immediate = source->immediate;
    destination->span = source->span;
    return destination->opcode != (LunaX8664MachineOpcode)UINT32_MAX &&
           destination->type != LUNA_X86_64_MACHINE_TYPE_INVALID &&
           destination->memory_type != LUNA_X86_64_MACHINE_TYPE_INVALID;
}

static bool luna_x86_64_machine_copy_blocks(const LunaVector *source,
                                            LunaVector *destination) {
    if (source->element_size != sizeof(LunaIrBlock) ||
        !luna_vector_reserve(destination, source->length)) {
        return false;
    }
    for (size_t block_index = 0U; block_index < source->length;
         block_index += 1U) {
        const LunaIrBlock *source_block =
            luna_vector_at_const(source, block_index);
        if (source_block == NULL || source_block->instructions.element_size !=
                                        sizeof(LunaIrInstruction)) {
            return false;
        }
        LunaX8664MachineBlock target_block = {
            .predecessor_count = source_block->predecessor_count,
            .terminated = source_block->terminated,
        };
        luna_vector_init(&target_block.instructions,
                         sizeof(LunaX8664MachineInstruction));
        bool success = luna_vector_reserve(&target_block.instructions,
                                           source_block->instructions.length);
        for (size_t instruction_index = 0U;
             success && instruction_index < source_block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *source_instruction = luna_vector_at_const(
                &source_block->instructions, instruction_index);
            LunaX8664MachineInstruction target_instruction = {0};
            success = source_instruction != NULL &&
                      luna_x86_64_machine_lower_instruction(
                          source_instruction, &target_instruction) &&
                      luna_vector_push(&target_block.instructions,
                                       &target_instruction);
        }
        if (!success || !luna_vector_push(destination, &target_block)) {
            luna_vector_destroy(&target_block.instructions);
            return false;
        }
    }
    return true;
}

static bool
luna_x86_64_machine_lower_function(const LunaIrFunction *source,
                                   LunaX8664MachineFunction *destination) {
    luna_x86_64_machine_function_init(destination);
    destination->module_name = source->module_name;
    destination->name = source->name;
    destination->linkage = luna_x86_64_machine_lower_linkage(source->linkage);
    destination->has_module_metadata_hash = source->has_module_metadata_hash;
    destination->module_metadata_hash = source->module_metadata_hash;
    destination->return_type =
        luna_x86_64_machine_lower_type(source->return_type);

    const bool success =
        destination->linkage != (LunaX8664MachineFunctionLinkage)UINT32_MAX &&
        destination->return_type != LUNA_X86_64_MACHINE_TYPE_INVALID &&
        luna_x86_64_machine_copy_types(&source->parameter_types,
                                       &destination->parameter_types) &&
        luna_x86_64_machine_copy_slots(&source->slots, &destination->slots) &&
        luna_x86_64_machine_copy_types(&source->value_types,
                                       &destination->value_types) &&
        luna_x86_64_machine_copy_arguments(&source->arguments,
                                           &destination->arguments) &&
        luna_x86_64_machine_copy_blocks(&source->blocks, &destination->blocks);
    if (!success) {
        luna_x86_64_machine_function_destroy(destination);
    }
    return success;
}

static bool
luna_x86_64_machine_copy_global(const LunaIrGlobal *source,
                                LunaX8664MachineGlobal *destination) {
    destination->alignment_bytes = source->alignment_bytes;
    destination->is_read_only = source->is_read_only;
    luna_vector_init(&destination->bytes, sizeof(uint8_t));
    if (source->bytes.element_size != sizeof(uint8_t) ||
        !luna_vector_reserve(&destination->bytes, source->bytes.length)) {
        return false;
    }
    for (size_t index = 0U; index < source->bytes.length; index += 1U) {
        const uint8_t *byte = luna_vector_at_const(&source->bytes, index);
        if (byte == NULL || !luna_vector_push(&destination->bytes, byte)) {
            return false;
        }
    }
    return true;
}

bool luna_x86_64_machine_lower(const LunaIrModule *source,
                               LunaDiagnosticEngine *diagnostics,
                               LunaX8664MachineModule *module) {
    if (diagnostics == NULL || module == NULL) {
        return false;
    }
    if (source == NULL || !luna_target_info_is_supported(source->target) ||
        !luna_target_info_is_supported(module->target) ||
        module->globals.length != 0U || module->functions.length != 0U ||
        module->entry_function != LUNA_X86_64_MACHINE_INVALID_ID) {
        luna_diagnostic_error_plain(
            diagnostics, "x86-64 machine lowering received invalid state");
        return false;
    }
    if (!luna_ir_verify(source, diagnostics->stream)) {
        luna_diagnostic_error_plain(
            diagnostics, "cannot lower invalid typed IR to x86-64 machine IR");
        return false;
    }

    bool success =
        luna_vector_reserve(&module->globals, source->globals.length) &&
        luna_vector_reserve(&module->functions, source->functions.length);
    for (size_t global_index = 0U;
         success && global_index < source->globals.length; global_index += 1U) {
        const LunaIrGlobal *source_global =
            luna_vector_at_const(&source->globals, global_index);
        LunaX8664MachineGlobal target_global = {0};
        success =
            source_global != NULL &&
            luna_x86_64_machine_copy_global(source_global, &target_global) &&
            luna_vector_push(&module->globals, &target_global);
        if (!success) {
            luna_vector_destroy(&target_global.bytes);
        }
    }
    for (size_t function_index = 0U;
         success && function_index < source->functions.length;
         function_index += 1U) {
        const LunaIrFunction *source_function =
            luna_vector_at_const(&source->functions, function_index);
        if (source_function == NULL) {
            success = false;
            break;
        }
        LunaX8664MachineFunction target_function;
        if (!luna_x86_64_machine_lower_function(source_function,
                                                &target_function)) {
            success = false;
            break;
        }
        if (!luna_vector_push(&module->functions, &target_function)) {
            luna_x86_64_machine_function_destroy(&target_function);
            success = false;
        }
    }

    if (!success) {
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while lowering x86-64 machine IR");
        return false;
    }
    module->kind = source->kind == LUNA_IR_MODULE_EXECUTABLE
                       ? LUNA_X86_64_MACHINE_MODULE_EXECUTABLE
                       : LUNA_X86_64_MACHINE_MODULE_LIBRARY;
    module->entry_function = source->entry_function;
    return true;
}
