#include "sema_internal.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void luna_sema_report_allocation_failure(LunaSemaContext *context) {
    context->failed = true;
    if (!context->allocation_failed) {
        luna_sema_fail_plain(context, "out of memory while building typed IR");
        context->allocation_failed = true;
    }
}

void luna_sema_fail(LunaSemaContext *context, LunaSourceSpan span,
                    const char *format, ...) {
    context->failed = true;
    va_list arguments;
    va_start(arguments, format);
    luna_diagnostic_error_v(context->diagnostics, span, format, arguments);
    va_end(arguments);
}

void luna_sema_fail_plain(LunaSemaContext *context, const char *format, ...) {
    context->failed = true;
    va_list arguments;
    va_start(arguments, format);
    luna_diagnostic_error_plain_v(context->diagnostics, format, arguments);
    va_end(arguments);
}

static const LunaFunction *
luna_sema_find_syntax_function(const LunaProgram *program,
                               LunaStringView name) {
    if (program == NULL) {
        return NULL;
    }
    for (const LunaFunction *function = program->first_function;
         function != NULL; function = function->next) {
        if (luna_string_view_equal(function->name, name)) {
            return function;
        }
    }
    return NULL;
}

static bool luna_sema_validate_unique_functions(LunaSemaContext *context,
                                                const LunaProgram *program) {
    bool success = true;
    for (const LunaFunction *function = program->first_function;
         function != NULL; function = function->next) {
        for (const LunaFunction *previous = program->first_function;
             previous != function; previous = previous->next) {
            if (!luna_string_view_equal(previous->name, function->name)) {
                continue;
            }
            luna_sema_fail(context, function->span, "duplicate function '%.*s'",
                           (int)function->name.length, function->name.data);
            luna_diagnostic_note(context->diagnostics, previous->span,
                                 "the first declaration of '%.*s' is here",
                                 (int)function->name.length,
                                 function->name.data);
            success = false;
            break;
        }
    }
    return success;
}

static bool luna_sema_linux_syscall_abi_arity(LunaStringView name,
                                              uint32_t *argument_count) {
    static const char prefix[] = "luna_linux_syscall";
    const size_t prefix_length = sizeof(prefix) - 1U;
    if (argument_count == NULL || name.data == NULL ||
        name.length != prefix_length + 1U ||
        memcmp(name.data, prefix, prefix_length) != 0 ||
        name.data[prefix_length] < '0' || name.data[prefix_length] > '6') {
        return false;
    }
    *argument_count = (uint32_t)(name.data[prefix_length] - '0');
    return true;
}

static bool
luna_sema_validate_linux_syscall_abi_signature(LunaSemaContext *context,
                                               const LunaFunction *syntax,
                                               LunaSemaTypeId return_type) {
    uint32_t argument_count = 0U;
    if (!syntax->is_external ||
        !luna_sema_linux_syscall_abi_arity(syntax->name, &argument_count)) {
        return true;
    }

    uint32_t parameter_count = 0U;
    bool parameters_are_words = true;
    for (const LunaParameter *parameter = syntax->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        if (parameter_count == UINT32_MAX) {
            parameters_are_words = false;
            break;
        }
        parameter_count += 1U;
        if (luna_sema_resolve_type(context, &parameter->type) !=
            LUNA_TYPE_USIZE) {
            parameters_are_words = false;
        }
    }

    const uint32_t expected_parameter_count = argument_count + 1U;
    if (return_type == LUNA_TYPE_ISIZE && parameters_are_words &&
        parameter_count == expected_parameter_count) {
        return true;
    }
    luna_sema_fail(context, syntax->span,
                   "project syscall ABI function '%.*s' requires %" PRIu32
                   " usize parameter%s and an isize result",
                   (int)syntax->name.length, syntax->name.data,
                   expected_parameter_count,
                   expected_parameter_count == 1U ? "" : "s");
    return false;
}

