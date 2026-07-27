#include "luna/frontend/parser/parser.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint32_t luna_parser_max_nesting_depth = 256U;

static bool luna_parser_enter_nesting(LunaParser *parser, LunaSourceSpan span) {
    if (parser->nesting_depth >= luna_parser_max_nesting_depth) {
        luna_diagnostic_error(parser->diagnostics, span,
                              "maximum parser nesting depth exceeded");
        return false;
    }

    parser->nesting_depth += 1U;
    return true;
}

static void luna_parser_leave_nesting(LunaParser *parser) {
    parser->nesting_depth -= 1U;
}

static void *luna_parser_allocate(LunaParser *parser, size_t size,
                                  size_t alignment) {
    void *allocation = luna_arena_allocate_zero(parser->arena, size, alignment);
    if (allocation == NULL) {
        luna_diagnostic_error_plain(parser->diagnostics,
                                    "out of memory while parsing");
    }
    return allocation;
}

static LunaStringView luna_parser_token_text(LunaToken token) {
    return luna_source_span_text(token.span);
}

static LunaSourceSpan luna_parser_join_spans(LunaSourceSpan first,
                                             LunaSourceSpan last) {
    if (first.source == NULL || first.source != last.source) {
        return first;
    }

    const size_t last_end = last.offset + last.length;
    return (LunaSourceSpan){
        .source = first.source,
        .offset = first.offset,
        .length = last_end >= first.offset ? last_end - first.offset : 0U,
        .line = first.line,
        .column = first.column,
    };
}

static void luna_parser_advance(LunaParser *parser) {
    parser->previous = parser->current;
    do {
        parser->current = luna_lexer_next(&parser->lexer);
    } while (parser->current.kind == LUNA_TOKEN_INVALID);
}

static bool luna_parser_check(const LunaParser *parser, LunaTokenKind kind) {
    return parser->current.kind == kind;
}

static bool luna_parser_match(LunaParser *parser, LunaTokenKind kind) {
    if (!luna_parser_check(parser, kind)) {
        return false;
    }

    luna_parser_advance(parser);
    return true;
}

static bool luna_parser_expect(LunaParser *parser, LunaTokenKind kind,
                               const char *context) {
    if (luna_parser_match(parser, kind)) {
        return true;
    }

    luna_diagnostic_error(parser->diagnostics, parser->current.span,
                          "expected %s %s, found %s",
                          luna_token_kind_name(kind), context,
                          luna_token_kind_name(parser->current.kind));
    return false;
}

static bool luna_parser_parse_integer(LunaParser *parser, LunaToken token,
                                      uint64_t *value);

static LunaTypeRef *luna_parser_allocate_type_ref(LunaParser *parser,
                                                  LunaTypeRef type) {
    LunaTypeRef *result = luna_parser_allocate(parser, sizeof(LunaTypeRef),
                                               _Alignof(LunaTypeRef));
    if (result != NULL) {
        *result = type;
    }
    return result;
}

static LunaTypeRef luna_parser_parse_type(LunaParser *parser) {
    const LunaToken token = parser->current;
    LunaTypeKind kind = LUNA_TYPE_INVALID;

    if (luna_parser_match(parser, LUNA_TOKEN_STAR)) {
        if (!luna_parser_enter_nesting(parser, token.span)) {
            return (LunaTypeRef){
                .kind = LUNA_TYPE_INVALID,
                .span = token.span,
            };
        }
        const bool is_read_only = luna_parser_match(parser, LUNA_TOKEN_CONST);
        const LunaTypeRef pointee = luna_parser_parse_type(parser);
        luna_parser_leave_nesting(parser);
        return (LunaTypeRef){
            .kind = LUNA_TYPE_POINTER,
            .span = luna_parser_join_spans(token.span, pointee.span),
            .as.pointer =
                {
                    .pointee = luna_parser_allocate_type_ref(parser, pointee),
                    .is_read_only = is_read_only,
                },
        };
    }

    if (luna_parser_match(parser, LUNA_TOKEN_LEFT_BRACKET)) {
        if (!luna_parser_enter_nesting(parser, token.span)) {
            return (LunaTypeRef){
                .kind = LUNA_TYPE_INVALID,
                .span = token.span,
            };
        }
        const LunaToken count_token = parser->current;
        uint64_t count = 0U;
        if (luna_parser_expect(parser, LUNA_TOKEN_INTEGER,
                               "as fixed-array length")) {
            (void)luna_parser_parse_integer(parser, count_token, &count);
        }
        (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACKET,
                                 "after fixed-array length");
        const LunaTypeRef element = luna_parser_parse_type(parser);
        luna_parser_leave_nesting(parser);
        return (LunaTypeRef){
            .kind = LUNA_TYPE_ARRAY,
            .span = luna_parser_join_spans(token.span, element.span),
            .as.array =
                {
                    .element = luna_parser_allocate_type_ref(parser, element),
                    .count = count,
                },
        };
    }

    switch (token.kind) {
    case LUNA_TOKEN_IDENTIFIER:
        luna_parser_advance(parser);
        return (LunaTypeRef){
            .kind = LUNA_TYPE_NAMED,
            .span = token.span,
            .as.name = luna_parser_token_text(token),
        };
    case LUNA_TOKEN_BOOL:
        kind = LUNA_TYPE_BOOL;
        break;
    case LUNA_TOKEN_I8:
        kind = LUNA_TYPE_I8;
        break;
    case LUNA_TOKEN_I16:
        kind = LUNA_TYPE_I16;
        break;
    case LUNA_TOKEN_I32:
        kind = LUNA_TYPE_I32;
        break;
    case LUNA_TOKEN_I64:
        kind = LUNA_TYPE_I64;
        break;
    case LUNA_TOKEN_ISIZE:
        kind = LUNA_TYPE_ISIZE;
        break;
    case LUNA_TOKEN_U8:
        kind = LUNA_TYPE_U8;
        break;
    case LUNA_TOKEN_U16:
        kind = LUNA_TYPE_U16;
        break;
    case LUNA_TOKEN_U32:
        kind = LUNA_TYPE_U32;
        break;
    case LUNA_TOKEN_U64:
        kind = LUNA_TYPE_U64;
        break;
    case LUNA_TOKEN_USIZE:
        kind = LUNA_TYPE_USIZE;
        break;
    case LUNA_TOKEN_F32:
        kind = LUNA_TYPE_F32;
        break;
    case LUNA_TOKEN_F64:
        kind = LUNA_TYPE_F64;
        break;
    case LUNA_TOKEN_VOID:
        kind = LUNA_TYPE_VOID;
        break;

    default:
        luna_diagnostic_error(parser->diagnostics, token.span,
                              "expected a type, found %s",
                              luna_token_kind_name(token.kind));
        return (LunaTypeRef){
            .kind = LUNA_TYPE_INVALID,
            .span = token.span,
        };
    }

    luna_parser_advance(parser);
    return (LunaTypeRef){
        .kind = kind,
        .span = token.span,
    };
}

