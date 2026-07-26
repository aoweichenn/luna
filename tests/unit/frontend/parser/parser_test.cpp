#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

namespace luna::test {

TEST(ParserTest, BuildsModuleImportsAndFunctionShape) {
    FrontendHarness harness{"export module compiler.frontend;\n"
                            "import core.text;\n"
                            "import core.io;\n"
                            "export fn add(left: i32, right: i32) -> i32 {\n"
                            "    return left + right;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    LunaProgram *program = harness.Program();
    ASSERT_NE(program, nullptr);
    EXPECT_TRUE(program->is_interface);
    EXPECT_EQ(
        std::string(program->module_name.data, program->module_name.length),
        "compiler.frontend");
    ASSERT_NE(program->first_import, nullptr);
    ASSERT_NE(program->first_import->next, nullptr);
    EXPECT_EQ(program->first_import->next->next, nullptr);
    ASSERT_NE(program->first_function, nullptr);
    EXPECT_TRUE(program->first_function->is_exported);
    EXPECT_EQ(program->first_function->parameter_count, 2U);
    EXPECT_EQ(program->first_function->return_type.kind, LUNA_TYPE_I32);
}

TEST(ParserTest, ReportsMissingDelimiterWithoutHanging) {
    FrontendHarness harness{"module test.bad;\n"
                            "fn main() -> i32 {\n"
                            "    let answer: i32 = 42\n"
                            "    return answer;\n"
                            "}\n"};

    EXPECT_FALSE(harness.Parse());
    EXPECT_GT(harness.ErrorCount(), 0U);
    EXPECT_NE(harness.Diagnostics().find("expected ';'"), std::string::npos);
}

TEST(ParserTest, RejectsInvalidIntegerSeparators) {
    for (const std::string literal : {"1_", "1__2", "0x", "0b_", "0o_"}) {
        FrontendHarness harness{"module test.integer;\n"
                                "fn main() -> i32 { return " +
                                literal + "; }\n"};
        EXPECT_FALSE(harness.Parse()) << literal;
        EXPECT_GT(harness.ErrorCount(), 0U) << literal;
    }
}

TEST(ParserTest, RejectsPathologicalNestingWithoutStackOverflow) {
    std::string source{"module test.depth;\nfn main() -> i32 { return "};
    source.append(300U, '(');
    source += "1";
    source.append(300U, ')');
    source += "; }\n";

    FrontendHarness harness{source};
    EXPECT_FALSE(harness.Parse());
    EXPECT_NE(harness.Diagnostics().find("maximum parser nesting depth"),
              std::string::npos);
}

TEST(ParserTest, ParsesI64FunctionAndLocalTypes) {
    FrontendHarness harness{"module test.i64_types;\n"
                            "fn widen(value: i64) -> i64 {\n"
                            "    let copy: i64 = value;\n"
                            "    return copy;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_I64);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_I64);
}

TEST(ParserTest, RejectsIntegerMagnitudeBeyondI64Minimum) {
    FrontendHarness harness{"module test.integer_magnitude;\n"
                            "fn value() -> i64 {\n"
                            "    return -9223372036854775809;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.Parse());
    EXPECT_NE(harness.Diagnostics().find("integer literal is too large"),
              std::string::npos);
}

TEST(ParserTest, BuildsChainedExplicitConversionNodes) {
    FrontendHarness harness{"module test.conversion_syntax;\n"
                            "fn main() -> i32 {\n"
                            "    return (1 as i64) as i32;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);
    const LunaStatement *statement = function->body->first;
    ASSERT_NE(statement, nullptr);
    ASSERT_EQ(statement->kind, LUNA_STATEMENT_RETURN);
    const LunaExpression *outer = statement->as.return_value;
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(outer->kind, LUNA_EXPRESSION_CAST);
    EXPECT_EQ(outer->as.cast.target_type.kind, LUNA_TYPE_I32);
    const LunaExpression *inner = outer->as.cast.operand;
    ASSERT_NE(inner, nullptr);
    ASSERT_EQ(inner->kind, LUNA_EXPRESSION_CAST);
    EXPECT_EQ(inner->as.cast.target_type.kind, LUNA_TYPE_I64);
}

}
