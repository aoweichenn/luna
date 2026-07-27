#include "instruction_rewrite_internal.h"

#include "register_allocation_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t luna_x86_64_instruction_rewrite_register_bit(
    LunaX8664PhysicalRegister physical_register) {
    return luna_x86_64_physical_register_bit_internal(physical_register);
}

static uint64_t
luna_x86_64_instruction_rewrite_register_range(LunaX8664PhysicalRegister first,
                                               LunaX8664PhysicalRegister last) {
    uint64_t mask = 0U;
    for (uint32_t register_value = (uint32_t)first;
         register_value <= (uint32_t)last; register_value += 1U) {
        mask |= luna_x86_64_instruction_rewrite_register_bit(
            (LunaX8664PhysicalRegister)register_value);
    }
    return mask;
}

uint64_t luna_x86_64_instruction_rewrite_reserved_register_mask(void) {
    const uint64_t general_mask =
        luna_x86_64_instruction_rewrite_register_bit(
            LUNA_X86_64_PHYSICAL_REGISTER_RAX) |
        luna_x86_64_instruction_rewrite_register_bit(
            LUNA_X86_64_PHYSICAL_REGISTER_RCX) |
        luna_x86_64_instruction_rewrite_register_bit(
            LUNA_X86_64_PHYSICAL_REGISTER_RDX) |
        luna_x86_64_instruction_rewrite_register_bit(
            LUNA_X86_64_PHYSICAL_REGISTER_RSI) |
        luna_x86_64_instruction_rewrite_register_bit(
            LUNA_X86_64_PHYSICAL_REGISTER_RDI) |
        luna_x86_64_instruction_rewrite_register_range(
            LUNA_X86_64_PHYSICAL_REGISTER_R8,
            LUNA_X86_64_PHYSICAL_REGISTER_R11);
    const uint64_t vector_mask = luna_x86_64_instruction_rewrite_register_range(
                                     LUNA_X86_64_PHYSICAL_REGISTER_XMM0,
                                     LUNA_X86_64_PHYSICAL_REGISTER_XMM7) |
                                 luna_x86_64_instruction_rewrite_register_bit(
                                     LUNA_X86_64_PHYSICAL_REGISTER_XMM15);
    return general_mask | vector_mask;
}

uint64_t luna_x86_64_instruction_rewrite_caller_saved_mask_internal(void) {
    return luna_x86_64_instruction_rewrite_reserved_register_mask() |
           luna_x86_64_instruction_rewrite_register_range(
               LUNA_X86_64_PHYSICAL_REGISTER_XMM8,
               LUNA_X86_64_PHYSICAL_REGISTER_XMM14);
}

static LunaX8664PhysicalRegister
luna_x86_64_instruction_rewrite_general_argument_register(uint32_t index) {
    static const LunaX8664PhysicalRegister registers[] = {
        LUNA_X86_64_PHYSICAL_REGISTER_RDI, LUNA_X86_64_PHYSICAL_REGISTER_RSI,
        LUNA_X86_64_PHYSICAL_REGISTER_RDX, LUNA_X86_64_PHYSICAL_REGISTER_RCX,
        LUNA_X86_64_PHYSICAL_REGISTER_R8,  LUNA_X86_64_PHYSICAL_REGISTER_R9,
    };
    return index < LUNA_X86_64_ABI_GENERAL_REGISTER_COUNT
               ? registers[index]
               : LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
}

static LunaX8664PhysicalRegister
luna_x86_64_instruction_rewrite_vector_argument_register(uint32_t index) {
    if (index >= LUNA_X86_64_ABI_VECTOR_REGISTER_COUNT) {
        return LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    }
    return (LunaX8664PhysicalRegister)((uint32_t)
                                           LUNA_X86_64_PHYSICAL_REGISTER_XMM0 +
                                       index);
}

static bool luna_x86_64_instruction_rewrite_add_fixed_register(
    uint64_t *mask, LunaX8664PhysicalRegister physical_register) {
    const uint64_t bit =
        luna_x86_64_instruction_rewrite_register_bit(physical_register);
    if (mask == NULL || bit == 0U) {
        return false;
    }
    *mask |= bit;
    return true;
}

