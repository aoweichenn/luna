#include "luna/middleend/sema/sema.h"

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__STDC_IEC_60559_BFP__)
#error "floating literals require IEC 60559 floating-point arithmetic"
#endif

_Static_assert(FLT_RADIX == 2, "floating literals require a binary host");
_Static_assert(FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "floating literals require IEEE-754 binary32");
_Static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "floating literals require IEEE-754 binary64");
_Static_assert(FLT_HAS_SUBNORM == 1 && DBL_HAS_SUBNORM == 1,
               "floating literals require subnormal support");
_Static_assert(sizeof(float) == sizeof(uint32_t),
               "floating literals require 32-bit float");
typedef int LunaSemaTypeId;

_Static_assert(sizeof(double) == sizeof(uint64_t),
               "floating literals require 64-bit double");

typedef struct LunaSemaFunction {
    const LunaFunction *syntax;
    LunaStringView module_name;
    LunaSemaTypeId return_type;
    LunaVector parameter_types;
    LunaIrFunctionId ir_id;
    bool is_exported;
} LunaSemaFunction;

typedef enum LunaSemaLayoutState {
    LUNA_SEMA_LAYOUT_UNRESOLVED,
    LUNA_SEMA_LAYOUT_RESOLVING,
    LUNA_SEMA_LAYOUT_RESOLVED,
    LUNA_SEMA_LAYOUT_INVALID
} LunaSemaLayoutState;

typedef struct LunaSemaType {
    LunaTypeKind kind;
    LunaSemaTypeId element_type;
    uint64_t array_count;
    bool is_read_only;
    const LunaTypeDeclaration *declaration;
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    LunaSemaLayoutState layout_state;
} LunaSemaType;

typedef struct LunaSemaNamedType {
    const LunaTypeDeclaration *syntax;
    LunaStringView module_name;
    LunaSemaTypeId type;
    bool is_exported;
} LunaSemaNamedType;

typedef struct LunaSemaField {
    const LunaField *syntax;
    LunaSemaTypeId owner_type;
    LunaSemaTypeId type;
    uint64_t offset;
} LunaSemaField;

typedef struct LunaSemaEnumMember {
    const LunaEnumMember *syntax;
    LunaSemaTypeId owner_type;
    uint64_t value;
} LunaSemaEnumMember;

typedef struct LunaSemaLocal {
    LunaStringView name;
    LunaSemaTypeId type;
    LunaIrSlotId slot;
    uint32_t scope_depth;
    bool is_mutable;
} LunaSemaLocal;

typedef struct LunaSemaControlFrame {
    LunaIrBlockId break_block;
    LunaIrBlockId continue_block;
    bool has_live_break;
    bool has_live_continue;
} LunaSemaControlFrame;

typedef struct LunaSemaSwitchArm {
    const LunaSwitchArm *syntax;
    LunaIrBlockId body_block;
} LunaSemaSwitchArm;

typedef struct LunaSemaSwitchLabel {
    uint64_t value;
    LunaSourceSpan span;
    LunaIrBlockId body_block;
} LunaSemaSwitchLabel;

typedef struct LunaCheckedValue {
    LunaIrValueId id;
    LunaSemaTypeId type;
} LunaCheckedValue;

typedef struct LunaSemaCallArgument {
    LunaIrValueId value;
    LunaIrSlotId preserved_slot;
    LunaSemaTypeId type;
    bool is_aggregate;
} LunaSemaCallArgument;

typedef enum LunaSemaLvalueStorage {
    LUNA_SEMA_LVALUE_INVALID,
    LUNA_SEMA_LVALUE_SLOT,
    LUNA_SEMA_LVALUE_ADDRESS
} LunaSemaLvalueStorage;

typedef struct LunaCheckedLvalue {
    LunaSemaTypeId type;
    LunaSemaLvalueStorage storage;
    LunaIrSlotId slot;
    LunaIrValueId address;
    bool is_mutable;
    bool requires_null_check;
} LunaCheckedLvalue;

typedef struct LunaSemaContext {
    const LunaProgram *interface_unit;
    const LunaProgram *implementation_unit;
    LunaDiagnosticEngine *diagnostics;
    LunaIrModule *module;
    LunaVector functions;
    LunaVector visible_functions;
    LunaVector types;
    LunaVector named_types;
    LunaVector visible_named_types;
    LunaVector fields;
    LunaVector enum_members;
    LunaVector locals;
    LunaVector control_frames;
    LunaIrFunction *current_function;
    LunaSemaFunction *current_semantic_function;
    const LunaFunction *current_syntax_function;
    LunaIrBlockId current_block;
    uint32_t scope_depth;
    uint64_t current_metadata_content_hash;
    bool reachable;
    bool checking_dead_code;
    bool allocation_failed;
    bool current_interface_is_metadata;
} LunaSemaContext;