static bool luna_parser_parse_integer(LunaParser *parser, LunaToken token,
                                      uint64_t *value) {
    const LunaStringView text = luna_parser_token_text(token);
    size_t index = 0U;
    uint64_t base = 10U;

    if (text.length >= 2U && text.data[0] == '0') {
        switch (text.data[1]) {
        case 'b':
        case 'B':
            base = 2U;
            index = 2U;
            break;
        case 'o':
        case 'O':
            base = 8U;
            index = 2U;
            break;
        case 'x':
        case 'X':
            base = 16U;
            index = 2U;
            break;
        default:
            break;
        }
    }

    uint64_t result = 0U;
    bool saw_digit = false;
    bool previous_was_separator = false;

    for (; index < text.length; index += 1U) {
        const char character = text.data[index];
        if (character == '_') {
            if (!saw_digit || previous_was_separator) {
                luna_diagnostic_error(
                    parser->diagnostics, token.span,
                    "invalid digit separator in integer literal");
                return false;
            }
            previous_was_separator = true;
            continue;
        }

        uint64_t digit = 0U;
        if (character >= '0' && character <= '9') {
            digit = (uint64_t)(unsigned int)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (uint64_t)(unsigned int)(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = (uint64_t)(unsigned int)(character - 'A' + 10);
        } else {
            luna_diagnostic_error(parser->diagnostics, token.span,
                                  "invalid digit in integer literal");
            return false;
        }

        if (digit >= base) {
            luna_diagnostic_error(parser->diagnostics, token.span,
                                  "digit is not valid for base %llu",
                                  (unsigned long long)base);
            return false;
        }

        if (result > (UINT64_MAX - digit) / base) {
            luna_diagnostic_error(parser->diagnostics, token.span,
                                  "integer literal is too large");
            return false;
        }
        result *= base;
        result += digit;
        saw_digit = true;
        previous_was_separator = false;
    }

    if (!saw_digit || previous_was_separator) {
        luna_diagnostic_error(parser->diagnostics, token.span,
                              "invalid integer literal");
        return false;
    }

    *value = result;
    return true;
}

static bool luna_parser_scan_float_digits(LunaStringView text, size_t *index) {
    bool saw_digit = false;
    bool previous_was_separator = false;

    while (*index < text.length) {
        const char character = text.data[*index];
        if (character >= '0' && character <= '9') {
            saw_digit = true;
            previous_was_separator = false;
            *index += 1U;
            continue;
        }
        if (character == '_') {
            if (!saw_digit || previous_was_separator ||
                *index + 1U >= text.length || text.data[*index + 1U] < '0' ||
                text.data[*index + 1U] > '9') {
                return false;
            }
            previous_was_separator = true;
            *index += 1U;
            continue;
        }
        break;
    }

    return saw_digit && !previous_was_separator;
}

static bool luna_parser_validate_float(LunaParser *parser, LunaToken token) {
    const LunaStringView text = luna_parser_token_text(token);
    size_t index = 0U;
    bool has_fraction = false;
    bool has_exponent = false;

    bool valid = luna_parser_scan_float_digits(text, &index);
    if (valid && index < text.length && text.data[index] == '.') {
        has_fraction = true;
        index += 1U;
        valid = luna_parser_scan_float_digits(text, &index);
    }

    if (valid && index < text.length &&
        (text.data[index] == 'e' || text.data[index] == 'E')) {
        has_exponent = true;
        index += 1U;
        if (index < text.length &&
            (text.data[index] == '+' || text.data[index] == '-')) {
            index += 1U;
        }
        valid = luna_parser_scan_float_digits(text, &index);
    }

    valid = valid && (has_fraction || has_exponent) && index == text.length;
    if (!valid) {
        luna_diagnostic_error(parser->diagnostics, token.span,
                              "invalid floating-point literal");
    }
    return valid;
}

static LunaExpression *luna_parser_parse_expression(LunaParser *parser);

static LunaExpression *luna_parser_new_expression(LunaParser *parser,
                                                  LunaExpressionKind kind,
                                                  LunaSourceSpan span) {
    LunaExpression *expression = luna_parser_allocate(
        parser, sizeof(LunaExpression), _Alignof(LunaExpression));
    if (expression == NULL) {
        return NULL;
    }

    expression->kind = kind;
    expression->span = span;
    expression->checked_type = LUNA_TYPE_INVALID;
    return expression;
}

static LunaExpression *luna_parser_parse_call(LunaParser *parser,
                                              LunaToken name_token) {
    LunaExpression *call = luna_parser_new_expression(
        parser, LUNA_EXPRESSION_CALL, name_token.span);
    if (call == NULL) {
        return NULL;
    }

    call->as.call.name = luna_parser_token_text(name_token);
    LunaExpression **next_argument = &call->as.call.first_argument;
    if (!luna_parser_enter_nesting(parser, name_token.span)) {
        return call;
    }

    if (!luna_parser_check(parser, LUNA_TOKEN_RIGHT_PAREN)) {
        do {
            LunaExpression *argument = luna_parser_parse_expression(parser);
            if (argument == NULL) {
                luna_parser_leave_nesting(parser);
                return call;
            }

            *next_argument = argument;
            next_argument = &argument->next;
            call->as.call.argument_count += 1U;
        } while (luna_parser_match(parser, LUNA_TOKEN_COMMA));
    }

    if (!luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                            "after function arguments")) {
        luna_parser_leave_nesting(parser);
        return call;
    }

    luna_parser_leave_nesting(parser);
    call->span = luna_parser_join_spans(name_token.span, parser->previous.span);
    return call;
}

