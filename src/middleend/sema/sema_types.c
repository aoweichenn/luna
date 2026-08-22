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

const LunaSemaType *luna_sema_type(const LunaSemaContext *context,
                                   LunaSemaTypeId type) {
    if (type < 0) {
        return NULL;
    }
    return luna_vector_at_const(&context->types, (size_t)type);
}

static LunaSemaType *luna_sema_type_mutable(LunaSemaContext *context,
                                            LunaSemaTypeId type) {
    if (type < 0) {
        return NULL;
    }
    return luna_vector_at(&context->types, (size_t)type);
}

bool luna_sema_initialize_types(LunaSemaContext *context) {
    for (int kind = (int)LUNA_TYPE_INVALID; kind <= (int)LUNA_TYPE_ARRAY;
         kind += 1) {
        const LunaSemaType type = {
            .kind = (LunaTypeKind)kind,
            .element_type = LUNA_TYPE_INVALID,
        };
        if (!luna_vector_push(&context->types, &type)) {
            return false;
        }
    }
    return true;
}

static LunaSemaTypeId luna_sema_intern_type(LunaSemaContext *context,
                                            LunaTypeKind kind,
                                            LunaSemaTypeId element_type,
                                            uint64_t array_count,
                                            bool is_read_only) {
    for (size_t index = (size_t)LUNA_TYPE_ARRAY + 1U;
         index < context->types.length; index += 1U) {
        const LunaSemaType *existing =
            luna_vector_at_const(&context->types, index);
        if (existing->kind == kind && existing->element_type == element_type &&
            existing->array_count == array_count &&
            existing->is_read_only == is_read_only) {
            return (LunaSemaTypeId)index;
        }
    }

    if (context->types.length > (size_t)INT_MAX) {
        return LUNA_TYPE_INVALID;
    }
    const LunaSemaType type = {
        .kind = kind,
        .element_type = element_type,
        .array_count = array_count,
        .is_read_only = is_read_only,
    };
    if (!luna_vector_push(&context->types, &type)) {
        return LUNA_TYPE_INVALID;
    }
    return (LunaSemaTypeId)(context->types.length - 1U);
}

const LunaSemaNamedType *
luna_sema_find_named_type(const LunaSemaContext *context, LunaStringView name) {
    for (size_t index = 0U; index < context->visible_named_types.length;
         index += 1U) {
        const uint32_t *named_type_index =
            luna_vector_at_const(&context->visible_named_types, index);
        const LunaSemaNamedType *named_type = luna_vector_at_const(
            &context->named_types, (size_t)*named_type_index);
        if (luna_string_view_equal(named_type->syntax->name, name)) {
            return named_type;
        }
    }
    return NULL;
}

static LunaSemaField *luna_sema_find_field_mutable(LunaSemaContext *context,
                                                   LunaSemaTypeId owner_type,
                                                   LunaStringView name) {
    for (size_t index = 0U; index < context->fields.length; index += 1U) {
        LunaSemaField *field = luna_vector_at(&context->fields, index);
        if (field->owner_type == owner_type &&
            luna_string_view_equal(field->syntax->name, name)) {
            return field;
        }
    }
    return NULL;
}

const LunaSemaField *luna_sema_find_field(const LunaSemaContext *context,
                                          LunaSemaTypeId owner_type,
                                          LunaStringView name) {
    for (size_t index = 0U; index < context->fields.length; index += 1U) {
        const LunaSemaField *field =
            luna_vector_at_const(&context->fields, index);
        if (field->owner_type == owner_type &&
            luna_string_view_equal(field->syntax->name, name)) {
            return field;
        }
    }
    return NULL;
}

const LunaSemaEnumMember *
luna_sema_find_enum_member(const LunaSemaContext *context,
                           LunaSemaTypeId owner_type, LunaStringView name) {
    for (size_t index = 0U; index < context->enum_members.length; index += 1U) {
        const LunaSemaEnumMember *member =
            luna_vector_at_const(&context->enum_members, index);
        if (member->owner_type == owner_type &&
            luna_string_view_equal(member->syntax->name, name)) {
            return member;
        }
    }
    return NULL;
}

LunaTypeKind luna_sema_type_kind(const LunaSemaContext *context,
                                 LunaSemaTypeId type) {
    const LunaSemaType *resolved = luna_sema_type(context, type);
    return resolved == NULL ? LUNA_TYPE_INVALID : resolved->kind;
}