static const LunaSemaType *luna_sema_type(const LunaSemaContext *context,
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

static bool luna_sema_initialize_types(LunaSemaContext *context) {
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

static const LunaSemaNamedType *
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

static const LunaSemaField *luna_sema_find_field(const LunaSemaContext *context,
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

static const LunaSemaEnumMember *
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

static LunaTypeKind luna_sema_type_kind(const LunaSemaContext *context,
                                        LunaSemaTypeId type) {
    const LunaSemaType *resolved = luna_sema_type(context, type);
    return resolved == NULL ? LUNA_TYPE_INVALID : resolved->kind;
}

static bool luna_sema_is_pointer_type(const LunaSemaContext *context,
                                      LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_POINTER;
}

static bool luna_sema_is_array_type(const LunaSemaContext *context,
                                    LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_ARRAY;
}

static bool luna_sema_is_record_type(const LunaSemaContext *context,
                                     LunaSemaTypeId type) {
    const LunaTypeKind kind = luna_sema_type_kind(context, type);
    return kind == LUNA_TYPE_STRUCT || kind == LUNA_TYPE_UNION;
}

static bool luna_sema_is_memory_type(const LunaSemaContext *context,
                                     LunaSemaTypeId type) {
    return luna_sema_is_array_type(context, type) ||
           luna_sema_is_record_type(context, type);
}

static bool luna_sema_is_enum_type(const LunaSemaContext *context,
                                   LunaSemaTypeId type) {
    return luna_sema_type_kind(context, type) == LUNA_TYPE_ENUM;
}

static LunaIrType luna_sema_ir_type(const LunaSemaContext *context,
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

static void luna_sema_report_allocation_failure(LunaSemaContext *context) {
    if (!context->allocation_failed) {
        luna_diagnostic_error_plain(context->diagnostics,
                                    "out of memory while building typed IR");
        context->allocation_failed = true;
    }
}

static bool luna_sema_append_type_name(const LunaSemaContext *context,
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

static bool luna_sema_type_layout(LunaSemaContext *context, LunaSemaTypeId type,
                                  uint64_t *size_bytes,
                                  uint32_t *alignment_bytes) {
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
                luna_diagnostic_error(
                    context->diagnostics, resolved->declaration->span,
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
                    luna_diagnostic_error(
                        context->diagnostics, field->syntax->type.span,
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
            luna_diagnostic_error(
                context->diagnostics, declaration->span,
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

static bool luna_sema_build_aggregate_layout(LunaSemaContext *context,
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

static LunaSemaTypeId luna_sema_resolve_type(LunaSemaContext *context,
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
            luna_diagnostic_error(
                context->diagnostics, type_ref->span, "unknown type '%.*s'",
                (int)type_ref->as.name.length, type_ref->as.name.data);
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
            luna_diagnostic_error(context->diagnostics, type_ref->span,
                                  "fixed-array length must be positive");
            return LUNA_TYPE_INVALID;
        }
        const LunaSemaTypeId element =
            luna_sema_resolve_type(context, type_ref->as.array.element);
        const LunaTypeKind element_kind = luna_sema_type_kind(context, element);
        if (element_kind == LUNA_TYPE_INVALID ||
            element_kind == LUNA_TYPE_VOID) {
            luna_diagnostic_error(context->diagnostics, type_ref->span,
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

static LunaSemaTypeId luna_sema_pointer_type(LunaSemaContext *context,
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

static bool luna_sema_validate_exported_type(LunaSemaContext *context,
                                             LunaSemaTypeId type,
                                             LunaSourceSpan span,
                                             LunaStringView exported_name) {
    const LunaSemaNamedType *private_type =
        luna_sema_private_named_type(context, type);
    if (private_type == NULL) {
        return true;
    }
    luna_diagnostic_error(
        context->diagnostics, span,
        "exported declaration '%.*s' exposes non-exported type '%.*s'",
        (int)exported_name.length, exported_name.data,
        (int)private_type->syntax->name.length,
        private_type->syntax->name.data);
    luna_diagnostic_note(context->diagnostics, private_type->syntax->span,
                         "the non-exported type is declared here");
    return false;
}

static bool luna_sema_collect_named_types(LunaSemaContext *context,
                                          const LunaProgram *program) {
    for (const LunaTypeDeclaration *declaration =
             program->first_type_declaration;
         declaration != NULL; declaration = declaration->next) {
        const LunaSemaNamedType *existing =
            luna_sema_find_named_type(context, declaration->name);
        if (existing != NULL) {
            luna_diagnostic_error(context->diagnostics, declaration->span,
                                  "duplicate type declaration '%.*s'",
                                  (int)declaration->name.length,
                                  declaration->name.data);
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

static bool
luna_sema_validate_exported_type_declarations(LunaSemaContext *context,
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
        luna_diagnostic_error(
            context->diagnostics, expression->span,
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
        luna_diagnostic_error(context->diagnostics, expression->span,
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
        luna_diagnostic_error(context->diagnostics, span,
                              "implicit enum member value overflows %s",
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
        luna_diagnostic_error(context->diagnostics, declaration->span,
                              "aggregate type '%.*s' must declare a field",
                              (int)declaration->name.length,
                              declaration->name.data);
        return false;
    }

    bool success = true;
    for (const LunaField *syntax = declaration->as.aggregate.first_field;
         syntax != NULL; syntax = syntax->next) {
        if (luna_sema_find_field_mutable(context, named_type->type,
                                         syntax->name) != NULL) {
            luna_diagnostic_error(context->diagnostics, syntax->span,
                                  "duplicate field '%.*s' in type '%.*s'",
                                  (int)syntax->name.length, syntax->name.data,
                                  (int)declaration->name.length,
                                  declaration->name.data);
            success = false;
            continue;
        }

        const LunaSemaTypeId field_type =
            luna_sema_resolve_type(context, &syntax->type);
        const LunaTypeKind field_kind =
            luna_sema_type_kind(context, field_type);
        if (field_kind == LUNA_TYPE_INVALID || field_kind == LUNA_TYPE_VOID) {
            if (field_kind == LUNA_TYPE_VOID) {
                luna_diagnostic_error(context->diagnostics, syntax->type.span,
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
        luna_diagnostic_error(context->diagnostics,
                              declaration->as.enumeration.underlying_type.span,
                              "enum underlying type must be a built-in "
                              "integer type");
        return false;
    }
    LunaSemaType *enum_type = luna_sema_type_mutable(context, named_type->type);
    enum_type->element_type = underlying_type;

    if (declaration->as.enumeration.member_count == 0U) {
        luna_diagnostic_error(context->diagnostics, declaration->span,
                              "enum type '%.*s' must declare a member",
                              (int)declaration->name.length,
                              declaration->name.data);
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
            luna_diagnostic_error(context->diagnostics, syntax->span,
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

static bool luna_sema_resolve_type_declarations(LunaSemaContext *context,
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

static bool
luna_sema_pointer_conversion_removes_read_only(const LunaSemaContext *context,
                                               LunaSemaTypeId source_type,
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

static LunaIrInstruction luna_sema_instruction(LunaIrOpcode opcode,
                                               LunaSourceSpan span) {
    return (LunaIrInstruction){
        .opcode = opcode,
        .type = LUNA_IR_TYPE_VOID,
        .memory_type = LUNA_IR_TYPE_VOID,
        .result = LUNA_IR_INVALID_ID,
        .left = LUNA_IR_INVALID_ID,
        .right = LUNA_IR_INVALID_ID,
        .slot = LUNA_IR_INVALID_ID,
        .true_block = LUNA_IR_INVALID_ID,
        .false_block = LUNA_IR_INVALID_ID,
        .callee = LUNA_IR_INVALID_ID,
        .global = LUNA_IR_INVALID_ID,
        .first_argument = 0U,
        .argument_count = 0U,
        .immediate = 0U,
        .span = span,
    };
}

static LunaIrBlock *luna_sema_current_block(LunaSemaContext *context) {
    return luna_ir_function_block(context->current_function,
                                  context->current_block);
}

static bool luna_sema_append_instruction(LunaSemaContext *context,
                                         const LunaIrInstruction *instruction) {
    if (!context->reachable ||
        !luna_ir_block_append(luna_sema_current_block(context), instruction)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    return true;
}

static LunaIrBlockId luna_sema_add_block(LunaSemaContext *context) {
    const LunaIrBlockId block_id =
        luna_ir_function_add_block(context->current_function);
    if (block_id == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return block_id;
}

static LunaIrValueId luna_sema_add_value(LunaSemaContext *context,
                                         LunaSemaTypeId type) {
    const LunaIrValueId value = luna_ir_function_add_value(
        context->current_function, luna_sema_ir_type(context, type));
    if (value == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return value;
}

static bool luna_sema_add_predecessor(LunaSemaContext *context,
                                      LunaIrBlockId block_id) {
    LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    if (block == NULL || block->predecessor_count == UINT32_MAX) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    block->predecessor_count += 1U;
    return true;
}

static bool luna_sema_emit_jump(LunaSemaContext *context, LunaIrBlockId target,
                                LunaSourceSpan span) {
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_JUMP, span);
    instruction.true_block = target;

    if (!luna_sema_append_instruction(context, &instruction) ||
        !luna_sema_add_predecessor(context, target)) {
        return false;
    }

    context->reachable = false;
    return true;
}

static bool luna_sema_emit_branch(LunaSemaContext *context,
                                  LunaIrValueId condition,
                                  LunaIrBlockId true_block,
                                  LunaIrBlockId false_block,
                                  LunaSourceSpan span) {
    LunaIrInstruction instruction = luna_sema_instruction(LUNA_IR_BRANCH, span);
    instruction.left = condition;
    instruction.true_block = true_block;
    instruction.false_block = false_block;

    if (!luna_sema_append_instruction(context, &instruction) ||
        !luna_sema_add_predecessor(context, true_block) ||
        !luna_sema_add_predecessor(context, false_block)) {
        return false;
    }

    context->reachable = false;
    return true;
}

static void luna_sema_set_block(LunaSemaContext *context,
                                LunaIrBlockId block_id) {
    context->current_block = block_id;
    const LunaIrBlock *block =
        luna_ir_function_block(context->current_function, block_id);
    context->reachable =
        block_id == 0U || (block != NULL && block->predecessor_count > 0U);
}

static LunaIrValueId
luna_sema_emit_value_instruction(LunaSemaContext *context,
                                 LunaIrInstruction *instruction,
                                 LunaSemaTypeId type) {
    instruction->type = luna_sema_ir_type(context, type);
    instruction->result = luna_sema_add_value(context, type);
    if (instruction->result == LUNA_IR_INVALID_ID ||
        !luna_sema_append_instruction(context, instruction)) {
        return LUNA_IR_INVALID_ID;
    }

    return instruction->result;
}

static LunaIrSlotId luna_sema_preserve_value(LunaSemaContext *context,
                                             LunaCheckedValue value,
                                             LunaSourceSpan span) {
    const LunaIrSlotId slot = luna_ir_function_add_slot(
        context->current_function, luna_sema_ir_type(context, value.type));
    if (slot == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return LUNA_IR_INVALID_ID;
    }

    LunaIrInstruction store = luna_sema_instruction(LUNA_IR_STORE, span);
    store.slot = slot;
    store.left = value.id;
    if (!luna_sema_append_instruction(context, &store)) {
        return LUNA_IR_INVALID_ID;
    }
    return slot;
}

static LunaCheckedValue luna_sema_reload_value(LunaSemaContext *context,
                                               LunaIrSlotId slot,
                                               LunaSemaTypeId type,
                                               LunaSourceSpan span) {
    LunaIrInstruction load = luna_sema_instruction(LUNA_IR_LOAD, span);
    load.slot = slot;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, type);
    return (LunaCheckedValue){
        .id = result,
        .type = type,
    };
}

static LunaSemaFunction *luna_sema_find_function(LunaSemaContext *context,
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

static bool luna_sema_add_imported_scope(LunaSemaContext *context,
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
                luna_diagnostic_error(
                    context->diagnostics, import->span,
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
                luna_diagnostic_error(
                    context->diagnostics, import->span,
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

static LunaSemaLocal *luna_sema_find_local(LunaSemaContext *context,
                                           LunaStringView name) {
    for (size_t index = context->locals.length; index > 0U; index -= 1U) {
        LunaSemaLocal *local = luna_vector_at(&context->locals, index - 1U);
        if (luna_string_view_equal(local->name, name)) {
            return local;
        }
    }

    return NULL;
}

static LunaSemaLocal *
luna_sema_find_local_in_current_scope(LunaSemaContext *context,
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

static bool luna_sema_add_local(LunaSemaContext *context, LunaStringView name,
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

static void luna_sema_enter_scope(LunaSemaContext *context) {
    context->scope_depth += 1U;
}

static void luna_sema_leave_scope(LunaSemaContext *context) {
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

static bool luna_sema_require_type(LunaSemaContext *context,
                                   LunaCheckedValue value,
                                   LunaSemaTypeId expected,
                                   LunaSourceSpan span) {
    if (value.type == expected) {
        return true;
    }

    LunaStringBuilder expected_name;
    LunaStringBuilder actual_name;
    luna_string_builder_init(&expected_name);
    luna_string_builder_init(&actual_name);
    const bool formatted =
        luna_sema_append_type_name(context, expected, &expected_name) &&
        luna_sema_append_type_name(context, value.type, &actual_name);
    if (formatted) {
        luna_diagnostic_error(context->diagnostics, span,
                              "expected %s, found %s",
                              luna_string_builder_data(&expected_name),
                              luna_string_builder_data(&actual_name));
    } else {
        luna_sema_report_allocation_failure(context);
    }
    luna_string_builder_destroy(&actual_name);
    luna_string_builder_destroy(&expected_name);
    return false;
}

static LunaCheckedValue luna_sema_invalid_value(void) {
    return (LunaCheckedValue){
        .id = LUNA_IR_INVALID_ID,
        .type = LUNA_TYPE_INVALID,
    };
}

static LunaCheckedValue
luna_sema_lower_expression_expected(LunaSemaContext *context,
                                    const LunaExpression *expression,
                                    LunaSemaTypeId expected_type);

static bool luna_sema_is_integer_type(LunaSemaTypeId type) {
    return type >= (LunaSemaTypeId)LUNA_TYPE_INVALID &&
           type <= (LunaSemaTypeId)LUNA_TYPE_F64 &&
           luna_type_kind_is_integer((LunaTypeKind)type);
}

static bool luna_sema_is_float_type(LunaSemaTypeId type) {
    return type >= (LunaSemaTypeId)LUNA_TYPE_INVALID &&
           type <= (LunaSemaTypeId)LUNA_TYPE_F64 &&
           luna_type_kind_is_float((LunaTypeKind)type);
}

static bool luna_sema_is_numeric_type(LunaSemaTypeId type) {
    return luna_sema_is_integer_type(type) || luna_sema_is_float_type(type);
}

static LunaIrOpcode
luna_sema_binary_integer_opcode(LunaTokenKind operator_kind) {
    switch (operator_kind) {
    case LUNA_TOKEN_PLUS:
    case LUNA_TOKEN_PLUS_EQUAL:
        return LUNA_IR_ADD_INTEGER;
    case LUNA_TOKEN_MINUS:
    case LUNA_TOKEN_MINUS_EQUAL:
        return LUNA_IR_SUB_INTEGER;
    case LUNA_TOKEN_STAR:
    case LUNA_TOKEN_STAR_EQUAL:
        return LUNA_IR_MUL_INTEGER;
    case LUNA_TOKEN_SLASH:
    case LUNA_TOKEN_SLASH_EQUAL:
        return LUNA_IR_DIV_INTEGER;
    case LUNA_TOKEN_PERCENT:
    case LUNA_TOKEN_PERCENT_EQUAL:
        return LUNA_IR_REM_INTEGER;
    case LUNA_TOKEN_AMPERSAND:
    case LUNA_TOKEN_AMPERSAND_EQUAL:
        return LUNA_IR_BIT_AND_INTEGER;
    case LUNA_TOKEN_PIPE:
    case LUNA_TOKEN_PIPE_EQUAL:
        return LUNA_IR_BIT_OR_INTEGER;
    case LUNA_TOKEN_CARET:
    case LUNA_TOKEN_CARET_EQUAL:
        return LUNA_IR_BIT_XOR_INTEGER;
    case LUNA_TOKEN_SHIFT_LEFT:
    case LUNA_TOKEN_SHIFT_LEFT_EQUAL:
        return LUNA_IR_SHIFT_LEFT_INTEGER;
    case LUNA_TOKEN_SHIFT_RIGHT:
    case LUNA_TOKEN_SHIFT_RIGHT_EQUAL:
        return LUNA_IR_SHIFT_RIGHT_INTEGER;
    case LUNA_TOKEN_LESS:
        return LUNA_IR_COMPARE_LESS_INTEGER;
    case LUNA_TOKEN_LESS_EQUAL:
        return LUNA_IR_COMPARE_LESS_EQUAL_INTEGER;
    case LUNA_TOKEN_GREATER:
        return LUNA_IR_COMPARE_GREATER_INTEGER;
    case LUNA_TOKEN_GREATER_EQUAL:
        return LUNA_IR_COMPARE_GREATER_EQUAL_INTEGER;
    default:
        return LUNA_IR_ADD_INTEGER;
    }
}

static bool luna_sema_binary_float_opcode(LunaTokenKind operator_kind,
                                          LunaIrOpcode *opcode) {
    switch (operator_kind) {
    case LUNA_TOKEN_PLUS:
    case LUNA_TOKEN_PLUS_EQUAL:
        *opcode = LUNA_IR_ADD_FLOAT;
        return true;
    case LUNA_TOKEN_MINUS:
    case LUNA_TOKEN_MINUS_EQUAL:
        *opcode = LUNA_IR_SUB_FLOAT;
        return true;
    case LUNA_TOKEN_STAR:
    case LUNA_TOKEN_STAR_EQUAL:
        *opcode = LUNA_IR_MUL_FLOAT;
        return true;
    case LUNA_TOKEN_SLASH:
    case LUNA_TOKEN_SLASH_EQUAL:
        *opcode = LUNA_IR_DIV_FLOAT;
        return true;
    default:
        return false;
    }
}

static LunaIrOpcode
luna_sema_float_comparison_opcode(LunaTokenKind operator_kind) {
    switch (operator_kind) {
    case LUNA_TOKEN_LESS:
        return LUNA_IR_COMPARE_LESS_FLOAT;
    case LUNA_TOKEN_LESS_EQUAL:
        return LUNA_IR_COMPARE_LESS_EQUAL_FLOAT;
    case LUNA_TOKEN_GREATER:
        return LUNA_IR_COMPARE_GREATER_FLOAT;
    case LUNA_TOKEN_GREATER_EQUAL:
        return LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT;
    default:
        return LUNA_IR_COMPARE_LESS_FLOAT;
    }
}

static uint64_t luna_sema_integer_maximum(const LunaSemaContext *context,
                                          LunaSemaTypeId type) {
    const LunaTypeKind kind = luna_sema_type_kind(context, type);
    const uint32_t width =
        luna_type_kind_bit_width(kind, &context->module->target->data_layout);
    if (width == 0U || width > 64U) {
        return 0U;
    }
    if (luna_type_kind_is_unsigned_integer(kind)) {
        return width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    }
    return width == 64U ? (uint64_t)INT64_MAX
                        : (UINT64_C(1) << (width - 1U)) - 1U;
}

static uint64_t
luna_sema_signed_minimum_magnitude(const LunaSemaContext *context,
                                   LunaSemaTypeId type) {
    const LunaTypeKind kind = luna_sema_type_kind(context, type);
    if (!luna_type_kind_is_signed_integer(kind)) {
        return 0U;
    }
    const uint32_t width =
        luna_type_kind_bit_width(kind, &context->module->target->data_layout);
    return width == 0U || width > 64U ? 0U : UINT64_C(1) << (width - 1U);
}

static bool luna_sema_float_literal_bits(LunaSemaContext *context,
                                         const LunaExpression *expression,
                                         LunaSemaTypeId type, uint64_t *bits) {
    const LunaStringView text = expression->as.floating;
    if (text.length == SIZE_MAX) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    char *normalized = malloc(text.length + 1U);
    if (normalized == NULL) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    size_t normalized_length = 0U;
    for (size_t index = 0U; index < text.length; index += 1U) {
        if (text.data[index] != '_') {
            normalized[normalized_length] = text.data[index];
            normalized_length += 1U;
        }
    }
    normalized[normalized_length] = '\0';

    char *end = NULL;
    bool valid = false;
    bool finite = false;
    if (type == LUNA_TYPE_F32) {
        const float value = strtof(normalized, &end);
        uint32_t value_bits = 0U;
        memcpy(&value_bits, &value, sizeof(value_bits));
        *bits = value_bits;
        finite = isfinite(value);
        valid = end == normalized + normalized_length;
    } else if (type == LUNA_TYPE_F64) {
        const double value = strtod(normalized, &end);
        memcpy(bits, &value, sizeof(*bits));
        finite = isfinite(value);
        valid = end == normalized + normalized_length;
    }
    free(normalized);

    if (!valid) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "invalid floating-point literal");
        return false;
    }
    if (!finite) {
        luna_diagnostic_error(
            context->diagnostics, expression->span,
            "floating-point literal does not fit in %s",
            luna_type_kind_name(luna_sema_type_kind(context, type)));
        return false;
    }
    return true;
}

static int luna_sema_hexadecimal_digit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static LunaCheckedValue
luna_sema_lower_string(LunaSemaContext *context,
                       const LunaExpression *expression) {
    const LunaStringView text = expression->as.string;
    if (text.length < 2U || text.data[0] != '"' ||
        text.data[text.length - 1U] != '"') {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "invalid string literal");
        return luna_sema_invalid_value();
    }

    LunaVector bytes;
    luna_vector_init(&bytes, sizeof(uint8_t));
    bool valid = true;
    for (size_t index = 1U; index + 1U < text.length; index += 1U) {
        unsigned char byte = (unsigned char)text.data[index];
        if (byte == (unsigned char)'\\') {
            index += 1U;
            if (index + 1U >= text.length) {
                valid = false;
                break;
            }
            const char escape = text.data[index];
            switch (escape) {
            case '\\':
                byte = (unsigned char)'\\';
                break;
            case '"':
                byte = (unsigned char)'"';
                break;
            case 'n':
                byte = (unsigned char)'\n';
                break;
            case 'r':
                byte = (unsigned char)'\r';
                break;
            case 't':
                byte = (unsigned char)'\t';
                break;
            case '0':
                byte = 0U;
                break;
            case 'x': {
                if (index + 2U >= text.length - 1U) {
                    valid = false;
                    break;
                }
                const int high =
                    luna_sema_hexadecimal_digit(text.data[index + 1U]);
                const int low =
                    luna_sema_hexadecimal_digit(text.data[index + 2U]);
                if (high < 0 || low < 0) {
                    valid = false;
                    break;
                }
                byte = (unsigned char)((unsigned int)high * 16U +
                                       (unsigned int)low);
                index += 2U;
                break;
            }
            default:
                valid = false;
                break;
            }
        }
        if (!valid || !luna_vector_push(&bytes, &byte)) {
            if (valid) {
                luna_sema_report_allocation_failure(context);
            }
            valid = false;
            break;
        }
    }

    const uint8_t terminator = 0U;
    if (valid && !luna_vector_push(&bytes, &terminator)) {
        luna_sema_report_allocation_failure(context);
        valid = false;
    }
    if (!valid) {
        if (!context->allocation_failed) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "invalid escape in string literal");
        }
        luna_vector_destroy(&bytes);
        return luna_sema_invalid_value();
    }

    const LunaIrGlobalId global = luna_ir_module_add_global(
        context->module, bytes.data, (uint64_t)bytes.length, 1U, true);
    luna_vector_destroy(&bytes);
    if (global == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return luna_sema_invalid_value();
    }

    LunaIrInstruction address =
        luna_sema_instruction(LUNA_IR_GLOBAL_ADDRESS, expression->span);
    address.global = global;
    const LunaSemaTypeId type =
        luna_sema_pointer_type(context, LUNA_TYPE_U8, true);
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &address, type);
    return (LunaCheckedValue){
        .id = result,
        .type = type,
    };
}

static LunaCheckedValue
luna_sema_lower_zero_initializer(LunaSemaContext *context,
                                 LunaSemaTypeId expected_type,
                                 LunaSourceSpan span) {
    const LunaTypeKind kind = luna_sema_type_kind(context, expected_type);
    LunaIrInstruction instruction =
        luna_sema_instruction(LUNA_IR_CONST_INTEGER, span);

    if (kind == LUNA_TYPE_POINTER) {
        instruction.opcode = LUNA_IR_CONST_NULL;
    } else if (kind == LUNA_TYPE_BOOL) {
        instruction.opcode = LUNA_IR_CONST_BOOL;
    } else if (luna_type_kind_is_float(kind)) {
        instruction.opcode = LUNA_IR_CONST_FLOAT;
    } else if (!luna_type_kind_is_integer(kind) && kind != LUNA_TYPE_ENUM) {
        luna_diagnostic_error(
            context->diagnostics, span,
            "zero initializer requires a scalar or fixed-array context");
        return luna_sema_invalid_value();
    }

    instruction.immediate = 0U;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &instruction, expected_type);
    return (LunaCheckedValue){
        .id = result,
        .type = expected_type,
    };
}

static LunaCheckedValue
luna_sema_lower_expression(LunaSemaContext *context,
                           const LunaExpression *expression) {
    return luna_sema_lower_expression_expected(context, expression,
                                               LUNA_TYPE_INVALID);
}

static LunaSemaTypeId
luna_sema_known_expression_type(LunaSemaContext *context,
                                const LunaExpression *expression);

static const LunaSemaEnumMember *
luna_sema_scoped_enum_member(LunaSemaContext *context,
                             const LunaExpression *expression,
                             LunaSemaTypeId *enum_type) {
    if (expression == NULL || expression->kind != LUNA_EXPRESSION_MEMBER ||
        expression->as.member.operator_kind != LUNA_TOKEN_DOT ||
        expression->as.member.base == NULL ||
        expression->as.member.base->kind != LUNA_EXPRESSION_NAME ||
        luna_sema_find_local(context, expression->as.member.base->as.name) !=
            NULL) {
        return NULL;
    }
    const LunaSemaNamedType *named_type =
        luna_sema_find_named_type(context, expression->as.member.base->as.name);
    if (named_type == NULL ||
        !luna_sema_is_enum_type(context, named_type->type)) {
        return NULL;
    }
    if (enum_type != NULL) {
        *enum_type = named_type->type;
    }
    return luna_sema_find_enum_member(context, named_type->type,
                                      expression->as.member.name);
}

static bool luna_sema_known_lvalue_type(LunaSemaContext *context,
                                        const LunaExpression *expression,
                                        LunaSemaTypeId *type,
                                        bool *is_mutable) {
    if (expression == NULL || type == NULL || is_mutable == NULL) {
        return false;
    }

    if (expression->kind == LUNA_EXPRESSION_NAME) {
        const LunaSemaLocal *local =
            luna_sema_find_local(context, expression->as.name);
        if (local == NULL) {
            return false;
        }
        *type = local->type;
        *is_mutable = local->is_mutable;
        return true;
    }

    if (expression->kind == LUNA_EXPRESSION_UNARY &&
        expression->as.unary.operator_kind == LUNA_TOKEN_STAR) {
        const LunaSemaTypeId pointer_type = luna_sema_known_expression_type(
            context, expression->as.unary.operand);
        const LunaSemaType *pointer = luna_sema_type(context, pointer_type);
        if (pointer == NULL || pointer->kind != LUNA_TYPE_POINTER ||
            luna_sema_type_kind(context, pointer->element_type) ==
                LUNA_TYPE_VOID) {
            return false;
        }
        *type = pointer->element_type;
        *is_mutable = !pointer->is_read_only;
        return true;
    }

    if (expression->kind == LUNA_EXPRESSION_INDEX) {
        const LunaSemaTypeId base_type =
            luna_sema_known_expression_type(context, expression->as.index.base);
        const LunaSemaType *base = luna_sema_type(context, base_type);
        if (base == NULL) {
            return false;
        }
        if (base->kind == LUNA_TYPE_ARRAY) {
            const LunaSemaTypeId element_type = base->element_type;
            LunaSemaTypeId ignored_type = LUNA_TYPE_INVALID;
            bool base_is_mutable = false;
            (void)luna_sema_known_lvalue_type(context,
                                              expression->as.index.base,
                                              &ignored_type, &base_is_mutable);
            *type = element_type;
            *is_mutable = base_is_mutable;
            return true;
        }
        if (base->kind == LUNA_TYPE_POINTER &&
            luna_sema_type_kind(context, base->element_type) !=
                LUNA_TYPE_VOID) {
            *type = base->element_type;
            *is_mutable = !base->is_read_only;
            return true;
        }
    }

    if (expression->kind == LUNA_EXPRESSION_MEMBER) {
        LunaSemaTypeId aggregate_type = LUNA_TYPE_INVALID;
        bool aggregate_is_mutable = false;
        if (expression->as.member.operator_kind == LUNA_TOKEN_DOT) {
            if (!luna_sema_known_lvalue_type(
                    context, expression->as.member.base, &aggregate_type,
                    &aggregate_is_mutable)) {
                aggregate_type = luna_sema_known_expression_type(
                    context, expression->as.member.base);
                aggregate_is_mutable = false;
            }
        } else if (expression->as.member.operator_kind == LUNA_TOKEN_ARROW) {
            const LunaSemaTypeId pointer_type = luna_sema_known_expression_type(
                context, expression->as.member.base);
            const LunaSemaType *pointer = luna_sema_type(context, pointer_type);
            if (pointer == NULL || pointer->kind != LUNA_TYPE_POINTER) {
                return false;
            }
            aggregate_type = pointer->element_type;
            aggregate_is_mutable = !pointer->is_read_only;
        } else {
            return false;
        }
        if (!luna_sema_is_record_type(context, aggregate_type)) {
            return false;
        }
        const LunaSemaField *field = luna_sema_find_field(
            context, aggregate_type, expression->as.member.name);
        if (field == NULL) {
            return false;
        }
        *type = field->type;
        *is_mutable = aggregate_is_mutable;
        return true;
    }

    return false;
}

static LunaSemaTypeId
luna_sema_known_expression_type(LunaSemaContext *context,
                                const LunaExpression *expression) {
    if (expression == NULL) {
        return LUNA_TYPE_INVALID;
    }

    switch (expression->kind) {
    case LUNA_EXPRESSION_INTEGER:
    case LUNA_EXPRESSION_FLOAT:
    case LUNA_EXPRESSION_NULL:
    case LUNA_EXPRESSION_ZERO_INITIALIZER:
    case LUNA_EXPRESSION_AGGREGATE_INITIALIZER:
        return LUNA_TYPE_INVALID;
    case LUNA_EXPRESSION_BOOLEAN:
        return LUNA_TYPE_BOOL;
    case LUNA_EXPRESSION_STRING:
        return luna_sema_pointer_type(context, LUNA_TYPE_U8, true);
    case LUNA_EXPRESSION_SIZEOF:
    case LUNA_EXPRESSION_ALIGNOF:
    case LUNA_EXPRESSION_OFFSETOF:
        return LUNA_TYPE_USIZE;
    case LUNA_EXPRESSION_NAME: {
        const LunaSemaLocal *local =
            luna_sema_find_local(context, expression->as.name);
        return local == NULL ? LUNA_TYPE_INVALID : local->type;
    }
    case LUNA_EXPRESSION_CALL: {
        const LunaSemaFunction *function =
            luna_sema_find_function(context, expression->as.call.name);
        return function == NULL ? LUNA_TYPE_INVALID : function->return_type;
    }
    case LUNA_EXPRESSION_CAST:
        return luna_sema_resolve_type(context,
                                      &expression->as.cast.target_type);
    case LUNA_EXPRESSION_CONDITIONAL: {
        const LunaSemaTypeId then_type = luna_sema_known_expression_type(
            context, expression->as.conditional.then_expression);
        const LunaSemaTypeId else_type = luna_sema_known_expression_type(
            context, expression->as.conditional.else_expression);
        if (then_type != LUNA_TYPE_INVALID && else_type != LUNA_TYPE_INVALID) {
            return then_type == else_type ? then_type : LUNA_TYPE_INVALID;
        }
        return then_type != LUNA_TYPE_INVALID ? then_type : else_type;
    }
    case LUNA_EXPRESSION_UNARY:
        if (expression->as.unary.operator_kind == LUNA_TOKEN_BANG) {
            return LUNA_TYPE_BOOL;
        }
        if (expression->as.unary.operator_kind == LUNA_TOKEN_STAR) {
            const LunaSemaTypeId pointer_type = luna_sema_known_expression_type(
                context, expression->as.unary.operand);
            const LunaSemaType *pointer = luna_sema_type(context, pointer_type);
            return pointer != NULL && pointer->kind == LUNA_TYPE_POINTER
                       ? pointer->element_type
                       : LUNA_TYPE_INVALID;
        }
        if (expression->as.unary.operator_kind == LUNA_TOKEN_AMPERSAND) {
            LunaSemaTypeId lvalue_type = LUNA_TYPE_INVALID;
            bool is_mutable = false;
            if (!luna_sema_known_lvalue_type(context,
                                             expression->as.unary.operand,
                                             &lvalue_type, &is_mutable)) {
                return LUNA_TYPE_INVALID;
            }
            return luna_sema_pointer_type(context, lvalue_type, !is_mutable);
        }
        return luna_sema_known_expression_type(context,
                                               expression->as.unary.operand);
    case LUNA_EXPRESSION_INDEX: {
        const LunaSemaTypeId base_type =
            luna_sema_known_expression_type(context, expression->as.index.base);
        const LunaSemaType *base = luna_sema_type(context, base_type);
        return base != NULL && (base->kind == LUNA_TYPE_POINTER ||
                                base->kind == LUNA_TYPE_ARRAY)
                   ? base->element_type
                   : LUNA_TYPE_INVALID;
    }
    case LUNA_EXPRESSION_MEMBER: {
        LunaSemaTypeId enum_type = LUNA_TYPE_INVALID;
        if (luna_sema_scoped_enum_member(context, expression, &enum_type) !=
            NULL) {
            return enum_type;
        }
        LunaSemaTypeId member_type = LUNA_TYPE_INVALID;
        bool is_mutable = false;
        return luna_sema_known_lvalue_type(context, expression, &member_type,
                                           &is_mutable)
                   ? member_type
                   : LUNA_TYPE_INVALID;
    }
    case LUNA_EXPRESSION_BINARY:
        switch (expression->as.binary.operator_kind) {
        case LUNA_TOKEN_LOGICAL_AND:
        case LUNA_TOKEN_LOGICAL_OR:
        case LUNA_TOKEN_EQUAL_EQUAL:
        case LUNA_TOKEN_BANG_EQUAL:
        case LUNA_TOKEN_LESS:
        case LUNA_TOKEN_LESS_EQUAL:
        case LUNA_TOKEN_GREATER:
        case LUNA_TOKEN_GREATER_EQUAL:
            return LUNA_TYPE_BOOL;
        default:
            break;
        }

        LunaSemaTypeId type = luna_sema_known_expression_type(
            context, expression->as.binary.left);
        if (type == LUNA_TYPE_INVALID) {
            type = luna_sema_known_expression_type(context,
                                                   expression->as.binary.right);
        }
        return type;
    }

    return LUNA_TYPE_INVALID;
}

static LunaSemaTypeId
luna_sema_default_literal_type(const LunaExpression *expression) {
    if (expression == NULL) {
        return LUNA_TYPE_INVALID;
    }

    switch (expression->kind) {
    case LUNA_EXPRESSION_INTEGER:
        return LUNA_TYPE_I32;
    case LUNA_EXPRESSION_FLOAT:
        return LUNA_TYPE_F64;
    case LUNA_EXPRESSION_UNARY:
        if (expression->as.unary.operator_kind == LUNA_TOKEN_PLUS ||
            expression->as.unary.operator_kind == LUNA_TOKEN_MINUS) {
            return luna_sema_default_literal_type(expression->as.unary.operand);
        }
        return LUNA_TYPE_INVALID;
    case LUNA_EXPRESSION_BINARY: {
        const LunaSemaTypeId left =
            luna_sema_default_literal_type(expression->as.binary.left);
        const LunaSemaTypeId right =
            luna_sema_default_literal_type(expression->as.binary.right);
        if (left == LUNA_TYPE_F64 || right == LUNA_TYPE_F64) {
            return LUNA_TYPE_F64;
        }
        if (left == LUNA_TYPE_I32 || right == LUNA_TYPE_I32) {
            return LUNA_TYPE_I32;
        }
        return LUNA_TYPE_INVALID;
    }
    case LUNA_EXPRESSION_CONDITIONAL: {
        const LunaSemaTypeId then_type = luna_sema_default_literal_type(
            expression->as.conditional.then_expression);
        const LunaSemaTypeId else_type = luna_sema_default_literal_type(
            expression->as.conditional.else_expression);
        if (then_type != LUNA_TYPE_INVALID && else_type != LUNA_TYPE_INVALID) {
            return then_type == else_type ? then_type : LUNA_TYPE_INVALID;
        }
        return then_type != LUNA_TYPE_INVALID ? then_type : else_type;
    }
    case LUNA_EXPRESSION_BOOLEAN:
    case LUNA_EXPRESSION_STRING:
    case LUNA_EXPRESSION_NULL:
    case LUNA_EXPRESSION_ZERO_INITIALIZER:
    case LUNA_EXPRESSION_AGGREGATE_INITIALIZER:
    case LUNA_EXPRESSION_NAME:
    case LUNA_EXPRESSION_INDEX:
    case LUNA_EXPRESSION_MEMBER:
    case LUNA_EXPRESSION_SIZEOF:
    case LUNA_EXPRESSION_ALIGNOF:
    case LUNA_EXPRESSION_OFFSETOF:
    case LUNA_EXPRESSION_CALL:
    case LUNA_EXPRESSION_CAST:
        return LUNA_TYPE_INVALID;
    }

    return LUNA_TYPE_INVALID;
}

static bool luna_sema_expression_can_branch(const LunaExpression *expression) {
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case LUNA_EXPRESSION_CONDITIONAL:
        return true;
    case LUNA_EXPRESSION_BINARY:
        return expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_AND ||
               expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_OR ||
               luna_sema_expression_can_branch(expression->as.binary.left) ||
               luna_sema_expression_can_branch(expression->as.binary.right);
    case LUNA_EXPRESSION_UNARY:
        return luna_sema_expression_can_branch(expression->as.unary.operand);
    case LUNA_EXPRESSION_INDEX:
        return luna_sema_expression_can_branch(expression->as.index.base) ||
               luna_sema_expression_can_branch(expression->as.index.index);
    case LUNA_EXPRESSION_MEMBER:
        return luna_sema_expression_can_branch(expression->as.member.base);
    case LUNA_EXPRESSION_CALL:
        for (const LunaExpression *argument =
                 expression->as.call.first_argument;
             argument != NULL; argument = argument->next) {
            if (luna_sema_expression_can_branch(argument)) {
                return true;
            }
        }
        return false;
    case LUNA_EXPRESSION_CAST:
        return luna_sema_expression_can_branch(expression->as.cast.operand);
    case LUNA_EXPRESSION_AGGREGATE_INITIALIZER:
        for (const LunaInitializerField *field =
                 expression->as.aggregate_initializer.first_field;
             field != NULL; field = field->next) {
            if (luna_sema_expression_can_branch(field->value)) {
                return true;
            }
        }
        return false;
    case LUNA_EXPRESSION_INTEGER:
    case LUNA_EXPRESSION_FLOAT:
    case LUNA_EXPRESSION_BOOLEAN:
    case LUNA_EXPRESSION_STRING:
    case LUNA_EXPRESSION_NULL:
    case LUNA_EXPRESSION_ZERO_INITIALIZER:
    case LUNA_EXPRESSION_NAME:
    case LUNA_EXPRESSION_SIZEOF:
    case LUNA_EXPRESSION_ALIGNOF:
    case LUNA_EXPRESSION_OFFSETOF:
        return false;
    }

    return false;
}

static LunaCheckedLvalue luna_sema_invalid_lvalue(void) {
    return (LunaCheckedLvalue){
        .type = LUNA_TYPE_INVALID,
        .storage = LUNA_SEMA_LVALUE_INVALID,
        .slot = LUNA_IR_INVALID_ID,
        .address = LUNA_IR_INVALID_ID,
    };
}

static LunaCheckedLvalue
luna_sema_lower_memory_assignment_value(LunaSemaContext *context,
                                        const LunaExpression *initializer,
                                        LunaSemaTypeId expected_type);
static bool luna_sema_is_lvalue_syntax(const LunaExpression *expression);

static bool luna_sema_emit_null_check(LunaSemaContext *context,
                                      LunaIrValueId address,
                                      LunaSourceSpan span) {
    LunaIrInstruction check = luna_sema_instruction(LUNA_IR_NULL_CHECK, span);
    check.left = address;
    return luna_sema_append_instruction(context, &check);
}

static LunaIrValueId luna_sema_lvalue_address(LunaSemaContext *context,
                                              const LunaCheckedLvalue *lvalue,
                                              LunaSourceSpan span) {
    if (lvalue->storage == LUNA_SEMA_LVALUE_ADDRESS) {
        return lvalue->address;
    }
    if (lvalue->storage != LUNA_SEMA_LVALUE_SLOT) {
        return LUNA_IR_INVALID_ID;
    }

    LunaIrInstruction address =
        luna_sema_instruction(LUNA_IR_ADDRESS_OF_SLOT, span);
    address.slot = lvalue->slot;
    const LunaSemaTypeId pointer_type =
        luna_sema_pointer_type(context, lvalue->type, !lvalue->is_mutable);
    return luna_sema_emit_value_instruction(context, &address, pointer_type);
}

static LunaCheckedLvalue
luna_sema_lower_lvalue(LunaSemaContext *context,
                       const LunaExpression *expression) {
    if (expression == NULL || !context->reachable) {
        return luna_sema_invalid_lvalue();
    }

    if (expression->kind == LUNA_EXPRESSION_NAME) {
        const LunaSemaLocal *local =
            luna_sema_find_local(context, expression->as.name);
        if (local == NULL) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "unknown local variable '%.*s'",
                                  (int)expression->as.name.length,
                                  expression->as.name.data);
            return luna_sema_invalid_lvalue();
        }
        return (LunaCheckedLvalue){
            .type = local->type,
            .storage = LUNA_SEMA_LVALUE_SLOT,
            .slot = local->slot,
            .address = LUNA_IR_INVALID_ID,
            .is_mutable = local->is_mutable,
        };
    }

    if (expression->kind == LUNA_EXPRESSION_UNARY &&
        expression->as.unary.operator_kind == LUNA_TOKEN_STAR) {
        LunaCheckedValue pointer =
            luna_sema_lower_expression(context, expression->as.unary.operand);
        const LunaSemaType *pointer_type =
            luna_sema_type(context, pointer.type);
        if (pointer_type == NULL || pointer_type->kind != LUNA_TYPE_POINTER) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "dereference requires a pointer operand");
            return luna_sema_invalid_lvalue();
        }
        if (luna_sema_type_kind(context, pointer_type->element_type) ==
            LUNA_TYPE_VOID) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "cannot dereference a pointer to void");
            return luna_sema_invalid_lvalue();
        }
        return (LunaCheckedLvalue){
            .type = pointer_type->element_type,
            .storage = LUNA_SEMA_LVALUE_ADDRESS,
            .slot = LUNA_IR_INVALID_ID,
            .address = pointer.id,
            .is_mutable = !pointer_type->is_read_only,
            .requires_null_check = true,
        };
    }

    if (expression->kind == LUNA_EXPRESSION_INDEX) {
        const LunaSemaTypeId known_base_type =
            luna_sema_known_expression_type(context, expression->as.index.base);
        const LunaSemaType *known_base =
            luna_sema_type(context, known_base_type);
        LunaIrValueId base_address = LUNA_IR_INVALID_ID;
        LunaSemaTypeId element_type = LUNA_TYPE_INVALID;
        bool is_mutable = false;

        if (known_base != NULL && known_base->kind == LUNA_TYPE_ARRAY) {
            LunaCheckedLvalue base =
                luna_sema_is_lvalue_syntax(expression->as.index.base)
                    ? luna_sema_lower_lvalue(context, expression->as.index.base)
                    : luna_sema_lower_memory_assignment_value(
                          context, expression->as.index.base, known_base_type);
            const LunaSemaType *array_type = luna_sema_type(context, base.type);
            if (array_type == NULL || array_type->kind != LUNA_TYPE_ARRAY) {
                return luna_sema_invalid_lvalue();
            }
            element_type = array_type->element_type;
            const uint64_t array_count = array_type->array_count;
            base_address =
                luna_sema_lvalue_address(context, &base, expression->span);
            is_mutable = base.is_mutable;

            LunaIrSlotId preserved_base = LUNA_IR_INVALID_ID;
            if (luna_sema_expression_can_branch(expression->as.index.index)) {
                const LunaSemaTypeId base_pointer_type = luna_sema_pointer_type(
                    context, base.type, !base.is_mutable);
                preserved_base =
                    luna_sema_preserve_value(context,
                                             (LunaCheckedValue){
                                                 .id = base_address,
                                                 .type = base_pointer_type,
                                             },
                                             expression->as.index.base->span);
                if (preserved_base == LUNA_IR_INVALID_ID) {
                    return luna_sema_invalid_lvalue();
                }
            }
            LunaCheckedValue index = luna_sema_lower_expression_expected(
                context, expression->as.index.index, LUNA_TYPE_USIZE);
            if (!luna_sema_require_type(context, index, LUNA_TYPE_USIZE,
                                        expression->as.index.index->span)) {
                return luna_sema_invalid_lvalue();
            }
            if (preserved_base != LUNA_IR_INVALID_ID) {
                const LunaSemaTypeId base_pointer_type = luna_sema_pointer_type(
                    context, base.type, !base.is_mutable);
                base_address = luna_sema_reload_value(
                                   context, preserved_base, base_pointer_type,
                                   expression->as.index.base->span)
                                   .id;
            }
            if (base.requires_null_check &&
                !luna_sema_emit_null_check(context, base_address,
                                           expression->as.index.base->span)) {
                return luna_sema_invalid_lvalue();
            }
            LunaIrInstruction bounds =
                luna_sema_instruction(LUNA_IR_BOUNDS_CHECK, expression->span);
            bounds.left = index.id;
            bounds.immediate = array_count;
            if (!luna_sema_append_instruction(context, &bounds)) {
                return luna_sema_invalid_lvalue();
            }

            uint64_t element_size = 0U;
            uint32_t element_alignment = 0U;
            if (!luna_sema_type_layout(context, element_type, &element_size,
                                       &element_alignment) ||
                element_alignment == 0U) {
                return luna_sema_invalid_lvalue();
            }
            LunaIrInstruction offset =
                luna_sema_instruction(LUNA_IR_POINTER_OFFSET, expression->span);
            offset.left = base_address;
            offset.right = index.id;
            offset.immediate = element_size;
            const LunaSemaTypeId pointer_result =
                luna_sema_pointer_type(context, element_type, !is_mutable);
            const LunaIrValueId address = luna_sema_emit_value_instruction(
                context, &offset, pointer_result);
            return (LunaCheckedLvalue){
                .type = element_type,
                .storage = LUNA_SEMA_LVALUE_ADDRESS,
                .slot = LUNA_IR_INVALID_ID,
                .address = address,
                .is_mutable = is_mutable,
            };
        }

        LunaCheckedValue pointer =
            luna_sema_lower_expression(context, expression->as.index.base);
        const LunaSemaType *pointer_type =
            luna_sema_type(context, pointer.type);
        if (pointer_type == NULL || pointer_type->kind != LUNA_TYPE_POINTER) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "indexing requires an array or pointer");
            return luna_sema_invalid_lvalue();
        }
        element_type = pointer_type->element_type;
        const bool pointer_is_read_only = pointer_type->is_read_only;
        if (luna_sema_type_kind(context, element_type) == LUNA_TYPE_VOID) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "cannot index a pointer to void");
            return luna_sema_invalid_lvalue();
        }
        LunaIrSlotId preserved_pointer = LUNA_IR_INVALID_ID;
        if (luna_sema_expression_can_branch(expression->as.index.index)) {
            preserved_pointer = luna_sema_preserve_value(
                context, pointer, expression->as.index.base->span);
            if (preserved_pointer == LUNA_IR_INVALID_ID) {
                return luna_sema_invalid_lvalue();
            }
        }
        LunaCheckedValue index = luna_sema_lower_expression_expected(
            context, expression->as.index.index, LUNA_TYPE_USIZE);
        if (!luna_sema_require_type(context, index, LUNA_TYPE_USIZE,
                                    expression->as.index.index->span)) {
            return luna_sema_invalid_lvalue();
        }
        if (preserved_pointer != LUNA_IR_INVALID_ID) {
            pointer =
                luna_sema_reload_value(context, preserved_pointer, pointer.type,
                                       expression->as.index.base->span);
        }
        if (!luna_sema_emit_null_check(context, pointer.id,
                                       expression->as.index.base->span)) {
            return luna_sema_invalid_lvalue();
        }
        uint64_t element_size = 0U;
        uint32_t element_alignment = 0U;
        if (!luna_sema_type_layout(context, element_type, &element_size,
                                   &element_alignment) ||
            element_alignment == 0U) {
            return luna_sema_invalid_lvalue();
        }
        LunaIrInstruction offset =
            luna_sema_instruction(LUNA_IR_POINTER_OFFSET, expression->span);
        offset.left = pointer.id;
        offset.right = index.id;
        offset.immediate = element_size;
        const LunaSemaTypeId pointer_result =
            luna_sema_pointer_type(context, element_type, pointer_is_read_only);
        const LunaIrValueId address =
            luna_sema_emit_value_instruction(context, &offset, pointer_result);
        return (LunaCheckedLvalue){
            .type = element_type,
            .storage = LUNA_SEMA_LVALUE_ADDRESS,
            .slot = LUNA_IR_INVALID_ID,
            .address = address,
            .is_mutable = !pointer_is_read_only,
        };
    }

    if (expression->kind == LUNA_EXPRESSION_MEMBER) {
        LunaSemaTypeId aggregate_type = LUNA_TYPE_INVALID;
        LunaIrValueId base_address = LUNA_IR_INVALID_ID;
        bool is_mutable = false;

        if (expression->as.member.operator_kind == LUNA_TOKEN_DOT) {
            const LunaSemaTypeId known_base_type =
                luna_sema_known_expression_type(context,
                                                expression->as.member.base);
            LunaCheckedLvalue base =
                luna_sema_is_lvalue_syntax(expression->as.member.base)
                    ? luna_sema_lower_lvalue(context,
                                             expression->as.member.base)
                    : luna_sema_lower_memory_assignment_value(
                          context, expression->as.member.base, known_base_type);
            if (base.storage == LUNA_SEMA_LVALUE_INVALID) {
                return luna_sema_invalid_lvalue();
            }
            aggregate_type = base.type;
            if (!luna_sema_is_record_type(context, aggregate_type)) {
                luna_diagnostic_error(
                    context->diagnostics, expression->as.member.base->span,
                    "'.' member access requires a struct or union lvalue");
                return luna_sema_invalid_lvalue();
            }
            base_address =
                luna_sema_lvalue_address(context, &base, expression->span);
            if (base.requires_null_check &&
                !luna_sema_emit_null_check(context, base_address,
                                           expression->as.member.base->span)) {
                return luna_sema_invalid_lvalue();
            }
            is_mutable = base.is_mutable;
        } else if (expression->as.member.operator_kind == LUNA_TOKEN_ARROW) {
            const LunaCheckedValue pointer =
                luna_sema_lower_expression(context, expression->as.member.base);
            const LunaSemaType *pointer_type =
                luna_sema_type(context, pointer.type);
            if (pointer_type == NULL ||
                pointer_type->kind != LUNA_TYPE_POINTER) {
                luna_diagnostic_error(context->diagnostics,
                                      expression->as.member.base->span,
                                      "'->' member access requires a pointer");
                return luna_sema_invalid_lvalue();
            }
            aggregate_type = pointer_type->element_type;
            is_mutable = !pointer_type->is_read_only;
            if (!luna_sema_is_record_type(context, aggregate_type)) {
                luna_diagnostic_error(
                    context->diagnostics, expression->as.member.base->span,
                    "'->' member access requires a pointer to struct or union");
                return luna_sema_invalid_lvalue();
            }
            base_address = pointer.id;
            if (!luna_sema_emit_null_check(context, base_address,
                                           expression->as.member.base->span)) {
                return luna_sema_invalid_lvalue();
            }
        } else {
            return luna_sema_invalid_lvalue();
        }

        const LunaSemaField *field = luna_sema_find_field(
            context, aggregate_type, expression->as.member.name);
        if (field == NULL) {
            const LunaSemaType *aggregate =
                luna_sema_type(context, aggregate_type);
            const LunaStringView aggregate_name =
                aggregate != NULL && aggregate->declaration != NULL
                    ? aggregate->declaration->name
                    : (LunaStringView){0};
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "type '%.*s' has no field named '%.*s'",
                                  (int)aggregate_name.length,
                                  aggregate_name.data,
                                  (int)expression->as.member.name.length,
                                  expression->as.member.name.data);
            return luna_sema_invalid_lvalue();
        }
        const LunaSemaTypeId field_type = field->type;
        const uint64_t field_offset = field->offset;
        LunaIrInstruction address =
            luna_sema_instruction(LUNA_IR_MEMBER_ADDRESS, expression->span);
        address.left = base_address;
        address.immediate = field_offset;
        const LunaSemaTypeId pointer_result =
            luna_sema_pointer_type(context, field_type, !is_mutable);
        const LunaIrValueId result =
            luna_sema_emit_value_instruction(context, &address, pointer_result);
        return (LunaCheckedLvalue){
            .type = field_type,
            .storage = LUNA_SEMA_LVALUE_ADDRESS,
            .slot = LUNA_IR_INVALID_ID,
            .address = result,
            .is_mutable = is_mutable,
        };
    }

    luna_diagnostic_error(context->diagnostics, expression->span,
                          "expression is not an assignable lvalue");
    return luna_sema_invalid_lvalue();
}

static LunaCheckedValue luna_sema_load_lvalue(LunaSemaContext *context,
                                              const LunaCheckedLvalue *lvalue,
                                              LunaSourceSpan span) {
    if (lvalue->storage == LUNA_SEMA_LVALUE_INVALID) {
        return luna_sema_invalid_value();
    }
    if (luna_sema_is_memory_type(context, lvalue->type)) {
        luna_diagnostic_error(context->diagnostics, span, "%s",
                              luna_sema_is_array_type(context, lvalue->type)
                                  ? "fixed arrays are not scalar values"
                                  : "aggregate objects are not scalar values");
        return luna_sema_invalid_value();
    }

    LunaIrInstruction load = luna_sema_instruction(
        lvalue->storage == LUNA_SEMA_LVALUE_SLOT ? LUNA_IR_LOAD
                                                 : LUNA_IR_LOAD_INDIRECT,
        span);
    if (lvalue->storage == LUNA_SEMA_LVALUE_SLOT) {
        load.slot = lvalue->slot;
    } else {
        if (lvalue->requires_null_check &&
            !luna_sema_emit_null_check(context, lvalue->address, span)) {
            return luna_sema_invalid_value();
        }
        load.left = lvalue->address;
    }
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, lvalue->type);
    return (LunaCheckedValue){
        .id = result,
        .type = lvalue->type,
    };
}

static bool luna_sema_store_lvalue(LunaSemaContext *context,
                                   const LunaCheckedLvalue *lvalue,
                                   LunaCheckedValue value,
                                   LunaSourceSpan span) {
    if (lvalue->storage == LUNA_SEMA_LVALUE_INVALID) {
        return false;
    }
    if (!lvalue->is_mutable) {
        luna_diagnostic_error(context->diagnostics, span,
                              "cannot assign through an immutable lvalue");
        return false;
    }
    if (luna_sema_is_memory_type(context, lvalue->type)) {
        luna_diagnostic_error(
            context->diagnostics, span, "%s",
            luna_sema_is_array_type(context, lvalue->type)
                ? "fixed arrays cannot be assigned as a whole"
                : "aggregate objects cannot be assigned as a whole");
        return false;
    }
    if (lvalue->requires_null_check &&
        !luna_sema_emit_null_check(context, lvalue->address, span)) {
        return false;
    }

    LunaIrInstruction store = luna_sema_instruction(
        lvalue->storage == LUNA_SEMA_LVALUE_SLOT ? LUNA_IR_STORE
                                                 : LUNA_IR_STORE_INDIRECT,
        span);
    if (lvalue->storage == LUNA_SEMA_LVALUE_SLOT) {
        store.slot = lvalue->slot;
        store.left = value.id;
    } else {
        store.memory_type = luna_sema_ir_type(context, lvalue->type);
        store.left = lvalue->address;
        store.right = value.id;
    }
    return luna_sema_append_instruction(context, &store);
}

static bool luna_sema_is_brace_initializer(const LunaExpression *expression) {
    return expression != NULL &&
           (expression->kind == LUNA_EXPRESSION_ZERO_INITIALIZER ||
            expression->kind == LUNA_EXPRESSION_AGGREGATE_INITIALIZER);
}

static bool luna_sema_is_lvalue_syntax(const LunaExpression *expression) {
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == LUNA_EXPRESSION_NAME ||
        expression->kind == LUNA_EXPRESSION_INDEX ||
        expression->kind == LUNA_EXPRESSION_MEMBER) {
        return true;
    }
    return expression->kind == LUNA_EXPRESSION_UNARY &&
           expression->as.unary.operator_kind == LUNA_TOKEN_STAR;
}

static LunaIrSlotId luna_sema_allocate_memory_slot(LunaSemaContext *context,
                                                   LunaSemaTypeId type,
                                                   LunaSourceSpan span) {
    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    if (!luna_sema_type_layout(context, type, &size_bytes, &alignment_bytes)) {
        luna_diagnostic_error(
            context->diagnostics, span, "%s",
            luna_sema_is_array_type(context, type)
                ? "fixed array has no valid target layout"
                : "aggregate object has no valid target layout");
        return LUNA_IR_INVALID_ID;
    }

    const LunaIrSlotId slot = luna_ir_function_add_memory_slot(
        context->current_function, size_bytes, alignment_bytes);
    if (slot == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
    }
    return slot;
}

static LunaCheckedLvalue
luna_sema_slot_field_lvalue(LunaSemaContext *context, LunaIrSlotId slot,
                            LunaSemaTypeId root_type, LunaSemaTypeId field_type,
                            uint64_t offset, LunaSourceSpan span) {
    LunaIrInstruction root_address =
        luna_sema_instruction(LUNA_IR_ADDRESS_OF_SLOT, span);
    root_address.slot = slot;
    const LunaSemaTypeId root_pointer_type =
        luna_sema_pointer_type(context, root_type, false);
    if (root_pointer_type == LUNA_TYPE_INVALID) {
        return luna_sema_invalid_lvalue();
    }
    const LunaIrValueId root = luna_sema_emit_value_instruction(
        context, &root_address, root_pointer_type);
    if (root == LUNA_IR_INVALID_ID) {
        return luna_sema_invalid_lvalue();
    }

    LunaIrInstruction field_address =
        luna_sema_instruction(LUNA_IR_MEMBER_ADDRESS, span);
    field_address.left = root;
    field_address.immediate = offset;
    const LunaSemaTypeId field_pointer_type =
        luna_sema_pointer_type(context, field_type, false);
    if (field_pointer_type == LUNA_TYPE_INVALID) {
        return luna_sema_invalid_lvalue();
    }
    const LunaIrValueId field = luna_sema_emit_value_instruction(
        context, &field_address, field_pointer_type);
    if (field == LUNA_IR_INVALID_ID) {
        return luna_sema_invalid_lvalue();
    }

    return (LunaCheckedLvalue){
        .type = field_type,
        .storage = LUNA_SEMA_LVALUE_ADDRESS,
        .slot = LUNA_IR_INVALID_ID,
        .address = field,
        .is_mutable = true,
    };
}

static bool luna_sema_emit_memory_copy(LunaSemaContext *context,
                                       const LunaCheckedLvalue *destination,
                                       const LunaCheckedLvalue *source,
                                       LunaSourceSpan span) {
    if (destination->type != source->type ||
        !luna_sema_is_memory_type(context, destination->type)) {
        luna_diagnostic_error(
            context->diagnostics, span,
            "memory copy requires lvalues of the exact same memory type");
        return false;
    }

    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    if (!luna_sema_type_layout(context, destination->type, &size_bytes,
                               &alignment_bytes) ||
        alignment_bytes == 0U) {
        luna_diagnostic_error(context->diagnostics, span,
                              "memory copy requires a valid object layout");
        return false;
    }

    const LunaIrValueId destination_address =
        luna_sema_lvalue_address(context, destination, span);
    const LunaIrValueId source_address =
        luna_sema_lvalue_address(context, source, span);
    if (destination_address == LUNA_IR_INVALID_ID ||
        source_address == LUNA_IR_INVALID_ID) {
        return false;
    }
    if (destination->requires_null_check &&
        !luna_sema_emit_null_check(context, destination_address, span)) {
        return false;
    }
    if (source->requires_null_check &&
        !luna_sema_emit_null_check(context, source_address, span)) {
        return false;
    }

    LunaIrInstruction copy = luna_sema_instruction(LUNA_IR_MEMORY_COPY, span);
    copy.left = destination_address;
    copy.right = source_address;
    copy.immediate = size_bytes;
    return luna_sema_append_instruction(context, &copy);
}

static LunaCheckedLvalue
luna_sema_lower_memory_source(LunaSemaContext *context,
                              const LunaExpression *expression,
                              LunaSemaTypeId expected_type) {
    if (expression->kind == LUNA_EXPRESSION_CALL ||
        expression->kind == LUNA_EXPRESSION_CONDITIONAL) {
        const LunaCheckedValue value = luna_sema_lower_expression_expected(
            context, expression, expected_type);
        if (!luna_sema_require_type(context, value, expected_type,
                                    expression->span)) {
            return luna_sema_invalid_lvalue();
        }
        return (LunaCheckedLvalue){
            .type = expected_type,
            .storage = LUNA_SEMA_LVALUE_ADDRESS,
            .slot = LUNA_IR_INVALID_ID,
            .address = value.id,
            .is_mutable = false,
        };
    }
    if (!luna_sema_is_lvalue_syntax(expression)) {
        luna_diagnostic_error(
            context->diagnostics, expression->span, "%s",
            luna_sema_is_array_type(context, expected_type)
                ? "fixed-array initialization requires '{}' or an lvalue of "
                  "the exact same array type"
                : "aggregate initialization requires braces or an lvalue of "
                  "the exact same aggregate type");
        return luna_sema_invalid_lvalue();
    }

    LunaCheckedLvalue source = luna_sema_lower_lvalue(context, expression);
    if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
        return source;
    }
    if (!luna_sema_require_type(context,
                                (LunaCheckedValue){
                                    .id = LUNA_IR_INVALID_ID,
                                    .type = source.type,
                                },
                                expected_type, expression->span)) {
        return luna_sema_invalid_lvalue();
    }
    return source;
}

static bool luna_sema_initialize_slot_value(LunaSemaContext *context,
                                            LunaIrSlotId slot,
                                            LunaSemaTypeId root_type,
                                            LunaSemaTypeId value_type,
                                            uint64_t offset,
                                            const LunaExpression *initializer);

static bool
luna_sema_validate_aggregate_initializer(LunaSemaContext *context,
                                         LunaSemaTypeId type,
                                         const LunaExpression *initializer) {
    if (!luna_sema_is_record_type(context, type)) {
        luna_diagnostic_error(
            context->diagnostics, initializer->span,
            "named aggregate initializer requires a struct or union context");
        return false;
    }

    const LunaSemaType *aggregate = luna_sema_type(context, type);
    uint32_t field_index = 0U;
    for (const LunaInitializerField *field =
             initializer->as.aggregate_initializer.first_field;
         field != NULL; field = field->next) {
        for (const LunaInitializerField *previous =
                 initializer->as.aggregate_initializer.first_field;
             previous != field; previous = previous->next) {
            if (luna_string_view_equal(previous->name, field->name)) {
                luna_diagnostic_error(context->diagnostics, field->span,
                                      "duplicate initializer field '%.*s'",
                                      (int)field->name.length,
                                      field->name.data);
                return false;
            }
        }

        if (luna_sema_find_field(context, type, field->name) == NULL) {
            const LunaStringView type_name =
                aggregate != NULL && aggregate->declaration != NULL
                    ? aggregate->declaration->name
                    : (LunaStringView){0};
            luna_diagnostic_error(context->diagnostics, field->span,
                                  "type '%.*s' has no field named '%.*s'",
                                  (int)type_name.length, type_name.data,
                                  (int)field->name.length, field->name.data);
            return false;
        }
        if (aggregate != NULL && aggregate->kind == LUNA_TYPE_UNION &&
            field_index != 0U) {
            luna_diagnostic_error(
                context->diagnostics, field->span,
                "union initializer may name at most one field");
            return false;
        }
        field_index += 1U;
    }
    return true;
}

static bool luna_sema_initialize_aggregate_fields(
    LunaSemaContext *context, LunaIrSlotId slot, LunaSemaTypeId root_type,
    LunaSemaTypeId aggregate_type, uint64_t aggregate_offset,
    const LunaExpression *initializer) {
    if (!luna_sema_validate_aggregate_initializer(context, aggregate_type,
                                                  initializer)) {
        return false;
    }

    for (const LunaInitializerField *field_initializer =
             initializer->as.aggregate_initializer.first_field;
         field_initializer != NULL;
         field_initializer = field_initializer->next) {
        const LunaSemaField *field = luna_sema_find_field(
            context, aggregate_type, field_initializer->name);
        if (field == NULL || field->offset > (uint64_t)INT32_MAX ||
            aggregate_offset > (uint64_t)INT32_MAX - field->offset) {
            luna_diagnostic_error(
                context->diagnostics, field_initializer->span,
                "aggregate initializer field offset exceeds the supported "
                "object-size range");
            return false;
        }
        if (!luna_sema_initialize_slot_value(
                context, slot, root_type, field->type,
                aggregate_offset + field->offset, field_initializer->value)) {
            return false;
        }
    }
    return true;
}

static bool luna_sema_initialize_slot_value(LunaSemaContext *context,
                                            LunaIrSlotId slot,
                                            LunaSemaTypeId root_type,
                                            LunaSemaTypeId value_type,
                                            uint64_t offset,
                                            const LunaExpression *initializer) {
    if (luna_sema_is_memory_type(context, value_type)) {
        if (initializer->kind == LUNA_EXPRESSION_ZERO_INITIALIZER) {
            return true;
        }
        if (initializer->kind == LUNA_EXPRESSION_AGGREGATE_INITIALIZER) {
            return luna_sema_initialize_aggregate_fields(
                context, slot, root_type, value_type, offset, initializer);
        }

        const LunaCheckedLvalue source =
            luna_sema_lower_memory_source(context, initializer, value_type);
        if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
            return false;
        }
        const LunaCheckedLvalue destination = luna_sema_slot_field_lvalue(
            context, slot, root_type, value_type, offset, initializer->span);
        return destination.storage != LUNA_SEMA_LVALUE_INVALID &&
               luna_sema_emit_memory_copy(context, &destination, &source,
                                          initializer->span);
    }

    if (initializer->kind == LUNA_EXPRESSION_AGGREGATE_INITIALIZER) {
        luna_diagnostic_error(
            context->diagnostics, initializer->span,
            "named aggregate initializer requires a struct or union context");
        return false;
    }

    LunaCheckedValue value =
        luna_sema_lower_expression_expected(context, initializer, value_type);
    if (!luna_sema_require_type(context, value, value_type,
                                initializer->span)) {
        return false;
    }
    const LunaCheckedLvalue destination = luna_sema_slot_field_lvalue(
        context, slot, root_type, value_type, offset, initializer->span);
    return destination.storage != LUNA_SEMA_LVALUE_INVALID &&
           luna_sema_store_lvalue(context, &destination, value,
                                  initializer->span);
}

static bool luna_sema_zero_memory_slot(LunaSemaContext *context,
                                       LunaIrSlotId slot, LunaSourceSpan span) {
    LunaIrInstruction zero = luna_sema_instruction(LUNA_IR_ZERO_SLOT, span);
    zero.slot = slot;
    return luna_sema_append_instruction(context, &zero);
}

static LunaCheckedLvalue
luna_sema_lower_memory_assignment_value(LunaSemaContext *context,
                                        const LunaExpression *initializer,
                                        LunaSemaTypeId expected_type) {
    if (!luna_sema_is_brace_initializer(initializer)) {
        return luna_sema_lower_memory_source(context, initializer,
                                             expected_type);
    }

    const LunaIrSlotId slot = luna_sema_allocate_memory_slot(
        context, expected_type, initializer->span);
    if (slot == LUNA_IR_INVALID_ID ||
        !luna_sema_zero_memory_slot(context, slot, initializer->span) ||
        !luna_sema_initialize_slot_value(context, slot, expected_type,
                                         expected_type, 0U, initializer)) {
        return luna_sema_invalid_lvalue();
    }
    return (LunaCheckedLvalue){
        .type = expected_type,
        .storage = LUNA_SEMA_LVALUE_SLOT,
        .slot = slot,
        .address = LUNA_IR_INVALID_ID,
        .is_mutable = false,
    };
}

static LunaCheckedValue
luna_sema_lower_logical(LunaSemaContext *context,
                        const LunaExpression *expression) {
    const bool is_and =
        expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_AND;

    LunaCheckedValue left = luna_sema_lower_expression_expected(
        context, expression->as.binary.left, LUNA_TYPE_BOOL);
    if (!luna_sema_require_type(context, left, LUNA_TYPE_BOOL,
                                expression->as.binary.left->span)) {
        return luna_sema_invalid_value();
    }

    const LunaIrSlotId result_slot =
        luna_ir_function_add_slot(context->current_function, LUNA_IR_TYPE_BOOL);
    const LunaIrBlockId right_block = luna_sema_add_block(context);
    const LunaIrBlockId short_block = luna_sema_add_block(context);
    const LunaIrBlockId merge_block = luna_sema_add_block(context);

    if (result_slot == LUNA_IR_INVALID_ID ||
        right_block == LUNA_IR_INVALID_ID ||
        short_block == LUNA_IR_INVALID_ID ||
        merge_block == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return luna_sema_invalid_value();
    }

    if (is_and) {
        (void)luna_sema_emit_branch(context, left.id, right_block, short_block,
                                    expression->span);
    } else {
        (void)luna_sema_emit_branch(context, left.id, short_block, right_block,
                                    expression->span);
    }

    luna_sema_set_block(context, short_block);
    LunaIrInstruction short_value =
        luna_sema_instruction(LUNA_IR_CONST_BOOL, expression->span);
    short_value.immediate = is_and ? 0 : 1;
    const LunaIrValueId short_id =
        luna_sema_emit_value_instruction(context, &short_value, LUNA_TYPE_BOOL);

    LunaIrInstruction short_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    short_store.slot = result_slot;
    short_store.left = short_id;
    (void)luna_sema_append_instruction(context, &short_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, right_block);
    LunaCheckedValue right = luna_sema_lower_expression_expected(
        context, expression->as.binary.right, LUNA_TYPE_BOOL);
    if (!luna_sema_require_type(context, right, LUNA_TYPE_BOOL,
                                expression->as.binary.right->span)) {
        return luna_sema_invalid_value();
    }

    LunaIrInstruction right_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    right_store.slot = result_slot;
    right_store.left = right.id;
    (void)luna_sema_append_instruction(context, &right_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, merge_block);
    LunaIrInstruction load =
        luna_sema_instruction(LUNA_IR_LOAD, expression->span);
    load.slot = result_slot;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, LUNA_TYPE_BOOL);

    return (LunaCheckedValue){
        .id = result,
        .type = LUNA_TYPE_BOOL,
    };
}

static LunaSemaTypeId
luna_sema_conditional_result_type(LunaSemaContext *context,
                                  const LunaExpression *expression,
                                  LunaSemaTypeId expected_type) {
    if (expected_type == LUNA_TYPE_BOOL ||
        luna_sema_is_numeric_type(expected_type) ||
        luna_sema_is_enum_type(context, expected_type) ||
        luna_sema_is_pointer_type(context, expected_type) ||
        luna_sema_is_memory_type(context, expected_type)) {
        return expected_type;
    }

    const LunaSemaTypeId then_type = luna_sema_known_expression_type(
        context, expression->as.conditional.then_expression);
    const LunaSemaTypeId else_type = luna_sema_known_expression_type(
        context, expression->as.conditional.else_expression);
    if (then_type != LUNA_TYPE_INVALID) {
        return then_type;
    }
    if (else_type != LUNA_TYPE_INVALID) {
        return else_type;
    }

    const LunaSemaTypeId then_default = luna_sema_default_literal_type(
        expression->as.conditional.then_expression);
    if (then_default != LUNA_TYPE_INVALID) {
        return then_default;
    }
    const LunaSemaTypeId else_default = luna_sema_default_literal_type(
        expression->as.conditional.else_expression);
    return else_default != LUNA_TYPE_INVALID ? else_default : LUNA_TYPE_I32;
}

static LunaCheckedValue
luna_sema_lower_conditional(LunaSemaContext *context,
                            const LunaExpression *expression,
                            LunaSemaTypeId expected_type) {
    LunaCheckedValue condition = luna_sema_lower_expression_expected(
        context, expression->as.conditional.condition, LUNA_TYPE_BOOL);
    if (!luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                                expression->as.conditional.condition->span)) {
        return luna_sema_invalid_value();
    }

    const LunaSemaTypeId result_type =
        luna_sema_conditional_result_type(context, expression, expected_type);
    if (luna_sema_is_memory_type(context, result_type)) {
        const LunaIrSlotId result_slot = luna_sema_allocate_memory_slot(
            context, result_type, expression->span);
        const LunaIrBlockId then_block = luna_sema_add_block(context);
        const LunaIrBlockId else_block = luna_sema_add_block(context);
        const LunaIrBlockId merge_block = luna_sema_add_block(context);
        if (result_slot == LUNA_IR_INVALID_ID ||
            then_block == LUNA_IR_INVALID_ID ||
            else_block == LUNA_IR_INVALID_ID ||
            merge_block == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            return luna_sema_invalid_value();
        }

        (void)luna_sema_emit_branch(context, condition.id, then_block,
                                    else_block, expression->span);
        const LunaCheckedLvalue destination = {
            .type = result_type,
            .storage = LUNA_SEMA_LVALUE_SLOT,
            .slot = result_slot,
            .address = LUNA_IR_INVALID_ID,
            .is_mutable = true,
        };

        luna_sema_set_block(context, then_block);
        const LunaCheckedLvalue then_value =
            luna_sema_lower_memory_assignment_value(
                context, expression->as.conditional.then_expression,
                result_type);
        if (then_value.storage == LUNA_SEMA_LVALUE_INVALID ||
            !luna_sema_emit_memory_copy(
                context, &destination, &then_value,
                expression->as.conditional.then_expression->span) ||
            !luna_sema_emit_jump(context, merge_block, expression->span)) {
            return luna_sema_invalid_value();
        }

        luna_sema_set_block(context, else_block);
        const LunaCheckedLvalue else_value =
            luna_sema_lower_memory_assignment_value(
                context, expression->as.conditional.else_expression,
                result_type);
        if (else_value.storage == LUNA_SEMA_LVALUE_INVALID ||
            !luna_sema_emit_memory_copy(
                context, &destination, &else_value,
                expression->as.conditional.else_expression->span) ||
            !luna_sema_emit_jump(context, merge_block, expression->span)) {
            return luna_sema_invalid_value();
        }

        luna_sema_set_block(context, merge_block);
        const LunaIrValueId result =
            luna_sema_lvalue_address(context, &destination, expression->span);
        return (LunaCheckedValue){
            .id = result,
            .type = result_type,
        };
    }
    if (result_type != LUNA_TYPE_BOOL &&
        !luna_sema_is_numeric_type(result_type) &&
        !luna_sema_is_enum_type(context, result_type) &&
        !luna_sema_is_pointer_type(context, result_type)) {
        luna_diagnostic_error(
            context->diagnostics, expression->span,
            "conditional operands require the same non-void scalar type");
        return luna_sema_invalid_value();
    }

    const LunaIrSlotId result_slot = luna_ir_function_add_slot(
        context->current_function, luna_sema_ir_type(context, result_type));
    const LunaIrBlockId then_block = luna_sema_add_block(context);
    const LunaIrBlockId else_block = luna_sema_add_block(context);
    const LunaIrBlockId merge_block = luna_sema_add_block(context);
    if (result_slot == LUNA_IR_INVALID_ID || then_block == LUNA_IR_INVALID_ID ||
        else_block == LUNA_IR_INVALID_ID || merge_block == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return luna_sema_invalid_value();
    }

    (void)luna_sema_emit_branch(context, condition.id, then_block, else_block,
                                expression->span);

    luna_sema_set_block(context, then_block);
    LunaCheckedValue then_value = luna_sema_lower_expression_expected(
        context, expression->as.conditional.then_expression, result_type);
    if (!luna_sema_require_type(
            context, then_value, result_type,
            expression->as.conditional.then_expression->span)) {
        return luna_sema_invalid_value();
    }
    LunaIrInstruction then_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    then_store.slot = result_slot;
    then_store.left = then_value.id;
    (void)luna_sema_append_instruction(context, &then_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, else_block);
    LunaCheckedValue else_value = luna_sema_lower_expression_expected(
        context, expression->as.conditional.else_expression, result_type);
    if (!luna_sema_require_type(
            context, else_value, result_type,
            expression->as.conditional.else_expression->span)) {
        return luna_sema_invalid_value();
    }
    LunaIrInstruction else_store =
        luna_sema_instruction(LUNA_IR_STORE, expression->span);
    else_store.slot = result_slot;
    else_store.left = else_value.id;
    (void)luna_sema_append_instruction(context, &else_store);
    (void)luna_sema_emit_jump(context, merge_block, expression->span);

    luna_sema_set_block(context, merge_block);
    LunaIrInstruction load =
        luna_sema_instruction(LUNA_IR_LOAD, expression->span);
    load.slot = result_slot;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &load, result_type);
    return (LunaCheckedValue){
        .id = result,
        .type = result_type,
    };
}

static LunaCheckedValue luna_sema_lower_call(LunaSemaContext *context,
                                             const LunaExpression *expression) {
    LunaSemaFunction *callee =
        luna_sema_find_function(context, expression->as.call.name);
    if (callee == NULL) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "unknown function '%.*s'",
                              (int)expression->as.call.name.length,
                              expression->as.call.name.data);
        return luna_sema_invalid_value();
    }

    if (expression->as.call.argument_count != callee->parameter_types.length) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "function '%.*s' expects %u arguments, found %u",
                              (int)callee->syntax->name.length,
                              callee->syntax->name.data,
                              (uint32_t)callee->parameter_types.length,
                              expression->as.call.argument_count);
        return luna_sema_invalid_value();
    }

    LunaVector arguments;
    luna_vector_init(&arguments, sizeof(LunaSemaCallArgument));

    const LunaExpression *argument = expression->as.call.first_argument;
    size_t parameter_index = 0U;
    while (argument != NULL &&
           parameter_index < callee->parameter_types.length) {
        const LunaSemaTypeId *parameter_type =
            luna_vector_at_const(&callee->parameter_types, parameter_index);
        if (luna_sema_is_memory_type(context, *parameter_type)) {
            const LunaCheckedLvalue source =
                luna_sema_lower_memory_assignment_value(context, argument,
                                                        *parameter_type);
            const LunaIrSlotId snapshot_slot = luna_sema_allocate_memory_slot(
                context, *parameter_type, argument->span);
            const LunaCheckedLvalue snapshot = {
                .type = *parameter_type,
                .storage = LUNA_SEMA_LVALUE_SLOT,
                .slot = snapshot_slot,
                .address = LUNA_IR_INVALID_ID,
                .is_mutable = true,
            };
            if (source.storage == LUNA_SEMA_LVALUE_INVALID ||
                snapshot_slot == LUNA_IR_INVALID_ID ||
                !luna_sema_emit_memory_copy(context, &snapshot, &source,
                                            argument->span)) {
                luna_vector_destroy(&arguments);
                return luna_sema_invalid_value();
            }
            const LunaSemaCallArgument checked_argument = {
                .value = LUNA_IR_INVALID_ID,
                .preserved_slot = snapshot_slot,
                .type = *parameter_type,
                .is_aggregate = true,
            };
            if (!luna_vector_push(&arguments, &checked_argument)) {
                luna_sema_report_allocation_failure(context);
                luna_vector_destroy(&arguments);
                return luna_sema_invalid_value();
            }
            argument = argument->next;
            parameter_index += 1U;
            continue;
        }

        LunaCheckedValue value = luna_sema_lower_expression_expected(
            context, argument, *parameter_type);
        if (!luna_sema_require_type(context, value, *parameter_type,
                                    argument->span)) {
            luna_vector_destroy(&arguments);
            return luna_sema_invalid_value();
        }

        bool later_argument_can_branch = false;
        for (const LunaExpression *later = argument->next; later != NULL;
             later = later->next) {
            if (luna_sema_expression_can_branch(later)) {
                later_argument_can_branch = true;
                break;
            }
        }

        LunaIrSlotId preserved_slot = LUNA_IR_INVALID_ID;
        if (later_argument_can_branch) {
            preserved_slot =
                luna_sema_preserve_value(context, value, argument->span);
            if (preserved_slot == LUNA_IR_INVALID_ID) {
                luna_vector_destroy(&arguments);
                return luna_sema_invalid_value();
            }
        }
        const LunaSemaCallArgument checked_argument = {
            .value = value.id,
            .preserved_slot = preserved_slot,
            .type = value.type,
            .is_aggregate = false,
        };
        if (!luna_vector_push(&arguments, &checked_argument)) {
            luna_sema_report_allocation_failure(context);
            luna_vector_destroy(&arguments);
            return luna_sema_invalid_value();
        }

        argument = argument->next;
        parameter_index += 1U;
    }

    if (arguments.length > UINT32_MAX ||
        context->current_function->arguments.length >
            UINT32_MAX - arguments.length) {
        luna_sema_report_allocation_failure(context);
        luna_vector_destroy(&arguments);
        return luna_sema_invalid_value();
    }

    const uint32_t first_argument =
        (uint32_t)context->current_function->arguments.length;
    for (size_t index = 0U; index < arguments.length; index += 1U) {
        const LunaSemaCallArgument *argument_value =
            luna_vector_at_const(&arguments, index);
        LunaCheckedValue value = {
            .id = argument_value->value,
            .type = argument_value->type,
        };
        if (argument_value->is_aggregate) {
            LunaIrInstruction address = luna_sema_instruction(
                LUNA_IR_ADDRESS_OF_SLOT, expression->span);
            address.slot = argument_value->preserved_slot;
            value.id = luna_sema_emit_value_instruction(context, &address,
                                                        argument_value->type);
        } else if (argument_value->preserved_slot != LUNA_IR_INVALID_ID) {
            value =
                luna_sema_reload_value(context, argument_value->preserved_slot,
                                       argument_value->type, expression->span);
        }
        if (!luna_vector_push(&context->current_function->arguments,
                              &value.id)) {
            luna_sema_report_allocation_failure(context);
            luna_vector_destroy(&arguments);
            return luna_sema_invalid_value();
        }
    }
    luna_vector_destroy(&arguments);

    LunaIrInstruction call =
        luna_sema_instruction(LUNA_IR_CALL, expression->span);
    call.callee = callee->ir_id;
    call.first_argument = first_argument;
    call.argument_count = expression->as.call.argument_count;

    const LunaSemaTypeId return_type = callee->return_type;
    if (return_type == LUNA_TYPE_VOID) {
        if (!luna_sema_append_instruction(context, &call)) {
            return luna_sema_invalid_value();
        }

        return (LunaCheckedValue){
            .id = LUNA_IR_INVALID_ID,
            .type = LUNA_TYPE_VOID,
        };
    }

    if (luna_sema_is_memory_type(context, return_type)) {
        const LunaIrSlotId result_slot = luna_sema_allocate_memory_slot(
            context, return_type, expression->span);
        if (result_slot == LUNA_IR_INVALID_ID) {
            return luna_sema_invalid_value();
        }
        call.slot = result_slot;
    }
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &call, return_type);
    return (LunaCheckedValue){
        .id = result,
        .type = return_type,
    };
}

static LunaCheckedValue
luna_sema_lower_expression_expected(LunaSemaContext *context,
                                    const LunaExpression *expression,
                                    LunaSemaTypeId expected_type) {
    if (expression == NULL || !context->reachable) {
        return luna_sema_invalid_value();
    }

    switch (expression->kind) {
    case LUNA_EXPRESSION_INTEGER: {
        const LunaSemaTypeId literal_type =
            luna_sema_is_integer_type(expected_type) ? expected_type
                                                     : LUNA_TYPE_I32;
        const uint64_t maximum =
            luna_sema_integer_maximum(context, literal_type);
        if (expression->as.integer > maximum) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "integer literal does not fit in %s",
                                  luna_type_kind_name(luna_sema_type_kind(
                                      context, literal_type)));
            return luna_sema_invalid_value();
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_INTEGER, expression->span);
        instruction.immediate = expression->as.integer;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, literal_type);
        return (LunaCheckedValue){
            .id = result,
            .type = literal_type,
        };
    }

    case LUNA_EXPRESSION_FLOAT: {
        const LunaSemaTypeId literal_type =
            luna_sema_is_float_type(expected_type) ? expected_type
                                                   : LUNA_TYPE_F64;
        uint64_t bits = 0U;
        if (!luna_sema_float_literal_bits(context, expression, literal_type,
                                          &bits)) {
            return luna_sema_invalid_value();
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_FLOAT, expression->span);
        instruction.immediate = bits;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, literal_type);
        return (LunaCheckedValue){
            .id = result,
            .type = literal_type,
        };
    }

    case LUNA_EXPRESSION_BOOLEAN: {
        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_BOOL, expression->span);
        instruction.immediate = expression->as.boolean ? 1 : 0;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, LUNA_TYPE_BOOL);
        return (LunaCheckedValue){
            .id = result,
            .type = LUNA_TYPE_BOOL,
        };
    }

    case LUNA_EXPRESSION_STRING:
        return luna_sema_lower_string(context, expression);

    case LUNA_EXPRESSION_NULL: {
        if (!luna_sema_is_pointer_type(context, expected_type)) {
            luna_diagnostic_error(context->diagnostics, expression->span,
                                  "null requires an expected pointer type");
            return luna_sema_invalid_value();
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_CONST_NULL, expression->span);
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, expected_type);
        return (LunaCheckedValue){
            .id = result,
            .type = expected_type,
        };
    }

    case LUNA_EXPRESSION_ZERO_INITIALIZER:
        return luna_sema_lower_zero_initializer(context, expected_type,
                                                expression->span);

    case LUNA_EXPRESSION_AGGREGATE_INITIALIZER:
        luna_diagnostic_error(
            context->diagnostics, expression->span,
            "named aggregate initializer requires an aggregate destination");
        return luna_sema_invalid_value();

    case LUNA_EXPRESSION_SIZEOF:
    case LUNA_EXPRESSION_ALIGNOF:
    case LUNA_EXPRESSION_OFFSETOF: {
        const LunaSemaTypeId queried_type =
            luna_sema_resolve_type(context, &expression->as.type_query.type);
        if (queried_type == LUNA_TYPE_INVALID) {
            return luna_sema_invalid_value();
        }

        uint64_t immediate = 0U;
        if (expression->kind == LUNA_EXPRESSION_OFFSETOF) {
            if (!luna_sema_is_record_type(context, queried_type)) {
                luna_diagnostic_error(
                    context->diagnostics, expression->span,
                    "offsetof requires a struct or union type");
                return luna_sema_invalid_value();
            }
            const LunaSemaField *field = luna_sema_find_field(
                context, queried_type, expression->as.type_query.member_name);
            if (field == NULL) {
                luna_diagnostic_error(
                    context->diagnostics, expression->span,
                    "offsetof names an unknown field '%.*s'",
                    (int)expression->as.type_query.member_name.length,
                    expression->as.type_query.member_name.data);
                return luna_sema_invalid_value();
            }
            immediate = field->offset;
        } else {
            uint64_t size_bytes = 0U;
            uint32_t alignment_bytes = 0U;
            if (!luna_sema_type_layout(context, queried_type, &size_bytes,
                                       &alignment_bytes)) {
                luna_diagnostic_error(
                    context->diagnostics, expression->span,
                    "layout query requires a type with a valid target layout");
                return luna_sema_invalid_value();
            }
            immediate = expression->kind == LUNA_EXPRESSION_SIZEOF
                            ? size_bytes
                            : (uint64_t)alignment_bytes;
        }

        LunaIrInstruction constant =
            luna_sema_instruction(LUNA_IR_CONST_INTEGER, expression->span);
        constant.immediate = immediate;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &constant, LUNA_TYPE_USIZE);
        return (LunaCheckedValue){
            .id = result,
            .type = LUNA_TYPE_USIZE,
        };
    }

    case LUNA_EXPRESSION_NAME:
    case LUNA_EXPRESSION_INDEX: {
        const LunaCheckedLvalue lvalue =
            luna_sema_lower_lvalue(context, expression);
        return luna_sema_load_lvalue(context, &lvalue, expression->span);
    }

    case LUNA_EXPRESSION_MEMBER: {
        LunaSemaTypeId enum_type = LUNA_TYPE_INVALID;
        const LunaSemaEnumMember *member =
            luna_sema_scoped_enum_member(context, expression, &enum_type);
        if (member != NULL) {
            LunaIrInstruction constant =
                luna_sema_instruction(LUNA_IR_CONST_INTEGER, expression->span);
            constant.immediate = member->value;
            const LunaIrValueId result =
                luna_sema_emit_value_instruction(context, &constant, enum_type);
            return (LunaCheckedValue){
                .id = result,
                .type = enum_type,
            };
        }

        if (expression->as.member.operator_kind == LUNA_TOKEN_DOT &&
            expression->as.member.base != NULL &&
            expression->as.member.base->kind == LUNA_EXPRESSION_NAME &&
            luna_sema_find_local(context,
                                 expression->as.member.base->as.name) == NULL) {
            const LunaSemaNamedType *named_type = luna_sema_find_named_type(
                context, expression->as.member.base->as.name);
            if (named_type != NULL) {
                if (luna_sema_is_enum_type(context, named_type->type)) {
                    luna_diagnostic_error(
                        context->diagnostics, expression->span,
                        "enum '%.*s' has no member named '%.*s'",
                        (int)named_type->syntax->name.length,
                        named_type->syntax->name.data,
                        (int)expression->as.member.name.length,
                        expression->as.member.name.data);
                } else {
                    luna_diagnostic_error(
                        context->diagnostics, expression->span,
                        "type '%.*s' is not a value; scoped member access is "
                        "only valid for enums",
                        (int)named_type->syntax->name.length,
                        named_type->syntax->name.data);
                }
                return luna_sema_invalid_value();
            }
        }

        const LunaCheckedLvalue lvalue =
            luna_sema_lower_lvalue(context, expression);
        return luna_sema_load_lvalue(context, &lvalue, expression->span);
    }

    case LUNA_EXPRESSION_CALL:
        return luna_sema_lower_call(context, expression);

    case LUNA_EXPRESSION_CAST: {
        const LunaSemaTypeId target_type =
            luna_sema_resolve_type(context, &expression->as.cast.target_type);
        const LunaSemaTypeId operand_hint = luna_sema_known_expression_type(
            context, expression->as.cast.operand);
        const LunaSemaTypeId operand_default =
            luna_sema_default_literal_type(expression->as.cast.operand);
        const LunaSemaType *target = luna_sema_type(context, target_type);
        const LunaSemaTypeId target_storage =
            target != NULL && target->kind == LUNA_TYPE_ENUM
                ? target->element_type
                : target_type;
        const bool target_supplies_literal_context =
            (luna_sema_is_integer_type(target_storage) &&
             luna_sema_is_integer_type(operand_default)) ||
            (luna_sema_is_float_type(target_type) &&
             luna_sema_is_float_type(operand_default)) ||
            (luna_sema_is_pointer_type(context, target_type) &&
             expression->as.cast.operand->kind == LUNA_EXPRESSION_NULL);
        LunaCheckedValue operand =
            operand_hint == LUNA_TYPE_INVALID && target_supplies_literal_context
                ? luna_sema_lower_expression_expected(
                      context, expression->as.cast.operand, target_storage)
                : luna_sema_lower_expression(context,
                                             expression->as.cast.operand);
        target = luna_sema_type(context, target_type);

        if (luna_sema_is_enum_type(context, operand.type) ||
            luna_sema_is_enum_type(context, target_type)) {
            const LunaSemaType *operand_semantic_type =
                luna_sema_type(context, operand.type);
            const bool enum_to_underlying =
                operand_semantic_type != NULL &&
                operand_semantic_type->kind == LUNA_TYPE_ENUM &&
                operand_semantic_type->element_type == target_type;
            const bool underlying_to_enum =
                target != NULL && target->kind == LUNA_TYPE_ENUM &&
                target->element_type == operand.type;
            if (!enum_to_underlying && !underlying_to_enum &&
                operand.type != target_type) {
                luna_diagnostic_error(
                    context->diagnostics, expression->span,
                    "enum conversion requires the enum's exact underlying "
                    "integer type");
                return luna_sema_invalid_value();
            }
            return (LunaCheckedValue){
                .id = operand.id,
                .type = target_type,
            };
        }

        const bool operand_is_pointer =
            luna_sema_is_pointer_type(context, operand.type);
        const bool target_is_pointer =
            luna_sema_is_pointer_type(context, target_type);
        if (operand_is_pointer && target_is_pointer) {
            if (luna_sema_pointer_conversion_removes_read_only(
                    context, operand.type, target_type)) {
                luna_diagnostic_error(
                    context->diagnostics, expression->span,
                    "pointer conversion cannot remove read-only qualification");
                return luna_sema_invalid_value();
            }
            return (LunaCheckedValue){
                .id = operand.id,
                .type = target_type,
            };
        }

        if (operand_is_pointer && target_type == LUNA_TYPE_USIZE) {
            LunaIrInstruction instruction = luna_sema_instruction(
                LUNA_IR_CONVERT_POINTER_TO_INTEGER, expression->span);
            instruction.left = operand.id;
            const LunaIrValueId result = luna_sema_emit_value_instruction(
                context, &instruction, target_type);
            return (LunaCheckedValue){
                .id = result,
                .type = target_type,
            };
        }

        if (operand.type == LUNA_TYPE_USIZE && target_is_pointer) {
            LunaIrInstruction instruction = luna_sema_instruction(
                LUNA_IR_CONVERT_INTEGER_TO_POINTER, expression->span);
            instruction.left = operand.id;
            const LunaIrValueId result = luna_sema_emit_value_instruction(
                context, &instruction, target_type);
            return (LunaCheckedValue){
                .id = result,
                .type = target_type,
            };
        }

        if (!luna_sema_is_numeric_type(operand.type) ||
            !luna_sema_is_numeric_type(target_type)) {
            luna_diagnostic_error(
                context->diagnostics, expression->span,
                "explicit conversion requires numeric source and target "
                "types, two pointer types, or a pointer and usize");
            return luna_sema_invalid_value();
        }

        if (operand.type == target_type) {
            return operand;
        }

        LunaIrOpcode opcode = LUNA_IR_CONVERT_FLOAT_TO_INTEGER;
        if (luna_sema_is_integer_type(operand.type)) {
            opcode = luna_sema_is_integer_type(target_type)
                         ? LUNA_IR_CONVERT_INTEGER
                         : LUNA_IR_CONVERT_INTEGER_TO_FLOAT;
        } else if (luna_sema_is_float_type(target_type)) {
            opcode = LUNA_IR_CONVERT_FLOAT;
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(opcode, expression->span);
        instruction.left = operand.id;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, target_type);
        return (LunaCheckedValue){
            .id = result,
            .type = target_type,
        };
    }

    case LUNA_EXPRESSION_CONDITIONAL:
        return luna_sema_lower_conditional(context, expression, expected_type);

    case LUNA_EXPRESSION_UNARY: {
        if (expression->as.unary.operator_kind == LUNA_TOKEN_AMPERSAND) {
            const LunaCheckedLvalue lvalue =
                luna_sema_lower_lvalue(context, expression->as.unary.operand);
            if (lvalue.storage == LUNA_SEMA_LVALUE_INVALID) {
                return luna_sema_invalid_value();
            }
            const LunaIrValueId address =
                luna_sema_lvalue_address(context, &lvalue, expression->span);
            const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                context, lvalue.type, !lvalue.is_mutable);
            return (LunaCheckedValue){
                .id = address,
                .type = pointer_type,
            };
        }

        if (expression->as.unary.operator_kind == LUNA_TOKEN_STAR) {
            const LunaCheckedLvalue lvalue =
                luna_sema_lower_lvalue(context, expression);
            return luna_sema_load_lvalue(context, &lvalue, expression->span);
        }

        LunaSemaTypeId operand_type = expected_type;
        if (!luna_sema_is_numeric_type(operand_type)) {
            operand_type = luna_sema_known_expression_type(
                context, expression->as.unary.operand);
        }
        if (!luna_sema_is_numeric_type(operand_type)) {
            operand_type =
                luna_sema_default_literal_type(expression->as.unary.operand);
        }
        if (!luna_sema_is_numeric_type(operand_type)) {
            operand_type = LUNA_TYPE_I32;
        }

        if (expression->as.unary.operator_kind == LUNA_TOKEN_MINUS &&
            luna_type_kind_is_signed_integer(
                luna_sema_type_kind(context, operand_type)) &&
            expression->as.unary.operand->kind == LUNA_EXPRESSION_INTEGER &&
            expression->as.unary.operand->as.integer ==
                luna_sema_signed_minimum_magnitude(context, operand_type)) {
            LunaIrInstruction instruction =
                luna_sema_instruction(LUNA_IR_CONST_INTEGER, expression->span);
            instruction.immediate = expression->as.unary.operand->as.integer;
            const LunaIrValueId result = luna_sema_emit_value_instruction(
                context, &instruction, operand_type);
            return (LunaCheckedValue){
                .id = result,
                .type = operand_type,
            };
        }

        const LunaSemaTypeId required_type =
            expression->as.unary.operator_kind == LUNA_TOKEN_BANG
                ? LUNA_TYPE_BOOL
                : operand_type;
        LunaCheckedValue operand = luna_sema_lower_expression_expected(
            context, expression->as.unary.operand, required_type);

        switch (expression->as.unary.operator_kind) {
        case LUNA_TOKEN_PLUS:
            if (!luna_sema_is_numeric_type(operand.type)) {
                luna_diagnostic_error(context->diagnostics, expression->span,
                                      "unary '+' requires a numeric operand");
                return luna_sema_invalid_value();
            }
            if (!luna_sema_require_type(context, operand, required_type,
                                        expression->span)) {
                return luna_sema_invalid_value();
            }
            return operand;

        case LUNA_TOKEN_MINUS:
            if (!luna_sema_is_numeric_type(operand.type)) {
                luna_diagnostic_error(context->diagnostics, expression->span,
                                      "unary '-' requires a numeric operand");
                return luna_sema_invalid_value();
            }
            if (!luna_sema_require_type(context, operand, required_type,
                                        expression->as.unary.operand->span)) {
                return luna_sema_invalid_value();
            }
            break;

        case LUNA_TOKEN_TILDE:
            if (!luna_sema_is_integer_type(operand.type)) {
                luna_diagnostic_error(context->diagnostics, expression->span,
                                      "unary '~' requires an integer operand");
                return luna_sema_invalid_value();
            }
            if (!luna_sema_require_type(context, operand, required_type,
                                        expression->as.unary.operand->span)) {
                return luna_sema_invalid_value();
            }
            break;

        case LUNA_TOKEN_BANG:
            if (!luna_sema_require_type(context, operand, LUNA_TYPE_BOOL,
                                        expression->as.unary.operand->span)) {
                return luna_sema_invalid_value();
            }
            break;

        default:
            return luna_sema_invalid_value();
        }

        const LunaSemaTypeId result_type =
            expression->as.unary.operator_kind == LUNA_TOKEN_BANG
                ? LUNA_TYPE_BOOL
                : operand.type;
        LunaIrOpcode opcode = LUNA_IR_BOOL_NOT;
        if (expression->as.unary.operator_kind == LUNA_TOKEN_MINUS) {
            opcode = luna_sema_is_float_type(operand.type)
                         ? LUNA_IR_NEG_FLOAT
                         : LUNA_IR_NEG_INTEGER;
        } else if (expression->as.unary.operator_kind == LUNA_TOKEN_TILDE) {
            opcode = LUNA_IR_BIT_NOT_INTEGER;
        }

        LunaIrInstruction instruction =
            luna_sema_instruction(opcode, expression->span);
        instruction.left = operand.id;
        const LunaIrValueId result = luna_sema_emit_value_instruction(
            context, &instruction, result_type);
        return (LunaCheckedValue){
            .id = result,
            .type = result_type,
        };
    }

    case LUNA_EXPRESSION_BINARY:
        break;
    }

    if (expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_AND ||
        expression->as.binary.operator_kind == LUNA_TOKEN_LOGICAL_OR) {
        return luna_sema_lower_logical(context, expression);
    }

    const LunaTokenKind operator_kind = expression->as.binary.operator_kind;
    const bool is_equality = operator_kind == LUNA_TOKEN_EQUAL_EQUAL ||
                             operator_kind == LUNA_TOKEN_BANG_EQUAL;
    const bool is_relational = operator_kind == LUNA_TOKEN_LESS ||
                               operator_kind == LUNA_TOKEN_LESS_EQUAL ||
                               operator_kind == LUNA_TOKEN_GREATER ||
                               operator_kind == LUNA_TOKEN_GREATER_EQUAL;

    LunaSemaTypeId operand_type = LUNA_TYPE_INVALID;
    if (!is_equality && !is_relational &&
        luna_sema_is_numeric_type(expected_type)) {
        operand_type = expected_type;
    }
    if (operand_type == LUNA_TYPE_INVALID) {
        operand_type = luna_sema_known_expression_type(
            context, expression->as.binary.left);
    }
    if (operand_type == LUNA_TYPE_INVALID) {
        operand_type = luna_sema_known_expression_type(
            context, expression->as.binary.right);
    }
    if (operand_type == LUNA_TYPE_INVALID) {
        operand_type =
            luna_sema_default_literal_type(expression->as.binary.left);
        if (operand_type == LUNA_TYPE_INVALID) {
            operand_type =
                luna_sema_default_literal_type(expression->as.binary.right);
        }
    }
    if (operand_type == LUNA_TYPE_INVALID) {
        operand_type = LUNA_TYPE_I32;
    }

    LunaCheckedValue left = luna_sema_lower_expression_expected(
        context, expression->as.binary.left, operand_type);
    if (!luna_sema_require_type(context, left, operand_type,
                                expression->as.binary.left->span)) {
        return luna_sema_invalid_value();
    }

    LunaIrSlotId preserved_left = LUNA_IR_INVALID_ID;
    if (luna_sema_expression_can_branch(expression->as.binary.right)) {
        preserved_left = luna_sema_preserve_value(
            context, left, expression->as.binary.left->span);
        if (preserved_left == LUNA_IR_INVALID_ID) {
            return luna_sema_invalid_value();
        }
    }
    LunaCheckedValue right = luna_sema_lower_expression_expected(
        context, expression->as.binary.right, operand_type);
    if (!luna_sema_require_type(context, right, operand_type,
                                expression->as.binary.right->span)) {
        return luna_sema_invalid_value();
    }
    if (preserved_left != LUNA_IR_INVALID_ID) {
        left = luna_sema_reload_value(context, preserved_left, operand_type,
                                      expression->as.binary.left->span);
    }

    if (is_equality && operand_type != LUNA_TYPE_BOOL &&
        !luna_sema_is_numeric_type(operand_type) &&
        !luna_sema_is_enum_type(context, operand_type) &&
        !luna_sema_is_pointer_type(context, operand_type)) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "equality requires bool, numeric, enum, or "
                              "exact pointer operands");
        return luna_sema_invalid_value();
    }
    if (is_relational && !luna_sema_is_numeric_type(operand_type)) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "ordering requires numeric operands");
        return luna_sema_invalid_value();
    }
    if (!is_equality && !is_relational &&
        !luna_sema_is_numeric_type(operand_type)) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "arithmetic and bitwise operators require "
                              "numeric operands");
        return luna_sema_invalid_value();
    }

    LunaIrOpcode opcode = LUNA_IR_ADD_INTEGER;
    if (is_equality) {
        opcode = operator_kind == LUNA_TOKEN_EQUAL_EQUAL
                     ? LUNA_IR_COMPARE_EQUAL
                     : LUNA_IR_COMPARE_NOT_EQUAL;
    } else if (is_relational) {
        opcode = luna_sema_is_float_type(operand_type)
                     ? luna_sema_float_comparison_opcode(operator_kind)
                     : luna_sema_binary_integer_opcode(operator_kind);
    } else if (luna_sema_is_float_type(operand_type)) {
        if (!luna_sema_binary_float_opcode(operator_kind, &opcode)) {
            luna_diagnostic_error(
                context->diagnostics, expression->span,
                "operator %s is not defined for floating-point operands",
                luna_token_kind_name(operator_kind));
            return luna_sema_invalid_value();
        }
    } else {
        opcode = luna_sema_binary_integer_opcode(operator_kind);
    }
    const LunaSemaTypeId result_type =
        is_equality || is_relational ? LUNA_TYPE_BOOL : operand_type;

    LunaIrInstruction instruction =
        luna_sema_instruction(opcode, expression->span);
    instruction.left = left.id;
    instruction.right = right.id;
    const LunaIrValueId result =
        luna_sema_emit_value_instruction(context, &instruction, result_type);
    return (LunaCheckedValue){
        .id = result,
        .type = result_type,
    };
}

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement);