static LunaExpression *luna_parser_parse_primary(LunaParser *parser) {
    const LunaToken token = parser->current;

    if (luna_parser_match(parser, LUNA_TOKEN_SIZEOF) ||
        luna_parser_match(parser, LUNA_TOKEN_ALIGNOF) ||
        luna_parser_match(parser, LUNA_TOKEN_OFFSETOF)) {
        if (!luna_parser_enter_nesting(parser, token.span)) {
            return NULL;
        }
        (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN,
                                 "after layout query");
        const LunaTypeRef type = luna_parser_parse_type(parser);
        LunaStringView member_name = {0};
        if (token.kind == LUNA_TOKEN_OFFSETOF) {
            (void)luna_parser_expect(parser, LUNA_TOKEN_COMMA,
                                     "after offsetof type");
            const LunaToken member_token = parser->current;
            if (luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                   "as offsetof field name")) {
                member_name = luna_parser_token_text(member_token);
            }
        }
        (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                                 "after layout query");
        luna_parser_leave_nesting(parser);

        LunaExpressionKind kind = LUNA_EXPRESSION_SIZEOF;
        if (token.kind == LUNA_TOKEN_ALIGNOF) {
            kind = LUNA_EXPRESSION_ALIGNOF;
        } else if (token.kind == LUNA_TOKEN_OFFSETOF) {
            kind = LUNA_EXPRESSION_OFFSETOF;
        }
        LunaExpression *expression = luna_parser_new_expression(
            parser, kind,
            luna_parser_join_spans(token.span, parser->previous.span));
        if (expression != NULL) {
            expression->as.type_query.type = type;
            expression->as.type_query.member_name = member_name;
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_INTEGER)) {
        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_INTEGER, token.span);
        if (expression != NULL) {
            uint64_t value = 0U;
            (void)luna_parser_parse_integer(parser, token, &value);
            expression->as.integer = value;
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_FLOAT)) {
        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_FLOAT, token.span);
        if (expression != NULL) {
            (void)luna_parser_validate_float(parser, token);
            expression->as.floating = luna_parser_token_text(token);
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_TRUE) ||
        luna_parser_match(parser, LUNA_TOKEN_FALSE)) {
        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_BOOLEAN, token.span);
        if (expression != NULL) {
            expression->as.boolean = token.kind == LUNA_TOKEN_TRUE;
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_STRING)) {
        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_STRING, token.span);
        if (expression != NULL) {
            expression->as.string = luna_parser_token_text(token);
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_NULL)) {
        return luna_parser_new_expression(parser, LUNA_EXPRESSION_NULL,
                                          token.span);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_LEFT_BRACE)) {
        (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACE,
                                 "for zero initializer");
        return luna_parser_new_expression(
            parser, LUNA_EXPRESSION_ZERO_INITIALIZER,
            luna_parser_join_spans(token.span, parser->previous.span));
    }

    if (luna_parser_match(parser, LUNA_TOKEN_IDENTIFIER)) {
        if (luna_parser_match(parser, LUNA_TOKEN_LEFT_PAREN)) {
            return luna_parser_parse_call(parser, token);
        }

        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_NAME, token.span);
        if (expression != NULL) {
            expression->as.name = luna_parser_token_text(token);
        }
        return expression;
    }

    if (luna_parser_match(parser, LUNA_TOKEN_LEFT_PAREN)) {
        if (!luna_parser_enter_nesting(parser, token.span)) {
            return NULL;
        }
        LunaExpression *expression = luna_parser_parse_expression(parser);
        luna_parser_leave_nesting(parser);
        (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                                 "after expression");
        return expression;
    }

    luna_diagnostic_error(parser->diagnostics, token.span,
                          "expected an expression, found %s",
                          luna_token_kind_name(token.kind));

    if (!luna_parser_check(parser, LUNA_TOKEN_END)) {
        luna_parser_advance(parser);
    }

    return NULL;
}

static LunaExpression *luna_parser_parse_postfix(LunaParser *parser) {
    LunaExpression *expression = luna_parser_parse_primary(parser);
    while (expression != NULL) {
        if (luna_parser_match(parser, LUNA_TOKEN_LEFT_BRACKET)) {
            const LunaToken left_bracket = parser->previous;
            if (!luna_parser_enter_nesting(parser, left_bracket.span)) {
                return expression;
            }
            LunaExpression *index = luna_parser_parse_expression(parser);
            (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACKET,
                                     "after index expression");
            luna_parser_leave_nesting(parser);
            if (index == NULL) {
                return expression;
            }

            LunaExpression *indexed = luna_parser_new_expression(
                parser, LUNA_EXPRESSION_INDEX,
                luna_parser_join_spans(expression->span,
                                       parser->previous.span));
            if (indexed == NULL) {
                return expression;
            }
            indexed->as.index.base = expression;
            indexed->as.index.index = index;
            expression = indexed;
            continue;
        }

        if (luna_parser_check(parser, LUNA_TOKEN_DOT) ||
            luna_parser_check(parser, LUNA_TOKEN_ARROW)) {
            const LunaToken operator_token = parser->current;
            luna_parser_advance(parser);
            const LunaToken name_token = parser->current;
            if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                    "as member name")) {
                return expression;
            }

            LunaExpression *member = luna_parser_new_expression(
                parser, LUNA_EXPRESSION_MEMBER,
                luna_parser_join_spans(expression->span, name_token.span));
            if (member == NULL) {
                return expression;
            }
            member->as.member.base = expression;
            member->as.member.name = luna_parser_token_text(name_token);
            member->as.member.operator_kind = operator_token.kind;
            expression = member;
            continue;
        }
        break;
    }
    return expression;
}

static bool luna_parser_is_unary_operator(LunaTokenKind kind) {
    return kind == LUNA_TOKEN_PLUS || kind == LUNA_TOKEN_MINUS ||
           kind == LUNA_TOKEN_BANG || kind == LUNA_TOKEN_TILDE ||
           kind == LUNA_TOKEN_STAR || kind == LUNA_TOKEN_AMPERSAND;
}

static LunaExpression *luna_parser_parse_unary(LunaParser *parser) {
    if (!luna_parser_is_unary_operator(parser->current.kind)) {
        return luna_parser_parse_postfix(parser);
    }

    const LunaToken operator_token = parser->current;
    if (!luna_parser_enter_nesting(parser, operator_token.span)) {
        return NULL;
    }
    luna_parser_advance(parser);
    LunaExpression *operand = luna_parser_parse_unary(parser);
    luna_parser_leave_nesting(parser);
    if (operand == NULL) {
        return NULL;
    }

    LunaExpression *expression = luna_parser_new_expression(
        parser, LUNA_EXPRESSION_UNARY,
        luna_parser_join_spans(operator_token.span, operand->span));
    if (expression != NULL) {
        expression->as.unary.operator_kind = operator_token.kind;
        expression->as.unary.operand = operand;
    }
    return expression;
}

