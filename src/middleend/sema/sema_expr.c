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
_Static_assert(sizeof(double) == sizeof(uint64_t),
               "floating literals require 64-bit double");

static LunaSemaTypeId
luna_sema_known_expression_type(LunaSemaContext *context,
                                const LunaExpression *expression);
static bool luna_sema_is_lvalue_syntax(const LunaExpression *expression);

void luna_sema_require_type(LunaSemaContext *context, LunaCheckedValue value,
                            LunaSemaTypeId expected, LunaSourceSpan span) {
    if (value.type == expected || context->failed) {
        return;
    }

    LunaStringBuilder expected_name;
    LunaStringBuilder actual_name;
    luna_string_builder_init(&expected_name);
    luna_string_builder_init(&actual_name);
    const bool formatted =
        luna_sema_append_type_name(context, expected, &expected_name) &&
        luna_sema_append_type_name(context, value.type, &actual_name);
    if (formatted) {
        luna_sema_fail(context, span, "expected %s, found %s",
                       luna_string_builder_data(&expected_name),
                       luna_string_builder_data(&actual_name));
    } else {
        luna_sema_report_allocation_failure(context);
    }
    luna_string_builder_destroy(&actual_name);
    luna_string_builder_destroy(&expected_name);
}

LunaCheckedValue luna_sema_invalid_value(void) {
    return (LunaCheckedValue){
        .id = LUNA_IR_INVALID_ID,
        .type = LUNA_TYPE_INVALID,
    };
}

bool luna_sema_is_integer_type(LunaSemaTypeId type) {
    return type >= (LunaSemaTypeId)LUNA_TYPE_INVALID &&
           type <= (LunaSemaTypeId)LUNA_TYPE_F64 &&
           luna_type_kind_is_integer((LunaTypeKind)type);
}

bool luna_sema_is_float_type(LunaSemaTypeId type) {
    return type >= (LunaSemaTypeId)LUNA_TYPE_INVALID &&
           type <= (LunaSemaTypeId)LUNA_TYPE_F64 &&
           luna_type_kind_is_float((LunaTypeKind)type);
}

bool luna_sema_is_numeric_type(LunaSemaTypeId type) {
    return luna_sema_is_integer_type(type) || luna_sema_is_float_type(type);
}