bool luna_sema_is_pointer_type(const LunaSemaContext *context,
                               LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_POINTER;
}

bool luna_sema_is_array_type(const LunaSemaContext *context,
                             LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_ARRAY;
}

bool luna_sema_is_record_type(const LunaSemaContext *context,
                              LunaSemaTypeId type) {
    const LunaTypeKind kind = luna_sema_type_kind(context, type);
    return kind == LUNA_TYPE_STRUCT || kind == LUNA_TYPE_UNION;
}

bool luna_sema_is_memory_type(const LunaSemaContext *context,
                              LunaSemaTypeId type) {
    return luna_sema_is_array_type(context, type) ||
           luna_sema_is_record_type(context, type);
}

bool luna_sema_is_enum_type(const LunaSemaContext *context,
                            LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_ENUM;
}

LunaIrType luna_sema_ir_type(const LunaSemaContext *context,
                             LunaSemaTypeId type) {
    switch (luna_sema_type_kind(context, type)) {
    case LUNA_TYPE_VOID:
        return LUNA_IR_TYPE_VOID;
    case LUNA_TYPE_BOOL:
        return LUNA_IR_TYPE_BOOL;
    case LUNA_TYPE_I8:
        return LUNA_IR_TYPE_I8;
    case LUNA_TYPE_I16:
        return LUNA_IR_TYPE_I16;
    case LUNA_TYPE_I32:
        return LUNA_IR_TYPE_I32;
    case LUNA_TYPE_I64:
        return LUNA_IR_TYPE_I64;
    case LUNA_TYPE_ISIZE:
        return LUNA_IR_TYPE_ISIZE;
    case LUNA_TYPE_U8:
        return LUNA_IR_TYPE_U8;
    case LUNA_TYPE_U16:
        return LUNA_IR_TYPE_U16;
    case LUNA_TYPE_U32:
        return LUNA_IR_TYPE_U32;
    case LUNA_TYPE_U64:
        return LUNA_IR_TYPE_U64;
    case LUNA_TYPE_USIZE:
        return LUNA_IR_TYPE_USIZE;
    case LUNA_TYPE_F32:
        return LUNA_IR_TYPE_F32;
    case LUNA_TYPE_F64:
        return LUNA_IR_TYPE_F64;
    case LUNA_TYPE_POINTER:
        return LUNA_IR_TYPE_POINTER;
    case LUNA_TYPE_ENUM: {
        const LunaSemaType *resolved = luna_sema_type(context, type);
        return resolved == NULL
                   ? LUNA_IR_TYPE_VOID
                   : luna_sema_ir_type(context, resolved->element_type);
    }
    case LUNA_TYPE_ARRAY:
    case LUNA_TYPE_STRUCT:
    case LUNA_TYPE_UNION:
        return LUNA_IR_TYPE_POINTER;
    case LUNA_TYPE_INVALID:
    case LUNA_TYPE_NAMED:
        break;
    }

    return LUNA_IR_TYPE_VOID;
}

bool luna_sema_append_type_name(const LunaSemaContext *context,
                                LunaSemaTypeId type,
                                LunaStringBuilder *output) {
    const LunaSemaType *resolved = luna_sema_type(context, type);
    if (resolved == NULL) {
        return luna_string_builder_append_c_string(output, "<invalid>");
    }

    if (resolved->kind == LUNA_TYPE_POINTER) {
        return luna_string_builder_append_c_string(
                   output, resolved->is_read_only ? "*const " : "*") &&
               luna_sema_append_type_name(context, resolved->element_type,
                                          output);
    }
    if (resolved->kind == LUNA_TYPE_ARRAY) {
        return luna_string_builder_append_format(output, "[%" PRIu64 "]",
                                                 resolved->array_count) &&
               luna_sema_append_type_name(context, resolved->element_type,
                                          output);
    }
    if ((resolved->kind == LUNA_TYPE_STRUCT ||
         resolved->kind == LUNA_TYPE_UNION ||
         resolved->kind == LUNA_TYPE_ENUM) &&
        resolved->declaration != NULL) {
        return luna_string_builder_append_view(output,
                                               resolved->declaration->name);
    }
    return luna_string_builder_append_c_string(
        output, luna_type_kind_name(resolved->kind));
}

