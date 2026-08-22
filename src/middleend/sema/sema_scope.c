#include "sema_internal.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

LunaSemaFunction *luna_sema_find_function(LunaSemaContext *context,
                                          LunaStringView name) {
    for (size_t index = 0U; index < context->visible_functions.length;
         index += 1U) {
        const uint32_t *function_index =
            luna_vector_at_const(&context->visible_functions, index);
        LunaSemaFunction *function =
            luna_vector_at(&context->functions, (size_t)*function_index);
        if (luna_string_view_equal(function->syntax->name, name)) {
            return function;
        }
    }

    return NULL;
}

bool luna_sema_add_imported_scope(LunaSemaContext *context,
                                  const LunaSemaImport *imports,
                                  uint32_t import_count) {
    bool success = true;
    for (uint32_t import_index = 0U; import_index < import_count;
         import_index += 1U) {
        const LunaSemaImport *import = &imports[import_index];
        const LunaStringView module_name = import->interface_unit->module_name;

        for (size_t index = 0U; index < context->named_types.length;
             index += 1U) {
            const LunaSemaNamedType *candidate =
                luna_vector_at_const(&context->named_types, index);
            if (!candidate->is_exported ||
                !luna_string_view_equal(candidate->module_name, module_name)) {
                continue;
            }

            const LunaSemaNamedType *existing =
                luna_sema_find_named_type(context, candidate->syntax->name);
            if (existing != NULL &&
                !luna_string_view_equal(existing->module_name, module_name)) {
                luna_sema_fail(
                    context, import->span,
                    "import of module '%.*s' makes type '%.*s' ambiguous",
                    (int)module_name.length, module_name.data,
                    (int)candidate->syntax->name.length,
                    candidate->syntax->name.data);
                luna_diagnostic_note(
                    context->diagnostics, existing->syntax->span,
                    "the first visible type named '%.*s' is declared here",
                    (int)candidate->syntax->name.length,
                    candidate->syntax->name.data);
                luna_diagnostic_note(
                    context->diagnostics, candidate->syntax->span,
                    "the conflicting exported type is declared here");
                success = false;
                continue;
            }
            if (existing != NULL) {
                continue;
            }

            const uint32_t named_type_index = (uint32_t)index;
            if (!luna_vector_push(&context->visible_named_types,
                                  &named_type_index)) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
        }

        for (size_t index = 0U; index < context->functions.length;
             index += 1U) {
            LunaSemaFunction *candidate =
                luna_vector_at(&context->functions, index);
            if (!candidate->is_exported ||
                !luna_string_view_equal(candidate->module_name, module_name)) {
                continue;
            }

            LunaSemaFunction *existing =
                luna_sema_find_function(context, candidate->syntax->name);
            if (existing != NULL &&
                !luna_string_view_equal(existing->module_name, module_name)) {
                luna_sema_fail(
                    context, import->span,
                    "import of module '%.*s' makes function '%.*s' ambiguous",
                    (int)module_name.length, module_name.data,
                    (int)candidate->syntax->name.length,
                    candidate->syntax->name.data);
                luna_diagnostic_note(
                    context->diagnostics, existing->syntax->span,
                    "the first visible function named '%.*s' is declared here",
                    (int)candidate->syntax->name.length,
                    candidate->syntax->name.data);
                luna_diagnostic_note(
                    context->diagnostics, candidate->syntax->span,
                    "the conflicting exported function is declared here");
                success = false;
                continue;
            }
            if (existing != NULL) {
                continue;
            }

            const uint32_t function_index = (uint32_t)index;
            if (!luna_vector_push(&context->visible_functions,
                                  &function_index)) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
        }
    }
    return success;
}

LunaSemaLocal *luna_sema_find_local(LunaSemaContext *context,
                                    LunaStringView name) {
    for (size_t index = context->locals.length; index > 0U; index -= 1U) {
        LunaSemaLocal *local = luna_vector_at(&context->locals, index - 1U);
        if (luna_string_view_equal(local->name, name)) {
            return local;
        }
    }

    return NULL;
}

LunaSemaLocal *luna_sema_find_local_in_current_scope(LunaSemaContext *context,
                                                     LunaStringView name) {
    for (size_t index = context->locals.length; index > 0U; index -= 1U) {
        LunaSemaLocal *local = luna_vector_at(&context->locals, index - 1U);
        if (local->scope_depth < context->scope_depth) {
            break;
        }
        if (luna_string_view_equal(local->name, name)) {
            return local;
        }
    }

    return NULL;
}

bool luna_sema_add_local(LunaSemaContext *context, LunaStringView name,
                         LunaSemaTypeId type, LunaIrSlotId slot,
                         bool is_mutable) {
    const LunaSemaLocal local = {
        .name = name,
        .type = type,
        .slot = slot,
        .scope_depth = context->scope_depth,
        .is_mutable = is_mutable,
    };

    if (!luna_vector_push(&context->locals, &local)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    return true;
}

void luna_sema_enter_scope(LunaSemaContext *context) {
    context->scope_depth += 1U;
}

void luna_sema_leave_scope(LunaSemaContext *context) {
    while (context->locals.length > 0U) {
        const LunaSemaLocal *local =
            luna_vector_at_const(&context->locals, context->locals.length - 1U);
        if (local->scope_depth < context->scope_depth) {
            break;
        }
        context->locals.length -= 1U;
    }

    context->scope_depth -= 1U;
}
