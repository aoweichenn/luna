#include "luna/backend/x86_64/machine_ir.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *
luna_x86_64_machine_opcode_name(LunaX8664MachineOpcode opcode) {
    switch (opcode) {
    case LUNA_X86_64_MACHINE_CONST_INTEGER:
        return "const.integer";
    case LUNA_X86_64_MACHINE_CONST_FLOAT:
        return "const.float";
    case LUNA_X86_64_MACHINE_CONST_BOOL:
        return "const.bool";
    case LUNA_X86_64_MACHINE_CONST_NULL:
        return "const.null";
    case LUNA_X86_64_MACHINE_LOAD:
        return "load.slot";
    case LUNA_X86_64_MACHINE_STORE:
        return "store.slot";
    case LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT:
        return "address.slot";
    case LUNA_X86_64_MACHINE_MEMBER_ADDRESS:
        return "address.member";
    case LUNA_X86_64_MACHINE_GLOBAL_ADDRESS:
        return "address.global";
    case LUNA_X86_64_MACHINE_ZERO_SLOT:
        return "zero.slot";
    case LUNA_X86_64_MACHINE_MEMORY_COPY:
        return "memory.copy";
    case LUNA_X86_64_MACHINE_LOAD_INDIRECT:
        return "load.memory";
    case LUNA_X86_64_MACHINE_STORE_INDIRECT:
        return "store.memory";
    case LUNA_X86_64_MACHINE_NULL_CHECK:
        return "check.null";
    case LUNA_X86_64_MACHINE_BOUNDS_CHECK:
        return "check.bounds";
    case LUNA_X86_64_MACHINE_POINTER_OFFSET:
        return "address.index";
    case LUNA_X86_64_MACHINE_NEG_INTEGER:
        return "neg.integer";
    case LUNA_X86_64_MACHINE_NEG_FLOAT:
        return "neg.float";
    case LUNA_X86_64_MACHINE_BIT_NOT_INTEGER:
        return "not.integer";
    case LUNA_X86_64_MACHINE_BOOL_NOT:
        return "not.bool";
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER:
        return "convert.integer";
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT:
        return "convert.float";
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_FLOAT:
        return "convert.integer_to_float";
    case LUNA_X86_64_MACHINE_CONVERT_FLOAT_TO_INTEGER:
        return "convert.float_to_integer";
    case LUNA_X86_64_MACHINE_CONVERT_POINTER_TO_INTEGER:
        return "convert.pointer_to_integer";
    case LUNA_X86_64_MACHINE_CONVERT_INTEGER_TO_POINTER:
        return "convert.integer_to_pointer";
    case LUNA_X86_64_MACHINE_ADD_INTEGER:
        return "add.integer";
    case LUNA_X86_64_MACHINE_SUB_INTEGER:
        return "sub.integer";
    case LUNA_X86_64_MACHINE_MUL_INTEGER:
        return "mul.integer";
    case LUNA_X86_64_MACHINE_DIV_INTEGER:
        return "div.integer";
    case LUNA_X86_64_MACHINE_REM_INTEGER:
        return "rem.integer";
    case LUNA_X86_64_MACHINE_BIT_AND_INTEGER:
        return "and.integer";
    case LUNA_X86_64_MACHINE_BIT_OR_INTEGER:
        return "or.integer";
    case LUNA_X86_64_MACHINE_BIT_XOR_INTEGER:
        return "xor.integer";
    case LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER:
        return "shift_left.integer";
    case LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER:
        return "shift_right.integer";
    case LUNA_X86_64_MACHINE_ADD_FLOAT:
        return "add.float";
    case LUNA_X86_64_MACHINE_SUB_FLOAT:
        return "sub.float";
    case LUNA_X86_64_MACHINE_MUL_FLOAT:
        return "mul.float";
    case LUNA_X86_64_MACHINE_DIV_FLOAT:
        return "div.float";
    case LUNA_X86_64_MACHINE_COMPARE_EQUAL:
        return "compare.equal";
    case LUNA_X86_64_MACHINE_COMPARE_NOT_EQUAL:
        return "compare.not_equal";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_INTEGER:
        return "compare.less.integer";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_INTEGER:
        return "compare.less_equal.integer";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_INTEGER:
        return "compare.greater.integer";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_INTEGER:
        return "compare.greater_equal.integer";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_FLOAT:
        return "compare.less.float";
    case LUNA_X86_64_MACHINE_COMPARE_LESS_EQUAL_FLOAT:
        return "compare.less_equal.float";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_FLOAT:
        return "compare.greater.float";
    case LUNA_X86_64_MACHINE_COMPARE_GREATER_EQUAL_FLOAT:
        return "compare.greater_equal.float";
    case LUNA_X86_64_MACHINE_CALL:
        return "call";
    case LUNA_X86_64_MACHINE_JUMP:
        return "jump";
    case LUNA_X86_64_MACHINE_BRANCH:
        return "branch";
    case LUNA_X86_64_MACHINE_RETURN:
        return "return";
    }
    return "invalid";
}