static bool luna_sema_align_up(uint64_t value, uint32_t alignment,
                               uint64_t *result) {
    if (alignment == 0U || result == NULL) {
        return false;
    }
    const uint64_t remainder = value % (uint64_t)alignment;
    const uint64_t padding =
        remainder == 0U ? 0U : (uint64_t)alignment - remainder;
    if (value > UINT64_MAX - padding) {
        return false;
    }
    *result = value + padding;
    return true;
}

bool luna_sema_type_layout(LunaSemaContext *context, LunaSemaTypeId type,
                           uint64_t *size_bytes, uint32_t *alignment_bytes) {
    LunaSemaType *resolved = luna_sema_type_mutable(context, type);
    if (resolved == NULL || size_bytes == NULL || alignment_bytes == NULL) {
        return false;
    }

    const LunaDataLayout *layout = &context->module->target->data_layout;
    const LunaScalarLayout *scalar = NULL;
    switch (resolved->kind) {
    case LUNA_TYPE_BOOL:
        scalar = &layout->boolean;
        break;
    case LUNA_TYPE_I8:
    case LUNA_TYPE_U8:
        scalar = &layout->integer8;
        break;
    case LUNA_TYPE_I16:
    case LUNA_TYPE_U16:
        scalar = &layout->integer16;
        break;
    case LUNA_TYPE_I32:
    case LUNA_TYPE_U32:
        scalar = &layout->integer32;
        break;
    case LUNA_TYPE_I64:
    case LUNA_TYPE_U64:
    case LUNA_TYPE_ISIZE:
    case LUNA_TYPE_USIZE:
        scalar = resolved->kind == LUNA_TYPE_ISIZE ||
                         resolved->kind == LUNA_TYPE_USIZE
                     ? &layout->pointer
                     : &layout->integer64;
        break;
    case LUNA_TYPE_F32:
        scalar = &layout->float32;
        break;
    case LUNA_TYPE_F64:
        scalar = &layout->float64;
        break;
    case LUNA_TYPE_POINTER:
        scalar = &layout->pointer;
        break;
    case LUNA_TYPE_ENUM:
        return luna_sema_type_layout(context, resolved->element_type,
                                     size_bytes, alignment_bytes);
    case LUNA_TYPE_ARRAY: {
        uint64_t element_size = 0U;
        uint32_t element_alignment = 0U;
        if (!luna_sema_type_layout(context, resolved->element_type,
                                   &element_size, &element_alignment) ||
            resolved->array_count == 0U ||
            element_size > UINT64_MAX / resolved->array_count ||
            element_size * resolved->array_count > (uint64_t)INT32_MAX) {
            return false;
        }
        *size_bytes = element_size * resolved->array_count;
        *alignment_bytes = element_alignment;
        return true;
    }
    case LUNA_TYPE_STRUCT:
    case LUNA_TYPE_UNION: {
        if (resolved->layout_state == LUNA_SEMA_LAYOUT_RESOLVED) {
            *size_bytes = resolved->size_bytes;
            *alignment_bytes = resolved->alignment_bytes;
            return true;
        }
        if (resolved->layout_state == LUNA_SEMA_LAYOUT_INVALID) {
            return false;
        }
        if (resolved->layout_state == LUNA_SEMA_LAYOUT_RESOLVING) {
            if (resolved->declaration != NULL) {
                luna_sema_fail(context, resolved->declaration->span,
                               "aggregate type '%.*s' contains itself by value",
                               (int)resolved->declaration->name.length,
                               resolved->declaration->name.data);
            }
            resolved->layout_state = LUNA_SEMA_LAYOUT_INVALID;
            return false;
        }

        resolved->layout_state = LUNA_SEMA_LAYOUT_RESOLVING;
        const LunaTypeKind aggregate_kind = resolved->kind;
        uint64_t aggregate_size = 0U;
        uint32_t aggregate_alignment = 1U;
        for (size_t index = 0U; index < context->fields.length; index += 1U) {
            LunaSemaField *field = luna_vector_at(&context->fields, index);
            if (field->owner_type != type) {
                continue;
            }

            uint64_t field_size = 0U;
            uint32_t field_alignment = 0U;
            const size_t errors_before =
                luna_diagnostic_error_count(context->diagnostics);
            if (!luna_sema_type_layout(context, field->type, &field_size,
                                       &field_alignment)) {
                if (luna_diagnostic_error_count(context->diagnostics) ==
                    errors_before) {
                    luna_sema_fail(context, field->syntax->type.span,
                                   "field '%.*s' has no valid target layout",
                                   (int)field->syntax->name.length,
                                   field->syntax->name.data);
                }
                LunaSemaType *aggregate = luna_sema_type_mutable(context, type);
                if (aggregate != NULL) {
                    aggregate->layout_state = LUNA_SEMA_LAYOUT_INVALID;
                }
                return false;
            }
            if (field_alignment > aggregate_alignment) {
                aggregate_alignment = field_alignment;
            }

            if (aggregate_kind == LUNA_TYPE_UNION) {
                field->offset = 0U;
                if (field_size > aggregate_size) {
                    aggregate_size = field_size;
                }
                continue;
            }

            uint64_t field_offset = 0U;
            if (!luna_sema_align_up(aggregate_size, field_alignment,
                                    &field_offset) ||
                field_offset > UINT64_MAX - field_size) {
                aggregate_size = UINT64_MAX;
                break;
            }
            field->offset = field_offset;
            aggregate_size = field_offset + field_size;
        }

        uint64_t final_size = 0U;
        if (aggregate_size > (uint64_t)INT32_MAX ||
            !luna_sema_align_up(aggregate_size, aggregate_alignment,
                                &final_size) ||
            final_size > (uint64_t)INT32_MAX) {
            const LunaTypeDeclaration *declaration =
                luna_sema_type(context, type)->declaration;
            luna_sema_fail(
                context, declaration->span,
                "aggregate type '%.*s' exceeds the supported object size",
                (int)declaration->name.length, declaration->name.data);
            LunaSemaType *aggregate = luna_sema_type_mutable(context, type);
            aggregate->layout_state = LUNA_SEMA_LAYOUT_INVALID;
            return false;
        }

        LunaSemaType *aggregate = luna_sema_type_mutable(context, type);
        aggregate->size_bytes = final_size;
        aggregate->alignment_bytes = aggregate_alignment;
        aggregate->layout_state = LUNA_SEMA_LAYOUT_RESOLVED;
        *size_bytes = final_size;
        *alignment_bytes = aggregate_alignment;
        return true;
    }
    case LUNA_TYPE_INVALID:
    case LUNA_TYPE_VOID:
    case LUNA_TYPE_NAMED:
        return false;
    }

    if (scalar == NULL || scalar->size_bits % 8U != 0U ||
        scalar->abi_alignment_bits % 8U != 0U) {
        return false;
    }
    *size_bytes = scalar->size_bits / 8U;
    *alignment_bytes = scalar->abi_alignment_bits / 8U;
    return *size_bytes != 0U && *alignment_bytes != 0U;
}

