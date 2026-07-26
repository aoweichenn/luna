#include "test_support.hpp"

#include <gtest/gtest.h>

#include <climits>
#include <cstddef>
#include <cstdint>

namespace luna::test {
namespace {

[[nodiscard]] LunaIrFunction *MainFunction(FrontendHarness &harness) {
    LunaIrModule *module = harness.Module();
    return luna_ir_module_function(module, module->entry_function);
}

}

TEST(IrVerifierTest, AcceptsWellTypedControlFlowGraph) {
    FrontendHarness harness{"module test.valid_ir;\n"
                            "fn main() -> i32 {\n"
                            "    var value: i32 = 1;\n"
                            "    while (value < 5) { value += 1; }\n"
                            "    return value;\n"
                            "}\n"};

    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(IrVerifierTest, RejectsValueUsedBeforeItsDefinition) {
    FrontendHarness harness{"module test.use_before_definition;\n"
                            "fn main() -> i32 { return 1 + 2; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *addition =
        FindInstruction(MainFunction(harness), LUNA_IR_ADD_I32);
    ASSERT_NE(addition, nullptr);
    addition->left = addition->result;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("used before its definition"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsWrongBranchConditionType) {
    FrontendHarness harness{"module test.branch_type;\n"
                            "fn main() -> i32 {\n"
                            "    var value: i32 = 1;\n"
                            "    if (true) { value = 2; }\n"
                            "    return value;\n"
                            "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = MainFunction(harness);
    LunaIrInstruction *integer = FindInstruction(function, LUNA_IR_CONST_I32);
    LunaIrInstruction *branch = FindInstruction(function, LUNA_IR_BRANCH);
    ASSERT_NE(integer, nullptr);
    ASSERT_NE(branch, nullptr);
    branch->left = integer->result;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("operand type does not match opcode"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsNonCanonicalBooleanConstant) {
    FrontendHarness harness{"module test.bool_constant;\n"
                            "fn main() -> i32 {\n"
                            "    if (true) { return 1; }\n"
                            "    return 0;\n"
                            "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *constant =
        FindInstruction(MainFunction(harness), LUNA_IR_CONST_BOOL);
    ASSERT_NE(constant, nullptr);
    constant->immediate = 2;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("exactly zero or one"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsDuplicateResultDefinition) {
    FrontendHarness harness{"module test.duplicate_result;\n"
                            "fn main() -> i32 { return 1 + 2; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = MainFunction(harness);
    auto *entry =
        static_cast<LunaIrBlock *>(luna_vector_at(&function->blocks, 0U));
    ASSERT_NE(entry, nullptr);
    ASSERT_GE(entry->instructions.length, 2U);
    auto *first = static_cast<LunaIrInstruction *>(
        luna_vector_at(&entry->instructions, 0U));
    auto *second = static_cast<LunaIrInstruction *>(
        luna_vector_at(&entry->instructions, 1U));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    second->result = first->result;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("defined more than once"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsIncorrectPredecessorMetadata) {
    FrontendHarness harness{"module test.predecessors;\n"
                            "fn main() -> i32 {\n"
                            "    if (true) { return 1; }\n"
                            "    return 2;\n"
                            "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = MainFunction(harness);
    ASSERT_GT(function->blocks.length, 1U);
    auto *block =
        static_cast<LunaIrBlock *>(luna_vector_at(&function->blocks, 1U));
    ASSERT_NE(block, nullptr);
    block->predecessor_count += 1U;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("predecessor count mismatch"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsOverlappingCallArgumentStorage) {
    FrontendHarness harness{
        "module test.argument_overlap;\n"
        "fn identity(value: i32) -> i32 { return value; }\n"
        "fn add(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return add(identity(20), 22); }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = MainFunction(harness);
    LunaIrInstruction *outer_call = nullptr;
    for (std::size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        auto *block = static_cast<LunaIrBlock *>(
            luna_vector_at(&function->blocks, block_index));
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            auto *instruction = static_cast<LunaIrInstruction *>(
                luna_vector_at(&block->instructions, instruction_index));
            if (instruction->opcode == LUNA_IR_CALL &&
                instruction->argument_count == 2U) {
                outer_call = instruction;
            }
        }
    }
    ASSERT_NE(outer_call, nullptr);
    outer_call->first_argument = 0U;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("overlaps another call"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsIncorrectCachedTerminationState) {
    FrontendHarness harness{"module test.termination_state;\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = MainFunction(harness);
    auto *entry =
        static_cast<LunaIrBlock *>(luna_vector_at(&function->blocks, 0U));
    ASSERT_NE(entry, nullptr);
    entry->terminated = false;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("cached termination state is wrong"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsI64OperandOnI32Opcode) {
    FrontendHarness harness{"module test.i64_opcode_type;\n"
                            "fn wide(value: i64) -> i64 {\n"
                            "    return value + 1;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *addition = FindInstruction(function, LUNA_IR_ADD_I64);
    ASSERT_NE(addition, nullptr);
    addition->opcode = LUNA_IR_ADD_I32;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("operand type does not match opcode"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsOutOfRangeI32Constant) {
    FrontendHarness harness{"module test.i32_immediate_range;\n"
                            "fn main() -> i32 { return 1; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *constant =
        FindInstruction(MainFunction(harness), LUNA_IR_CONST_I32);
    ASSERT_NE(constant, nullptr);
    constant->immediate =
        static_cast<std::int64_t>(INT32_MAX) + static_cast<std::int64_t>(1);

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("outside the i32 range"),
              std::string::npos);
}

}
