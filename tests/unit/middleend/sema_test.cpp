#include "test_support.hpp"

#include <gtest/gtest.h>

#include <climits>
#include <cstddef>

namespace luna::test {

TEST(SemaTest, LowersNestedCallsWithoutOverlappingArguments) {
    FrontendHarness harness{
        "module test.nested;\n"
        "fn identity(value: i32) -> i32 { return value; }\n"
        "fn sum(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return sum(identity(20), 22); }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrModule *module = harness.Module();
    ASSERT_EQ(module->functions.length, 3U);
    LunaIrFunction *main_function = luna_ir_module_function(module, 2U);
    ASSERT_NE(main_function, nullptr);
    ASSERT_EQ(main_function->arguments.length, 3U);

    LunaIrInstruction *inner_call = nullptr;
    LunaIrInstruction *outer_call = nullptr;
    for (std::size_t block_index = 0U;
         block_index < main_function->blocks.length; block_index += 1U) {
        auto *block = static_cast<LunaIrBlock *>(
            luna_vector_at(&main_function->blocks, block_index));
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            auto *instruction = static_cast<LunaIrInstruction *>(
                luna_vector_at(&block->instructions, instruction_index));
            if (instruction->opcode != LUNA_IR_CALL) {
                continue;
            }
            if (instruction->callee == 0U) {
                inner_call = instruction;
            } else if (instruction->callee == 1U) {
                outer_call = instruction;
            }
        }
    }

    ASSERT_NE(inner_call, nullptr);
    ASSERT_NE(outer_call, nullptr);
    EXPECT_EQ(inner_call->first_argument, 0U);
    EXPECT_EQ(inner_call->argument_count, 1U);
    EXPECT_EQ(outer_call->first_argument, 1U);
    EXPECT_EQ(outer_call->argument_count, 2U);

    const auto *outer_first = static_cast<const LunaIrValueId *>(
        luna_vector_at_const(&main_function->arguments, 1U));
    ASSERT_NE(outer_first, nullptr);
    EXPECT_EQ(*outer_first, inner_call->result);
}

TEST(SemaTest, AcceptsMinimumI32Literal) {
    FrontendHarness harness{"module test.minimum;\n"
                            "fn main() -> i32 { return -2147483648; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *main_function =
        luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *constant =
        FindInstruction(main_function, LUNA_IR_CONST_I32);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->immediate, INT32_MIN);
}

TEST(SemaTest, RejectsImplicitIntegerTruthiness) {
    FrontendHarness harness{"module test.truthiness;\n"
                            "fn main() -> i32 {\n"
                            "    if (1) { return 1; }\n"
                            "    return 0;\n"
                            "}\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("expected bool, found i32"),
              std::string::npos);
}

TEST(SemaTest, RejectsWrongCallArityAndArgumentType) {
    FrontendHarness wrong_arity{"module test.arity;\n"
                                "fn take(value: i32) -> i32 { return value; }\n"
                                "fn main() -> i32 { return take(1, 2); }\n"};
    EXPECT_FALSE(wrong_arity.ParseAndLower());
    EXPECT_NE(wrong_arity.Diagnostics().find("expects 1 arguments, found 2"),
              std::string::npos);

    FrontendHarness wrong_type{"module test.call_type;\n"
                               "fn take(value: i32) -> i32 { return value; }\n"
                               "fn main() -> i32 { return take(true); }\n"};
    EXPECT_FALSE(wrong_type.ParseAndLower());
    EXPECT_NE(wrong_type.Diagnostics().find("expected i32, found bool"),
              std::string::npos);
}

TEST(SemaTest, RejectsIllTypedUnreachableCode) {
    FrontendHarness harness{"module test.unreachable;\n"
                            "fn main() -> i32 {\n"
                            "    return 0;\n"
                            "    let broken: bool = 1;\n"
                            "}\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("expected bool, found i32"),
              std::string::npos);
}

TEST(SemaTest, KeepsWellTypedUnreachableCodeOutsideLiveControlFlow) {
    FrontendHarness harness{"module test.unreachable_valid;\n"
                            "fn main() -> i32 {\n"
                            "    return 0;\n"
                            "    let ignored: i32 = 1;\n"
                            "}\n"};

    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

}