static bool
luna_sema_append_aggregate_components(LunaSemaContext *context,
                                      LunaSemaTypeId type, uint64_t base_offset,
                                      LunaIrAggregateLayout *layout) {
    const LunaSemaType *resolved = luna_sema_type(context, type);
    if (resolved == NULL) {
        return false;
    }
    if (resolved->kind == LUNA_TYPE_ENUM) {
        return luna_sema_append_aggregate_components(
            context, resolved->element_type, base_offset, layout);
    }
    if (resolved->kind == LUNA_TYPE_ARRAY) {
        uint64_t element_size = 0U;
        uint32_t element_alignment = 0U;
        if (!luna_sema_type_layout(context, resolved->element_type,
                                   &element_size, &element_alignment)) {
            return false;
        }
        for (uint64_t index = 0U; index < resolved->array_count; index += 1U) {
            if (index > (UINT64_MAX - base_offset) / element_size ||
                !luna_sema_append_aggregate_components(
                    context, resolved->element_type,
                    base_offset + index * element_size, layout)) {
                return false;
            }
        }
        return true;
    }
    if (resolved->kind == LUNA_TYPE_STRUCT ||
        resolved->kind == LUNA_TYPE_UNION) {
        for (size_t index = 0U; index < context->fields.length; index += 1U) {
            const LunaSemaField *field =
                luna_vector_at_const(&context->fields, index);
            if (field == NULL || field->owner_type != type) {
                continue;
            }
            if (field->offset > UINT64_MAX - base_offset ||
                !luna_sema_append_aggregate_components(
                    context, field->type, base_offset + field->offset,
                    layout)) {
                return false;
            }
        }
        return true;
    }

    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    return luna_sema_type_layout(context, type, &size_bytes,
                                 &alignment_bytes) &&
           luna_ir_aggregate_layout_add_component(
               layout, base_offset, size_bytes, alignment_bytes,
               luna_sema_ir_type(context, type));
}