static bool luna_sema_validate_function_signature(LunaSemaContext *context,
                                                  const LunaFunction *syntax) {
    bool success = true;
    if (syntax->is_external &&
        (luna_string_view_equal_c_string(syntax->name, "_start") ||
         (syntax->name.length >= 2U && syntax->name.data[0] == '_' &&
          syntax->name.data[1] == 'L'))) {
        luna_sema_fail(
            context, syntax->span,
            "external function name '%.*s' is reserved by the bootstrap ABI",
            (int)syntax->name.length, syntax->name.data);
        success = false;
    }

    const LunaSemaTypeId return_type =
        luna_sema_resolve_type(context, &syntax->return_type);
    if (return_type == LUNA_TYPE_INVALID) {
        success = false;
    } else if (syntax->is_external &&
               luna_sema_is_array_type(context, return_type)) {
        luna_sema_fail(context, syntax->return_type.span,
                       "fixed arrays have no by-value external C return ABI");
        success = false;
    } else if (luna_sema_is_memory_type(context, return_type)) {
        uint64_t size_bytes = 0U;
        uint32_t alignment_bytes = 0U;
        if (!luna_sema_type_layout(context, return_type, &size_bytes,
                                   &alignment_bytes) ||
            size_bytes == 0U) {
            luna_sema_fail(
                context, syntax->return_type.span,
                "aggregate return type requires a positive target layout");
            success = false;
        }
    }

    for (const LunaParameter *parameter = syntax->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        for (const LunaParameter *previous = syntax->first_parameter;
             previous != parameter; previous = previous->next) {
            if (!luna_string_view_equal(previous->name, parameter->name)) {
                continue;
            }
            luna_sema_fail(context, parameter->span,
                           "duplicate parameter '%.*s'",
                           (int)parameter->name.length, parameter->name.data);
            luna_diagnostic_note(context->diagnostics, previous->span,
                                 "the first parameter named '%.*s' is here",
                                 (int)parameter->name.length,
                                 parameter->name.data);
            success = false;
            break;
        }

        const LunaSemaTypeId parameter_type =
            luna_sema_resolve_type(context, &parameter->type);
        if (parameter_type == LUNA_TYPE_INVALID) {
            success = false;
            continue;
        }
        if (parameter_type == LUNA_TYPE_VOID) {
            luna_sema_fail(context, parameter->span,
                           "parameter '%.*s' has an invalid type",
                           (int)parameter->name.length, parameter->name.data);
            success = false;
            continue;
        }
        if (syntax->is_external &&
            luna_sema_is_array_type(context, parameter_type)) {
            luna_sema_fail(
                context, parameter->type.span,
                "fixed arrays have no by-value external C parameter ABI");
            success = false;
            continue;
        }
        if (luna_sema_is_memory_type(context, parameter_type)) {
            uint64_t size_bytes = 0U;
            uint32_t alignment_bytes = 0U;
            if (!luna_sema_type_layout(context, parameter_type, &size_bytes,
                                       &alignment_bytes) ||
                size_bytes == 0U) {
                luna_sema_fail(
                    context, parameter->type.span,
                    "aggregate parameter type requires a positive target "
                    "layout");
                success = false;
            }
        }
    }
    if (return_type != LUNA_TYPE_INVALID &&
        !luna_sema_validate_linux_syscall_abi_signature(context, syntax,
                                                        return_type)) {
        success = false;
    }
    return success;
}

static bool luna_sema_validate_interface_functions(LunaSemaContext *context) {
    if (context->interface_unit == NULL) {
        return true;
    }

    bool success =
        luna_sema_validate_unique_functions(context, context->interface_unit);
    for (const LunaFunction *function = context->interface_unit->first_function;
         function != NULL; function = function->next) {
        if (!function->is_declaration) {
            luna_sema_fail(
                context, function->span,
                "module interface function '%.*s' must be a declaration "
                "without a body",
                (int)function->name.length, function->name.data);
            success = false;
        }
        const bool signature_is_valid =
            luna_sema_validate_function_signature(context, function);
        if (!signature_is_valid) {
            success = false;
            continue;
        }
        if (!function->is_exported) {
            continue;
        }

        const LunaSemaTypeId return_type =
            luna_sema_resolve_type(context, &function->return_type);
        if (!luna_sema_validate_exported_type(context, return_type,
                                              function->return_type.span,
                                              function->name)) {
            success = false;
        }
        for (const LunaParameter *parameter = function->first_parameter;
             parameter != NULL; parameter = parameter->next) {
            const LunaSemaTypeId parameter_type =
                luna_sema_resolve_type(context, &parameter->type);
            if (!luna_sema_validate_exported_type(context, parameter_type,
                                                  parameter->type.span,
                                                  function->name)) {
                success = false;
            }
        }
    }
    return success;
}

static bool
luna_sema_validate_implementation_declarations(LunaSemaContext *context) {
    bool success = luna_sema_validate_unique_functions(
        context, context->implementation_unit);
    for (const LunaTypeDeclaration *declaration =
             context->implementation_unit->first_type_declaration;
         declaration != NULL; declaration = declaration->next) {
        if (!declaration->is_exported) {
            continue;
        }
        luna_sema_fail(
            context, declaration->span,
            "'export' is only allowed on declarations in a module interface");
        success = false;
    }

    for (const LunaFunction *function =
             context->implementation_unit->first_function;
         function != NULL; function = function->next) {
        if (function->is_exported) {
            luna_sema_fail(
                context, function->span,
                "'export' is only allowed on declarations in a module "
                "interface");
            success = false;
        }
        if (function->is_external && !function->is_declaration) {
            luna_sema_fail(context, function->span,
                           "external function '%.*s' must be a declaration",
                           (int)function->name.length, function->name.data);
            success = false;
        } else if (!function->is_external && function->is_declaration) {
            luna_sema_fail(context, function->span,
                           "implementation function '%.*s' must have a body",
                           (int)function->name.length, function->name.data);
            success = false;
        }
        if (!luna_sema_validate_function_signature(context, function)) {
            success = false;
        }
    }
    return success;
}

