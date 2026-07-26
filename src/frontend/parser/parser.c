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

static bool luna_parser_is_type_token(LunaTokenKind kind) {
    switch (kind) {
    case LUNA_TOKEN_BOOL:
    case LUNA_TOKEN_I8:
    case LUNA_TOKEN_I16:
    case LUNA_TOKEN_I32:
    case LUNA_TOKEN_I64:
    case LUNA_TOKEN_U8:
    case LUNA_TOKEN_U16:
    case LUNA_TOKEN_U32:
    case LUNA_TOKEN_U64:
    case LUNA_TOKEN_ISIZE:
    case LUNA_TOKEN_USIZE:
    case LUNA_TOKEN_F32:
    case LUNA_TOKEN_F64:
    case LUNA_TOKEN_VOID:
        return true;

    default:
        return false;
    }
}

static LunaTypeRef luna_parser_parse_type(LunaParser *parser) {
    const LunaToken token = parser->current;
    LunaTypeKind kind = LUNA_TYPE_INVALID;

    switch (token.kind) {
    case LUNA_TOKEN_BOOL:
        kind = LUNA_TYPE_BOOL;
        break;
    case LUNA_TOKEN_I32:
        kind = LUNA_TYPE_I32;
        break;
    case LUNA_TOKEN_VOID:
        kind = LUNA_TYPE_VOID;
        break;

    default:
        if (luna_parser_is_type_token(token.kind)) {
            luna_diagnostic_error(
                parser->diagnostics, token.span,
                "type %s is accepted by the language design but is not "
                "implemented in milestone M0",
                luna_token_kind_name(token.kind));
            luna_parser_advance(parser);
            return (LunaTypeRef){
                .kind = LUNA_TYPE_INVALID,
                .span = token.span,
            };
        }

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
                                      int64_t *value) {
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

        if (result > (uint64_t)INT64_MAX / base) {
            luna_diagnostic_error(parser->diagnostics, token.span,
                                  "integer literal is too large");
            return false;
        }
        result *= base;

        if (result > (uint64_t)INT64_MAX - digit) {
            luna_diagnostic_error(parser->diagnostics, token.span,
                                  "integer literal is too large");
            return false;
        }
        result += digit;
        saw_digit = true;
        previous_was_separator = false;
    }

    if (!saw_digit || previous_was_separator) {
        luna_diagnostic_error(parser->diagnostics, token.span,
                              "invalid integer literal");
        return false;
    }

    *value = (int64_t)result;
    return true;
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

    if (luna_parser_match(parser, LUNA_TOKEN_INTEGER)) {
        LunaExpression *expression = luna_parser_new_expression(
            parser, LUNA_EXPRESSION_INTEGER, token.span);
        if (expression != NULL) {
            int64_t value = 0;
            (void)luna_parser_parse_integer(parser, token, &value);
            expression->as.integer = value;
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

static bool luna_parser_is_unary_operator(LunaTokenKind kind) {
    return kind == LUNA_TOKEN_PLUS || kind == LUNA_TOKEN_MINUS ||
           kind == LUNA_TOKEN_BANG || kind == LUNA_TOKEN_TILDE;
}

static LunaExpression *luna_parser_parse_unary(LunaParser *parser) {
    if (!luna_parser_is_unary_operator(parser->current.kind)) {
        return luna_parser_parse_primary(parser);
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
    LunaExpression *left = luna_parser_parse_unary(parser);
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

static LunaExpression *luna_parser_parse_expression(LunaParser *parser) {
    return luna_parser_parse_binary(parser, 1);
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

    if (luna_parser_check(parser, LUNA_TOKEN_DO) ||
        luna_parser_check(parser, LUNA_TOKEN_FOR) ||
        luna_parser_check(parser, LUNA_TOKEN_SWITCH)) {
        const LunaToken unsupported = parser->current;
        luna_diagnostic_error(
            parser->diagnostics, unsupported.span,
            "%s is accepted by the language design but is not implemented "
            "in milestone M0",
            luna_token_kind_name(unsupported.kind));
        luna_parser_advance(parser);
        return NULL;
    }

    LunaExpression *expression = luna_parser_parse_expression(parser);
    if (expression == NULL) {
        return NULL;
    }

    if (luna_parser_is_assignment_operator(parser->current.kind)) {
        const LunaToken operator_token = parser->current;
        luna_parser_advance(parser);
        LunaExpression *value = luna_parser_parse_expression(parser);
        (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON,
                                 "after assignment");

        if (expression->kind != LUNA_EXPRESSION_NAME) {
            luna_diagnostic_error(
                parser->diagnostics, expression->span,
                "milestone M0 assignments require a local variable name");
            return NULL;
        }

        LunaStatement *statement = luna_parser_new_statement(
            parser, LUNA_STATEMENT_ASSIGNMENT,
            luna_parser_join_spans(expression->span, parser->previous.span));
        if (statement != NULL) {
            statement->as.assignment.name = expression->as.name;
            statement->as.assignment.operator_kind = operator_token.kind;
            statement->as.assignment.value = value;
        }
        return statement;
    }

    (void)luna_parser_expect(parser, LUNA_TOKEN_SEMICOLON, "after expression");
    LunaStatement *statement = luna_parser_new_statement(
        parser, LUNA_STATEMENT_EXPRESSION,
        luna_parser_join_spans(expression->span, parser->previous.span));
    if (statement != NULL) {
        statement->as.expression = expression;
    }
    return statement;
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
    function->is_declaration = is_declaration;
    function->first_parameter = parameters;
    function->parameter_count = parameter_count;
    function->return_type = return_type;
    function->body = body;
    return function;
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

    LunaFunction **next_function = &program->first_function;
    while (!luna_parser_check(parser, LUNA_TOKEN_END)) {
        const bool is_exported = luna_parser_match(parser, LUNA_TOKEN_EXPORT);
        const LunaSourceSpan start_span = parser->current.span;

        if (!luna_parser_match(parser, LUNA_TOKEN_FN)) {
            luna_diagnostic_error(
                parser->diagnostics, parser->current.span,
                "milestone M0 expects a function declaration, found %s",
                luna_token_kind_name(parser->current.kind));
            luna_parser_advance(parser);
            continue;
        }

        LunaFunction *function =
            luna_parser_parse_function(parser, is_exported, start_span);
        if (function != NULL) {
            *next_function = function;
            next_function = &function->next;
        }
    }

    return program;
}