bool luna_sema_build_aggregate_layout(LunaSemaContext *context,
                                      LunaSemaTypeId type,
                                      LunaIrAggregateLayout *layout) {
    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    if (layout == NULL ||
        !luna_sema_type_layout(context, type, &size_bytes, &alignment_bytes) ||
        size_bytes == 0U) {
        return false;
    }

    luna_ir_aggregate_layout_init(layout, true, size_bytes, alignment_bytes);
    if (size_bytes <= 16U &&
        !luna_sema_append_aggregate_components(context, type, 0U, layout)) {
        luna_ir_aggregate_layout_destroy(layout);
        return false;
    }
    return true;
}

LunaSemaTypeId luna_sema_resolve_type(LunaSemaContext *context,
                                      const LunaTypeRef *type_ref) {
    if (type_ref == NULL) {
        return LUNA_TYPE_INVALID;
    }
    if (type_ref->kind >= LUNA_TYPE_VOID && type_ref->kind <= LUNA_TYPE_F64) {
        return (LunaSemaTypeId)type_ref->kind;
    }

    if (type_ref->kind == LUNA_TYPE_NAMED) {
        const LunaSemaNamedType *named_type =
            luna_sema_find_named_type(context, type_ref->as.name);
        if (named_type == NULL) {
            luna_sema_fail(context, type_ref->span, "unknown type '%.*s'",
                           (int)type_ref->as.name.length,
                           type_ref->as.name.data);
            return LUNA_TYPE_INVALID;
        }
        return named_type->type;
    }

    if (type_ref->kind == LUNA_TYPE_POINTER) {
        const LunaSemaTypeId pointee =
            luna_sema_resolve_type(context, type_ref->as.pointer.pointee);
        if (pointee == LUNA_TYPE_INVALID) {
            return LUNA_TYPE_INVALID;
        }
        const LunaSemaTypeId result =
            luna_sema_intern_type(context, LUNA_TYPE_POINTER, pointee, 0U,
                                  type_ref->as.pointer.is_read_only);
        if (result == LUNA_TYPE_INVALID) {
            luna_sema_report_allocation_failure(context);
        }
        return result;
    }

    if (type_ref->kind == LUNA_TYPE_ARRAY) {
        if (type_ref->as.array.count == 0U) {
            luna_sema_fail(context, type_ref->span,
                           "fixed-array length must be positive");
            return LUNA_TYPE_INVALID;
        }
        const LunaSemaTypeId element =
            luna_sema_resolve_type(context, type_ref->as.array.element);
        const LunaTypeKind element_kind = luna_sema_type_kind(context, element);
        if (element_kind == LUNA_TYPE_INVALID ||
            element_kind == LUNA_TYPE_VOID) {
            luna_sema_fail(context, type_ref->span,
                           "fixed-array element type cannot be void");
            return LUNA_TYPE_INVALID;
        }
        const LunaSemaTypeId result = luna_sema_intern_type(
            context, LUNA_TYPE_ARRAY, element, type_ref->as.array.count, false);
        if (result == LUNA_TYPE_INVALID) {
            luna_sema_report_allocation_failure(context);
            return LUNA_TYPE_INVALID;
        }

        return result;
    }

    return LUNA_TYPE_INVALID;
}

LunaSemaTypeId luna_sema_pointer_type(LunaSemaContext *context,
                                      LunaSemaTypeId pointee,
                                      bool is_read_only) {
    const LunaSemaTypeId result = luna_sema_intern_type(
        context, LUNA_TYPE_POINTER, pointee, 0U, is_read_only);
    if (result == LUNA_TYPE_INVALID) {
        luna_sema_report_allocation_failure(context);
    }
    return result;
}