static bool luna_sema_lower_dead_expression(LunaSemaContext *context,
                                            LunaIrBlockId block_id,
                                            const LunaExpression *expression,
                                            LunaSemaTypeId expected_type,
                                            LunaSourceSpan span) {
    const bool was_checking_dead_code = context->checking_dead_code;
    context->current_block = block_id;
    context->reachable = true;
    context->checking_dead_code = true;

    LunaCheckedValue value =
        luna_sema_lower_expression_expected(context, expression, expected_type);
    const bool success =
        luna_sema_require_type(context, value, expected_type, expression->span);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, context->current_block, span);
    }

    context->checking_dead_code = was_checking_dead_code;
    context->reachable = false;
    return success;
}

static bool luna_sema_lower_dead_statement(LunaSemaContext *context,
                                           LunaIrBlockId block_id,
                                           const LunaStatement *statement,
                                           LunaSourceSpan span) {
    const bool was_checking_dead_code = context->checking_dead_code;
    context->current_block = block_id;
    context->reachable = true;
    context->checking_dead_code = true;

    const bool success = luna_sema_lower_statement(context, statement);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, context->current_block, span);
    }

    context->checking_dead_code = was_checking_dead_code;
    context->reachable = false;
    return success;
}

static void luna_sema_lower_block(LunaSemaContext *context,
                                  const LunaBlock *block, bool create_scope) {
    if (block == NULL) {
        return;
    }

    if (create_scope) {
        luna_sema_enter_scope(context);
    }

    bool owns_dead_region = false;
    for (const LunaStatement *statement = block->first; statement != NULL;
         statement = statement->next) {
        if (!context->reachable) {
            const LunaIrBlockId dead_block = luna_sema_add_block(context);
            if (dead_block == LUNA_IR_INVALID_ID) {
                break;
            }

            context->current_block = dead_block;
            context->reachable = true;
            if (!context->checking_dead_code) {
                context->checking_dead_code = true;
                owns_dead_region = true;
            }
        }

        (void)luna_sema_lower_statement(context, statement);
    }

    if (owns_dead_region) {
        if (context->reachable) {
            (void)luna_sema_emit_jump(context, context->current_block,
                                      block->span);
        }
        context->checking_dead_code = false;
        context->reachable = false;
    }

    if (create_scope) {
        luna_sema_leave_scope(context);
    }
}