static LunaExpression *luna_parser_parse_cast(LunaParser *parser) {
    LunaExpression *expression = luna_parser_parse_unary(parser);
    while (expression != NULL && luna_parser_match(parser, LUNA_TOKEN_AS)) {
        const LunaTypeRef target_type = luna_parser_parse_type(parser);
        LunaExpression *cast = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_CAST,
            luna_parser_join_spans(expression->span, target_type.span));
        if (cast == NULL) {
            return expression;
        }
        cast->as.cast.operand = expression;
        cast->as.cast.target_type = target_type;
        expression = cast;
    }
    return expression;
}

static int luna_parser_binary_precedence(LunaTokenKind kind) {
    switch (kind) {
    case LUNA_TOKEN_LOGICAL_OR:
        return 1;
    case LUNA_TOKEN_LOGICAL_AND:
        return 2;
    case LUNA_TOKEN_PIPE:
        return 3;
    case LUNA_TOKEN_CARET:
        return 4;
    case LUNA_TOKEN_AMPERSAND:
        return 5;
    case LUNA_TOKEN_EQUAL_EQUAL:
    case LUNA_TOKEN_BANG_EQUAL:
        return 6;
    case LUNA_TOKEN_LESS:
    case LUNA_TOKEN_LESS_EQUAL:
    case LUNA_TOKEN_GREATER:
    case LUNA_TOKEN_GREATER_EQUAL:
        return 7;
    case LUNA_TOKEN_SHIFT_LEFT:
    case LUNA_TOKEN_SHIFT_RIGHT:
        return 8;
    case LUNA_TOKEN_PLUS:
    case LUNA_TOKEN_MINUS:
        return 9;
    case LUNA_TOKEN_STAR:
    case LUNA_TOKEN_SLASH:
    case LUNA_TOKEN_PERCENT:
        return 10;
    default:
        return 0;
    }
}

static LunaExpression *luna_parser_parse_binary(LunaParser *parser,
                                                int minimum_precedence) {
    LunaExpression *left = luna_parser_parse_cast(parser);
    if (left == NULL) {
        return NULL;
    }

    for (;;) {
        const int precedence =
            luna_parser_binary_precedence(parser->current.kind);
        if (precedence < minimum_precedence) {
            break;
        }

        const LunaToken operator_token = parser->current;
        luna_parser_advance(parser);

        LunaExpression *right =
            luna_parser_parse_binary(parser, precedence + 1);
        if (right == NULL) {
            return left;
        }

        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_BINARY,
            luna_parser_join_spans(left->span, right->span));
        if (expression == NULL) {
            return left;
        }

        expression->as.binary.operator_kind = operator_token.kind;
        expression->as.binary.left = left;
        expression->as.binary.right = right;
        left = expression;
    }

    return left;
}

static LunaExpression *luna_parser_parse_conditional(LunaParser *parser) {
    LunaExpression *condition = luna_parser_parse_binary(parser, 1);
    if (condition == NULL || !luna_parser_match(parser, LUNA_TOKEN_QUESTION)) {
        return condition;
    }

    const LunaToken question_token = parser->previous;
    if (!luna_parser_enter_nesting(parser, question_token.span)) {
        return condition;
    }

    LunaExpression *then_expression = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_COLON,
                             "between conditional operands");
    LunaExpression *else_expression = luna_parser_parse_conditional(parser);
    luna_parser_leave_nesting(parser);
    if (then_expression == NULL || else_expression == NULL) {
        return condition;
    }

    LunaExpression *expression = luna_parser_new_expression(
        parser, LUNA_EXPRESSION_CONDITIONAL,
        luna_parser_join_spans(condition->span, else_expression->span));
    if (expression != NULL) {
        expression->as.conditional.condition = condition;
        expression->as.conditional.then_expression = then_expression;
        expression->as.conditional.else_expression = else_expression;
    }
    return expression;
}

static LunaExpression *luna_parser_parse_expression(LunaParser *parser) {
    return luna_parser_parse_conditional(parser);
}

static LunaBlock *luna_parser_parse_block(LunaParser *parser);

static LunaStatement *luna_parser_new_statement(LunaParser *parser,
                                                LunaStatementKind kind,
                                                LunaSourceSpan span) {
    LunaStatement *statement = luna_parser_allocate(
        parser, sizeof(LunaStatement), _Alignof(LunaStatement));
    if (statement != NULL) {
        statement->kind = kind;
        statement->span = span;
    }
    return statement;
}

static LunaStatement *luna_parser_wrap_block(LunaParser *parser,
                                             LunaBlock *block) {
    if (block == NULL) {
        return NULL;
    }

    LunaStatement *statement =
        luna_parser_new_statement(parser, LUNA_STATEMENT_BLOCK, block->span);
    if (statement != NULL) {
        statement->as.block = *block;
    }
    return statement;
}

static LunaStatement *luna_parser_parse_if_statement(LunaParser *parser) {
    const LunaToken if_token = parser->previous;
    if (!luna_parser_enter_nesting(parser, if_token.span)) {
        return NULL;
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN, "after 'if'");
    LunaExpression *condition = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                             "after if condition");

    LunaBlock *then_block = luna_parser_parse_block(parser);
    LunaStatement *else_branch = NULL;

    if (luna_parser_match(parser, LUNA_TOKEN_ELSE)) {
        if (luna_parser_match(parser, LUNA_TOKEN_IF)) {
            else_branch = luna_parser_parse_if_statement(parser);
        } else {
            else_branch =
                luna_parser_wrap_block(parser, luna_parser_parse_block(parser));
        }
    }

    LunaSourceSpan end_span = if_token.span;
    if (else_branch != NULL) {
        end_span = else_branch->span;
    } else if (then_block != NULL) {
        end_span = then_block->span;
    }

    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_IF,
        luna_parser_join_spans(if_token.span, end_span));
    if (statement != NULL) {
        statement->as.if_statement.condition = condition;
        statement->as.if_statement.then_block = then_block;
        statement->as.if_statement.else_branch = else_branch;
    }
    luna_parser_leave_nesting(parser);
    return statement;
}

static LunaStatement *luna_parser_parse_while_statement(LunaParser *parser) {
    const LunaToken while_token = parser->previous;
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN, "after 'while'");
    LunaExpression *condition = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                             "after while condition");
    LunaBlock *body = luna_parser_parse_block(parser);

    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_WHILE,
        body == NULL ? while_token.span
                     : luna_parser_join_spans(while_token.span, body->span));
    if (statement != NULL) {
        statement->as.while_statement.condition = condition;
        statement->as.while_statement.body = body;
    }
    return statement;
}