static const LunaSemaNamedType *
luna_sema_private_named_type(const LunaSemaContext *context,
                             LunaSemaTypeId type) {
    const LunaSemaType *resolved = luna_sema_type(context, type);
    if (resolved == NULL) {
        return NULL;
    }
    if (resolved->kind == LUNA_TYPE_POINTER ||
        resolved->kind == LUNA_TYPE_ARRAY) {
        return luna_sema_private_named_type(context, resolved->element_type);
    }
    if (resolved->kind != LUNA_TYPE_STRUCT &&
        resolved->kind != LUNA_TYPE_UNION && resolved->kind != LUNA_TYPE_ENUM) {
        return NULL;
    }

    for (size_t index = 0U; index < context->named_types.length; index += 1U) {
        const LunaSemaNamedType *named_type =
            luna_vector_at_const(&context->named_types, index);
        if (named_type->type == type) {
            return named_type->is_exported ? NULL : named_type;
        }
    }
    return NULL;
}

bool luna_sema_validate_exported_type(LunaSemaContext *context,
                                      LunaSemaTypeId type, LunaSourceSpan span,
                                      LunaStringView exported_name) {
    const LunaSemaNamedType *private_type =
        luna_sema_private_named_type(context, type);
    if (private_type == NULL) {
        return true;
    }
    luna_sema_fail(
        context, span,
        "exported declaration '%.*s' exposes non-exported type '%.*s'",
        (int)exported_name.length, exported_name.data,
        (int)private_type->syntax->name.length,
        private_type->syntax->name.data);
    luna_diagnostic_note(context->diagnostics, private_type->syntax->span,
                         "the non-exported type is declared here");
    return false;
}