static bool luna_sema_lower_if(LunaSemaContext *context,
                               const LunaStatement *statement) {
    LunaCheckedValue condition = luna_sema_lower_expression_expected(
        context, statement->as.if_statement.condition, LUNA_TYPE_BOOL);
    if (!luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                                statement->as.if_statement.condition->span)) {
        return false;
    }

    const LunaIrBlockId then_block = luna_sema_add_block(context);
    const LunaIrBlockId else_block = luna_sema_add_block(context);
    const LunaIrBlockId merge_block = luna_sema_add_block(context);
    if (then_block == LUNA_IR_INVALID_ID || else_block == LUNA_IR_INVALID_ID ||
        merge_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_branch(context, condition.id, then_block, else_block,
                                statement->span);

    luna_sema_set_block(context, then_block);
    luna_sema_lower_block(context, statement->as.if_statement.then_block, true);
    const bool then_falls_through = context->reachable;
    if (then_falls_through) {
        (void)luna_sema_emit_jump(context, merge_block, statement->span);
    }

    luna_sema_set_block(context, else_block);
    if (statement->as.if_statement.else_branch != NULL) {
        (void)luna_sema_lower_statement(context,
                                        statement->as.if_statement.else_branch);
    }
    const bool else_falls_through = context->reachable;
    if (else_falls_through) {
        (void)luna_sema_emit_jump(context, merge_block, statement->span);
    }

    luna_sema_set_block(context, merge_block);
    context->reachable = then_falls_through || else_falls_through;
    return true;
}