static bool luna_x86_64_instruction_rewrite_add_parameter_piece(
    const LunaX8664AbiParameterLocation *location, uint32_t piece_index,
    LunaX8664InstructionFixedRegisters *fixed_registers) {
    LunaX8664PhysicalRegister physical_register =
        LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    if (location->pieces[piece_index].kind ==
        LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER) {
        physical_register =
            luna_x86_64_instruction_rewrite_general_argument_register(
                location->pieces[piece_index].register_index);
    } else if (location->pieces[piece_index].kind ==
               LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER) {
        physical_register =
            luna_x86_64_instruction_rewrite_vector_argument_register(
                location->pieces[piece_index].register_index);
    }
    if (!luna_x86_64_instruction_rewrite_add_fixed_register(
            &fixed_registers->input_mask, physical_register) ||
        !luna_x86_64_instruction_rewrite_add_fixed_register(
            &fixed_registers->parallel_move_destination_mask,
            physical_register) ||
        fixed_registers->parallel_move_count == UINT32_MAX) {
        return false;
    }
    fixed_registers->parallel_move_count += 1U;
    return true;
}

static bool luna_x86_64_instruction_rewrite_add_call_parameter(
    const LunaX8664AbiParameterLocation *location,
    LunaX8664InstructionFixedRegisters *fixed_registers) {
    if (location->kind == LUNA_X86_64_ABI_LOCATION_STACK) {
        return true;
    }
    if (location->is_aggregate) {
        for (uint32_t piece_index = 0U; piece_index < location->piece_count;
             piece_index += 1U) {
            if (!luna_x86_64_instruction_rewrite_add_parameter_piece(
                    location, piece_index, fixed_registers)) {
                return false;
            }
        }
        return true;
    }

    LunaX8664PhysicalRegister physical_register =
        LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    if (location->kind == LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER) {
        physical_register =
            luna_x86_64_instruction_rewrite_general_argument_register(
                location->register_index);
    } else if (location->kind == LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER) {
        physical_register =
            luna_x86_64_instruction_rewrite_vector_argument_register(
                location->register_index);
    }
    if (!luna_x86_64_instruction_rewrite_add_fixed_register(
            &fixed_registers->input_mask, physical_register) ||
        !luna_x86_64_instruction_rewrite_add_fixed_register(
            &fixed_registers->parallel_move_destination_mask,
            physical_register) ||
        fixed_registers->parallel_move_count == UINT32_MAX) {
        return false;
    }
    fixed_registers->parallel_move_count += 1U;
    return true;
}

static bool luna_x86_64_instruction_rewrite_add_call_return(
    const LunaX8664MachineFunction *callee,
    const LunaX8664FunctionAbi *callee_abi,
    LunaX8664InstructionFixedRegisters *fixed_registers) {
    if (callee_abi->return_location.is_aggregate) {
        if (callee_abi->return_location.uses_hidden_pointer) {
            if (fixed_registers->parallel_move_count == UINT32_MAX) {
                return false;
            }
            fixed_registers->parallel_move_count += 1U;
            return luna_x86_64_instruction_rewrite_add_fixed_register(
                       &fixed_registers->input_mask,
                       LUNA_X86_64_PHYSICAL_REGISTER_RDI) &&
                   luna_x86_64_instruction_rewrite_add_fixed_register(
                       &fixed_registers->parallel_move_destination_mask,
                       LUNA_X86_64_PHYSICAL_REGISTER_RDI) &&
                   luna_x86_64_instruction_rewrite_add_fixed_register(
                       &fixed_registers->output_mask,
                       LUNA_X86_64_PHYSICAL_REGISTER_RAX);
        }
        for (uint32_t piece_index = 0U;
             piece_index < callee_abi->return_location.piece_count;
             piece_index += 1U) {
            const uint32_t register_index =
                callee_abi->return_location.pieces[piece_index].register_index;
            if (register_index >= LUNA_X86_64_ABI_MAX_REGISTER_EIGHTBYTES) {
                return false;
            }
            const LunaX8664PhysicalRegister physical_register =
                callee_abi->return_location.pieces[piece_index].abi_class ==
                        LUNA_X86_64_ABI_CLASS_INTEGER
                    ? (register_index == 0U ? LUNA_X86_64_PHYSICAL_REGISTER_RAX
                                            : LUNA_X86_64_PHYSICAL_REGISTER_RDX)
                    : (register_index == 0U
                           ? LUNA_X86_64_PHYSICAL_REGISTER_XMM0
                           : LUNA_X86_64_PHYSICAL_REGISTER_XMM1);
            if (!luna_x86_64_instruction_rewrite_add_fixed_register(
                    &fixed_registers->output_mask, physical_register)) {
                return false;
            }
        }
        return true;
    }
    if (callee->return_type == LUNA_X86_64_MACHINE_TYPE_VOID) {
        return true;
    }
    return luna_x86_64_instruction_rewrite_add_fixed_register(
        &fixed_registers->output_mask,
        luna_x86_64_machine_type_is_float(callee->return_type)
            ? LUNA_X86_64_PHYSICAL_REGISTER_XMM0
            : LUNA_X86_64_PHYSICAL_REGISTER_RAX);
}