bool luna_sema_collect_named_types(LunaSemaContext *context,
                                   const LunaProgram *program) {
    for (const LunaTypeDeclaration *declaration =
             program->first_type_declaration;
         declaration != NULL; declaration = declaration->next) {
        const LunaSemaNamedType *existing =
            luna_sema_find_named_type(context, declaration->name);
        if (existing != NULL) {
            luna_sema_fail(
                context, declaration->span, "duplicate type declaration '%.*s'",
                (int)declaration->name.length, declaration->name.data);
            luna_diagnostic_note(context->diagnostics, existing->syntax->span,
                                 "the first declaration of '%.*s' is here",
                                 (int)declaration->name.length,
                                 declaration->name.data);
            continue;
        }
        if (context->types.length > (size_t)INT_MAX) {
            luna_sema_report_allocation_failure(context);
            return false;
        }

        const LunaSemaType type = {
            .kind = declaration->kind,
            .element_type = LUNA_TYPE_INVALID,
            .declaration = declaration,
        };
        if (!luna_vector_push(&context->types, &type)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
        const LunaSemaNamedType named_type = {
            .syntax = declaration,
            .module_name = program->module_name,
            .type = (LunaSemaTypeId)(context->types.length - 1U),
            .is_exported = program->is_interface && declaration->is_exported,
        };
        if (!luna_vector_push(&context->named_types, &named_type)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
        const uint32_t named_type_index =
            (uint32_t)(context->named_types.length - 1U);
        if (!luna_vector_push(&context->visible_named_types,
                              &named_type_index)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
    }
    return true;
}

bool luna_sema_validate_exported_type_declarations(LunaSemaContext *context,
                                                   size_t first_named_type) {
    bool success = true;
    for (size_t index = first_named_type; index < context->named_types.length;
         index += 1U) {
        const LunaSemaNamedType *named_type =
            luna_vector_at_const(&context->named_types, index);
        if (!named_type->is_exported) {
            continue;
        }
        for (size_t field_index = 0U; field_index < context->fields.length;
             field_index += 1U) {
            const LunaSemaField *field =
                luna_vector_at_const(&context->fields, field_index);
            if (field->owner_type == named_type->type &&
                !luna_sema_validate_exported_type(context, field->type,
                                                  field->syntax->type.span,
                                                  named_type->syntax->name)) {
                success = false;
            }
        }
    }
    return success;
}

static bool luna_sema_enum_literal_value(LunaSemaContext *context,
                                         const LunaExpression *expression,
                                         LunaSemaTypeId underlying_type,
                                         uint64_t *value) {
    if (expression == NULL || value == NULL) {
        return false;
    }

    bool is_negative = false;
    const LunaExpression *literal = expression;
    if (expression->kind == LUNA_EXPRESSION_UNARY &&
        (expression->as.unary.operator_kind == LUNA_TOKEN_PLUS ||
         expression->as.unary.operator_kind == LUNA_TOKEN_MINUS)) {
        is_negative = expression->as.unary.operator_kind == LUNA_TOKEN_MINUS;
        literal = expression->as.unary.operand;
    }
    if (literal == NULL || literal->kind != LUNA_EXPRESSION_INTEGER) {
        luna_sema_fail(
            context, expression->span,
            "enum member value must be an integer literal with an optional "
            "sign");
        return false;
    }

    const LunaTypeKind kind = luna_sema_type_kind(context, underlying_type);
    const uint32_t width =
        luna_type_kind_bit_width(kind, &context->module->target->data_layout);
    if (width == 0U || width > 64U) {
        return false;
    }
    const uint64_t mask =
        width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
    const bool is_signed = luna_type_kind_is_signed_integer(kind);
    const uint64_t signed_limit = is_signed ? UINT64_C(1) << (width - 1U) : 0U;
    const uint64_t maximum = is_signed ? signed_limit - UINT64_C(1) : mask;
    const uint64_t magnitude = literal->as.integer;

    if ((is_negative && (!is_signed || magnitude > signed_limit)) ||
        (!is_negative && magnitude > maximum)) {
        luna_sema_fail(context, expression->span,
                       "enum member value does not fit in %s",
                       luna_type_kind_name(kind));
        return false;
    }
    *value = is_negative ? (UINT64_C(0) - magnitude) & mask : magnitude;
    return true;
}

static bool luna_sema_next_enum_value(LunaSemaContext *context,
                                      LunaSemaTypeId underlying_type,
                                      uint64_t previous, LunaSourceSpan span,
                                      uint64_t *value) {
    const LunaTypeKind kind = luna_sema_type_kind(context, underlying_type);
    const uint32_t width =
        luna_type_kind_bit_width(kind, &context->module->target->data_layout);
    const uint64_t mask =
        width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
    const bool is_signed = luna_type_kind_is_signed_integer(kind);
    const uint64_t sign_bit = is_signed ? UINT64_C(1) << (width - 1U) : 0U;
    const uint64_t maximum = is_signed ? sign_bit - UINT64_C(1) : mask;
    if ((is_signed && (previous & sign_bit) == 0U && previous == maximum) ||
        (!is_signed && previous == maximum)) {
        luna_sema_fail(context, span, "implicit enum member value overflows %s",
                       luna_type_kind_name(kind));
        return false;
    }
    *value = (previous + UINT64_C(1)) & mask;
    return true;
}

static bool
luna_sema_collect_aggregate_fields(LunaSemaContext *context,
                                   const LunaSemaNamedType *named_type) {
    const LunaTypeDeclaration *declaration = named_type->syntax;
    if (declaration->as.aggregate.field_count == 0U) {
        luna_sema_fail(context, declaration->span,
                       "aggregate type '%.*s' must declare a field",
                       (int)declaration->name.length, declaration->name.data);
        return false;
    }

    bool success = true;
    for (const LunaField *syntax = declaration->as.aggregate.first_field;
         syntax != NULL; syntax = syntax->next) {
        if (luna_sema_find_field_mutable(context, named_type->type,
                                         syntax->name) != NULL) {
            luna_sema_fail(
                context, syntax->span, "duplicate field '%.*s' in type '%.*s'",
                (int)syntax->name.length, syntax->name.data,
                (int)declaration->name.length, declaration->name.data);
            success = false;
            continue;
        }

        const LunaSemaTypeId field_type =
            luna_sema_resolve_type(context, &syntax->type);
        const LunaTypeKind field_kind =
            luna_sema_type_kind(context, field_type);
        if (field_kind == LUNA_TYPE_INVALID || field_kind == LUNA_TYPE_VOID) {
            if (field_kind == LUNA_TYPE_VOID) {
                luna_sema_fail(context, syntax->type.span,
                               "aggregate field cannot have type void");
            }
            success = false;
            continue;
        }

        const LunaSemaField field = {
            .syntax = syntax,
            .owner_type = named_type->type,
            .type = field_type,
        };
        if (!luna_vector_push(&context->fields, &field)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
    }
    return success;
}

static bool
luna_sema_collect_enum_members(LunaSemaContext *context,
                               const LunaSemaNamedType *named_type) {
    const LunaTypeDeclaration *declaration = named_type->syntax;
    const LunaSemaTypeId underlying_type = luna_sema_resolve_type(
        context, &declaration->as.enumeration.underlying_type);
    if (!luna_type_kind_is_integer(
            luna_sema_type_kind(context, underlying_type))) {
        luna_sema_fail(context,
                       declaration->as.enumeration.underlying_type.span,
                       "enum underlying type must be a built-in "
                       "integer type");
        return false;
    }
    LunaSemaType *enum_type = luna_sema_type_mutable(context, named_type->type);
    enum_type->element_type = underlying_type;

    if (declaration->as.enumeration.member_count == 0U) {
        luna_sema_fail(context, declaration->span,
                       "enum type '%.*s' must declare a member",
                       (int)declaration->name.length, declaration->name.data);
        return false;
    }

    bool success = true;
    bool has_previous = false;
    uint64_t previous = 0U;
    for (const LunaEnumMember *syntax =
             declaration->as.enumeration.first_member;
         syntax != NULL; syntax = syntax->next) {
        if (luna_sema_find_enum_member(context, named_type->type,
                                       syntax->name) != NULL) {
            luna_sema_fail(context, syntax->span,
                           "duplicate enum member '%.*s' in type '%.*s'",
                           (int)syntax->name.length, syntax->name.data,
                           (int)declaration->name.length,
                           declaration->name.data);
            success = false;
            continue;
        }

        uint64_t value = 0U;
        bool value_is_valid = true;
        if (syntax->initializer != NULL) {
            value_is_valid = luna_sema_enum_literal_value(
                context, syntax->initializer, underlying_type, &value);
        } else if (has_previous) {
            value_is_valid = luna_sema_next_enum_value(
                context, underlying_type, previous, syntax->span, &value);
        }
        if (!value_is_valid) {
            success = false;
            continue;
        }

        const LunaSemaEnumMember member = {
            .syntax = syntax,
            .owner_type = named_type->type,
            .value = value,
        };
        if (!luna_vector_push(&context->enum_members, &member)) {
            luna_sema_report_allocation_failure(context);
            return false;
        }
        previous = value;
        has_previous = true;
    }
    return success;
}

bool luna_sema_resolve_type_declarations(LunaSemaContext *context,
                                         size_t first_named_type) {
    bool success = true;
    for (size_t index = first_named_type; index < context->named_types.length;
         index += 1U) {
        const LunaSemaNamedType *named_type =
            luna_vector_at_const(&context->named_types, index);
        if (named_type->syntax->kind == LUNA_TYPE_ENUM) {
            if (!luna_sema_collect_enum_members(context, named_type)) {
                success = false;
            }
        } else if (!luna_sema_collect_aggregate_fields(context, named_type)) {
            success = false;
        }
    }

    for (size_t index = first_named_type; index < context->named_types.length;
         index += 1U) {
        const LunaSemaNamedType *named_type =
            luna_vector_at_const(&context->named_types, index);
        if (named_type->syntax->kind == LUNA_TYPE_ENUM) {
            continue;
        }
        uint64_t size_bytes = 0U;
        uint32_t alignment_bytes = 0U;
        if (!luna_sema_type_layout(context, named_type->type, &size_bytes,
                                   &alignment_bytes)) {
            success = false;
        }
    }
    return success;
}

static const LunaSemaType *
luna_sema_next_pointer_qualifier(const LunaSemaContext *context,
                                 LunaSemaTypeId *type) {
    while (type != NULL && *type != LUNA_TYPE_INVALID) {
        const LunaSemaType *resolved = luna_sema_type(context, *type);
        if (resolved == NULL) {
            *type = LUNA_TYPE_INVALID;
            return NULL;
        }
        if (resolved->kind == LUNA_TYPE_POINTER) {
            *type = resolved->element_type;
            return resolved;
        }
        if (resolved->kind == LUNA_TYPE_ARRAY) {
            *type = resolved->element_type;
            continue;
        }
        *type = LUNA_TYPE_INVALID;
    }
    return NULL;
}

bool luna_sema_pointer_conversion_removes_read_only(
    const LunaSemaContext *context, LunaSemaTypeId source_type,
    LunaSemaTypeId target_type) {
    LunaSemaTypeId source_cursor = source_type;
    LunaSemaTypeId target_cursor = target_type;
    for (;;) {
        const LunaSemaType *source =
            luna_sema_next_pointer_qualifier(context, &source_cursor);
        if (source == NULL) {
            return false;
        }
        const LunaSemaType *target =
            luna_sema_next_pointer_qualifier(context, &target_cursor);
        if (source->is_read_only && (target == NULL || !target->is_read_only)) {
            return true;
        }
    }
}
