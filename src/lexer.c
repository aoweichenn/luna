#include "luna/lexer.h"

#include "luna/string_view.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct LunaKeyword {
    const char *text;
    LunaTokenKind kind;
} LunaKeyword;

static const LunaKeyword luna_keywords[] = {
    {"alignof", LUNA_TOKEN_ALIGNOF},
    {"as", LUNA_TOKEN_AS},
    {"bool", LUNA_TOKEN_BOOL},
    {"break", LUNA_TOKEN_BREAK},
    {"case", LUNA_TOKEN_CASE},
    {"const", LUNA_TOKEN_CONST},
    {"continue", LUNA_TOKEN_CONTINUE},
    {"default", LUNA_TOKEN_DEFAULT},
    {"do", LUNA_TOKEN_DO},
    {"else", LUNA_TOKEN_ELSE},
    {"enum", LUNA_TOKEN_ENUM},
    {"export", LUNA_TOKEN_EXPORT},
    {"extern", LUNA_TOKEN_EXTERN},
    {"f32", LUNA_TOKEN_F32},
    {"f64", LUNA_TOKEN_F64},
    {"false", LUNA_TOKEN_FALSE},
    {"fn", LUNA_TOKEN_FN},
    {"for", LUNA_TOKEN_FOR},
    {"i16", LUNA_TOKEN_I16},
    {"i32", LUNA_TOKEN_I32},
    {"i64", LUNA_TOKEN_I64},
    {"i8", LUNA_TOKEN_I8},
    {"if", LUNA_TOKEN_IF},
    {"import", LUNA_TOKEN_IMPORT},
    {"isize", LUNA_TOKEN_ISIZE},
    {"let", LUNA_TOKEN_LET},
    {"module", LUNA_TOKEN_MODULE},
    {"null", LUNA_TOKEN_NULL},
    {"offsetof", LUNA_TOKEN_OFFSETOF},
    {"return", LUNA_TOKEN_RETURN},
    {"sizeof", LUNA_TOKEN_SIZEOF},
    {"struct", LUNA_TOKEN_STRUCT},
    {"switch", LUNA_TOKEN_SWITCH},
    {"true", LUNA_TOKEN_TRUE},
    {"u16", LUNA_TOKEN_U16},
    {"u32", LUNA_TOKEN_U32},
    {"u64", LUNA_TOKEN_U64},
    {"u8", LUNA_TOKEN_U8},
    {"union", LUNA_TOKEN_UNION},
    {"usize", LUNA_TOKEN_USIZE},
    {"var", LUNA_TOKEN_VAR},
    {"void", LUNA_TOKEN_VOID},
    {"while", LUNA_TOKEN_WHILE},
};