static LunaStatement *
luna_parser_parse_declaration(LunaParser *parser, bool is_mutable,
                              LunaSourceSpan keyword_span) {
    const LunaToken name_token = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                            "as variable name")) {
        return NULL;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_COLON, "after variable name");
    const LunaTypeRef type = luna_parser_parse_type(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_EQUAL,
                             "before variable initializer");
    LunaExpression *initializer = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                             "after variable declaration");

    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_DECLARATION,
        luna_parser_join_spans(keyword_span, parser->previous.span));
    if (statement != NULL) {
        statement->as.declaration.is_mutable = is_mutable;
        statement->as.declaration.name = luna_parser_token_text(name_token);
        statement->as.declaration.type = type;
        statement->as.declaration.initializer = initializer;
    }
    return statement;
}

static bool luna_parser_is_assignment_operator(LunaTokenKind kind) {
    switch (kind) {
    case LUNA_TOKEN_EQUAL:
    case LUNA_TOKEN_PLUS_EQUAL:
    case LUNA_TOKEN_MINUS_EQUAL:
    case LUNA_TOKEN_STAR_EQUAL:
    case LUNA_TOKEN_SLASH_EQUAL:
    case LUNA_TOKEN_PERCENT_EQUAL:
    case LUNA_TOKEN_AMPERSAND_EQUAL:
    case LUNA_TOKEN_PIPE_EQUAL:
    case LUNA_TOKEN_CARET_EQUAL:
    case LUNA_TOKEN_SHIFT_LEFT_EQUAL:
    case LUNA_TOKEN_SHIFT_RIGHT_EQUAL:
        return true;

    default:
        return false;
    }
}

static LunaStatement *luna_parser_parse_expression_or_assignment_statement(
    LunaParser *parser, LunaTokenKind terminator, const char *context) {
    LunaExpression *expression = luna_parser_parse_expression(parser);
    if (expression == NULL) {
        return NULL;
    }

    if (luna_parser_is_assignment_operator(parser->current.kind)) {
        const LunaToken operator_token = parser->current;
        luna_parser_advance(parser);
        LunaExpression *value = luna_parser_parse_expression(parser);
        (void)luna_parser_expect(parser, terminator, context);

        LunaStatement *statement = luna_parser_new_statement(
            parser, LUNA_STATEMENT_ASSIGNMENT,
            luna_parser_join_spans(expression->span, parser->previous.span));
        if (statement != NULL) {
            statement->as.assignment.target = expression;
            statement->as.assignment.operator_kind = operator_token.kind;
            statement->as.assignment.value = value;
        }
        return statement;
    }

    (void)luna_parser_expect(parser, terminator, context);
    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_EXPRESSION,
        luna_parser_join_spans(expression->span, parser->previous.span));
    if (statement != NULL) {
        statement->as.expression = expression;
    }
    return statement;
}

static LunaStatement *luna_parser_parse_do_statement(LunaParser *parser) {
    const LunaToken do_token = parser->previous;
    if (!luna_parser_enter_nesting(parser, do_token.span)) {
        return NULL;
    }

    LunaBlock *body = luna_parser_parse_block(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_WHILE, "after do body");
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN, "after 'while'");
    LunaExpression *condition = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                             "after do-while condition");
    (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                             "after do-while statement");

    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_DO,
        luna_parser_join_spans(do_token.span, parser->previous.span));
    if (statement != NULL) {
        statement->as.do_statement.body = body;
        statement->as.do_statement.condition = condition;
    }
    luna_parser_leave_nesting(parser);
    return statement;
}

static LunaStatement *luna_parser_parse_for_statement(LunaParser *parser) {
    const LunaToken for_token = parser->previous;
    if (!luna_parser_enter_nesting(parser, for_token.span)) {
        return NULL;
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN, "after 'for'");

    LunaStatement *initializer = NULL;
    if (!luna_parser_match(parser, LUNA_TOKEN_SEMICOLON)) {
        if (luna_parser_match(parser, LUNA_TOKEN_LET)) {
            initializer = luna_parser_parse_declaration(parser, false,
                                                        parser->previous.span);
        } else if (luna_parser_match(parser, LUNA_TOKEN_VAR)) {
            initializer = luna_parser_parse_declaration(parser, true,
                                                        parser->previous.span);
        } else {
            initializer = luna_parser_parse_expression_or_assignment_statement(
                parser, LUNA_TOKEN_SEMICOLON, "after for initializer");
        }
    }

    LunaExpression *condition = NULL;
    if (!luna_parser_match(parser, LUNA_TOKEN_SEMICOLON)) {
        condition = luna_parser_parse_expression(parser);
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                                 "after for condition");
    }

    LunaStatement *update = NULL;
    if (!luna_parser_match(parser, LUNA_TOKEN_RIGHT_PAREN)) {
        update = luna_parser_parse_expression_or_assignment_statement(
            parser, LUNA_TOKEN_RIGHT_PAREN, "after for update");
    }

    LunaBlock *body = luna_parser_parse_block(parser);
    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_FOR,
        body == NULL ? for_token.span
                     : luna_parser_join_spans(for_token.span, body->span));
    if (statement != NULL) {
        statement->as.for_statement.initializer = initializer;
        statement->as.for_statement.condition = condition;
        statement->as.for_statement.update = update;
        statement->as.for_statement.body = body;
    }
    luna_parser_leave_nesting(parser);
    return statement;
}

static LunaExpression *luna_parser_parse_switch_label(LunaParser *parser) {
    if (luna_parser_check(parser, LUNA_TOKEN_IDENTIFIER)) {
        return luna_parser_parse_postfix(parser);
    }

    LunaToken sign_token = {
        .kind = LUNA_TOKEN_INVALID,
    };
    if (luna_parser_check(parser, LUNA_TOKEN_PLUS) ||
        luna_parser_check(parser, LUNA_TOKEN_MINUS)) {
        sign_token = parser->current;
        luna_parser_advance(parser);
    }

    const LunaToken literal_token = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_INTEGER,
                            "as switch case label")) {
        return NULL;
    }

    LunaExpression *literal = luna_parser_new_expression(
        parser, LUNA_EXPRESSION_INTEGER, literal_token.span);
    if (literal == NULL) {
        return NULL;
    }
    (void)luna_parser_parse_integer(parser, literal_token,
                                    &literal->as.integer);

    if (sign_token.kind == LUNA_TOKEN_INVALID) {
        return literal;
    }

    LunaExpression *expression = luna_parser_new_expression(
        parser, LUNA_EXPRESSION_UNARY,
        luna_parser_join_spans(sign_token.span, literal->span));
    if (expression != NULL) {
        expression->as.unary.operator_kind = sign_token.kind;
        expression->as.unary.operand = literal;
    }
    return expression;
}