static bool luna_x86_64_instruction_rewrite_call_fixed_registers(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineInstruction *instruction,
    LunaX8664InstructionFixedRegisters *fixed_registers) {
    const LunaX8664MachineFunction *callee =
        luna_x86_64_machine_module_function_const(module, instruction->callee);
    const LunaX8664FunctionAbi *callee_abi =
        luna_vector_at_const(&abi->functions, (size_t)instruction->callee);
    if (callee == NULL || callee_abi == NULL ||
        callee_abi->parameter_locations.length != instruction->argument_count) {
        return false;
    }
    for (uint32_t argument_index = 0U;
         argument_index < instruction->argument_count; argument_index += 1U) {
        const LunaX8664AbiParameterLocation *location = luna_vector_at_const(
            &callee_abi->parameter_locations, (size_t)argument_index);
        if (location == NULL ||
            !luna_x86_64_instruction_rewrite_add_call_parameter(
                location, fixed_registers)) {
            return false;
        }
    }
    if (!luna_x86_64_instruction_rewrite_add_call_return(callee, callee_abi,
                                                         fixed_registers)) {
        return false;
    }
    fixed_registers->clobbered_mask =
        luna_x86_64_instruction_rewrite_caller_saved_mask_internal();
    return true;
}

bool luna_x86_64_instruction_fixed_registers_internal(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineInstruction *instruction,
    LunaX8664InstructionFixedRegisters *fixed_registers) {
    if (module == NULL || abi == NULL || instruction == NULL ||
        fixed_registers == NULL) {
        return false;
    }
    memset(fixed_registers, 0, sizeof(*fixed_registers));
    if (instruction->opcode == LUNA_X86_64_MACHINE_CALL) {
        return luna_x86_64_instruction_rewrite_call_fixed_registers(
            module, abi, instruction, fixed_registers);
    }

    const uint64_t rax = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_RAX);
    const uint64_t rcx = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_RCX);
    const uint64_t rdx = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_RDX);
    const uint64_t rdi = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_RDI);
    const uint64_t rsi = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_RSI);
    const uint64_t r10 = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_R10);
    const uint64_t r11 = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_R11);
    const uint64_t xmm0 = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM0);
    const uint64_t xmm1 = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM1);
    const uint64_t xmm15 = luna_x86_64_instruction_rewrite_register_bit(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM15);

    switch (instruction->opcode) {
    case LUNA_X86_64_MACHINE_CONST_BOOL:
    case LUNA_X86_64_MACHINE_JUMP:
        break;
    case LUNA_X86_64_MACHINE_CONST_INTEGER:
        if (luna_x86_64_machine_type_bit_width(instruction->type) == 64U) {
            fixed_registers->output_mask = rax;
            fixed_registers->clobbered_mask = rax;
        }
        break;
    case LUNA_X86_64_MACHINE_CONST_FLOAT:
        fixed_registers->output_mask = rax;
        fixed_registers->clobbered_mask = rax;
        break;
    case LUNA_X86_64_MACHINE_DIV_INTEGER:
    case LUNA_X86_64_MACHINE_REM_INTEGER:
        fixed_registers->input_mask = rax | rdx;
        fixed_registers->output_mask = rax | rdx;
        fixed_registers->clobbered_mask = rax | rcx | rdx;
        break;
    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER:
        fixed_registers->input_mask = rax | rcx;
        fixed_registers->output_mask = rax;
        fixed_registers->clobbered_mask = rax | rcx;
        break;
    case LUNA_X86_64_MACHINE_ZERO_SLOT:
        fixed_registers->input_mask = rdi | rcx;
        fixed_registers->clobbered_mask = rax | rcx | rdi;
        break;
    case LUNA_X86_64_MACHINE_MEMORY_COPY:
        fixed_registers->input_mask = rdi | rsi | rcx;
        fixed_registers->clobbered_mask = rax | rcx | rdi | rsi;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
    case LUNA_X86_64_MACHINE_ADD_FLOAT:
    case LUNA_X86_64_MACHINE_SUB_FLOAT:
    case LUNA_X86_64_MACHINE_MUL_FLOAT:
    case LUNA_X86_64_MACHINE_DIV_FLOAT:
        fixed_registers->input_mask = xmm0;
        fixed_registers->output_mask = xmm0;
        fixed_registers->clobbered_mask = xmm0;
        break;
    case LUNA_X86_64_MACHINE_NEG_FLOAT:
        fixed_registers->input_mask = rax;
        fixed_registers->output_mask = rax;
        fixed_registers->clobbered_mask = rax | rdx;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
        fixed_registers->input_mask = rax;
        fixed_registers->output_mask = xmm0;
        fixed_registers->clobbered_mask = rax | rdx | xmm0;
        break;
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
        fixed_registers->input_mask = xmm0;
        fixed_registers->output_mask = rax;
        fixed_registers->clobbered_mask = rax | rdx | xmm0 | xmm1;
        break;
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        fixed_registers->input_mask = xmm0;
        fixed_registers->output_mask = rax;
        fixed_registers->clobbered_mask = rax | rcx | xmm0;
        break;
    case LUNA_X86_64_MACHINE_RETURN:
        fixed_registers->input_mask = rax | rdx | r11 | xmm0 | xmm1;
        fixed_registers->output_mask = rax | rdx | xmm0 | xmm1;
        fixed_registers->clobbered_mask =
            rax | rcx | rdx | rdi | rsi | r10 | r11 | xmm0 | xmm1 | xmm15;
        break;
    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
    case LUNA_X86_64_MACHINE_NULL_CHECK:
    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
    case LUNA_X86_64_MACHINE_BRANCH:
    case LUNA_X86_64_MACHINE_CONST_NULL:
    case LUNA_X86_64_MACHINE_LOAD:
    case LUNA_X86_64_MACHINE_STORE:
    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
    case LUNA_X86_64_MACHINE_NEG_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
    case LUNA_X86_64_MACHINE_BOOL_NOT:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
    case LUNA_X86_64_MACHINE_ADD_INTEGER:
    case LUNA_X86_64_MACHINE_SUB_INTEGER:
    case LUNA_X86_64_MACHINE_MUL_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
        fixed_registers->clobbered_mask = rax | rcx | xmm0;
        break;
    case LUNA_X86_64_MACHINE_CALL:
        return false;
    }
    return true;
}

