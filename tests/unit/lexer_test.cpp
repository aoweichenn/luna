#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace luna::test {

TEST(LexerTest, RecognizesModuleFunctionAndOperators) {
    constexpr std::string_view LUNA_TEST_SOURCE =
        "export module compiler.test;\n"
        "// comment\n"
        "fn main() -> i32 {\n"
        "  var value: i32 = 0xff + 0b10;\n"
        "  value <<= 1;\n"
        "  return value >= 2 && true;\n"
        "}\n";
    constexpr std::array LUNA_TEST_EXPECTED = {
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

    FrontendHarness harness{LUNA_TEST_SOURCE};
    ASSERT_TRUE(harness.IsReady());
    LunaLexer lexer{};
    luna_lexer_init(&lexer, harness.Source(), harness.DiagnosticEngine());

    for (std::size_t index = 0U; index < LUNA_TEST_EXPECTED.size();
         index += 1U) {
        const LunaToken token = luna_lexer_next(&lexer);
        EXPECT_EQ(token.kind, LUNA_TEST_EXPECTED[index]) << "token " << index;
    }
    EXPECT_EQ(harness.ErrorCount(), 0U);
}

TEST(LexerTest, ReportsUnterminatedQuotedAndBlockCommentTokens) {
    for (const std::string_view source_text : {"\"unterminated\n", "'x\n"}) {
        FrontendHarness harness{source_text};
        ASSERT_TRUE(harness.IsReady());
        LunaLexer lexer{};
        luna_lexer_init(&lexer, harness.Source(), harness.DiagnosticEngine());

        const LunaToken token = luna_lexer_next(&lexer);
        EXPECT_EQ(token.kind, LUNA_TOKEN_INVALID);
        EXPECT_EQ(harness.ErrorCount(), 1U);
    }

    FrontendHarness comment_harness{"/* unterminated"};
    ASSERT_TRUE(comment_harness.IsReady());
    LunaLexer lexer{};
    luna_lexer_init(&lexer, comment_harness.Source(),
                    comment_harness.DiagnosticEngine());
    EXPECT_EQ(luna_lexer_next(&lexer).kind, LUNA_TOKEN_END);
    EXPECT_EQ(comment_harness.ErrorCount(), 1U);
}

TEST(LexerTest, NeverTreatsEmbeddedNullAsEndOfFile) {
    constexpr std::array<char, 3U> LUNA_TEST_SOURCE = {'a', '\0', 'b'};
    FrontendHarness harness{
        std::string_view(LUNA_TEST_SOURCE.data(), LUNA_TEST_SOURCE.size())};
    ASSERT_TRUE(harness.IsReady());
    LunaLexer lexer{};
    luna_lexer_init(&lexer, harness.Source(), harness.DiagnosticEngine());

    EXPECT_EQ(luna_lexer_next(&lexer).kind, LUNA_TOKEN_IDENTIFIER);
    EXPECT_EQ(luna_lexer_next(&lexer).kind, LUNA_TOKEN_INVALID);
    EXPECT_EQ(luna_lexer_next(&lexer).kind, LUNA_TOKEN_IDENTIFIER);
    EXPECT_EQ(luna_lexer_next(&lexer).kind, LUNA_TOKEN_END);
    EXPECT_EQ(harness.ErrorCount(), 1U);
}

}