LunaIrOpcode luna_sema_binary_integer_opcode(LunaTokenKind operator_kind) {
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

bool luna_sema_binary_float_opcode(LunaTokenKind operator_kind,
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

uint64_t luna_sema_integer_maximum(const LunaSemaContext *context,
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
        luna_sema_fail(context, expression->span,
                       "invalid floating-point literal");
        return false;
    }
    if (!finite) {
        luna_sema_fail(context, expression->span,
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
        luna_sema_fail(context, expression->span, "invalid string literal");
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
            luna_sema_fail(context, expression->span,
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
        luna_sema_fail(
            context, span,
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

LunaCheckedValue luna_sema_lower_expression(LunaSemaContext *context,
                                            const LunaExpression *expression) {
    return luna_sema_lower_expression_expected(context, expression,
                                               LUNA_TYPE_INVALID);
}

const LunaSemaEnumMember *
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

bool luna_sema_expression_can_branch(const LunaExpression *expression) {
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

static void luna_sema_emit_null_check(LunaSemaContext *context,
                                      LunaIrValueId address,
                                      LunaSourceSpan span) {
    if (context->failed) {
        return;
    }
    LunaIrInstruction check = luna_sema_instruction(LUNA_IR_NULL_CHECK, span);
    check.left = address;
    luna_sema_append_instruction(context, &check);
}

LunaIrValueId luna_sema_lvalue_address(LunaSemaContext *context,
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

LunaCheckedLvalue luna_sema_lower_lvalue(LunaSemaContext *context,
                                         const LunaExpression *expression) {
    if (expression == NULL || !context->reachable) {
        return luna_sema_invalid_lvalue();
    }

    if (expression->kind == LUNA_EXPRESSION_NAME) {
        const LunaSemaLocal *local =
            luna_sema_find_local(context, expression->as.name);
        if (local == NULL) {
            luna_sema_fail(
                context, expression->span, "unknown local variable '%.*s'",
                (int)expression->as.name.length, expression->as.name.data);
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
            luna_sema_fail(context, expression->span,
                           "dereference requires a pointer operand");
            return luna_sema_invalid_lvalue();
        }
        if (luna_sema_type_kind(context, pointer_type->element_type) ==
            LUNA_TYPE_VOID) {
            luna_sema_fail(context, expression->span,
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
            luna_sema_require_type(context, index, LUNA_TYPE_USIZE,
                                   expression->as.index.index->span);
            if (context->failed) {
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
            if (base.requires_null_check) {
                luna_sema_emit_null_check(context, base_address,
                                          expression->as.index.base->span);
                if (context->failed) {
                    return luna_sema_invalid_lvalue();
                }
            }
            LunaIrInstruction bounds =
                luna_sema_instruction(LUNA_IR_BOUNDS_CHECK, expression->span);
            bounds.left = index.id;
            bounds.immediate = array_count;
            luna_sema_append_instruction(context, &bounds);
            if (context->failed) {
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
            luna_sema_fail(context, expression->span,
                           "indexing requires an array or pointer");
            return luna_sema_invalid_lvalue();
        }
        element_type = pointer_type->element_type;
        const bool pointer_is_read_only = pointer_type->is_read_only;
        if (luna_sema_type_kind(context, element_type) == LUNA_TYPE_VOID) {
            luna_sema_fail(context, expression->span,
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
        luna_sema_require_type(context, index, LUNA_TYPE_USIZE,
                               expression->as.index.index->span);
        if (context->failed) {
            return luna_sema_invalid_lvalue();
        }
        if (preserved_pointer != LUNA_IR_INVALID_ID) {
            pointer =
                luna_sema_reload_value(context, preserved_pointer, pointer.type,
                                       expression->as.index.base->span);
        }
        luna_sema_emit_null_check(context, pointer.id,
                                  expression->as.index.base->span);
        if (context->failed) {
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
                luna_sema_fail(
                    context, expression->as.member.base->span,
                    "'.' member access requires a struct or union lvalue");
                return luna_sema_invalid_lvalue();
            }
            base_address =
                luna_sema_lvalue_address(context, &base, expression->span);
            if (base.requires_null_check) {
                luna_sema_emit_null_check(context, base_address,
                                          expression->as.member.base->span);
                if (context->failed) {
                    return luna_sema_invalid_lvalue();
                }
            }
            is_mutable = base.is_mutable;
        } else if (expression->as.member.operator_kind == LUNA_TOKEN_ARROW) {
            const LunaCheckedValue pointer =
                luna_sema_lower_expression(context, expression->as.member.base);
            const LunaSemaType *pointer_type =
                luna_sema_type(context, pointer.type);
            if (pointer_type == NULL ||
                pointer_type->kind != LUNA_TYPE_POINTER) {
                luna_sema_fail(context, expression->as.member.base->span,
                               "'->' member access requires a pointer");
                return luna_sema_invalid_lvalue();
            }
            aggregate_type = pointer_type->element_type;
            is_mutable = !pointer_type->is_read_only;
            if (!luna_sema_is_record_type(context, aggregate_type)) {
                luna_sema_fail(
                    context, expression->as.member.base->span,
                    "'->' member access requires a pointer to struct or union");
                return luna_sema_invalid_lvalue();
            }
            base_address = pointer.id;
            luna_sema_emit_null_check(context, base_address,
                                      expression->as.member.base->span);
            if (context->failed) {
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
            luna_sema_fail(context, expression->span,
                           "type '%.*s' has no field named '%.*s'",
                           (int)aggregate_name.length, aggregate_name.data,
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

    luna_sema_fail(context, expression->span,
                   "expression is not an assignable lvalue");
    return luna_sema_invalid_lvalue();
}

LunaCheckedValue luna_sema_load_lvalue(LunaSemaContext *context,
                                       const LunaCheckedLvalue *lvalue,
                                       LunaSourceSpan span) {
    if (lvalue->storage == LUNA_SEMA_LVALUE_INVALID) {
        return luna_sema_invalid_value();
    }
    if (luna_sema_is_memory_type(context, lvalue->type)) {
        luna_sema_fail(context, span, "%s",
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
        if (lvalue->requires_null_check) {
            luna_sema_emit_null_check(context, lvalue->address, span);
            if (context->failed) {
                return luna_sema_invalid_value();
            }
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

void luna_sema_store_lvalue(LunaSemaContext *context,
                            const LunaCheckedLvalue *lvalue,
                            LunaCheckedValue value, LunaSourceSpan span) {
    if (lvalue->storage == LUNA_SEMA_LVALUE_INVALID || context->failed) {
        return;
    }
    if (!lvalue->is_mutable) {
        luna_sema_fail(context, span,
                       "cannot assign through an immutable lvalue");
        return;
    }
    if (luna_sema_is_memory_type(context, lvalue->type)) {
        luna_sema_fail(context, span, "%s",
                       luna_sema_is_array_type(context, lvalue->type)
                           ? "fixed arrays cannot be assigned as a whole"
                           : "aggregate objects cannot be assigned as a whole");
        return;
    }
    if (lvalue->requires_null_check) {
        luna_sema_emit_null_check(context, lvalue->address, span);
        if (context->failed) {
            return;
        }
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
    luna_sema_append_instruction(context, &store);
}

bool luna_sema_is_brace_initializer(const LunaExpression *expression) {
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

LunaIrSlotId luna_sema_allocate_memory_slot(LunaSemaContext *context,
                                            LunaSemaTypeId type,
                                            LunaSourceSpan span) {
    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    if (!luna_sema_type_layout(context, type, &size_bytes, &alignment_bytes)) {
        luna_sema_fail(context, span, "%s",
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

void luna_sema_emit_memory_copy(LunaSemaContext *context,
                                const LunaCheckedLvalue *destination,
                                const LunaCheckedLvalue *source,
                                LunaSourceSpan span) {
    if (destination->type != source->type || context->failed ||
        !luna_sema_is_memory_type(context, destination->type)) {
        luna_sema_fail(
            context, span,
            "memory copy requires lvalues of the exact same memory type");
        return;
    }

    uint64_t size_bytes = 0U;
    uint32_t alignment_bytes = 0U;
    if (!luna_sema_type_layout(context, destination->type, &size_bytes,
                               &alignment_bytes) ||
        alignment_bytes == 0U) {
        luna_sema_fail(context, span,
                       "memory copy requires a valid object layout");
        return;
    }

    const LunaIrValueId destination_address =
        luna_sema_lvalue_address(context, destination, span);
    const LunaIrValueId source_address =
        luna_sema_lvalue_address(context, source, span);
    if (destination_address == LUNA_IR_INVALID_ID ||
        source_address == LUNA_IR_INVALID_ID || context->failed) {
        return;
    }
    if (destination->requires_null_check) {
        luna_sema_emit_null_check(context, destination_address, span);
        if (context->failed) {
            return;
        }
    }
    if (source->requires_null_check) {
        luna_sema_emit_null_check(context, source_address, span);
        if (context->failed) {
            return;
        }
    }

    LunaIrInstruction copy = luna_sema_instruction(LUNA_IR_MEMORY_COPY, span);
    copy.left = destination_address;
    copy.right = source_address;
    copy.immediate = size_bytes;
    luna_sema_append_instruction(context, &copy);
}

LunaCheckedLvalue
luna_sema_lower_memory_source(LunaSemaContext *context,
                              const LunaExpression *expression,
                              LunaSemaTypeId expected_type) {
    if (expression->kind == LUNA_EXPRESSION_CALL ||
        expression->kind == LUNA_EXPRESSION_CONDITIONAL) {
        const LunaCheckedValue value = luna_sema_lower_expression_expected(
            context, expression, expected_type);
        luna_sema_require_type(context, value, expected_type, expression->span);
        if (context->failed) {
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
        luna_sema_fail(
            context, expression->span, "%s",
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
    luna_sema_require_type(context,
                           (LunaCheckedValue){
                               .id = LUNA_IR_INVALID_ID,
                               .type = source.type,
                           },
                           expected_type, expression->span);
    if (context->failed) {
        return luna_sema_invalid_lvalue();
    }
    return source;
}

static bool
luna_sema_validate_aggregate_initializer(LunaSemaContext *context,
                                         LunaSemaTypeId type,
                                         const LunaExpression *initializer) {
    if (!luna_sema_is_record_type(context, type)) {
        luna_sema_fail(
            context, initializer->span,
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
                luna_sema_fail(context, field->span,
                               "duplicate initializer field '%.*s'",
                               (int)field->name.length, field->name.data);
                return false;
            }
        }

        if (luna_sema_find_field(context, type, field->name) == NULL) {
            const LunaStringView type_name =
                aggregate != NULL && aggregate->declaration != NULL
                    ? aggregate->declaration->name
                    : (LunaStringView){0};
            luna_sema_fail(context, field->span,
                           "type '%.*s' has no field named '%.*s'",
                           (int)type_name.length, type_name.data,
                           (int)field->name.length, field->name.data);
            return false;
        }
        if (aggregate != NULL && aggregate->kind == LUNA_TYPE_UNION &&
            field_index != 0U) {
            luna_sema_fail(context, field->span,
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
            luna_sema_fail(
                context, field_initializer->span,
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

bool luna_sema_initialize_slot_value(LunaSemaContext *context,
                                     LunaIrSlotId slot,
                                     LunaSemaTypeId root_type,
                                     LunaSemaTypeId value_type, uint64_t offset,
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
        if (destination.storage == LUNA_SEMA_LVALUE_INVALID) {
            return false;
        }
        luna_sema_emit_memory_copy(context, &destination, &source,
                                   initializer->span);
        return !context->failed;
    }

    if (initializer->kind == LUNA_EXPRESSION_AGGREGATE_INITIALIZER) {
        luna_sema_fail(
            context, initializer->span,
            "named aggregate initializer requires a struct or union context");
        return false;
    }

    LunaCheckedValue value =
        luna_sema_lower_expression_expected(context, initializer, value_type);
    luna_sema_require_type(context, value, value_type, initializer->span);
    if (context->failed) {
        return false;
    }
    const LunaCheckedLvalue destination = luna_sema_slot_field_lvalue(
        context, slot, root_type, value_type, offset, initializer->span);
    if (destination.storage == LUNA_SEMA_LVALUE_INVALID) {
        return false;
    }
    luna_sema_store_lvalue(context, &destination, value, initializer->span);
    return !context->failed;
}

void luna_sema_zero_memory_slot(LunaSemaContext *context, LunaIrSlotId slot,
                                LunaSourceSpan span) {
    if (context->failed) {
        return;
    }
    LunaIrInstruction zero = luna_sema_instruction(LUNA_IR_ZERO_SLOT, span);
    zero.slot = slot;
    luna_sema_append_instruction(context, &zero);
}

LunaCheckedLvalue
luna_sema_lower_memory_assignment_value(LunaSemaContext *context,
                                        const LunaExpression *initializer,
                                        LunaSemaTypeId expected_type) {
    if (!luna_sema_is_brace_initializer(initializer)) {
        return luna_sema_lower_memory_source(context, initializer,
                                             expected_type);
    }

    const LunaIrSlotId slot = luna_sema_allocate_memory_slot(
        context, expected_type, initializer->span);
    if (slot == LUNA_IR_INVALID_ID) {
        return luna_sema_invalid_lvalue();
    }
    luna_sema_zero_memory_slot(context, slot, initializer->span);
    if (!luna_sema_initialize_slot_value(context, slot, expected_type,
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
    luna_sema_require_type(context, left, LUNA_TYPE_BOOL,
                           expression->as.binary.left->span);
    if (context->failed) {
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
    luna_sema_require_type(context, right, LUNA_TYPE_BOOL,
                           expression->as.binary.right->span);
    if (context->failed) {
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
    luna_sema_require_type(context, condition, LUNA_TYPE_BOOL,
                           expression->as.conditional.condition->span);
    if (context->failed) {
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
        if (then_value.storage == LUNA_SEMA_LVALUE_INVALID) {
            return luna_sema_invalid_value();
        }
        luna_sema_emit_memory_copy(
            context, &destination, &then_value,
            expression->as.conditional.then_expression->span);
        if (context->failed) {
            return luna_sema_invalid_value();
        }
        luna_sema_emit_jump(context, merge_block, expression->span);
        if (context->failed) {
            return luna_sema_invalid_value();
        }

        luna_sema_set_block(context, else_block);
        const LunaCheckedLvalue else_value =
            luna_sema_lower_memory_assignment_value(
                context, expression->as.conditional.else_expression,
                result_type);
        if (else_value.storage == LUNA_SEMA_LVALUE_INVALID) {
            return luna_sema_invalid_value();
        }
        luna_sema_emit_memory_copy(
            context, &destination, &else_value,
            expression->as.conditional.else_expression->span);
        if (context->failed) {
            return luna_sema_invalid_value();
        }
        luna_sema_emit_jump(context, merge_block, expression->span);
        if (context->failed) {
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
        luna_sema_fail(
            context, expression->span,
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
    luna_sema_require_type(context, then_value, result_type,
                           expression->as.conditional.then_expression->span);
    if (context->failed) {
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
    luna_sema_require_type(context, else_value, result_type,
                           expression->as.conditional.else_expression->span);
    if (context->failed) {
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
        luna_sema_fail(context, expression->span, "unknown function '%.*s'",
                       (int)expression->as.call.name.length,
                       expression->as.call.name.data);
        return luna_sema_invalid_value();
    }

    if (expression->as.call.argument_count != callee->parameter_types.length) {
        luna_sema_fail(context, expression->span,
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
                snapshot_slot == LUNA_IR_INVALID_ID) {
                luna_vector_destroy(&arguments);
                return luna_sema_invalid_value();
            }
            luna_sema_emit_memory_copy(context, &snapshot, &source,
                                       argument->span);
            if (context->failed) {
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
        luna_sema_require_type(context, value, *parameter_type, argument->span);
        if (context->failed) {
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
        luna_sema_append_instruction(context, &call);
        if (context->failed) {
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

LunaCheckedValue
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
            luna_sema_fail(context, expression->span,
                           "integer literal does not fit in %s",
                           luna_type_kind_name(
                               luna_sema_type_kind(context, literal_type)));
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
            luna_sema_fail(context, expression->span,
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
        luna_sema_fail(
            context, expression->span,
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
                luna_sema_fail(context, expression->span,
                               "offsetof requires a struct or union type");
                return luna_sema_invalid_value();
            }
            const LunaSemaField *field = luna_sema_find_field(
                context, queried_type, expression->as.type_query.member_name);
            if (field == NULL) {
                luna_sema_fail(
                    context, expression->span,
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
                luna_sema_fail(
                    context, expression->span,
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
                    luna_sema_fail(context, expression->span,
                                   "enum '%.*s' has no member named '%.*s'",
                                   (int)named_type->syntax->name.length,
                                   named_type->syntax->name.data,
                                   (int)expression->as.member.name.length,
                                   expression->as.member.name.data);
                } else {
                    luna_sema_fail(
                        context, expression->span,
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
                luna_sema_fail(
                    context, expression->span,
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
                luna_sema_fail(
                    context, expression->span,
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
            luna_sema_fail(
                context, expression->span,
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
                luna_sema_fail(context, expression->span,
                               "unary '+' requires a numeric operand");
                return luna_sema_invalid_value();
            }
            luna_sema_require_type(context, operand, required_type,
                                   expression->span);
            if (context->failed) {
                return luna_sema_invalid_value();
            }
            return operand;

        case LUNA_TOKEN_MINUS:
            if (!luna_sema_is_numeric_type(operand.type)) {
                luna_sema_fail(context, expression->span,
                               "unary '-' requires a numeric operand");
                return luna_sema_invalid_value();
            }
            luna_sema_require_type(context, operand, required_type,
                                   expression->as.unary.operand->span);
            if (context->failed) {
                return luna_sema_invalid_value();
            }
            break;

        case LUNA_TOKEN_TILDE:
            if (!luna_sema_is_integer_type(operand.type)) {
                luna_sema_fail(context, expression->span,
                               "unary '~' requires an integer operand");
                return luna_sema_invalid_value();
            }
            luna_sema_require_type(context, operand, required_type,
                                   expression->as.unary.operand->span);
            if (context->failed) {
                return luna_sema_invalid_value();
            }
            break;

        case LUNA_TOKEN_BANG:
            luna_sema_require_type(context, operand, LUNA_TYPE_BOOL,
                                   expression->as.unary.operand->span);
            if (context->failed) {
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
    luna_sema_require_type(context, left, operand_type,
                           expression->as.binary.left->span);
    if (context->failed) {
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
    luna_sema_require_type(context, right, operand_type,
                           expression->as.binary.right->span);
    if (context->failed) {
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
        luna_sema_fail(context, expression->span,
                       "equality requires bool, numeric, enum, or "
                       "exact pointer operands");
        return luna_sema_invalid_value();
    }
    if (is_relational && !luna_sema_is_numeric_type(operand_type)) {
        luna_sema_fail(context, expression->span,
                       "ordering requires numeric operands");
        return luna_sema_invalid_value();
    }
    if (!is_equality && !is_relational &&
        !luna_sema_is_numeric_type(operand_type)) {
        luna_sema_fail(context, expression->span,
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
            luna_sema_fail(
                context, expression->span,
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