static LunaStatement *luna_parser_parse_switch_statement(LunaParser *parser) {
    const LunaToken switch_token = parser->previous;
    if (!luna_parser_enter_nesting(parser, switch_token.span)) {
        return NULL;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN, "after 'switch'");
    LunaExpression *expression = luna_parser_parse_expression(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                             "after switch expression");
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_BRACE, "to begin switch");

    LunaSwitchArm *first_arm = NULL;
    LunaSwitchArm **next_arm = &first_arm;
    uint32_t arm_count = 0U;
    while (!luna_parser_check(parser, LUNA_TOKEN_RIGHT_BRACE) &&
           !luna_parser_check(parser, LUNA_TOKEN_END)) {
        const LunaToken arm_token = parser->current;
        const bool is_case = luna_parser_match(parser, LUNA_TOKEN_CASE);
        const bool is_default =
            !is_case && luna_parser_match(parser, LUNA_TOKEN_DEFAULT);
        if (!is_case && !is_default) {
            luna_diagnostic_error(parser->diagnostics, parser->current.span,
                                  "expected 'case' or 'default' inside switch");
            luna_parser_advance(parser);
            continue;
        }

        LunaExpression *first_label = NULL;
        LunaExpression **next_label = &first_label;
        uint32_t label_count = 0U;
        bool label_failed = false;
        if (is_case) {
            do {
                LunaExpression *label = luna_parser_parse_switch_label(parser);
                if (label == NULL) {
                    label_failed = true;
                    break;
                }
                *next_label = label;
                next_label = &label->next;
                if (label_count == UINT32_MAX) {
                    luna_diagnostic_error(parser->diagnostics, label->span,
                                          "too many switch case labels");
                    break;
                }
                label_count += 1U;
            } while (luna_parser_match(parser, LUNA_TOKEN_COMMA));
        }

        if (label_failed) {
            while (!luna_parser_check(parser, LUNA_TOKEN_LEFT_BRACE) &&
                   !luna_parser_check(parser, LUNA_TOKEN_CASE) &&
                   !luna_parser_check(parser, LUNA_TOKEN_DEFAULT) &&
                   !luna_parser_check(parser, LUNA_TOKEN_RIGHT_BRACE) &&
                   !luna_parser_check(parser, LUNA_TOKEN_END)) {
                luna_parser_advance(parser);
            }
        }

        LunaBlock *body = luna_parser_parse_block(parser);
        LunaSwitchArm *arm = luna_parser_allocate(parser, sizeof(LunaSwitchArm),
                                                  _Alignof(LunaSwitchArm));
        if (arm == NULL) {
            break;
        }
        arm->span = body == NULL
                        ? arm_token.span
                        : luna_parser_join_spans(arm_token.span, body->span);
        arm->is_default = is_default;
        arm->first_label = first_label;
        arm->label_count = label_count;
        arm->body = body;
        *next_arm = arm;
        next_arm = &arm->next;

        if (arm_count == UINT32_MAX) {
            luna_diagnostic_error(parser->diagnostics, arm->span,
                                  "too many switch arms");
            break;
        }
        arm_count += 1U;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACE, "to end switch");
    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_SWITCH,
        luna_parser_join_spans(switch_token.span, parser->previous.span));
    if (statement != NULL) {
        statement->as.switch_statement.expression = expression;
        statement->as.switch_statement.first_arm = first_arm;
        statement->as.switch_statement.arm_count = arm_count;
    }
    luna_parser_leave_nesting(parser);
    return statement;
}

static LunaStatement *luna_parser_parse_statement(LunaParser *parser) {
    if (luna_parser_check(parser, LUNA_TOKEN_LEFT_BRACE)) {
        return luna_parser_wrap_block(parser, luna_parser_parse_block(parser));
    }

    if (luna_parser_match(parser, LUNA_TOKEN_LET)) {
        return luna_parser_parse_declaration(parser, false,
                                             parser->previous.span);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_VAR)) {
        return luna_parser_parse_declaration(parser, true,
                                             parser->previous.span);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_IF)) {
        return luna_parser_parse_if_statement(parser);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_WHILE)) {
        return luna_parser_parse_while_statement(parser);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_DO)) {
        return luna_parser_parse_do_statement(parser);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_FOR)) {
        return luna_parser_parse_for_statement(parser);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_SWITCH)) {
        return luna_parser_parse_switch_statement(parser);
    }

    if (luna_parser_match(parser, LUNA_TOKEN_BREAK)) {
        const LunaSourceSpan start = parser->previous.span;
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON, "after 'break'");
        return luna_parser_new_statement(
            parser, LUNA_STATEMENT_BREAK,
            luna_parser_join_spans(start, parser->previous.span));
    }

    if (luna_parser_match(parser, LUNA_TOKEN_CONTINUE)) {
        const LunaSourceSpan start = parser->previous.span;
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                                 "after 'continue'");
        return luna_parser_new_statement(
            parser, LUNA_STATEMENT_CONTINUE,
            luna_parser_join_spans(start, parser->previous.span));
    }

    if (luna_parser_match(parser, LUNA_TOKEN_RETURN)) {
        const LunaSourceSpan start = parser->previous.span;
        LunaExpression *value = NULL;
        if (!luna_parser_check(parser, LUNA_TOKEN_SEMICOLON)) {
            value = luna_parser_parse_expression(parser);
        }
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                                 "after return statement");

        LunaStatement *statement = luna_parser_new_statement(
            parser, LUNA_STATEMENT_RETURN,
            luna_parser_join_spans(start, parser->previous.span));
        if (statement != NULL) {
            statement->as.return_value = value;
        }
        return statement;
    }

    return luna_parser_parse_expression_or_assignment_statement(
        parser, LUNA_TOKEN_SEMICOLON, "after expression");
}