static void luna_sema_report_function_type_mismatch(
    LunaSemaContext *context, const LunaFunction *declaration,
    const LunaFunction *definition, const char *component, uint32_t index,
    LunaSourceSpan span, LunaSemaTypeId expected, LunaSemaTypeId actual) {
    LunaStringBuilder expected_name;
    LunaStringBuilder actual_name;
    luna_string_builder_init(&expected_name);
    luna_string_builder_init(&actual_name);
    const bool formatted =
        luna_sema_append_type_name(context, expected, &expected_name) &&
        luna_sema_append_type_name(context, actual, &actual_name);
    if (!formatted) {
        luna_sema_report_allocation_failure(context);
    } else if (index == 0U) {
        luna_sema_fail(
            context, span,
            "%s of implementation function '%.*s' does not match the "
            "interface declaration: expected %s, found %s",
            component, (int)definition->name.length, definition->name.data,
            luna_string_builder_data(&expected_name),
            luna_string_builder_data(&actual_name));
    } else {
        luna_sema_fail(
            context, span,
            "%s %" PRIu32 " of implementation function '%.*s' does not match "
            "the interface declaration: expected %s, found %s",
            component, index, (int)definition->name.length,
            definition->name.data, luna_string_builder_data(&expected_name),
            luna_string_builder_data(&actual_name));
    }
    if (formatted) {
        luna_diagnostic_note(context->diagnostics, declaration->span,
                             "the interface declaration is here");
    }
    luna_string_builder_destroy(&actual_name);
    luna_string_builder_destroy(&expected_name);
}

static bool
luna_sema_function_signatures_match(LunaSemaContext *context,
                                    const LunaFunction *declaration,
                                    const LunaFunction *definition) {
    if (declaration->parameter_count != definition->parameter_count) {
        luna_sema_fail(
            context, definition->span,
            "implementation function '%.*s' has %" PRIu32
            " parameters, but its interface declaration has %" PRIu32,
            (int)definition->name.length, definition->name.data,
            definition->parameter_count, declaration->parameter_count);
        luna_diagnostic_note(context->diagnostics, declaration->span,
                             "the interface declaration is here");
        return false;
    }

    bool success = true;
    const LunaSemaTypeId expected_return =
        luna_sema_resolve_type(context, &declaration->return_type);
    const LunaSemaTypeId actual_return =
        luna_sema_resolve_type(context, &definition->return_type);
    if (expected_return != actual_return) {
        luna_sema_report_function_type_mismatch(
            context, declaration, definition, "return type", 0U,
            definition->return_type.span, expected_return, actual_return);
        success = false;
    }

    const LunaParameter *expected_parameter = declaration->first_parameter;
    const LunaParameter *actual_parameter = definition->first_parameter;
    uint32_t parameter_index = 1U;
    while (expected_parameter != NULL && actual_parameter != NULL) {
        const LunaSemaTypeId expected_type =
            luna_sema_resolve_type(context, &expected_parameter->type);
        const LunaSemaTypeId actual_type =
            luna_sema_resolve_type(context, &actual_parameter->type);
        if (expected_type != actual_type) {
            luna_sema_report_function_type_mismatch(
                context, declaration, definition, "parameter", parameter_index,
                actual_parameter->type.span, expected_type, actual_type);
            success = false;
        }
        expected_parameter = expected_parameter->next;
        actual_parameter = actual_parameter->next;
        parameter_index += 1U;
    }
    return success;
}

static LunaSemaFunction *
luna_sema_find_external_symbol(LunaSemaContext *context, LunaStringView name) {
    for (size_t index = 0U; index < context->functions.length; index += 1U) {
        LunaSemaFunction *function = luna_vector_at(&context->functions, index);
        if (function->syntax->is_external &&
            luna_string_view_equal(function->syntax->name, name)) {
            return function;
        }
    }
    return NULL;
}

static bool luna_sema_external_signatures_match(const LunaSemaFunction *left,
                                                const LunaSemaFunction *right) {
    if (left->return_type != right->return_type ||
        left->parameter_types.length != right->parameter_types.length) {
        return false;
    }
    for (size_t index = 0U; index < left->parameter_types.length; index += 1U) {
        const LunaSemaTypeId *left_type =
            luna_vector_at_const(&left->parameter_types, index);
        const LunaSemaTypeId *right_type =
            luna_vector_at_const(&right->parameter_types, index);
        if (*left_type != *right_type) {
            return false;
        }
    }
    return true;
}