static bool luna_sema_lower_while(LunaSemaContext *context,
                                  const LunaStatement *statement) {
    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (condition_block == LUNA_IR_INVALID_ID ||
        body_block == LUNA_IR_INVALID_ID || exit_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_jump(context, condition_block, statement->span);

    luna_sema_set_block(context, condition_block);
    LunaCheckedValue condition = luna_sema_lower_expression_expected(
        context, statement->as.while_statement.condition, LUNA_TYPE_BOOL);
    if (!luna_sema_require_type(
            context, condition, LUNA_TYPE_BOOL,
            statement->as.while_statement.condition->span)) {
        return false;
    }
    (void)luna_sema_emit_branch(context, condition.id, body_block, exit_block,
                                statement->span);

    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = condition_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.while_statement.body, true);
    if (context->reachable) {
        (void)luna_sema_emit_jump(context, condition_block, statement->span);
    }
    context->control_frames.length -= 1U;

    luna_sema_set_block(context, exit_block);
    context->reachable = true;
    return true;
}

static bool luna_sema_lower_do(LunaSemaContext *context,
                               const LunaStatement *statement) {
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (body_block == LUNA_IR_INVALID_ID ||
        condition_block == LUNA_IR_INVALID_ID ||
        exit_block == LUNA_IR_INVALID_ID) {
        return false;
    }

    (void)luna_sema_emit_jump(context, body_block, statement->span);
    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = condition_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.do_statement.body, true);
    const bool body_falls_through = context->reachable;
    if (body_falls_through) {
        (void)luna_sema_emit_jump(context, condition_block, statement->span);
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool condition_is_live =
        body_falls_through || completed_frame->has_live_continue;
    const bool exit_is_live =
        completed_frame->has_live_break || condition_is_live;
    context->control_frames.length -= 1U;

    if (condition_is_live) {
        luna_sema_set_block(context, condition_block);
        LunaCheckedValue condition = luna_sema_lower_expression_expected(
            context, statement->as.do_statement.condition, LUNA_TYPE_BOOL);
        if (!luna_sema_require_type(
                context, condition, LUNA_TYPE_BOOL,
                statement->as.do_statement.condition->span)) {
            return false;
        }
        (void)luna_sema_emit_branch(context, condition.id, body_block,
                                    exit_block, statement->span);
    } else if (!luna_sema_lower_dead_expression(
                   context, condition_block,
                   statement->as.do_statement.condition, LUNA_TYPE_BOOL,
                   statement->span)) {
        return false;
    }

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    return true;
}