static const char *
luna_x86_64_machine_linkage_name(LunaX8664MachineFunctionLinkage linkage) {
    switch (linkage) {
    case LUNA_X86_64_MACHINE_LINKAGE_INTERNAL:
        return "internal";
    case LUNA_X86_64_MACHINE_LINKAGE_MODULE_EXPORT:
        return "module-export";
    case LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT:
        return "module-import";
    case LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C:
        return "external-c";
    }
    return "invalid";
}

static const char *luna_x86_64_machine_register_class_name(
    LunaX8664MachineRegisterClass register_class) {
    switch (register_class) {
    case LUNA_X86_64_MACHINE_REGISTER_GENERAL:
        return "gpr";
    case LUNA_X86_64_MACHINE_REGISTER_FLOAT:
        return "fpr";
    case LUNA_X86_64_MACHINE_REGISTER_NONE:
        return "none";
    }
    return "invalid";
}

static bool luna_x86_64_machine_print_function_name(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function) {
    if (function->linkage != LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C &&
        (!luna_string_builder_append_view(output, function->module_name) ||
         !luna_string_builder_append_c_string(output, "::"))) {
        return false;
    }
    return luna_string_builder_append_view(output, function->name);
}

static bool
luna_x86_64_machine_print_signature(LunaStringBuilder *output,
                                    const LunaX8664MachineFunction *function) {
    if (!luna_string_builder_append_c_string(output, "(")) {
        return false;
    }
    for (size_t index = 0U; index < function->parameter_types.length;
         index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->parameter_types, index);
        if (type == NULL ||
            (index > 0U &&
             !luna_string_builder_append_c_string(output, ", ")) ||
            !luna_string_builder_append_format(
                output, "%s:%s", luna_x86_64_machine_type_name(*type),
                luna_x86_64_machine_register_class_name(
                    luna_x86_64_machine_type_register_class(*type)))) {
            return false;
        }
    }
    return luna_string_builder_append_format(
        output, ") -> %s",
        luna_x86_64_machine_type_name(function->return_type));
}

static bool
luna_x86_64_machine_opcode_has_immediate(LunaX8664MachineOpcode opcode) {
    return opcode == LUNA_X86_64_MACHINE_CONST_INTEGER ||
           opcode == LUNA_X86_64_MACHINE_CONST_FLOAT ||
           opcode == LUNA_X86_64_MACHINE_CONST_BOOL ||
           opcode == LUNA_X86_64_MACHINE_CONST_NULL ||
           opcode == LUNA_X86_64_MACHINE_MEMBER_ADDRESS ||
           opcode == LUNA_X86_64_MACHINE_MEMORY_COPY ||
           opcode == LUNA_X86_64_MACHINE_BOUNDS_CHECK ||
           opcode == LUNA_X86_64_MACHINE_POINTER_OFFSET;
}

static bool luna_x86_64_machine_opcode_has_slot(LunaX8664MachineOpcode opcode) {
    return opcode == LUNA_X86_64_MACHINE_LOAD ||
           opcode == LUNA_X86_64_MACHINE_STORE ||
           opcode == LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT ||
           opcode == LUNA_X86_64_MACHINE_ZERO_SLOT;
}

