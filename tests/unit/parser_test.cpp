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

}
