#include "test.h"

#include "luna/diagnostic.h"
#include "luna/lexer.h"
#include "luna/source.h"
#include "luna/token.h"

#include <stddef.h>
#include <stdio.h>

static bool luna_test_lexer_token_sequence(void) {
    static const char source_text[] = "export module compiler.test;\n"
                                      "// comment\n"
                                      "fn main() -> i32 {\n"
                                      "  var value: i32 = 0xff + 0b10;\n"
                                      "  value <<= 1;\n"
                                      "  return value >= 2 && true;\n"
                                      "}\n";

    static const LunaTokenKind expected[] = {
        LUNA_TOKEN_EXPORT,
        LUNA_TOKEN_MODULE,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_DOT,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_SEMICOLON,
        LUNA_TOKEN_FN,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_LEFT_PAREN,
        LUNA_TOKEN_RIGHT_PAREN,
        LUNA_TOKEN_ARROW,
        LUNA_TOKEN_I32,
        LUNA_TOKEN_LEFT_BRACE,
        LUNA_TOKEN_VAR,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_COLON,
        LUNA_TOKEN_I32,
        LUNA_TOKEN_EQUAL,
        LUNA_TOKEN_INTEGER,
        LUNA_TOKEN_PLUS,
        LUNA_TOKEN_INTEGER,
        LUNA_TOKEN_SEMICOLON,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_SHIFT_LEFT_EQUAL,
        LUNA_TOKEN_INTEGER,
        LUNA_TOKEN_SEMICOLON,
        LUNA_TOKEN_RETURN,
        LUNA_TOKEN_IDENTIFIER,
        LUNA_TOKEN_GREATER_EQUAL,
        LUNA_TOKEN_INTEGER,
        LUNA_TOKEN_LOGICAL_AND,
        LUNA_TOKEN_TRUE,
        LUNA_TOKEN_SEMICOLON,
        LUNA_TOKEN_RIGHT_BRACE,
        LUNA_TOKEN_END,
    };

    LunaSourceFile source;
    if (!LUNA_TEST_EXPECT(
            luna_source_from_memory("<lexer-test>", source_text, &source))) {
        return false;
    }

    FILE *diagnostic_file = tmpfile();
    if (!LUNA_TEST_EXPECT(diagnostic_file != NULL)) {
        luna_source_destroy(&source);
        return false;
    }

    LunaDiagnosticEngine diagnostics;
    luna_diagnostic_init(&diagnostics, diagnostic_file);

    LunaLexer lexer;
    luna_lexer_init(&lexer, &source, &diagnostics);

    bool success = true;
    const size_t expected_count = sizeof(expected) / sizeof(expected[0]);
    for (size_t index = 0U; index < expected_count; index += 1U) {
        const LunaToken token = luna_lexer_next(&lexer);
        if (!LUNA_TEST_EXPECT(token.kind == expected[index])) {
            (void)fprintf(stderr, "token %zu: expected %s, found %s\n", index,
                          luna_token_kind_name(expected[index]),
                          luna_token_kind_name(token.kind));
            success = false;
            break;
        }
    }

    success =
        LUNA_TEST_EXPECT(luna_diagnostic_error_count(&diagnostics) == 0U) &&
        success;

    (void)fclose(diagnostic_file);
    luna_source_destroy(&source);
    return success;
}

static bool luna_test_lexer_reports_unterminated_string(void) {
    LunaSourceFile source;
    if (!LUNA_TEST_EXPECT(luna_source_from_memory(
            "<lexer-error-test>", "\"unterminated\n", &source))) {
        return false;
    }

    FILE *diagnostic_file = tmpfile();
    if (!LUNA_TEST_EXPECT(diagnostic_file != NULL)) {
        luna_source_destroy(&source);
        return false;
    }

    LunaDiagnosticEngine diagnostics;
    luna_diagnostic_init(&diagnostics, diagnostic_file);
    LunaLexer lexer;
    luna_lexer_init(&lexer, &source, &diagnostics);

    const LunaToken token = luna_lexer_next(&lexer);
    const bool success =
        LUNA_TEST_EXPECT(token.kind == LUNA_TOKEN_INVALID) &&
        LUNA_TEST_EXPECT(luna_diagnostic_error_count(&diagnostics) == 1U);

    (void)fclose(diagnostic_file);
    luna_source_destroy(&source);
    return success;
}

bool luna_test_lexer(void) {
    return luna_test_lexer_token_sequence() &&
           luna_test_lexer_reports_unterminated_string();
}