static bool
luna_x86_64_machine_print_uses(LunaStringBuilder *output,
                               const LunaX8664MachineFunction *function,
                               const LunaX8664MachineInstruction *instruction) {
    const uint32_t use_count =
        luna_x86_64_machine_instruction_use_count(instruction);
    if (use_count == 0U ||
        !luna_string_builder_append_c_string(output, " uses=[")) {
        return use_count == 0U;
    }
    for (uint32_t index = 0U; index < use_count; index += 1U) {
        const LunaX8664MachineVirtualRegister use =
            luna_x86_64_machine_instruction_use(function, instruction, index);
        if ((index > 0U &&
             !luna_string_builder_append_c_string(output, ", ")) ||
            !luna_string_builder_append_format(output, "%%v%" PRIu32, use)) {
            return false;
        }
    }
    return luna_string_builder_append_c_string(output, "]");
}

static bool luna_x86_64_machine_print_instruction(
    LunaStringBuilder *output, const LunaX8664MachineFunction *function,
    const LunaX8664MachineInstruction *instruction) {
    LunaX8664MachineVirtualRegister definition = LUNA_X86_64_MACHINE_INVALID_ID;
    if (!luna_string_builder_append_c_string(output, "    ")) {
        return false;
    }
    if (luna_x86_64_machine_instruction_definition(instruction, &definition) &&
        !luna_string_builder_append_format(output, "%%v%" PRIu32 " = ",
                                           definition)) {
        return false;
    }
    if (!luna_string_builder_append_format(
            output, "%s type=%s",
            luna_x86_64_machine_opcode_name(instruction->opcode),
            luna_x86_64_machine_type_name(instruction->type)) ||
        !luna_x86_64_machine_print_uses(output, function, instruction)) {
        return false;
    }
    if (instruction->memory_type != LUNA_X86_64_MACHINE_TYPE_VOID &&
        !luna_string_builder_append_format(
            output, " memory=%s",
            luna_x86_64_machine_type_name(instruction->memory_type))) {
        return false;
    }
    if (luna_x86_64_machine_opcode_has_slot(instruction->opcode) &&
        !luna_string_builder_append_format(output, " slot=$s%" PRIu32,
                                           instruction->slot)) {
        return false;
    }
    if (instruction->opcode == LUNA_X86_64_MACHINE_GLOBAL_ADDRESS &&
        !luna_string_builder_append_format(output, " global=@g%" PRIu32,
                                           instruction->global)) {
        return false;
    }
    if (instruction->opcode == LUNA_X86_64_MACHINE_CALL &&
        !luna_string_builder_append_format(output, " callee=@f%" PRIu32,
                                           instruction->callee)) {
        return false;
    }
    if ((instruction->opcode == LUNA_X86_64_MACHINE_JUMP ||
         instruction->opcode == LUNA_X86_64_MACHINE_BRANCH) &&
        !luna_string_builder_append_format(output, " true=bb%" PRIu32,
                                           instruction->true_block)) {
        return false;
    }
    if (instruction->opcode == LUNA_X86_64_MACHINE_BRANCH &&
        !luna_string_builder_append_format(output, " false=bb%" PRIu32,
                                           instruction->false_block)) {
        return false;
    }
    if (luna_x86_64_machine_opcode_has_immediate(instruction->opcode) &&
        !luna_string_builder_append_format(output, " imm=0x%016" PRIx64,
                                           instruction->immediate)) {
        return false;
    }
    return luna_string_builder_append_c_string(output, "\n");
}