static bool luna_sema_add_ir_function(LunaSemaContext *context,
                                      const LunaFunction *syntax,
                                      LunaStringView module_name,
                                      LunaIrFunctionLinkage linkage,
                                      bool is_exported) {
    LunaSemaFunction *existing = luna_sema_find_function(context, syntax->name);
    if (existing != NULL) {
        luna_sema_fail(context, syntax->span, "duplicate function '%.*s'",
                       (int)syntax->name.length, syntax->name.data);
        luna_diagnostic_note(context->diagnostics, existing->syntax->span,
                             "the first declaration of '%.*s' is here",
                             (int)syntax->name.length, syntax->name.data);
        return false;
    }

    const LunaSemaTypeId return_type =
        luna_sema_resolve_type(context, &syntax->return_type);
    LunaSemaFunction function = {
        .syntax = syntax,
        .module_name = module_name,
        .return_type = return_type,
        .ir_id = LUNA_IR_INVALID_ID,
        .is_exported = is_exported,
    };
    luna_vector_init(&function.parameter_types, sizeof(LunaSemaTypeId));

    for (const LunaParameter *parameter = syntax->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        const LunaSemaTypeId parameter_type =
            luna_sema_resolve_type(context, &parameter->type);
        if (!luna_vector_push(&function.parameter_types, &parameter_type)) {
            luna_sema_report_allocation_failure(context);
            luna_vector_destroy(&function.parameter_types);
            return false;
        }
    }

    if (linkage == LUNA_IR_LINKAGE_EXTERNAL_C) {
        LunaSemaFunction *external =
            luna_sema_find_external_symbol(context, syntax->name);
        if (external != NULL) {
            if (!luna_sema_external_signatures_match(external, &function)) {
                luna_sema_fail(
                    context, syntax->span,
                    "external symbol '%.*s' has a conflicting declaration",
                    (int)syntax->name.length, syntax->name.data);
                luna_diagnostic_note(
                    context->diagnostics, external->syntax->span,
                    "the first declaration of this external symbol is here");
                luna_vector_destroy(&function.parameter_types);
                return false;
            }
            function.ir_id = external->ir_id;
        }
    }

    LunaIrFunction *ir_function = NULL;
    if (function.ir_id == LUNA_IR_INVALID_ID) {
        const LunaIrFunctionId ir_id = luna_ir_module_add_function(
            context->module, module_name, syntax->name,
            luna_sema_ir_type(context, return_type), linkage);
        if (ir_id == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            luna_vector_destroy(&function.parameter_types);
            return false;
        }
        function.ir_id = ir_id;

        ir_function = luna_ir_module_function(context->module, ir_id);
        if (luna_sema_is_memory_type(context, return_type)) {
            luna_ir_aggregate_layout_destroy(&ir_function->return_aggregate);
            if (!luna_sema_build_aggregate_layout(
                    context, return_type, &ir_function->return_aggregate)) {
                luna_sema_report_allocation_failure(context);
                luna_vector_destroy(&function.parameter_types);
                return false;
            }
        }
        if (context->current_interface_is_metadata &&
            (linkage == LUNA_IR_LINKAGE_MODULE_EXPORT ||
             linkage == LUNA_IR_LINKAGE_MODULE_IMPORT)) {
            ir_function->has_module_metadata_hash = true;
            ir_function->module_metadata_hash =
                context->current_metadata_content_hash;
        }
    }

    if (ir_function != NULL) {
        for (size_t index = 0U; index < function.parameter_types.length;
             index += 1U) {
            const LunaSemaTypeId *parameter_type =
                luna_vector_at_const(&function.parameter_types, index);
            const LunaIrType type = luna_sema_ir_type(context, *parameter_type);
            LunaIrAggregateLayout aggregate;
            if (luna_sema_is_memory_type(context, *parameter_type)) {
                if (!luna_sema_build_aggregate_layout(context, *parameter_type,
                                                      &aggregate)) {
                    luna_sema_report_allocation_failure(context);
                    luna_vector_destroy(&function.parameter_types);
                    return false;
                }
            } else {
                luna_ir_aggregate_layout_init(&aggregate, false, 0U, 0U);
            }
            if (!luna_vector_push(&ir_function->parameter_types, &type)) {
                luna_ir_aggregate_layout_destroy(&aggregate);
                luna_sema_report_allocation_failure(context);
                luna_vector_destroy(&function.parameter_types);
                return false;
            }
            if (!luna_vector_push(&ir_function->parameter_aggregates,
                                  &aggregate)) {
                luna_ir_aggregate_layout_destroy(&aggregate);
                luna_sema_report_allocation_failure(context);
                luna_vector_destroy(&function.parameter_types);
                return false;
            }
            if ((linkage == LUNA_IR_LINKAGE_INTERNAL ||
                 linkage == LUNA_IR_LINKAGE_MODULE_EXPORT) &&
                (luna_sema_is_memory_type(context, *parameter_type)
                     ? luna_ir_function_add_memory_slot(
                           ir_function, aggregate.size_bytes,
                           aggregate.alignment_bytes)
                     : luna_ir_function_add_slot(ir_function, type)) ==
                    LUNA_IR_INVALID_ID) {
                luna_sema_report_allocation_failure(context);
                luna_vector_destroy(&function.parameter_types);
                return false;
            }
        }
    }

    if (!luna_vector_push(&context->functions, &function)) {
        luna_sema_report_allocation_failure(context);
        luna_vector_destroy(&function.parameter_types);
        return false;
    }
    const uint32_t function_index = (uint32_t)(context->functions.length - 1U);
    if (!luna_vector_push(&context->visible_functions, &function_index)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    return true;
}

static bool luna_sema_collect_functions(LunaSemaContext *context) {
    bool success = true;
    if (context->interface_unit != NULL) {
        for (const LunaFunction *declaration =
                 context->interface_unit->first_function;
             declaration != NULL; declaration = declaration->next) {
            const LunaFunction *definition = luna_sema_find_syntax_function(
                context->implementation_unit, declaration->name);
            if (declaration->is_external) {
                if (definition != NULL) {
                    luna_sema_fail(
                        context, definition->span,
                        "function '%.*s' is external in the interface and "
                        "cannot have an implementation declaration",
                        (int)definition->name.length, definition->name.data);
                    luna_diagnostic_note(context->diagnostics,
                                         declaration->span,
                                         "the external interface declaration "
                                         "is here");
                    success = false;
                }
                continue;
            }

            if (definition == NULL) {
                luna_sema_fail(
                    context, declaration->span,
                    "interface function '%.*s' has no implementation "
                    "definition",
                    (int)declaration->name.length, declaration->name.data);
                success = false;
                continue;
            }
            if (definition->is_external || definition->is_declaration) {
                luna_sema_fail(
                    context, definition->span,
                    "implementation of interface function '%.*s' must be a "
                    "Luna definition with a body",
                    (int)definition->name.length, definition->name.data);
                luna_diagnostic_note(context->diagnostics, declaration->span,
                                     "the interface declaration is here");
                success = false;
                continue;
            }
            if (!luna_sema_function_signatures_match(context, declaration,
                                                     definition)) {
                success = false;
            }
        }
    }

    if (!success) {
        return false;
    }

    if (context->interface_unit != NULL) {
        for (const LunaFunction *declaration =
                 context->interface_unit->first_function;
             declaration != NULL; declaration = declaration->next) {
            if (declaration->is_external &&
                !luna_sema_add_ir_function(
                    context, declaration, context->interface_unit->module_name,
                    LUNA_IR_LINKAGE_EXTERNAL_C, declaration->is_exported)) {
                return false;
            }
        }
    }

    for (const LunaFunction *definition =
             context->implementation_unit->first_function;
         definition != NULL; definition = definition->next) {
        bool is_exported = false;
        if (context->interface_unit != NULL) {
            const LunaFunction *declaration = luna_sema_find_syntax_function(
                context->interface_unit, definition->name);
            is_exported = declaration != NULL && declaration->is_exported &&
                          !declaration->is_external;
        }
        LunaIrFunctionLinkage linkage = LUNA_IR_LINKAGE_INTERNAL;
        if (definition->is_external) {
            linkage = LUNA_IR_LINKAGE_EXTERNAL_C;
        } else if (is_exported &&
                   !luna_string_view_equal_c_string(definition->name, "main")) {
            linkage = LUNA_IR_LINKAGE_MODULE_EXPORT;
        }
        if (!luna_sema_add_ir_function(
                context, definition, context->implementation_unit->module_name,
                linkage, is_exported)) {
            return false;
        }
    }
    return true;
}

static bool luna_sema_collect_precompiled_functions(LunaSemaContext *context) {
    for (const LunaFunction *declaration =
             context->interface_unit->first_function;
         declaration != NULL; declaration = declaration->next) {
        if (!declaration->is_exported) {
            continue;
        }
        const LunaIrFunctionLinkage linkage =
            declaration->is_external ? LUNA_IR_LINKAGE_EXTERNAL_C
                                     : LUNA_IR_LINKAGE_MODULE_IMPORT;
        if (!luna_sema_add_ir_function(context, declaration,
                                       context->interface_unit->module_name,
                                       linkage, true)) {
            return false;
        }
    }
    return true;
}

static bool luna_sema_find_entry(LunaSemaContext *context) {
    LunaSemaFunction *entry = luna_sema_find_function(
        context, luna_string_view_from_c_string("main"));
    if (entry == NULL) {
        luna_sema_fail_plain(context, "program has no main function");
        return false;
    }

    if (entry->syntax->is_external) {
        luna_sema_fail(context, entry->syntax->span,
                       "bootstrap entry point 'main' must be defined in Luna");
        return false;
    }

    bool has_command_line_parameters = false;
    if (entry->parameter_types.length == 2U) {
        const LunaSemaTypeId *argument_count_type =
            luna_vector_at_const(&entry->parameter_types, 0U);
        const LunaSemaTypeId *argument_vector_type =
            luna_vector_at_const(&entry->parameter_types, 1U);
        const LunaSemaType *outer_pointer =
            argument_vector_type == NULL
                ? NULL
                : luna_sema_type(context, *argument_vector_type);
        const LunaSemaType *inner_pointer =
            outer_pointer == NULL || outer_pointer->kind != LUNA_TYPE_POINTER
                ? NULL
                : luna_sema_type(context, outer_pointer->element_type);
        has_command_line_parameters =
            argument_count_type != NULL &&
            *argument_count_type == LUNA_TYPE_USIZE && outer_pointer != NULL &&
            !outer_pointer->is_read_only && inner_pointer != NULL &&
            inner_pointer->kind == LUNA_TYPE_POINTER &&
            inner_pointer->is_read_only &&
            inner_pointer->element_type == LUNA_TYPE_U8;
    }

    if ((entry->parameter_types.length != 0U && !has_command_line_parameters) ||
        entry->return_type != LUNA_TYPE_I32) {
        luna_sema_fail(context, entry->syntax->span,
                       "entry point must be 'fn main() -> i32' or "
                       "'fn main(argc: usize, argv: **const u8) -> i32'");
        return false;
    }

    context->module->entry_function = entry->ir_id;
    return true;
}

static void luna_sema_lower_function(LunaSemaContext *context,
                                     LunaSemaFunction *function) {
    context->current_function =
        luna_ir_module_function(context->module, function->ir_id);
    context->current_semantic_function = function;
    context->current_syntax_function = function->syntax;
    context->locals.length = 0U;
    context->control_frames.length = 0U;
    context->scope_depth = 0U;
    context->checking_dead_code = false;

    const LunaIrBlockId entry_block = luna_sema_add_block(context);
    if (entry_block == LUNA_IR_INVALID_ID) {
        return;
    }
    context->current_block = entry_block;
    context->reachable = true;

    uint32_t parameter_index = 0U;
    for (const LunaParameter *parameter = function->syntax->first_parameter;
         parameter != NULL; parameter = parameter->next) {
        if (luna_sema_find_local_in_current_scope(context, parameter->name) !=
            NULL) {
            luna_sema_fail(context, parameter->span,
                           "duplicate parameter '%.*s'",
                           (int)parameter->name.length, parameter->name.data);
            continue;
        }

        const LunaSemaTypeId *parameter_type = luna_vector_at_const(
            &function->parameter_types, (size_t)parameter_index);
        (void)luna_sema_add_local(context, parameter->name, *parameter_type,
                                  parameter_index, false);
        parameter_index += 1U;
    }

    luna_sema_lower_block(context, function->syntax->body, true);

    if (context->reachable) {
        const LunaSemaTypeId return_type = function->return_type;
        if (return_type == LUNA_TYPE_VOID) {
            LunaIrInstruction return_instruction =
                luna_sema_instruction(LUNA_IR_RETURN, function->syntax->span);
            (void)luna_sema_append_instruction(context, &return_instruction);
            context->reachable = false;
        } else {
            luna_sema_fail(
                context, function->syntax->span,
                "not every path in non-void function '%.*s' returns a value",
                (int)function->syntax->name.length,
                function->syntax->name.data);
        }
    }
}

static bool luna_sema_validate_module_units(const LunaSemaModule *input,
                                            LunaDiagnosticEngine *diagnostics) {
    const LunaProgram *interface_unit = input->interface_unit;
    const LunaProgram *implementation_unit = input->implementation_unit;
    if ((input->has_metadata_interface &&
         (interface_unit == NULL || !interface_unit->is_interface)) ||
        (!input->has_metadata_interface &&
         input->metadata_content_hash != UINT64_C(0))) {
        luna_diagnostic_error_plain(
            diagnostics, "semantic module metadata identity is malformed");
        return false;
    }
    if (input->is_precompiled) {
        if (interface_unit == NULL || implementation_unit != NULL ||
            !interface_unit->is_interface || input->is_compilation_root ||
            !input->has_metadata_interface) {
            luna_diagnostic_error_plain(
                diagnostics, "precompiled semantic module input is malformed");
            return false;
        }
        return true;
    }
    if (implementation_unit == NULL) {
        if (interface_unit != NULL) {
            luna_diagnostic_error(
                diagnostics, interface_unit->module_span,
                "module interface requires a matching implementation unit");
        } else {
            luna_diagnostic_error_plain(
                diagnostics, "semantic lowering requires an implementation "
                             "module unit");
        }
        return false;
    }
    if (implementation_unit->is_interface) {
        luna_diagnostic_error(
            diagnostics, implementation_unit->module_span,
            "implementation unit must start with 'module', not 'export "
            "module'");
    }
    if (interface_unit != NULL && !interface_unit->is_interface) {
        luna_diagnostic_error(diagnostics, interface_unit->module_span,
                              "interface unit must start with 'export module'");
    }
    if (interface_unit != NULL &&
        !luna_string_view_equal(interface_unit->module_name,
                                implementation_unit->module_name)) {
        luna_diagnostic_error(
            diagnostics, implementation_unit->module_span,
            "implementation module '%.*s' does not match interface module "
            "'%.*s'",
            (int)implementation_unit->module_name.length,
            implementation_unit->module_name.data,
            (int)interface_unit->module_name.length,
            interface_unit->module_name.data);
        luna_diagnostic_note(diagnostics, interface_unit->module_span,
                             "the interface module declaration is here");
    }
    return luna_diagnostic_error_count(diagnostics) == 0U;
}

static void luna_sema_context_init(LunaSemaContext *context,
                                   LunaDiagnosticEngine *diagnostics,
                                   LunaIrModule *module) {
    *context = (LunaSemaContext){
        .diagnostics = diagnostics,
        .module = module,
    };
    luna_vector_init(&context->functions, sizeof(LunaSemaFunction));
    luna_vector_init(&context->visible_functions, sizeof(uint32_t));
    luna_vector_init(&context->types, sizeof(LunaSemaType));
    luna_vector_init(&context->named_types, sizeof(LunaSemaNamedType));
    luna_vector_init(&context->visible_named_types, sizeof(uint32_t));
    luna_vector_init(&context->fields, sizeof(LunaSemaField));
    luna_vector_init(&context->enum_members, sizeof(LunaSemaEnumMember));
    luna_vector_init(&context->locals, sizeof(LunaSemaLocal));
    luna_vector_init(&context->control_frames, sizeof(LunaSemaControlFrame));
    if (!luna_sema_initialize_types(context)) {
        luna_sema_report_allocation_failure(context);
    }
}

static void luna_sema_context_destroy(LunaSemaContext *context) {
    for (size_t index = 0U; index < context->functions.length; index += 1U) {
        LunaSemaFunction *function = luna_vector_at(&context->functions, index);
        luna_vector_destroy(&function->parameter_types);
    }
    luna_vector_destroy(&context->functions);
    luna_vector_destroy(&context->visible_functions);
    luna_vector_destroy(&context->types);
    luna_vector_destroy(&context->named_types);
    luna_vector_destroy(&context->visible_named_types);
    luna_vector_destroy(&context->fields);
    luna_vector_destroy(&context->enum_members);
    luna_vector_destroy(&context->locals);
    luna_vector_destroy(&context->control_frames);
}

static bool luna_sema_lower_one_module(LunaSemaContext *context,
                                       const LunaSemaModule *input) {
    context->interface_unit = input->interface_unit;
    context->implementation_unit = input->implementation_unit;
    context->current_interface_is_metadata = input->has_metadata_interface;
    context->current_metadata_content_hash = input->metadata_content_hash;
    context->visible_functions.length = 0U;
    context->visible_named_types.length = 0U;
    context->locals.length = 0U;
    context->control_frames.length = 0U;

    if (!luna_sema_validate_module_units(input, context->diagnostics)) {
        return false;
    }
    if ((input->interface_import_count > 0U &&
         input->interface_imports == NULL) ||
        (input->implementation_import_count > 0U &&
         input->implementation_imports == NULL)) {
        luna_sema_fail_plain(
            context, "semantic module input has an invalid import table");
        return false;
    }

    (void)luna_sema_add_imported_scope(context, input->interface_imports,
                                       input->interface_import_count);

    if (input->interface_unit != NULL &&
        luna_diagnostic_error_count(context->diagnostics) == 0U) {
        const size_t first_interface_type = context->named_types.length;
        (void)luna_sema_collect_named_types(context, input->interface_unit);
        (void)luna_sema_resolve_type_declarations(context,
                                                  first_interface_type);
        (void)luna_sema_validate_exported_type_declarations(
            context, first_interface_type);
        (void)luna_sema_validate_interface_functions(context);
    }

    if (input->is_precompiled) {
        if (luna_diagnostic_error_count(context->diagnostics) == 0U) {
            (void)luna_sema_collect_precompiled_functions(context);
        }
        return luna_diagnostic_error_count(context->diagnostics) == 0U &&
               !context->allocation_failed;
    }

    if (luna_diagnostic_error_count(context->diagnostics) == 0U) {
        (void)luna_sema_add_imported_scope(context,
                                           input->implementation_imports,
                                           input->implementation_import_count);
    }

    if (luna_diagnostic_error_count(context->diagnostics) == 0U) {
        const size_t first_implementation_type = context->named_types.length;
        (void)luna_sema_collect_named_types(context,
                                            input->implementation_unit);
        (void)luna_sema_resolve_type_declarations(context,
                                                  first_implementation_type);
        (void)luna_sema_validate_implementation_declarations(context);
    }

    const size_t first_module_function = context->functions.length;
    if (luna_diagnostic_error_count(context->diagnostics) == 0U) {
        if (luna_sema_collect_functions(context) &&
            luna_diagnostic_error_count(context->diagnostics) == 0U &&
            input->is_executable_root) {
            (void)luna_sema_find_entry(context);
        }
    }

    if (luna_diagnostic_error_count(context->diagnostics) == 0U) {
        for (size_t index = first_module_function;
             index < context->functions.length; index += 1U) {
            LunaSemaFunction *function =
                luna_vector_at(&context->functions, index);
            if (!function->syntax->is_external) {
                luna_sema_lower_function(context, function);
            }
        }
    }
    return luna_diagnostic_error_count(context->diagnostics) == 0U &&
           !context->allocation_failed;
}

bool luna_sema_lower_modules(const LunaSemaModule *modules,
                             uint32_t module_count,
                             LunaDiagnosticEngine *diagnostics,
                             LunaIrModule *module) {
    if (module == NULL || module->target == NULL ||
        !luna_data_layout_is_valid(&module->target->data_layout)) {
        luna_diagnostic_error_plain(
            diagnostics, "semantic lowering requires a valid target layout");
        return false;
    }
    if (modules == NULL || module_count == 0U) {
        luna_diagnostic_error_plain(diagnostics,
                                    "semantic lowering requires modules");
        return false;
    }

    uint32_t compilation_root_count = 0U;
    uint32_t executable_root_count = 0U;
    for (uint32_t index = 0U; index < module_count; index += 1U) {
        if (modules[index].is_compilation_root) {
            compilation_root_count += 1U;
        }
        if (modules[index].is_executable_root) {
            executable_root_count += 1U;
            if (!modules[index].is_compilation_root) {
                luna_diagnostic_error_plain(
                    diagnostics,
                    "executable semantic root must be the compilation root");
                return false;
            }
        }
    }
    if (compilation_root_count != 1U || executable_root_count > 1U) {
        luna_diagnostic_error_plain(
            diagnostics,
            "semantic lowering requires exactly one compilation root module");
        return false;
    }
    module->kind = executable_root_count == 1U ? LUNA_IR_MODULE_EXECUTABLE
                                               : LUNA_IR_MODULE_LIBRARY;

    LunaSemaContext context;
    luna_sema_context_init(&context, diagnostics, module);
    for (uint32_t index = 0U;
         index < module_count && luna_diagnostic_error_count(diagnostics) == 0U;
         index += 1U) {
        (void)luna_sema_lower_one_module(&context, &modules[index]);
    }

    const bool success = luna_diagnostic_error_count(diagnostics) == 0U &&
                         !context.allocation_failed;
    luna_sema_context_destroy(&context);
    return success;
}

bool luna_sema_lower_module(const LunaProgram *interface_unit,
                            const LunaProgram *implementation_unit,
                            LunaDiagnosticEngine *diagnostics,
                            LunaIrModule *module) {
    const LunaImport *unresolved_import =
        interface_unit != NULL && interface_unit->first_import != NULL
            ? interface_unit->first_import
        : implementation_unit != NULL ? implementation_unit->first_import
                                      : NULL;
    if (unresolved_import != NULL) {
        luna_diagnostic_error(
            diagnostics, unresolved_import->span,
            "imports must be lowered through the module dependency graph");
        return false;
    }

    const LunaSemaModule input = {
        .interface_unit = interface_unit,
        .implementation_unit = implementation_unit,
        .is_compilation_root = true,
        .is_executable_root = true,
    };
    return luna_sema_lower_modules(&input, 1U, diagnostics, module);
}

bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module) {
    if (program == NULL) {
        luna_diagnostic_error_plain(diagnostics,
                                    "semantic lowering requires a source unit");
        return false;
    }
    return program->is_interface
               ? luna_sema_lower_module(program, NULL, diagnostics, module)
               : luna_sema_lower_module(NULL, program, diagnostics, module);
}