static LunaBlock *luna_parser_parse_block(LunaParser *parser) {
    if (!luna_parser_expect(parser, LUNA_TOKEN_LEFT_BRACE, "to begin block")) {
        return NULL;
    }
    const LunaSourceSpan start_span = parser->previous.span;
    if (!luna_parser_enter_nesting(parser, start_span)) {
        return NULL;
    }

    LunaBlock *block =
        luna_parser_allocate(parser, sizeof(LunaBlock), _Alignof(LunaBlock));
    if (block == NULL) {
        luna_parser_leave_nesting(parser);
        return NULL;
    }

    while (!luna_parser_check(parser, LUNA_TOKEN_RIGHT_BRACE) &&
           !luna_parser_check(parser, LUNA_TOKEN_END)) {
        const size_t offset_before = parser->current.span.offset;
        LunaStatement *statement = luna_parser_parse_statement(parser);
        if (statement != NULL) {
            if (block->first == NULL) {
                block->first = statement;
            } else {
                block->last->next = statement;
            }
            block->last = statement;
        }

        if (parser->current.span.offset == offset_before &&
            !luna_parser_check(parser, LUNA_TOKEN_END)) {
            luna_parser_advance(parser);
        }
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACE, "to end block");
    block->span = luna_parser_join_spans(start_span, parser->previous.span);
    luna_parser_leave_nesting(parser);
    return block;
}

static LunaParameter *luna_parser_parse_parameters(LunaParser *parser,
                                                   uint32_t *parameter_count) {
    LunaParameter *first = NULL;
    LunaParameter **next = &first;

    if (luna_parser_check(parser, LUNA_TOKEN_RIGHT_PAREN)) {
        return NULL;
    }

    do {
        const LunaToken name_token = parser->current;
        if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                "as parameter name")) {
            break;
        }

        (void)luna_parser_expect(parser, LUNA_TOKEN_COLON,
                                 "after parameter name");
        const LunaTypeRef type = luna_parser_parse_type(parser);

        LunaParameter *parameter = luna_parser_allocate(
            parser, sizeof(LunaParameter), _Alignof(LunaParameter));
        if (parameter == NULL) {
            break;
        }

        parameter->name = luna_parser_token_text(name_token);
        parameter->type = type;
        parameter->span = luna_parser_join_spans(name_token.span, type.span);
        *next = parameter;
        next = &parameter->next;
        *parameter_count += 1U;
    } while (luna_parser_match(parser, LUNA_TOKEN_COMMA));

    return first;
}

static LunaFunction *luna_parser_parse_function(LunaParser *parser,
                                                bool is_exported,
                                                bool is_external,
                                                LunaSourceSpan start_span) {
    const LunaToken name_token = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                            "as function name")) {
        return NULL;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_PAREN,
                             "after function name");
    uint32_t parameter_count = 0U;
    LunaParameter *parameters =
        luna_parser_parse_parameters(parser, &parameter_count);
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_PAREN,
                             "after parameters");

    LunaTypeRef return_type = {
        .kind = LUNA_TYPE_VOID,
        .span = parser->previous.span,
    };
    if (luna_parser_match(parser, LUNA_TOKEN_ARROW)) {
        return_type = luna_parser_parse_type(parser);
    }

    const bool is_declaration = luna_parser_match(parser, LUNA_TOKEN_SEMICOLON);
    LunaBlock *body = NULL;
    if (!is_declaration) {
        body = luna_parser_parse_block(parser);
    }

    LunaFunction *function = luna_parser_allocate(parser, sizeof(LunaFunction),
                                                  _Alignof(LunaFunction));
    if (function == NULL) {
        return NULL;
    }

    function->name = luna_parser_token_text(name_token);
    LunaSourceSpan end_span = parser->previous.span;
    if (!is_declaration && body != NULL) {
        end_span = body->span;
    }
    function->span = luna_parser_join_spans(start_span, end_span);
    function->is_exported = is_exported;
    function->is_external = is_external;
    function->is_declaration = is_declaration;
    function->first_parameter = parameters;
    function->parameter_count = parameter_count;
    function->return_type = return_type;
    function->body = body;
    return function;
}

static LunaTypeDeclaration *
luna_parser_parse_aggregate_declaration(LunaParser *parser, LunaTypeKind kind,
                                        bool is_exported,
                                        LunaSourceSpan start_span) {
    const LunaToken name_token = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                            "as aggregate type name")) {
        return NULL;
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_BRACE,
                             "to begin aggregate declaration");

    LunaField *first_field = NULL;
    LunaField **next_field = &first_field;
    uint32_t field_count = 0U;
    while (!luna_parser_check(parser, LUNA_TOKEN_RIGHT_BRACE) &&
           !luna_parser_check(parser, LUNA_TOKEN_END)) {
        const LunaToken field_name = parser->current;
        if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                "as field name")) {
            luna_parser_advance(parser);
            continue;
        }
        (void)luna_parser_expect(parser, LUNA_TOKEN_COLON, "after field name");
        const LunaTypeRef field_type = luna_parser_parse_type(parser);
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON, "after field");

        LunaField *field = luna_parser_allocate(parser, sizeof(LunaField),
                                                _Alignof(LunaField));
        if (field == NULL) {
            return NULL;
        }
        field->name = luna_parser_token_text(field_name);
        field->type = field_type;
        field->span = luna_parser_join_spans(field_name.span, field_type.span);
        *next_field = field;
        next_field = &field->next;
        if (field_count == UINT32_MAX) {
            luna_diagnostic_error(parser->diagnostics, field->span,
                                  "too many aggregate fields");
        } else {
            field_count += 1U;
        }
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACE,
                             "to end aggregate declaration");

    LunaTypeDeclaration *declaration = luna_parser_allocate(
        parser, sizeof(LunaTypeDeclaration), _Alignof(LunaTypeDeclaration));
    if (declaration == NULL) {
        return NULL;
    }
    declaration->kind = kind;
    declaration->name = luna_parser_token_text(name_token);
    declaration->span =
        luna_parser_join_spans(start_span, parser->previous.span);
    declaration->is_exported = is_exported;
    declaration->as.aggregate.first_field = first_field;
    declaration->as.aggregate.field_count = field_count;
    return declaration;
}