static bool
luna_x86_64_machine_print_function(LunaStringBuilder *output,
                                   const LunaX8664MachineFunction *function,
                                   size_t function_index) {
    const bool is_declaration =
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_MODULE_IMPORT ||
        function->linkage == LUNA_X86_64_MACHINE_LINKAGE_EXTERNAL_C;
    if (!luna_string_builder_append_format(
            output, "%s @f%zu ", is_declaration ? "declare" : "define",
            function_index) ||
        !luna_x86_64_machine_print_function_name(output, function) ||
        !luna_string_builder_append_format(
            output, " linkage=%s ",
            luna_x86_64_machine_linkage_name(function->linkage)) ||
        !luna_x86_64_machine_print_signature(output, function)) {
        return false;
    }
    if (function->has_module_metadata_hash &&
        !luna_string_builder_append_format(output, " metadata=0x%016" PRIx64,
                                           function->module_metadata_hash)) {
        return false;
    }
    if (is_declaration) {
        return luna_string_builder_append_c_string(output, "\n\n");
    }
    if (!luna_string_builder_append_c_string(output, " {\n")) {
        return false;
    }

    for (size_t slot_index = 0U; slot_index < function->slots.length;
         slot_index += 1U) {
        const LunaX8664MachineStackSlot *slot =
            luna_vector_at_const(&function->slots, slot_index);
        if (slot == NULL ||
            !luna_string_builder_append_format(
                output,
                "  stack $s%zu type=%s size=%" PRIu64 " align=%" PRIu32
                " class=%s\n",
                slot_index, luna_x86_64_machine_type_name(slot->type),
                slot->size_bytes, slot->alignment_bytes,
                slot->is_scalar ? "scalar" : "memory")) {
            return false;
        }
    }
    for (size_t value_index = 0U; value_index < function->value_types.length;
         value_index += 1U) {
        const LunaX8664MachineType *type =
            luna_vector_at_const(&function->value_types, value_index);
        if (type == NULL ||
            !luna_string_builder_append_format(
                output, "  vreg %%v%zu type=%s class=%s\n", value_index,
                luna_x86_64_machine_type_name(*type),
                luna_x86_64_machine_register_class_name(
                    luna_x86_64_machine_type_register_class(*type)))) {
            return false;
        }
    }
    if ((function->slots.length > 0U || function->value_types.length > 0U) &&
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    for (size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        const LunaX8664MachineBlock *block =
            luna_vector_at_const(&function->blocks, block_index);
        if (block == NULL || !luna_string_builder_append_format(
                                 output, "  bb%zu predecessors=%" PRIu32 ":\n",
                                 block_index, block->predecessor_count)) {
            return false;
        }
        for (size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *instruction =
                luna_vector_at_const(&block->instructions, instruction_index);
            if (instruction == NULL || !luna_x86_64_machine_print_instruction(
                                           output, function, instruction)) {
                return false;
            }
        }
    }
    return luna_string_builder_append_c_string(output, "}\n\n");
}

bool luna_x86_64_machine_print(const LunaX8664MachineModule *module,
                               LunaStringBuilder *output) {
    if (module == NULL || output == NULL || output->length != 0U ||
        !luna_target_info_is_supported(module->target)) {
        return false;
    }
    if (!luna_string_builder_append_format(
            output,
            "target-machine x86_64\n"
            "target-triple \"%s\"\n"
            "module-kind %s\n\n",
            module->target->triple,
            module->kind == LUNA_X86_64_MACHINE_MODULE_EXECUTABLE
                ? "executable"
                : "library")) {
        return false;
    }

    for (size_t global_index = 0U; global_index < module->globals.length;
         global_index += 1U) {
        const LunaX8664MachineGlobal *global =
            luna_vector_at_const(&module->globals, global_index);
        if (global == NULL ||
            !luna_string_builder_append_format(
                output, "global @g%zu align=%" PRIu32 " section=%s bytes=[",
                global_index, global->alignment_bytes,
                global->is_read_only ? "rodata" : "data")) {
            return false;
        }
        for (size_t byte_index = 0U; byte_index < global->bytes.length;
             byte_index += 1U) {
            const uint8_t *byte =
                luna_vector_at_const(&global->bytes, byte_index);
            if (byte == NULL ||
                (byte_index > 0U &&
                 !luna_string_builder_append_c_string(output, " ")) ||
                !luna_string_builder_append_format(output, "%02" PRIx8,
                                                   *byte)) {
                return false;
            }
        }
        if (!luna_string_builder_append_c_string(output, "]\n")) {
            return false;
        }
    }
    if (module->globals.length > 0U &&
        !luna_string_builder_append_c_string(output, "\n")) {
        return false;
    }

    for (size_t function_index = 0U; function_index < module->functions.length;
         function_index += 1U) {
        const LunaX8664MachineFunction *function =
            luna_vector_at_const(&module->functions, function_index);
        if (function == NULL || !luna_x86_64_machine_print_function(
                                    output, function, function_index)) {
            return false;
        }
    }
    return true;
}