static bool luna_sema_lower_for(LunaSemaContext *context,
                                const LunaStatement *statement) {
    luna_sema_enter_scope(context);
    if (statement->as.for_statement.initializer != NULL &&
        !luna_sema_lower_statement(context,
                                   statement->as.for_statement.initializer)) {
        luna_sema_leave_scope(context);
        return false;
    }

    const LunaIrBlockId condition_block = luna_sema_add_block(context);
    const LunaIrBlockId body_block = luna_sema_add_block(context);
    const LunaIrBlockId update_block =
        statement->as.for_statement.update == NULL
            ? LUNA_IR_INVALID_ID
            : luna_sema_add_block(context);
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (condition_block == LUNA_IR_INVALID_ID ||
        body_block == LUNA_IR_INVALID_ID ||
        (statement->as.for_statement.update != NULL &&
         update_block == LUNA_IR_INVALID_ID) ||
        exit_block == LUNA_IR_INVALID_ID) {
        luna_sema_leave_scope(context);
        return false;
    }

    (void)luna_sema_emit_jump(context, condition_block, statement->span);
    luna_sema_set_block(context, condition_block);
    const bool condition_can_exit =
        statement->as.for_statement.condition != NULL;
    if (condition_can_exit) {
        LunaCheckedValue condition = luna_sema_lower_expression_expected(
            context, statement->as.for_statement.condition, LUNA_TYPE_BOOL);
        if (!luna_sema_require_type(
                context, condition, LUNA_TYPE_BOOL,
                statement->as.for_statement.condition->span)) {
            luna_sema_leave_scope(context);
            return false;
        }
        (void)luna_sema_emit_branch(context, condition.id, body_block,
                                    exit_block, statement->span);
    } else {
        (void)luna_sema_emit_jump(context, body_block, statement->span);
    }

    const LunaIrBlockId continue_block =
        update_block == LUNA_IR_INVALID_ID ? condition_block : update_block;
    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = continue_block,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        luna_sema_leave_scope(context);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;

    luna_sema_set_block(context, body_block);
    luna_sema_lower_block(context, statement->as.for_statement.body, true);
    const bool body_falls_through = context->reachable;
    if (body_falls_through) {
        (void)luna_sema_emit_jump(context, continue_block, statement->span);
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool update_is_live =
        body_falls_through || completed_frame->has_live_continue;
    const bool exit_is_live =
        condition_can_exit || completed_frame->has_live_break;
    context->control_frames.length -= 1U;

    if (update_block != LUNA_IR_INVALID_ID) {
        if (update_is_live) {
            luna_sema_set_block(context, update_block);
            if (!luna_sema_lower_statement(
                    context, statement->as.for_statement.update)) {
                luna_sema_leave_scope(context);
                return false;
            }
            if (context->reachable) {
                (void)luna_sema_emit_jump(context, condition_block,
                                          statement->span);
            }
        } else if (!luna_sema_lower_dead_statement(
                       context, update_block,
                       statement->as.for_statement.update, statement->span)) {
            luna_sema_leave_scope(context);
            return false;
        }
    }

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    luna_sema_leave_scope(context);
    return true;
}

static bool luna_sema_switch_label_value(LunaSemaContext *context,
                                         const LunaExpression *expression,
                                         LunaSemaTypeId switch_type,
                                         uint64_t *value) {
    if (expression == NULL) {
        luna_diagnostic_error_plain(context->diagnostics,
                                    "switch case label is missing");
        return false;
    }

    if (luna_sema_is_enum_type(context, switch_type)) {
        LunaSemaTypeId label_type = LUNA_TYPE_INVALID;
        const LunaSemaEnumMember *member =
            luna_sema_scoped_enum_member(context, expression, &label_type);
        if (member == NULL || label_type != switch_type) {
            luna_diagnostic_error(
                context->diagnostics, expression->span,
                "enum switch case label must be a member of the controlling "
                "enum");
            return false;
        }
        *value = member->value;
        return true;
    }

    bool is_negative = false;
    const LunaExpression *literal = expression;
    if (literal->kind == LUNA_EXPRESSION_UNARY) {
        if (literal->as.unary.operator_kind != LUNA_TOKEN_PLUS &&
            literal->as.unary.operator_kind != LUNA_TOKEN_MINUS) {
            luna_diagnostic_error(
                context->diagnostics, expression->span,
                "expected integer literal as switch case label");
            return false;
        }
        is_negative = literal->as.unary.operator_kind == LUNA_TOKEN_MINUS;
        literal = literal->as.unary.operand;
    }

    if (literal == NULL || literal->kind != LUNA_EXPRESSION_INTEGER) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "expected integer literal as switch case label");
        return false;
    }

    const LunaTypeKind switch_kind = luna_sema_type_kind(context, switch_type);
    const uint32_t width = luna_type_kind_bit_width(
        switch_kind, &context->module->target->data_layout);
    if (width == 0U || width > 64U) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "switch has invalid controlling integer type");
        return false;
    }
    const uint64_t mask =
        width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    const uint64_t magnitude = literal->as.integer;
    uint64_t maximum = luna_sema_integer_maximum(context, switch_type);
    if (is_negative && luna_type_kind_is_signed_integer(switch_kind)) {
        maximum = UINT64_C(1) << (width - 1U);
    }
    if (magnitude > maximum) {
        luna_diagnostic_error(context->diagnostics, expression->span,
                              "switch case label does not fit in %s",
                              luna_type_kind_name(switch_kind));
        return false;
    }

    *value = is_negative ? (UINT64_C(0) - magnitude) & mask : magnitude;
    return true;
}