static LunaTypeDeclaration *
luna_parser_parse_enum_declaration(LunaParser *parser, bool is_exported,
                                   LunaSourceSpan start_span) {
    const LunaToken name_token = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                            "as enum type name")) {
        return NULL;
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_COLON,
                             "before enum underlying type");
    const LunaTypeRef underlying_type = luna_parser_parse_type(parser);
    (void)luna_parser_expect(parser, LUNA_TOKEN_LEFT_BRACE,
                             "to begin enum declaration");

    LunaEnumMember *first_member = NULL;
    LunaEnumMember **next_member = &first_member;
    uint32_t member_count = 0U;
    while (!luna_parser_check(parser, LUNA_TOKEN_RIGHT_BRACE) &&
           !luna_parser_check(parser, LUNA_TOKEN_END)) {
        const LunaToken member_name = parser->current;
        if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                "as enum member name")) {
            luna_parser_advance(parser);
            continue;
        }

        LunaExpression *initializer = NULL;
        if (luna_parser_match(parser, LUNA_TOKEN_EQUAL)) {
            initializer = luna_parser_parse_expression(parser);
        }

        LunaEnumMember *member = luna_parser_allocate(
            parser, sizeof(LunaEnumMember), _Alignof(LunaEnumMember));
        if (member == NULL) {
            return NULL;
        }
        member->name = luna_parser_token_text(member_name);
        member->initializer = initializer;
        member->span =
            initializer == NULL
                ? member_name.span
                : luna_parser_join_spans(member_name.span, initializer->span);
        *next_member = member;
        next_member = &member->next;
        if (member_count == UINT32_MAX) {
            luna_diagnostic_error(parser->diagnostics, member->span,
                                  "too many enum members");
        } else {
            member_count += 1U;
        }

        if (!luna_parser_match(parser, LUNA_TOKEN_COMMA)) {
            break;
        }
    }
    (void)luna_parser_expect(parser, LUNA_TOKEN_RIGHT_BRACE,
                             "to end enum declaration");

    LunaTypeDeclaration *declaration = luna_parser_allocate(
        parser, sizeof(LunaTypeDeclaration), _Alignof(LunaTypeDeclaration));
    if (declaration == NULL) {
        return NULL;
    }
    declaration->kind = LUNA_TYPE_ENUM;
    declaration->name = luna_parser_token_text(name_token);
    declaration->span =
        luna_parser_join_spans(start_span, parser->previous.span);
    declaration->is_exported = is_exported;
    declaration->as.enumeration.underlying_type = underlying_type;
    declaration->as.enumeration.first_member = first_member;
    declaration->as.enumeration.member_count = member_count;
    return declaration;
}

static bool luna_parser_parse_module_name(LunaParser *parser,
                                          LunaStringView *name,
                                          LunaSourceSpan *span) {
    const LunaToken first = parser->current;
    if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER, "as module name")) {
        return false;
    }

    LunaToken last = first;
    while (luna_parser_match(parser, LUNA_TOKEN_DOT)) {
        last = parser->current;
        if (!luna_parser_expect(parser, LUNA_TOKEN_IDENTIFIER,
                                "after '.' in module name")) {
            return false;
        }
    }

    *span = luna_parser_join_spans(first.span, last.span);
    *name = luna_source_span_text(*span);
    return true;
}

void luna_parser_init(LunaParser *parser, const LunaSourceFile *source,
                      LunaDiagnosticEngine *diagnostics, LunaArena *arena) {
    luna_lexer_init(&parser->lexer, source, diagnostics);
    parser->diagnostics = diagnostics;
    parser->arena = arena;
    parser->current = (LunaToken){0};
    parser->previous = (LunaToken){0};
    parser->nesting_depth = 0U;
    luna_parser_advance(parser);
}

LunaProgram *luna_parser_parse_program(LunaParser *parser) {
    LunaProgram *program = luna_parser_allocate(parser, sizeof(LunaProgram),
                                                _Alignof(LunaProgram));
    if (program == NULL) {
        return NULL;
    }
    program->source = parser->lexer.source;

    program->is_interface = luna_parser_match(parser, LUNA_TOKEN_EXPORT);
    if (!luna_parser_expect(parser, LUNA_TOKEN_MODULE,
                            "at the start of a source unit")) {
        return program;
    }

    if (!luna_parser_parse_module_name(parser, &program->module_name,
                                       &program->module_span)) {
        return program;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                             "after module declaration");

    LunaImport **next_import = &program->first_import;
    while (luna_parser_match(parser, LUNA_TOKEN_IMPORT)) {
        const LunaSourceSpan import_start = parser->previous.span;
        LunaImport *import = luna_parser_allocate(parser, sizeof(LunaImport),
                                                  _Alignof(LunaImport));
        if (import == NULL) {
            return program;
        }

        if (!luna_parser_parse_module_name(parser, &import->module_name,
                                           &import->span)) {
            return program;
        }
        import->span = luna_parser_join_spans(import_start, import->span);
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON, "after import");

        *next_import = import;
        next_import = &import->next;
    }

    LunaTypeDeclaration **next_type = &program->first_type_declaration;
    LunaFunction **next_function = &program->first_function;
    while (!luna_parser_check(parser, LUNA_TOKEN_END)) {
        const LunaSourceSpan start_span = parser->current.span;
        const bool is_exported = luna_parser_match(parser, LUNA_TOKEN_EXPORT);
        const bool is_external = luna_parser_match(parser, LUNA_TOKEN_EXTERN);

        LunaTypeKind declaration_kind = LUNA_TYPE_INVALID;
        if (luna_parser_match(parser, LUNA_TOKEN_STRUCT)) {
            declaration_kind = LUNA_TYPE_STRUCT;
        } else if (luna_parser_match(parser, LUNA_TOKEN_UNION)) {
            declaration_kind = LUNA_TYPE_UNION;
        }
        if (declaration_kind != LUNA_TYPE_INVALID) {
            if (is_external) {
                luna_diagnostic_error(
                    parser->diagnostics, start_span,
                    "aggregate type declarations cannot be external");
            }
            LunaTypeDeclaration *declaration =
                luna_parser_parse_aggregate_declaration(
                    parser, declaration_kind, is_exported, start_span);
            if (declaration != NULL) {
                *next_type = declaration;
                next_type = &declaration->next;
            }
            continue;
        }

        if (luna_parser_match(parser, LUNA_TOKEN_ENUM)) {
            if (is_external) {
                luna_diagnostic_error(
                    parser->diagnostics, start_span,
                    "enum type declarations cannot be external");
            }
            LunaTypeDeclaration *declaration =
                luna_parser_parse_enum_declaration(parser, is_exported,
                                                   start_span);
            if (declaration != NULL) {
                *next_type = declaration;
                next_type = &declaration->next;
            }
            continue;
        }

        if (!luna_parser_match(parser, LUNA_TOKEN_FN)) {
            luna_diagnostic_error(
                parser->diagnostics, parser->current.span,
                "expected a type or function declaration, found %s",
                luna_token_kind_name(parser->current.kind));
            luna_parser_advance(parser);
            continue;
        }

        LunaFunction *function = luna_parser_parse_function(
            parser, is_exported, is_external, start_span);
        if (function != NULL) {
            if (is_external && !function->is_declaration) {
                luna_diagnostic_error(
                    parser->diagnostics, function->span,
                    "external function declaration must end with ';'");
            }
            *next_function = function;
            next_function = &function->next;
        }
    }

    return program;
}