void luna_x86_64_function_instruction_rewrite_destroy_internal(
    LunaX8664FunctionInstructionRewrite *rewrite) {
    if (rewrite == NULL) {
        return;
    }
    for (size_t instruction_index = 0U;
         instruction_index < rewrite->instructions.length;
         instruction_index += 1U) {
        LunaX8664RewrittenInstruction *instruction =
            luna_vector_at(&rewrite->instructions, instruction_index);
        if (instruction != NULL) {
            luna_vector_destroy(&instruction->use_locations);
        }
    }
    luna_vector_destroy(&rewrite->instructions);
    luna_vector_destroy(&rewrite->value_locations);
    memset(rewrite, 0, sizeof(*rewrite));
}

void luna_x86_64_instruction_rewrite_init(
    LunaX8664ModuleInstructionRewrite *rewrite) {
    if (rewrite != NULL) {
        luna_vector_init(&rewrite->functions,
                         sizeof(LunaX8664FunctionInstructionRewrite));
    }
}

void luna_x86_64_instruction_rewrite_destroy(
    LunaX8664ModuleInstructionRewrite *rewrite) {
    if (rewrite == NULL) {
        return;
    }
    for (size_t function_index = 0U; function_index < rewrite->functions.length;
         function_index += 1U) {
        LunaX8664FunctionInstructionRewrite *function =
            luna_vector_at(&rewrite->functions, function_index);
        luna_x86_64_function_instruction_rewrite_destroy_internal(function);
    }
    luna_vector_destroy(&rewrite->functions);
}