static bool luna_sema_lower_switch(LunaSemaContext *context,
                                   const LunaStatement *statement) {
    LunaCheckedValue controlling = luna_sema_lower_expression(
        context, statement->as.switch_statement.expression);
    if (!luna_sema_is_integer_type(controlling.type) &&
        !luna_sema_is_enum_type(context, controlling.type)) {
        luna_diagnostic_error(context->diagnostics,
                              statement->as.switch_statement.expression->span,
                              "switch expression requires an integer type or "
                              "scoped enum");
        return false;
    }

    const LunaIrSlotId controlling_slot =
        luna_ir_function_add_slot(context->current_function,
                                  luna_sema_ir_type(context, controlling.type));
    const LunaIrBlockId exit_block = luna_sema_add_block(context);
    if (controlling_slot == LUNA_IR_INVALID_ID ||
        exit_block == LUNA_IR_INVALID_ID) {
        luna_sema_report_allocation_failure(context);
        return false;
    }
    LunaIrInstruction controlling_store =
        luna_sema_instruction(LUNA_IR_STORE, statement->span);
    controlling_store.slot = controlling_slot;
    controlling_store.left = controlling.id;
    if (!luna_sema_append_instruction(context, &controlling_store)) {
        return false;
    }

    LunaVector arms;
    LunaVector labels;
    luna_vector_init(&arms, sizeof(LunaSemaSwitchArm));
    luna_vector_init(&labels, sizeof(LunaSemaSwitchLabel));
    LunaIrBlockId default_block = LUNA_IR_INVALID_ID;
    bool valid = true;

    for (const LunaSwitchArm *arm = statement->as.switch_statement.first_arm;
         arm != NULL; arm = arm->next) {
        const LunaIrBlockId body_block = luna_sema_add_block(context);
        if (body_block == LUNA_IR_INVALID_ID) {
            valid = false;
            break;
        }
        const LunaSemaSwitchArm lowered_arm = {
            .syntax = arm,
            .body_block = body_block,
        };
        if (!luna_vector_push(&arms, &lowered_arm)) {
            luna_sema_report_allocation_failure(context);
            valid = false;
            break;
        }

        if (arm->is_default) {
            if (default_block != LUNA_IR_INVALID_ID) {
                luna_diagnostic_error(context->diagnostics, arm->span,
                                      "switch has more than one default arm");
                valid = false;
                continue;
            }
            default_block = body_block;
            continue;
        }

        if (arm->first_label == NULL) {
            luna_diagnostic_error(context->diagnostics, arm->span,
                                  "switch case requires at least one label");
            valid = false;
            continue;
        }

        for (const LunaExpression *label = arm->first_label; label != NULL;
             label = label->next) {
            LunaSemaSwitchLabel lowered_label = {
                .span = label->span,
                .body_block = body_block,
            };
            if (!luna_sema_switch_label_value(context, label, controlling.type,
                                              &lowered_label.value)) {
                valid = false;
                continue;
            }

            for (size_t index = 0U; index < labels.length; index += 1U) {
                const LunaSemaSwitchLabel *existing =
                    luna_vector_at_const(&labels, index);
                if (existing->value == lowered_label.value) {
                    luna_diagnostic_error(
                        context->diagnostics, label->span,
                        "duplicate switch case value for type %s",
                        luna_type_kind_name(
                            luna_sema_type_kind(context, controlling.type)));
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }
            if (!luna_vector_push(&labels, &lowered_label)) {
                luna_sema_report_allocation_failure(context);
                valid = false;
                break;
            }
        }
    }

    if (!valid) {
        luna_vector_destroy(&labels);
        luna_vector_destroy(&arms);
        return false;
    }

    const LunaIrBlockId no_match_block =
        default_block == LUNA_IR_INVALID_ID ? exit_block : default_block;
    if (labels.length == 0U) {
        (void)luna_sema_emit_jump(context, no_match_block, statement->span);
    } else {
        for (size_t index = 0U; index < labels.length; index += 1U) {
            const LunaSemaSwitchLabel *label =
                luna_vector_at_const(&labels, index);
            const bool is_last = index + 1U == labels.length;
            const LunaIrBlockId next_block =
                is_last ? no_match_block : luna_sema_add_block(context);
            if (next_block == LUNA_IR_INVALID_ID) {
                luna_vector_destroy(&labels);
                luna_vector_destroy(&arms);
                return false;
            }

            LunaIrInstruction load =
                luna_sema_instruction(LUNA_IR_LOAD, label->span);
            load.slot = controlling_slot;
            const LunaIrValueId loaded = luna_sema_emit_value_instruction(
                context, &load, controlling.type);

            LunaIrInstruction constant =
                luna_sema_instruction(LUNA_IR_CONST_INTEGER, label->span);
            constant.immediate = label->value;
            const LunaIrValueId expected = luna_sema_emit_value_instruction(
                context, &constant, controlling.type);

            LunaIrInstruction comparison =
                luna_sema_instruction(LUNA_IR_COMPARE_EQUAL, label->span);
            comparison.left = loaded;
            comparison.right = expected;
            const LunaIrValueId matches = luna_sema_emit_value_instruction(
                context, &comparison, LUNA_TYPE_BOOL);
            (void)luna_sema_emit_branch(context, matches, label->body_block,
                                        next_block, label->span);
            if (!is_last) {
                luna_sema_set_block(context, next_block);
            }
        }
    }

    const LunaSemaControlFrame frame = {
        .break_block = exit_block,
        .continue_block = LUNA_IR_INVALID_ID,
    };
    if (!luna_vector_push(&context->control_frames, &frame)) {
        luna_sema_report_allocation_failure(context);
        luna_vector_destroy(&labels);
        luna_vector_destroy(&arms);
        return false;
    }
    const size_t frame_index = context->control_frames.length - 1U;
    bool arm_falls_through = false;
    for (size_t index = 0U; index < arms.length; index += 1U) {
        const LunaSemaSwitchArm *arm = luna_vector_at_const(&arms, index);
        luna_sema_set_block(context, arm->body_block);
        luna_sema_lower_block(context, arm->syntax->body, true);
        if (context->reachable) {
            arm_falls_through = true;
            (void)luna_sema_emit_jump(context, exit_block, arm->syntax->span);
        }
    }

    const LunaSemaControlFrame *completed_frame =
        luna_vector_at_const(&context->control_frames, frame_index);
    const bool exit_is_live = default_block == LUNA_IR_INVALID_ID ||
                              arm_falls_through ||
                              completed_frame->has_live_break;
    context->control_frames.length -= 1U;
    luna_vector_destroy(&labels);
    luna_vector_destroy(&arms);

    context->current_block = exit_block;
    context->reachable = exit_is_live;
    return true;
}

static bool luna_sema_lower_statement(LunaSemaContext *context,
                                      const LunaStatement *statement) {
    switch (statement->kind) {
    case LUNA_STATEMENT_BLOCK:
        luna_sema_lower_block(context, &statement->as.block, true);
        return true;

    case LUNA_STATEMENT_DECLARATION: {
        const LunaSemaTypeId declared_type =
            luna_sema_resolve_type(context, &statement->as.declaration.type);
        if (declared_type == LUNA_TYPE_INVALID) {
            return false;
        }
        if (declared_type == LUNA_TYPE_VOID) {
            luna_diagnostic_error(context->diagnostics,
                                  statement->as.declaration.type.span,
                                  "local variables cannot have type void");
            return false;
        }

        if (luna_sema_find_local_in_current_scope(
                context, statement->as.declaration.name) != NULL) {
            luna_diagnostic_error(context->diagnostics, statement->span,
                                  "duplicate local variable '%.*s'",
                                  (int)statement->as.declaration.name.length,
                                  statement->as.declaration.name.data);
            return false;
        }

        if (luna_sema_is_memory_type(context, declared_type)) {
            const LunaExpression *initializer =
                statement->as.declaration.initializer;
            const LunaIrSlotId slot = luna_sema_allocate_memory_slot(
                context, declared_type, statement->as.declaration.type.span);
            if (slot == LUNA_IR_INVALID_ID) {
                return false;
            }

            if (luna_sema_is_brace_initializer(initializer)) {
                if (!luna_sema_zero_memory_slot(context, slot,
                                                initializer->span) ||
                    !luna_sema_initialize_slot_value(
                        context, slot, declared_type, declared_type, 0U,
                        initializer)) {
                    return false;
                }
            } else {
                const LunaCheckedLvalue source = luna_sema_lower_memory_source(
                    context, initializer, declared_type);
                if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
                    return false;
                }
                const LunaCheckedLvalue destination = {
                    .type = declared_type,
                    .storage = LUNA_SEMA_LVALUE_SLOT,
                    .slot = slot,
                    .address = LUNA_IR_INVALID_ID,
                    .is_mutable = true,
                };
                if (!luna_sema_emit_memory_copy(context, &destination, &source,
                                                initializer->span)) {
                    return false;
                }
            }

            return luna_sema_add_local(context, statement->as.declaration.name,
                                       declared_type, slot,
                                       statement->as.declaration.is_mutable);
        }

        LunaCheckedValue initializer = luna_sema_lower_expression_expected(
            context, statement->as.declaration.initializer, declared_type);
        if (!luna_sema_require_type(
                context, initializer, declared_type,
                statement->as.declaration.initializer->span)) {
            return false;
        }

        const LunaIrSlotId slot = luna_ir_function_add_slot(
            context->current_function,
            luna_sema_ir_type(context, declared_type));
        if (slot == LUNA_IR_INVALID_ID) {
            luna_sema_report_allocation_failure(context);
            return false;
        }

        if (!luna_sema_add_local(context, statement->as.declaration.name,
                                 declared_type, slot,
                                 statement->as.declaration.is_mutable)) {
            return false;
        }

        LunaIrInstruction store =
            luna_sema_instruction(LUNA_IR_STORE, statement->span);
        store.slot = slot;
        store.left = initializer.id;
        return luna_sema_append_instruction(context, &store);
    }

    case LUNA_STATEMENT_ASSIGNMENT: {
        LunaCheckedLvalue lvalue =
            luna_sema_lower_lvalue(context, statement->as.assignment.target);
        if (lvalue.storage == LUNA_SEMA_LVALUE_INVALID) {
            return false;
        }
        if (!lvalue.is_mutable) {
            if (statement->as.assignment.target->kind == LUNA_EXPRESSION_NAME) {
                const LunaStringView name =
                    statement->as.assignment.target->as.name;
                luna_diagnostic_error(context->diagnostics, statement->span,
                                      "cannot assign to immutable local '%.*s'",
                                      (int)name.length, name.data);
            } else {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "cannot assign through an immutable lvalue");
            }
            return false;
        }
        if (luna_sema_is_memory_type(context, lvalue.type)) {
            if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "compound assignment requires a scalar numeric type");
                return false;
            }

            LunaIrSlotId preserved_address = LUNA_IR_INVALID_ID;
            if (lvalue.storage == LUNA_SEMA_LVALUE_ADDRESS &&
                luna_sema_expression_can_branch(
                    statement->as.assignment.value)) {
                const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                    context, lvalue.type, !lvalue.is_mutable);
                preserved_address = luna_sema_preserve_value(
                    context,
                    (LunaCheckedValue){
                        .id = lvalue.address,
                        .type = pointer_type,
                    },
                    statement->as.assignment.target->span);
                if (preserved_address == LUNA_IR_INVALID_ID) {
                    return false;
                }
            }

            const LunaCheckedLvalue source =
                luna_sema_lower_memory_assignment_value(
                    context, statement->as.assignment.value, lvalue.type);
            if (source.storage == LUNA_SEMA_LVALUE_INVALID) {
                return false;
            }

            if (preserved_address != LUNA_IR_INVALID_ID) {
                const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                    context, lvalue.type, !lvalue.is_mutable);
                lvalue.address = luna_sema_reload_value(
                                     context, preserved_address, pointer_type,
                                     statement->as.assignment.target->span)
                                     .id;
            }
            return luna_sema_emit_memory_copy(context, &lvalue, &source,
                                              statement->span);
        }

        LunaIrSlotId preserved_address = LUNA_IR_INVALID_ID;
        if (lvalue.storage == LUNA_SEMA_LVALUE_ADDRESS &&
            luna_sema_expression_can_branch(statement->as.assignment.value)) {
            const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                context, lvalue.type, !lvalue.is_mutable);
            preserved_address =
                luna_sema_preserve_value(context,
                                         (LunaCheckedValue){
                                             .id = lvalue.address,
                                             .type = pointer_type,
                                         },
                                         statement->as.assignment.target->span);
            if (preserved_address == LUNA_IR_INVALID_ID) {
                return false;
            }
        }

        LunaCheckedValue current = luna_sema_invalid_value();
        LunaIrSlotId current_slot = LUNA_IR_INVALID_ID;
        if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
            if (!luna_sema_is_numeric_type(lvalue.type)) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "compound assignment requires a numeric type");
                return false;
            }

            current = luna_sema_load_lvalue(context, &lvalue, statement->span);
            if (current.type == LUNA_TYPE_INVALID) {
                return false;
            }
            lvalue.requires_null_check = false;

            current_slot = luna_ir_function_add_slot(
                context->current_function,
                luna_sema_ir_type(context, lvalue.type));
            if (current_slot == LUNA_IR_INVALID_ID) {
                luna_sema_report_allocation_failure(context);
                return false;
            }
            LunaIrInstruction preserve =
                luna_sema_instruction(LUNA_IR_STORE, statement->span);
            preserve.slot = current_slot;
            preserve.left = current.id;
            if (!luna_sema_append_instruction(context, &preserve)) {
                return false;
            }
        }

        LunaCheckedValue value = luna_sema_lower_expression_expected(
            context, statement->as.assignment.value, lvalue.type);
        if (!luna_sema_require_type(context, value, lvalue.type,
                                    statement->as.assignment.value->span)) {
            return false;
        }

        if (preserved_address != LUNA_IR_INVALID_ID) {
            const LunaSemaTypeId pointer_type = luna_sema_pointer_type(
                context, lvalue.type, !lvalue.is_mutable);
            lvalue.address =
                luna_sema_reload_value(context, preserved_address, pointer_type,
                                       statement->as.assignment.target->span)
                    .id;
        }

        if (statement->as.assignment.operator_kind != LUNA_TOKEN_EQUAL) {
            LunaIrInstruction reload =
                luna_sema_instruction(LUNA_IR_LOAD, statement->span);
            reload.slot = current_slot;
            current.id =
                luna_sema_emit_value_instruction(context, &reload, lvalue.type);

            LunaIrOpcode opcode = LUNA_IR_ADD_INTEGER;
            if (luna_sema_is_float_type(lvalue.type) &&
                !luna_sema_binary_float_opcode(
                    statement->as.assignment.operator_kind, &opcode)) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "operator %s is not defined for floating-point operands",
                    luna_token_kind_name(
                        statement->as.assignment.operator_kind));
                return false;
            }
            if (luna_sema_is_integer_type(lvalue.type)) {
                opcode = luna_sema_binary_integer_opcode(
                    statement->as.assignment.operator_kind);
            }

            LunaIrInstruction operation =
                luna_sema_instruction(opcode, statement->span);
            operation.left = current.id;
            operation.right = value.id;
            value.id = luna_sema_emit_value_instruction(context, &operation,
                                                        lvalue.type);
        }

        return luna_sema_store_lvalue(context, &lvalue, value, statement->span);
    }

    case LUNA_STATEMENT_EXPRESSION:
        (void)luna_sema_lower_expression(context, statement->as.expression);
        return true;

    case LUNA_STATEMENT_IF:
        return luna_sema_lower_if(context, statement);

    case LUNA_STATEMENT_WHILE:
        return luna_sema_lower_while(context, statement);

    case LUNA_STATEMENT_DO:
        return luna_sema_lower_do(context, statement);

    case LUNA_STATEMENT_FOR:
        return luna_sema_lower_for(context, statement);

    case LUNA_STATEMENT_SWITCH:
        return luna_sema_lower_switch(context, statement);

    case LUNA_STATEMENT_BREAK:
    case LUNA_STATEMENT_CONTINUE: {
        if (statement->kind == LUNA_STATEMENT_BREAK) {
            if (context->control_frames.length == 0U) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "break is only valid inside a loop or switch");
                return false;
            }

            LunaSemaControlFrame *frame = luna_vector_at(
                &context->control_frames, context->control_frames.length - 1U);
            if (!context->checking_dead_code) {
                frame->has_live_break = true;
            }
            return luna_sema_emit_jump(context, frame->break_block,
                                       statement->span);
        }

        for (size_t index = context->control_frames.length; index > 0U;
             index -= 1U) {
            LunaSemaControlFrame *frame =
                luna_vector_at(&context->control_frames, index - 1U);
            if (frame->continue_block == LUNA_IR_INVALID_ID) {
                continue;
            }
            if (!context->checking_dead_code) {
                frame->has_live_continue = true;
            }
            return luna_sema_emit_jump(context, frame->continue_block,
                                       statement->span);
        }

        luna_diagnostic_error(context->diagnostics, statement->span,
                              "continue is only valid inside a loop");
        return false;
    }

    case LUNA_STATEMENT_RETURN: {
        const LunaSemaTypeId expected =
            context->current_semantic_function->return_type;
        LunaIrInstruction instruction =
            luna_sema_instruction(LUNA_IR_RETURN, statement->span);

        if (statement->as.return_value == NULL) {
            if (expected != LUNA_TYPE_VOID) {
                luna_diagnostic_error(
                    context->diagnostics, statement->span,
                    "non-void function requires a return value");
                return false;
            }
        } else if (luna_sema_is_memory_type(context, expected)) {
            const LunaCheckedLvalue source =
                luna_sema_lower_memory_assignment_value(
                    context, statement->as.return_value, expected);
            const LunaIrSlotId snapshot_slot = luna_sema_allocate_memory_slot(
                context, expected, statement->as.return_value->span);
            const LunaCheckedLvalue snapshot = {
                .type = expected,
                .storage = LUNA_SEMA_LVALUE_SLOT,
                .slot = snapshot_slot,
                .address = LUNA_IR_INVALID_ID,
                .is_mutable = true,
            };
            if (source.storage == LUNA_SEMA_LVALUE_INVALID ||
                snapshot_slot == LUNA_IR_INVALID_ID ||
                !luna_sema_emit_memory_copy(context, &snapshot, &source,
                                            statement->as.return_value->span)) {
                return false;
            }
            instruction.left = luna_sema_lvalue_address(
                context, &snapshot, statement->as.return_value->span);
            if (instruction.left == LUNA_IR_INVALID_ID) {
                return false;
            }
        } else {
            LunaCheckedValue value = luna_sema_lower_expression_expected(
                context, statement->as.return_value, expected);
            if (!luna_sema_require_type(context, value, expected,
                                        statement->as.return_value->span)) {
                return false;
            }
            instruction.left = value.id;
        }

        if (!luna_sema_append_instruction(context, &instruction)) {
            return false;
        }
        context->reachable = false;
        return true;
    }
    }

    return false;
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
            luna_diagnostic_error(context->diagnostics, function->span,
                                  "duplicate function '%.*s'",
                                  (int)function->name.length,
                                  function->name.data);
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
    luna_diagnostic_error(
        context->diagnostics, syntax->span,
        "project syscall ABI function '%.*s' requires %" PRIu32
        " usize parameter%s and an isize result",
        (int)syntax->name.length, syntax->name.data, expected_parameter_count,
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
        luna_diagnostic_error(
            context->diagnostics, syntax->span,
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
        luna_diagnostic_error(
            context->diagnostics, syntax->return_type.span,
            "fixed arrays have no by-value external C return ABI");
        success = false;
    } else if (luna_sema_is_memory_type(context, return_type)) {
        uint64_t size_bytes = 0U;
        uint32_t alignment_bytes = 0U;
        if (!luna_sema_type_layout(context, return_type, &size_bytes,
                                   &alignment_bytes) ||
            size_bytes == 0U) {
            luna_diagnostic_error(
                context->diagnostics, syntax->return_type.span,
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
            luna_diagnostic_error(context->diagnostics, parameter->span,
                                  "duplicate parameter '%.*s'",
                                  (int)parameter->name.length,
                                  parameter->name.data);
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
            luna_diagnostic_error(context->diagnostics, parameter->span,
                                  "parameter '%.*s' has an invalid type",
                                  (int)parameter->name.length,
                                  parameter->name.data);
            success = false;
            continue;
        }
        if (syntax->is_external &&
            luna_sema_is_array_type(context, parameter_type)) {
            luna_diagnostic_error(
                context->diagnostics, parameter->type.span,
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
                luna_diagnostic_error(
                    context->diagnostics, parameter->type.span,
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
            luna_diagnostic_error(
                context->diagnostics, function->span,
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
        luna_diagnostic_error(
            context->diagnostics, declaration->span,
            "'export' is only allowed on declarations in a module interface");
        success = false;
    }

    for (const LunaFunction *function =
             context->implementation_unit->first_function;
         function != NULL; function = function->next) {
        if (function->is_exported) {
            luna_diagnostic_error(
                context->diagnostics, function->span,
                "'export' is only allowed on declarations in a module "
                "interface");
            success = false;
        }
        if (function->is_external && !function->is_declaration) {
            luna_diagnostic_error(
                context->diagnostics, function->span,
                "external function '%.*s' must be a declaration",
                (int)function->name.length, function->name.data);
            success = false;
        } else if (!function->is_external && function->is_declaration) {
            luna_diagnostic_error(
                context->diagnostics, function->span,
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
        luna_diagnostic_error(
            context->diagnostics, span,
            "%s of implementation function '%.*s' does not match the "
            "interface declaration: expected %s, found %s",
            component, (int)definition->name.length, definition->name.data,
            luna_string_builder_data(&expected_name),
            luna_string_builder_data(&actual_name));
    } else {
        luna_diagnostic_error(
            context->diagnostics, span,
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
        luna_diagnostic_error(
            context->diagnostics, definition->span,
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
        luna_diagnostic_error(context->diagnostics, syntax->span,
                              "duplicate function '%.*s'",
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
                luna_diagnostic_error(
                    context->diagnostics, syntax->span,
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
                    luna_diagnostic_error(
                        context->diagnostics, definition->span,
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
                luna_diagnostic_error(
                    context->diagnostics, declaration->span,
                    "interface function '%.*s' has no implementation "
                    "definition",
                    (int)declaration->name.length, declaration->name.data);
                success = false;
                continue;
            }
            if (definition->is_external || definition->is_declaration) {
                luna_diagnostic_error(
                    context->diagnostics, definition->span,
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
        luna_diagnostic_error_plain(context->diagnostics,
                                    "program has no main function");
        return false;
    }

    if (entry->syntax->is_external) {
        luna_diagnostic_error(
            context->diagnostics, entry->syntax->span,
            "bootstrap entry point 'main' must be defined in Luna");
        return false;
    }

    if (entry->parameter_types.length != 0U ||
        entry->return_type != LUNA_TYPE_I32) {
        luna_diagnostic_error(
            context->diagnostics, entry->syntax->span,
            "bootstrap entry point must be 'fn main() -> i32'");
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
            luna_diagnostic_error(context->diagnostics, parameter->span,
                                  "duplicate parameter '%.*s'",
                                  (int)parameter->name.length,
                                  parameter->name.data);
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
            luna_diagnostic_error(
                context->diagnostics, function->syntax->span,
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
        luna_diagnostic_error_plain(
            context->diagnostics,
            "semantic module input has an invalid import table");
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