static bool luna_is_ascii_letter(char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

static bool luna_is_decimal_digit(char character) {
    return character >= '0' && character <= '9';
}

static bool luna_is_hex_digit(char character) {
    return luna_is_decimal_digit(character) ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

static bool luna_lexer_at_end(const LunaLexer *lexer) {
    return lexer->offset >= lexer->source->length;
}

static char luna_lexer_peek(const LunaLexer *lexer) {
    if (luna_lexer_at_end(lexer)) {
        return '\0';
    }
    return lexer->source->text[lexer->offset];
}

static char luna_lexer_peek_next(const LunaLexer *lexer) {
    if (lexer->offset + 1U >= lexer->source->length) {
        return '\0';
    }
    return lexer->source->text[lexer->offset + 1U];
}

static char luna_lexer_advance(LunaLexer *lexer) {
    const char character = luna_lexer_peek(lexer);
    if (character == '\0' && luna_lexer_at_end(lexer)) {
        return '\0';
    }

    lexer->offset += 1U;
    if (character == '\n') {
        lexer->line += 1U;
        lexer->column = 1U;
    } else {
        lexer->column += 1U;
    }
    return character;
}

static bool luna_lexer_match(LunaLexer *lexer, char expected) {
    if (luna_lexer_peek(lexer) != expected) {
        return false;
    }

    (void)luna_lexer_advance(lexer);
    return true;
}

static LunaToken luna_lexer_make_token(const LunaLexer *lexer,
                                       LunaTokenKind kind, size_t start_offset,
                                       uint32_t start_line,
                                       uint32_t start_column) {
    return (LunaToken){
        .kind = kind,
        .span =
            {
                .source = lexer->source,
                .offset = start_offset,
                .length = lexer->offset - start_offset,
                .line = start_line,
                .column = start_column,
            },
    };
}

static void luna_lexer_skip_trivia(LunaLexer *lexer) {
    for (;;) {
        const char character = luna_lexer_peek(lexer);

        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n') {
            (void)luna_lexer_advance(lexer);
            continue;
        }

        if (character == '/' && luna_lexer_peek_next(lexer) == '/') {
            while (!luna_lexer_at_end(lexer) &&
                   luna_lexer_peek(lexer) != '\n') {
                (void)luna_lexer_advance(lexer);
            }
            continue;
        }

        if (character == '/' && luna_lexer_peek_next(lexer) == '*') {
            const size_t start_offset = lexer->offset;
            const uint32_t start_line = lexer->line;
            const uint32_t start_column = lexer->column;
            (void)luna_lexer_advance(lexer);
            (void)luna_lexer_advance(lexer);

            while (!luna_lexer_at_end(lexer) &&
                   !(luna_lexer_peek(lexer) == '*' &&
                     luna_lexer_peek_next(lexer) == '/')) {
                (void)luna_lexer_advance(lexer);
            }

            if (luna_lexer_at_end(lexer)) {
                LunaSourceSpan span = {
                    .source = lexer->source,
                    .offset = start_offset,
                    .length = lexer->offset - start_offset,
                    .line = start_line,
                    .column = start_column,
                };
                luna_diagnostic_error(lexer->diagnostics, span,
                                      "unterminated block comment");
                return;
            }

            (void)luna_lexer_advance(lexer);
            (void)luna_lexer_advance(lexer);
            continue;
        }

        return;
    }
}

static LunaTokenKind luna_lexer_identifier_kind(const LunaSourceFile *source,
                                                size_t start, size_t length) {
    const LunaStringView identifier =
        luna_string_view(source->text + start, length);

    const size_t keyword_count =
        sizeof(luna_keywords) / sizeof(luna_keywords[0]);
    for (size_t index = 0U; index < keyword_count; index += 1U) {
        if (luna_string_view_equal_c_string(identifier,
                                            luna_keywords[index].text)) {
            return luna_keywords[index].kind;
        }
    }

    return LUNA_TOKEN_IDENTIFIER;
}

static LunaToken luna_lexer_scan_identifier(LunaLexer *lexer,
                                            size_t start_offset,
                                            uint32_t start_line,
                                            uint32_t start_column) {
    while (luna_is_ascii_letter(luna_lexer_peek(lexer)) ||
           luna_is_decimal_digit(luna_lexer_peek(lexer)) ||
           luna_lexer_peek(lexer) == '_') {
        (void)luna_lexer_advance(lexer);
    }

    const LunaTokenKind kind = luna_lexer_identifier_kind(
        lexer->source, start_offset, lexer->offset - start_offset);
    return luna_lexer_make_token(lexer, kind, start_offset, start_line,
                                 start_column);
}

static bool luna_digit_matches_base(char character, unsigned int base) {
    switch (base) {
    case 2U:
        return character == '0' || character == '1';
    case 8U:
        return character >= '0' && character <= '7';
    case 10U:
        return luna_is_decimal_digit(character);
    case 16U:
        return luna_is_hex_digit(character);
    default:
        return false;
    }
}

static LunaToken luna_lexer_scan_number(LunaLexer *lexer, size_t start_offset,
                                        uint32_t start_line,
                                        uint32_t start_column) {
    unsigned int base = 10U;
    bool is_float = false;

    if (lexer->source->text[start_offset] == '0' &&
        (luna_lexer_peek(lexer) == 'b' || luna_lexer_peek(lexer) == 'B')) {
        base = 2U;
        (void)luna_lexer_advance(lexer);
    } else if (lexer->source->text[start_offset] == '0' &&
               (luna_lexer_peek(lexer) == 'o' ||
                luna_lexer_peek(lexer) == 'O')) {
        base = 8U;
        (void)luna_lexer_advance(lexer);
    } else if (lexer->source->text[start_offset] == '0' &&
               (luna_lexer_peek(lexer) == 'x' ||
                luna_lexer_peek(lexer) == 'X')) {
        base = 16U;
        (void)luna_lexer_advance(lexer);
    }

    while (luna_digit_matches_base(luna_lexer_peek(lexer), base) ||
           luna_lexer_peek(lexer) == '_') {
        (void)luna_lexer_advance(lexer);
    }

    if (base == 10U && luna_lexer_peek(lexer) == '.' &&
        luna_is_decimal_digit(luna_lexer_peek_next(lexer))) {
        is_float = true;
        (void)luna_lexer_advance(lexer);
        while (luna_is_decimal_digit(luna_lexer_peek(lexer)) ||
               luna_lexer_peek(lexer) == '_') {
            (void)luna_lexer_advance(lexer);
        }
    }

    if (base == 10U &&
        (luna_lexer_peek(lexer) == 'e' || luna_lexer_peek(lexer) == 'E')) {
        is_float = true;
        (void)luna_lexer_advance(lexer);
        if (luna_lexer_peek(lexer) == '+' || luna_lexer_peek(lexer) == '-') {
            (void)luna_lexer_advance(lexer);
        }
        while (luna_is_decimal_digit(luna_lexer_peek(lexer)) ||
               luna_lexer_peek(lexer) == '_') {
            (void)luna_lexer_advance(lexer);
        }
    }

    return luna_lexer_make_token(
        lexer, is_float ? LUNA_TOKEN_FLOAT : LUNA_TOKEN_INTEGER, start_offset,
        start_line, start_column);
}

static LunaToken luna_lexer_scan_quoted(LunaLexer *lexer, char quote,
                                        LunaTokenKind kind, size_t start_offset,
                                        uint32_t start_line,
                                        uint32_t start_column) {
    bool terminated = false;

    while (!luna_lexer_at_end(lexer)) {
        const char character = luna_lexer_advance(lexer);

        if (character == quote) {
            terminated = true;
            break;
        }

        if (character == '\\' && !luna_lexer_at_end(lexer)) {
            (void)luna_lexer_advance(lexer);
            continue;
        }

        if (character == '\n') {
            break;
        }
    }

    LunaToken token =
        luna_lexer_make_token(lexer, terminated ? kind : LUNA_TOKEN_INVALID,
                              start_offset, start_line, start_column);

    if (!terminated) {
        luna_diagnostic_error(lexer->diagnostics, token.span, "unterminated %s",
                              kind == LUNA_TOKEN_STRING ? "string literal"
                                                        : "character literal");
    }

    return token;
}

void luna_lexer_init(LunaLexer *lexer, const LunaSourceFile *source,
                     LunaDiagnosticEngine *diagnostics) {
    lexer->source = source;
    lexer->diagnostics = diagnostics;
    lexer->offset = 0U;
    lexer->line = 1U;
    lexer->column = 1U;
}

LunaToken luna_lexer_next(LunaLexer *lexer) {
    luna_lexer_skip_trivia(lexer);

    const size_t start_offset = lexer->offset;
    const uint32_t start_line = lexer->line;
    const uint32_t start_column = lexer->column;

    if (luna_lexer_at_end(lexer)) {
        return luna_lexer_make_token(lexer, LUNA_TOKEN_END, start_offset,
                                     start_line, start_column);
    }

    const char character = luna_lexer_advance(lexer);
    if (luna_is_ascii_letter(character) || character == '_') {
        return luna_lexer_scan_identifier(lexer, start_offset, start_line,
                                          start_column);
    }

    if (luna_is_decimal_digit(character)) {
        return luna_lexer_scan_number(lexer, start_offset, start_line,
                                      start_column);
    }

    LunaTokenKind kind = LUNA_TOKEN_INVALID;
    switch (character) {
    case '(':
        kind = LUNA_TOKEN_LEFT_PAREN;
        break;
    case ')':
        kind = LUNA_TOKEN_RIGHT_PAREN;
        break;
    case '{':
        kind = LUNA_TOKEN_LEFT_BRACE;
        break;
    case '}':
        kind = LUNA_TOKEN_RIGHT_BRACE;
        break;
    case '[':
        kind = LUNA_TOKEN_LEFT_BRACKET;
        break;
    case ']':
        kind = LUNA_TOKEN_RIGHT_BRACKET;
        break;
    case ',':
        kind = LUNA_TOKEN_COMMA;
        break;
    case '.':
        kind = LUNA_TOKEN_DOT;
        break;
    case ';':
        kind = LUNA_TOKEN_SEMICOLON;
        break;
    case ':':
        kind = LUNA_TOKEN_COLON;
        break;
    case '?':
        kind = LUNA_TOKEN_QUESTION;
        break;
    case '~':
        kind = LUNA_TOKEN_TILDE;
        break;

    case '+':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_PLUS_EQUAL
                                            : LUNA_TOKEN_PLUS;
        break;

    case '-':
        if (luna_lexer_match(lexer, '>')) {
            kind = LUNA_TOKEN_ARROW;
        } else if (luna_lexer_match(lexer, '=')) {
            kind = LUNA_TOKEN_MINUS_EQUAL;
        } else {
            kind = LUNA_TOKEN_MINUS;
        }
        break;

    case '*':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_STAR_EQUAL
                                            : LUNA_TOKEN_STAR;
        break;

    case '/':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_SLASH_EQUAL
                                            : LUNA_TOKEN_SLASH;
        break;

    case '%':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_PERCENT_EQUAL
                                            : LUNA_TOKEN_PERCENT;
        break;

    case '&':
        if (luna_lexer_match(lexer, '&')) {
            kind = LUNA_TOKEN_LOGICAL_AND;
        } else if (luna_lexer_match(lexer, '=')) {
            kind = LUNA_TOKEN_AMPERSAND_EQUAL;
        } else {
            kind = LUNA_TOKEN_AMPERSAND;
        }
        break;

    case '|':
        if (luna_lexer_match(lexer, '|')) {
            kind = LUNA_TOKEN_LOGICAL_OR;
        } else if (luna_lexer_match(lexer, '=')) {
            kind = LUNA_TOKEN_PIPE_EQUAL;
        } else {
            kind = LUNA_TOKEN_PIPE;
        }
        break;

    case '^':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_CARET_EQUAL
                                            : LUNA_TOKEN_CARET;
        break;

    case '!':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_BANG_EQUAL
                                            : LUNA_TOKEN_BANG;
        break;

    case '=':
        kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_EQUAL_EQUAL
                                            : LUNA_TOKEN_EQUAL;
        break;

    case '<':
        if (luna_lexer_match(lexer, '<')) {
            kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_SHIFT_LEFT_EQUAL
                                                : LUNA_TOKEN_SHIFT_LEFT;
        } else {
            kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_LESS_EQUAL
                                                : LUNA_TOKEN_LESS;
        }
        break;

    case '>':
        if (luna_lexer_match(lexer, '>')) {
            kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_SHIFT_RIGHT_EQUAL
                                                : LUNA_TOKEN_SHIFT_RIGHT;
        } else {
            kind = luna_lexer_match(lexer, '=') ? LUNA_TOKEN_GREATER_EQUAL
                                                : LUNA_TOKEN_GREATER;
        }
        break;

    case '"':
        return luna_lexer_scan_quoted(lexer, '"', LUNA_TOKEN_STRING,
                                      start_offset, start_line, start_column);

    case '\'':
        return luna_lexer_scan_quoted(lexer, '\'', LUNA_TOKEN_CHARACTER,
                                      start_offset, start_line, start_column);

    default:
        break;
    }

    LunaToken token = luna_lexer_make_token(lexer, kind, start_offset,
                                            start_line, start_column);

    if (kind == LUNA_TOKEN_INVALID) {
        luna_diagnostic_error(lexer->diagnostics, token.span,
                              "unexpected character");
    }

    return token;
}
