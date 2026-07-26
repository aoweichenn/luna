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

TEST(ParserTest, ParsesUnsignedFunctionTypesAndMaximumLiteral) {
    FrontendHarness harness{"module test.unsigned_types;\n"
                            "fn widen(value: u32) -> u64 {\n"
                            "    let maximum: u64 = 18446744073709551615;\n"
                            "    return maximum;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_U64);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_U32);
    ASSERT_NE(function->body, nullptr);
    const LunaStatement *declaration = function->body->first;
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->kind, LUNA_STATEMENT_DECLARATION);
    ASSERT_NE(declaration->as.declaration.initializer, nullptr);
    EXPECT_EQ(declaration->as.declaration.initializer->as.integer, UINT64_MAX);
}

TEST(ParserTest, ParsesAllNarrowIntegerTypes) {
    FrontendHarness harness{"module test.narrow_types;\n"
                            "fn signed_pair(left: i8, right: i16) -> i16 {\n"
                            "    return left as i16;\n"
                            "}\n"
                            "fn unsigned_pair(left: u8, right: u16) -> u16 {\n"
                            "    return left as u16;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *signed_function = harness.Program()->first_function;
    ASSERT_NE(signed_function, nullptr);
    EXPECT_EQ(signed_function->return_type.kind, LUNA_TYPE_I16);
    ASSERT_NE(signed_function->first_parameter, nullptr);
    EXPECT_EQ(signed_function->first_parameter->type.kind, LUNA_TYPE_I8);
    ASSERT_NE(signed_function->first_parameter->next, nullptr);
    EXPECT_EQ(signed_function->first_parameter->next->type.kind, LUNA_TYPE_I16);

    const LunaFunction *unsigned_function = signed_function->next;
    ASSERT_NE(unsigned_function, nullptr);
    EXPECT_EQ(unsigned_function->return_type.kind, LUNA_TYPE_U16);
    ASSERT_NE(unsigned_function->first_parameter, nullptr);
    EXPECT_EQ(unsigned_function->first_parameter->type.kind, LUNA_TYPE_U8);
    ASSERT_NE(unsigned_function->first_parameter->next, nullptr);
    EXPECT_EQ(unsigned_function->first_parameter->next->type.kind,
              LUNA_TYPE_U16);
}

TEST(ParserTest, RejectsIntegerMagnitudeBeyondU64Maximum) {
    FrontendHarness harness{"module test.integer_magnitude;\n"
                            "fn value() -> u64 {\n"
                            "    return 18446744073709551616;\n"
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