static bool luna_x86_64_instruction_rewrite_copy_location(
    const LunaX8664FunctionRegisterAllocation *allocation,
    LunaX8664MachineVirtualRegister virtual_register,
    LunaX8664VirtualRegisterAllocation *location) {
    const LunaX8664VirtualRegisterAllocation *source = luna_vector_at_const(
        &allocation->allocations, (size_t)virtual_register);
    if (source == NULL || location == NULL) {
        return false;
    }
    *location = *source;
    return true;
}

static bool luna_x86_64_instruction_rewrite_build_instruction(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    const LunaX8664MachineInstruction *source, uint64_t position,
    LunaX8664RewrittenInstruction *instruction) {
    memset(instruction, 0, sizeof(*instruction));
    luna_vector_init(&instruction->use_locations,
                     sizeof(LunaX8664VirtualRegisterAllocation));
    instruction->position = position;
    instruction->opcode = source->opcode;
    instruction->result_location.kind = LUNA_X86_64_ALLOCATION_INVALID;
    instruction->result_location.physical_register =
        LUNA_X86_64_PHYSICAL_REGISTER_INVALID;
    instruction->result_location.spill_slot = LUNA_X86_64_MACHINE_INVALID_ID;

    LunaX8664InstructionFixedRegisters fixed_registers;
    if (!luna_x86_64_instruction_fixed_registers_internal(module, abi, source,
                                                          &fixed_registers)) {
        return false;
    }
    instruction->fixed_input_register_mask = fixed_registers.input_mask;
    instruction->fixed_output_register_mask = fixed_registers.output_mask;
    instruction->clobbered_register_mask = fixed_registers.clobbered_mask;
    instruction->parallel_move_destination_mask =
        fixed_registers.parallel_move_destination_mask;
    instruction->parallel_move_count = fixed_registers.parallel_move_count;

    LunaX8664MachineVirtualRegister definition = LUNA_X86_64_MACHINE_INVALID_ID;
    if (luna_x86_64_machine_instruction_definition(source, &definition)) {
        instruction->has_result = true;
        if (!luna_x86_64_instruction_rewrite_copy_location(
                allocation, definition, &instruction->result_location)) {
            return false;
        }
    }

    const uint32_t use_count =
        luna_x86_64_machine_instruction_use_count(source);
    if (!luna_vector_reserve(&instruction->use_locations, use_count)) {
        return false;
    }
    for (uint32_t use_index = 0U; use_index < use_count; use_index += 1U) {
        const LunaX8664MachineVirtualRegister virtual_register =
            luna_x86_64_machine_instruction_use(function, source, use_index);
        LunaX8664VirtualRegisterAllocation location;
        if (!luna_x86_64_instruction_rewrite_copy_location(
                allocation, virtual_register, &location) ||
            !luna_vector_push(&instruction->use_locations, &location)) {
            return false;
        }
    }
    return true;
}

static bool luna_x86_64_instruction_rewrite_build_function(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664MachineFunction *function,
    const LunaX8664FunctionRegisterAllocation *allocation,
    LunaX8664FunctionInstructionRewrite *rewrite) {
    memset(rewrite, 0, sizeof(*rewrite));
    luna_vector_init(&rewrite->value_locations,
                     sizeof(LunaX8664VirtualRegisterAllocation));
    luna_vector_init(&rewrite->instructions,
                     sizeof(LunaX8664RewrittenInstruction));
    rewrite->spill_slot_count = allocation->spill_slot_count;
    rewrite->used_register_mask = allocation->used_register_mask;
    rewrite->used_callee_saved_register_mask =
        allocation->used_callee_saved_register_mask;
    if (!luna_vector_reserve(&rewrite->value_locations,
                             allocation->allocations.length) ||
        !luna_vector_reserve(&rewrite->instructions,
                             (size_t)allocation->instruction_count)) {
        return false;
    }
    for (size_t value_index = 0U; value_index < allocation->allocations.length;
         value_index += 1U) {
        const LunaX8664VirtualRegisterAllocation *location =
            luna_vector_at_const(&allocation->allocations, value_index);
        if (location == NULL ||
            !luna_vector_push(&rewrite->value_locations, location)) {
            return false;
        }
    }

    uint64_t position = 0U;
    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block == NULL) {
            return false;
        }
        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *source =
                luna_vector_at_const(&block->instructions, instruction_index);
            LunaX8664RewrittenInstruction instruction = {0};
            if (source == NULL ||
                !luna_x86_64_instruction_rewrite_build_instruction(
                    module, abi, function, allocation, source, position,
                    &instruction)) {
                luna_vector_destroy(&instruction.use_locations);
                return false;
            }
            if (!luna_vector_push(&rewrite->instructions, &instruction)) {
                luna_vector_destroy(&instruction.use_locations);
                return false;
            }
            if (position == UINT64_MAX) {
                return false;
            }
            position += 1U;
        }
    }
    rewrite->instruction_count = position;
    return position == allocation->instruction_count;
}

bool luna_x86_64_instruction_rewrite_build(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    LunaX8664ModuleInstructionRewrite *rewrite, FILE *error_stream) {
    if (module == NULL || abi == NULL || liveness == NULL ||
        allocation == NULL || rewrite == NULL ||
        rewrite->functions.length != 0U ||
        !luna_x86_64_abi_verify(module, abi, error_stream) ||
        !luna_x86_64_liveness_verify(module, liveness, error_stream) ||
        !luna_x86_64_register_allocation_verify(module, liveness, allocation,
                                                error_stream) ||
        !luna_vector_reserve(&rewrite->functions, module->functions.length)) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        const LunaX8664FunctionRegisterAllocation *function_allocation =
            luna_vector_at_const(&allocation->functions, function_index);
        LunaX8664FunctionInstructionRewrite function_rewrite = {0};
        if (function == NULL || function_allocation == NULL ||
            !luna_x86_64_instruction_rewrite_build_function(
                module, abi, function, function_allocation,
                &function_rewrite)) {
            luna_x86_64_function_instruction_rewrite_destroy_internal(
                &function_rewrite);
            luna_x86_64_instruction_rewrite_destroy(rewrite);
            luna_x86_64_instruction_rewrite_init(rewrite);
            return false;
        }
        if (!luna_vector_push(&rewrite->functions, &function_rewrite)) {
            luna_x86_64_function_instruction_rewrite_destroy_internal(
                &function_rewrite);
            luna_x86_64_instruction_rewrite_destroy(rewrite);
            luna_x86_64_instruction_rewrite_init(rewrite);
            return false;
        }
    }

    if (!luna_x86_64_instruction_rewrite_verify(
            module, abi, liveness, allocation, rewrite, error_stream)) {
        luna_x86_64_instruction_rewrite_destroy(rewrite);
        luna_x86_64_instruction_rewrite_init(rewrite);
        return false;
    }
    return true;
}
