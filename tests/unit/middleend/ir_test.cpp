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

[[nodiscard]] LunaTargetInfo Pointer32Target() {
    LunaTargetInfo target = *luna_target_info_default();
    target.triple = "test32-unknown-none";
    target.architecture = LUNA_TARGET_ARCHITECTURE_UNKNOWN;
    target.operating_system = LUNA_TARGET_OPERATING_SYSTEM_UNKNOWN;
    target.abi = LUNA_TARGET_ABI_UNKNOWN;
    target.data_layout.pointer.size_bits = 32U;
    target.data_layout.pointer.abi_alignment_bits = 32U;
    return target;
}

}

TEST(IrTypeTest, ReportsAllIntegerMetadata) {
    const LunaDataLayout &layout = luna_target_info_default()->data_layout;

    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_I8), "i8");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_I8));
    EXPECT_TRUE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_I8));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_I8, &layout), 8U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_I16), "i16");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_I16));
    EXPECT_TRUE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_I16));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_I16, &layout), 16U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_U8), "u8");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_U8));
    EXPECT_FALSE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_U8));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_U8, &layout), 8U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_U16), "u16");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_U16));
    EXPECT_FALSE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_U16));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_U16, &layout), 16U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_ISIZE), "isize");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_ISIZE));
    EXPECT_TRUE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_ISIZE));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_ISIZE, &layout), 64U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_USIZE), "usize");
    EXPECT_TRUE(luna_ir_type_is_integer(LUNA_IR_TYPE_USIZE));
    EXPECT_FALSE(luna_ir_type_is_signed_integer(LUNA_IR_TYPE_USIZE));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_USIZE, &layout), 64U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_F32), "f32");
    EXPECT_TRUE(luna_ir_type_is_float(LUNA_IR_TYPE_F32));
    EXPECT_FALSE(luna_ir_type_is_integer(LUNA_IR_TYPE_F32));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_F32, &layout), 32U);
    EXPECT_STREQ(luna_ir_type_name(LUNA_IR_TYPE_F64), "f64");
    EXPECT_TRUE(luna_ir_type_is_float(LUNA_IR_TYPE_F64));
    EXPECT_FALSE(luna_ir_type_is_integer(LUNA_IR_TYPE_F64));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_F64, &layout), 64U);
    EXPECT_FALSE(luna_ir_type_is_float(LUNA_IR_TYPE_BOOL));
    EXPECT_EQ(luna_ir_type_bit_width(LUNA_IR_TYPE_ISIZE, nullptr), 0U);
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

TEST(IrVerifierTest, AcceptsGenericUnsignedIntegerInstructions) {
    FrontendHarness harness{
        "module test.valid_unsigned_ir;\n"
        "fn calculate(value: u64) -> u64 {\n"
        "    return (value / 3) >> 2;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    if (calculate(18446744073709551615) > 1) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(IrVerifierTest, AcceptsWellTypedFloatingPointInstructions) {
    FrontendHarness harness{"module test.valid_float_ir;\n"
                            "fn calculate(left: f32, right: f32) -> f32 {\n"
                            "    return -((left + right) * 2.0 / 4.0);\n"
                            "}\n"
                            "fn ordered(left: f64, right: f64) -> bool {\n"
                            "    return left <= right;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(IrVerifierTest, AcceptsBodylessExternalCDeclarations) {
    FrontendHarness harness{
        "module test.external_ir;\n"
        "extern fn c_mix(value: i8, size: usize, factor: f32) -> bool;\n"
        "fn main() -> i32 { return c_mix(-1, 2, 3.0) ? 42 : 1; }\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *external = luna_ir_module_function(harness.Module(), 0U);
    ASSERT_NE(external, nullptr);
    EXPECT_EQ(external->linkage, LUNA_IR_LINKAGE_EXTERNAL_C);
    EXPECT_EQ(external->blocks.length, 0U);
    EXPECT_EQ(external->slots.length, 0U);
}

TEST(IrVerifierTest, RejectsDefinitionsAndEntryPointsWithExternalLinkage) {
    FrontendHarness definition{"module test.external_definition_ir;\n"
                               "extern fn c_value() -> i32;\n"
                               "fn main() -> i32 { return c_value(); }\n"};
    ASSERT_TRUE(definition.Verify()) << definition.Diagnostics();
    LunaIrFunction *external = luna_ir_module_function(definition.Module(), 0U);
    ASSERT_NE(external, nullptr);
    ASSERT_NE(luna_ir_function_add_slot(external, LUNA_IR_TYPE_I32),
              LUNA_IR_INVALID_ID);
    EXPECT_FALSE(definition.Verify());
    EXPECT_NE(definition.Diagnostics().find("external function 0 has a "
                                            "definition"),
              std::string::npos);

    FrontendHarness entry{"module test.external_entry_ir;\n"
                          "extern fn c_value() -> i32;\n"
                          "fn main() -> i32 { return c_value(); }\n"};
    ASSERT_TRUE(entry.Verify()) << entry.Diagnostics();
    entry.Module()->entry_function = 0U;
    EXPECT_FALSE(entry.Verify());
    EXPECT_NE(entry.Diagnostics().find(
                  "entry function must be an internal fn() -> i32"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsAMissingTargetLayout) {
    FrontendHarness harness{"module test.missing_target;\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    harness.Module()->target = nullptr;
    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("target data layout"),
              std::string::npos);
}

TEST(IrVerifierTest, UsesTargetWidthWhenValidatingPointerSizedConstants) {
    const LunaTargetInfo target32 = Pointer32Target();
    FrontendHarness harness{"module test.usize_ir_width;\n"
                            "fn value() -> usize { return 1; }\n"
                            "fn main() -> i32 { return 0; }\n",
                            &target32};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *constant = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    constant->immediate = std::uint64_t{1} << 32U;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("exceeds its type storage width"),
              std::string::npos);
}

TEST(IrPrinterTest, UsesTargetWidthWhenNamingIntegerConversions) {
    const LunaTargetInfo target32 = Pointer32Target();
    FrontendHarness harness{
        "module test.isize_ir_print;\n"
        "fn narrow(value: i64) -> isize { return value as isize; }\n"
        "fn main() -> i32 { return 0; }\n",
        &target32};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    const bool printed = luna_ir_print(harness.Module(), &output);
    const std::string text =
        printed ? std::string{luna_string_builder_data(&output), output.length}
                : std::string{};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(printed);
    EXPECT_NE(text.find("target \"test32-unknown-none\""), std::string::npos);
    EXPECT_NE(text.find("trunc.i64.isize"), std::string::npos);
}

TEST(IrPrinterTest, PrintsExternalSignaturesWithoutBodiesOrSlots) {
    FrontendHarness harness{
        "module test.external_ir_print;\n"
        "extern fn c_mix(value: i8, size: usize, factor: f64) -> bool;\n"
        "fn main() -> i32 { return c_mix(-1, 2, 3.0) ? 42 : 1; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    const bool printed = luna_ir_print(harness.Module(), &output);
    const std::string text =
        printed ? std::string{luna_string_builder_data(&output), output.length}
                : std::string{};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(printed);
    EXPECT_NE(text.find("extern fn @c_mix(i8, usize, f64) -> bool"),
              std::string::npos);
    EXPECT_EQ(text.find("fn @c_mix($0:"), std::string::npos);
}

TEST(IrPrinterTest, QualifiesCollidingInternalFunctionNamesAcrossModules) {
    CompilationHarness harness{
        "module app.ir_names;\n"
        "import lib.left;\n"
        "import lib.right;\n"
        "fn main() -> i32 { return left() + right(); }\n",
        "export module lib.left;\n"
        "export fn left() -> i32;\n",
        "module lib.left;\n"
        "fn helper() -> i32 { return 20; }\n"
        "fn left() -> i32 { return helper(); }\n",
        "export module lib.right;\n"
        "export fn right() -> i32;\n",
        "module lib.right;\n"
        "fn helper() -> i32 { return 22; }\n"
        "fn right() -> i32 { return helper(); }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    const bool printed = luna_ir_print(harness.Module(), &output);
    const std::string text =
        printed ? std::string{luna_string_builder_data(&output), output.length}
                : std::string{};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(printed);
    EXPECT_NE(text.find("fn @lib.left::helper("), std::string::npos);
    EXPECT_NE(text.find("fn @lib.right::helper("), std::string::npos);
    EXPECT_NE(text.find("call @lib.left::helper("), std::string::npos);
    EXPECT_NE(text.find("call @lib.right::helper("), std::string::npos);
}

TEST(IrPrinterTest, PrintsTypedAggregateMemberAddresses) {
    FrontendHarness harness{
        "module test.aggregate_ir_print;\n"
        "struct Pair { byte: u8; value: i32; }\n"
        "fn main() -> i32 {\n"
        "    var pair: Pair = {}; pair.value = 42; return pair.value;\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    const bool printed = luna_ir_print(harness.Module(), &output);
    const std::string text =
        printed ? std::string{luna_string_builder_data(&output), output.length}
                : std::string{};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(printed);
    EXPECT_NE(text.find("member_address 4"), std::string::npos);
    EXPECT_NE(text.find("zero $0"), std::string::npos);
    EXPECT_NE(text.find("store_indirect.i32"), std::string::npos);
}

TEST(IrPrinterTest, PrintsSizedMemoryCopiesWithDestinationFirst) {
    FrontendHarness harness{
        "module test.memory_copy_ir_print;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn main() -> i32 {\n"
        "    var first: Pair = { left = 20, right = 22, };\n"
        "    var second: Pair = first;\n"
        "    return second.left + second.right;\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    const bool printed = luna_ir_print(harness.Module(), &output);
    const std::string text =
        printed ? std::string{luna_string_builder_data(&output), output.length}
                : std::string{};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(printed);
    EXPECT_NE(text.find("memory_copy 8, %"), std::string::npos);
    EXPECT_NE(FindInstruction(MainFunction(harness), LUNA_IR_MEMORY_COPY),
              nullptr);
}

TEST(IrVerifierTest, ValidatesAggregateMemberAddressOperandsAndOffsets) {
    constexpr std::string_view LUNA_TEST_SOURCE =
        "module test.aggregate_ir_verify;\n"
        "struct Pair { byte: u8; value: i32; }\n"
        "fn main() -> i32 {\n"
        " let wrong: i32 = 1; var pair: Pair = {};\n"
        " pair.value = 42; return pair.value + wrong;\n"
        "}\n";

    FrontendHarness displacement{LUNA_TEST_SOURCE};
    ASSERT_TRUE(displacement.Verify()) << displacement.Diagnostics();
    LunaIrInstruction *member =
        FindInstruction(MainFunction(displacement), LUNA_IR_MEMBER_ADDRESS);
    ASSERT_NE(member, nullptr);
    member->immediate = static_cast<std::uint64_t>(INT32_MAX) + 1U;
    EXPECT_FALSE(displacement.Verify());
    EXPECT_NE(displacement.Diagnostics().find(
                  "member address offset exceeds backend displacement"),
              std::string::npos);

    FrontendHarness operand{LUNA_TEST_SOURCE};
    ASSERT_TRUE(operand.Verify()) << operand.Diagnostics();
    member = FindInstruction(MainFunction(operand), LUNA_IR_MEMBER_ADDRESS);
    LunaIrInstruction *integer =
        FindInstruction(MainFunction(operand), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(member, nullptr);
    ASSERT_NE(integer, nullptr);
    member->left = integer->result;
    EXPECT_FALSE(operand.Verify());
    EXPECT_NE(operand.Diagnostics().find("operand type does not match opcode"),
              std::string::npos);
}

TEST(IrVerifierTest, ValidatesMemoryCopySizeAndPointerOperands) {
    constexpr std::string_view LUNA_TEST_SOURCE =
        "module test.memory_copy_ir_verify;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn main() -> i32 {\n"
        " var first: Pair = { left = 20, right = 22, };\n"
        " var second: Pair = first;\n"
        " return second.left + second.right;\n"
        "}\n";

    FrontendHarness zero_size{LUNA_TEST_SOURCE};
    ASSERT_TRUE(zero_size.Verify()) << zero_size.Diagnostics();
    LunaIrInstruction *copy =
        FindInstruction(MainFunction(zero_size), LUNA_IR_MEMORY_COPY);
    ASSERT_NE(copy, nullptr);
    copy->immediate = 0U;
    EXPECT_FALSE(zero_size.Verify());
    EXPECT_NE(zero_size.Diagnostics().find(
                  "memory copy size must fit the positive object-size range"),
              std::string::npos);

    FrontendHarness oversized{LUNA_TEST_SOURCE};
    ASSERT_TRUE(oversized.Verify()) << oversized.Diagnostics();
    copy = FindInstruction(MainFunction(oversized), LUNA_IR_MEMORY_COPY);
    ASSERT_NE(copy, nullptr);
    copy->immediate = static_cast<std::uint64_t>(INT32_MAX) + 1U;
    EXPECT_FALSE(oversized.Verify());
    EXPECT_NE(oversized.Diagnostics().find(
                  "memory copy size must fit the positive object-size range"),
              std::string::npos);

    FrontendHarness operand{LUNA_TEST_SOURCE};
    ASSERT_TRUE(operand.Verify()) << operand.Diagnostics();
    copy = FindInstruction(MainFunction(operand), LUNA_IR_MEMORY_COPY);
    LunaIrInstruction *integer =
        FindInstruction(MainFunction(operand), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(integer, nullptr);
    copy->right = integer->result;
    EXPECT_FALSE(operand.Verify());
    EXPECT_NE(operand.Diagnostics().find("operand type does not match opcode"),
              std::string::npos);

    FrontendHarness destination_operand{LUNA_TEST_SOURCE};
    ASSERT_TRUE(destination_operand.Verify())
        << destination_operand.Diagnostics();
    copy =
        FindInstruction(MainFunction(destination_operand), LUNA_IR_MEMORY_COPY);
    integer = FindInstruction(MainFunction(destination_operand),
                              LUNA_IR_CONST_INTEGER);
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(integer, nullptr);
    copy->left = integer->result;
    EXPECT_FALSE(destination_operand.Verify());
    EXPECT_NE(destination_operand.Diagnostics().find(
                  "operand type does not match opcode"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsValueUsedBeforeItsDefinition) {
    FrontendHarness harness{"module test.use_before_definition;\n"
                            "fn main() -> i32 { return 1 + 2; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *addition =
        FindInstruction(MainFunction(harness), LUNA_IR_ADD_INTEGER);
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
    LunaIrInstruction *integer =
        FindInstruction(function, LUNA_IR_CONST_INTEGER);
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

TEST(IrVerifierTest, RejectsOperandThatDoesNotMatchGenericOpcodeType) {
    FrontendHarness harness{"module test.i64_opcode_type;\n"
                            "fn wide(value: i64) -> i64 {\n"
                            "    return value + 1;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *addition =
        FindInstruction(function, LUNA_IR_ADD_INTEGER);
    ASSERT_NE(addition, nullptr);
    addition->type = LUNA_IR_TYPE_I32;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("operand type does not match opcode"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsConstantThatExceedsItsStorageWidth) {
    FrontendHarness harness{"module test.i32_immediate_range;\n"
                            "fn main() -> i32 { return 1; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *constant =
        FindInstruction(MainFunction(harness), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    constant->immediate =
        static_cast<std::uint64_t>(UINT32_MAX) + static_cast<std::uint64_t>(1U);

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("exceeds its type storage width"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsNarrowConstantsThatExceedTheirStorageWidth) {
    FrontendHarness harness{"module test.narrow_immediate_range;\n"
                            "fn byte() -> u8 { return 1; }\n"
                            "fn word() -> u16 { return 1; }\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *byte_constant = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_INTEGER);
    LunaIrInstruction *word_constant = FindInstruction(
        luna_ir_module_function(harness.Module(), 1U), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(byte_constant, nullptr);
    ASSERT_NE(word_constant, nullptr);

    byte_constant->immediate = std::uint64_t{1} << 8U;
    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("exceeds its type storage width"),
              std::string::npos);

    byte_constant->immediate = 1U;
    word_constant->immediate = std::uint64_t{1} << 16U;
    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("exceeds its type storage width"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsF32ConstantsThatExceedTheirStorageWidth) {
    FrontendHarness harness{"module test.f32_immediate_range;\n"
                            "fn value() -> f32 { return 1.0; }\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *constant = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_FLOAT);
    ASSERT_NE(constant, nullptr);
    constant->immediate = std::uint64_t{1} << 32U;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("f32 constant exceeds"),
              std::string::npos);
}

TEST(IrVerifierTest, KeepsIntegerAndFloatingOpcodesDisjoint) {
    FrontendHarness floating{
        "module test.float_opcode_type;\n"
        "fn value(left: f64, right: f64) -> f64 { return left + right; }\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(floating.Verify()) << floating.Diagnostics();
    LunaIrInstruction *float_add = FindInstruction(
        luna_ir_module_function(floating.Module(), 0U), LUNA_IR_ADD_FLOAT);
    ASSERT_NE(float_add, nullptr);
    float_add->opcode = LUNA_IR_ADD_INTEGER;
    EXPECT_FALSE(floating.Verify());
    EXPECT_NE(floating.Diagnostics().find(
                  "integer binary operation has invalid type"),
              std::string::npos);

    FrontendHarness integer{
        "module test.integer_opcode_type;\n"
        "fn value(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(integer.Verify()) << integer.Diagnostics();
    LunaIrInstruction *integer_add = FindInstruction(
        luna_ir_module_function(integer.Module(), 0U), LUNA_IR_ADD_INTEGER);
    ASSERT_NE(integer_add, nullptr);
    integer_add->opcode = LUNA_IR_ADD_FLOAT;
    EXPECT_FALSE(integer.Verify());
    EXPECT_NE(integer.Diagnostics().find(
                  "floating binary operation has invalid type"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsRedundantIntegerConversion) {
    FrontendHarness harness{"module test.conversion_ir;\n"
                            "fn widen(value: i32) -> i64 {\n"
                            "    return value as i64;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *conversion =
        FindInstruction(function, LUNA_IR_CONVERT_INTEGER);
    ASSERT_NE(conversion, nullptr);
    conversion->type = LUNA_IR_TYPE_I32;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("integer conversion is redundant"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsRedundantFloatingConversion) {
    FrontendHarness harness{"module test.float_conversion_ir;\n"
                            "fn widen(value: f32) -> f64 {\n"
                            "    return value as f64;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *conversion = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONVERT_FLOAT);
    ASSERT_NE(conversion, nullptr);
    conversion->type = LUNA_IR_TYPE_F32;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find("floating conversion is redundant"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsInvalidFloatingWidthConversionTypes) {
    FrontendHarness invalid_source{"module test.float_conversion_source_ir;\n"
                                   "fn widen(value: f32) -> f64 {\n"
                                   "    let marker: bool = true;\n"
                                   "    return value as f64;\n"
                                   "}\n"
                                   "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(invalid_source.Verify()) << invalid_source.Diagnostics();
    LunaIrFunction *source_function =
        luna_ir_module_function(invalid_source.Module(), 0U);
    LunaIrInstruction *boolean =
        FindInstruction(source_function, LUNA_IR_CONST_BOOL);
    LunaIrInstruction *source_conversion =
        FindInstruction(source_function, LUNA_IR_CONVERT_FLOAT);
    ASSERT_NE(boolean, nullptr);
    ASSERT_NE(source_conversion, nullptr);
    source_conversion->left = boolean->result;
    EXPECT_FALSE(invalid_source.Verify());
    EXPECT_NE(invalid_source.Diagnostics().find(
                  "floating conversion has invalid source type"),
              std::string::npos);

    FrontendHarness invalid_result{
        "module test.float_conversion_result_ir;\n"
        "fn widen(value: f32) -> f64 { return value as f64; }\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(invalid_result.Verify()) << invalid_result.Diagnostics();
    LunaIrInstruction *result_conversion =
        FindInstruction(luna_ir_module_function(invalid_result.Module(), 0U),
                        LUNA_IR_CONVERT_FLOAT);
    ASSERT_NE(result_conversion, nullptr);
    result_conversion->type = LUNA_IR_TYPE_I64;
    EXPECT_FALSE(invalid_result.Verify());
    EXPECT_NE(invalid_result.Diagnostics().find(
                  "floating conversion has invalid result type"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsInvalidCrossCategoryConversions) {
    FrontendHarness integer_to_float{
        "module test.integer_to_float_ir;\n"
        "fn convert(value: i32) -> f32 { return value as f32; }\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(integer_to_float.Verify()) << integer_to_float.Diagnostics();
    LunaIrInstruction *to_float =
        FindInstruction(luna_ir_module_function(integer_to_float.Module(), 0U),
                        LUNA_IR_CONVERT_INTEGER_TO_FLOAT);
    ASSERT_NE(to_float, nullptr);
    to_float->type = LUNA_IR_TYPE_I32;
    EXPECT_FALSE(integer_to_float.Verify());
    EXPECT_NE(integer_to_float.Diagnostics().find(
                  "integer-to-floating conversion has invalid result type"),
              std::string::npos);

    FrontendHarness invalid_integer_source{
        "module test.integer_to_float_source_ir;\n"
        "fn convert(value: i32) -> f32 {\n"
        "    let marker: bool = true;\n"
        "    return value as f32;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(invalid_integer_source.Verify())
        << invalid_integer_source.Diagnostics();
    LunaIrFunction *integer_source_function =
        luna_ir_module_function(invalid_integer_source.Module(), 0U);
    LunaIrInstruction *integer_source_boolean =
        FindInstruction(integer_source_function, LUNA_IR_CONST_BOOL);
    LunaIrInstruction *invalid_to_float = FindInstruction(
        integer_source_function, LUNA_IR_CONVERT_INTEGER_TO_FLOAT);
    ASSERT_NE(integer_source_boolean, nullptr);
    ASSERT_NE(invalid_to_float, nullptr);
    invalid_to_float->left = integer_source_boolean->result;
    EXPECT_FALSE(invalid_integer_source.Verify());
    EXPECT_NE(invalid_integer_source.Diagnostics().find(
                  "integer-to-floating conversion has invalid source type"),
              std::string::npos);

    FrontendHarness float_to_integer{"module test.float_to_integer_ir;\n"
                                     "fn convert(value: f64) -> i64 {\n"
                                     "    let marker: bool = true;\n"
                                     "    return value as i64;\n"
                                     "}\n"
                                     "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(float_to_integer.Verify()) << float_to_integer.Diagnostics();
    LunaIrFunction *function =
        luna_ir_module_function(float_to_integer.Module(), 0U);
    LunaIrInstruction *boolean = FindInstruction(function, LUNA_IR_CONST_BOOL);
    LunaIrInstruction *to_integer =
        FindInstruction(function, LUNA_IR_CONVERT_FLOAT_TO_INTEGER);
    ASSERT_NE(boolean, nullptr);
    ASSERT_NE(to_integer, nullptr);
    to_integer->left = boolean->result;
    EXPECT_FALSE(float_to_integer.Verify());
    EXPECT_NE(float_to_integer.Diagnostics().find(
                  "floating-to-integer conversion has invalid source type"),
              std::string::npos);

    FrontendHarness invalid_integer_result{
        "module test.float_to_integer_result_ir;\n"
        "fn convert(value: f64) -> i64 { return value as i64; }\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(invalid_integer_result.Verify())
        << invalid_integer_result.Diagnostics();
    LunaIrInstruction *invalid_to_integer = FindInstruction(
        luna_ir_module_function(invalid_integer_result.Module(), 0U),
        LUNA_IR_CONVERT_FLOAT_TO_INTEGER);
    ASSERT_NE(invalid_to_integer, nullptr);
    invalid_to_integer->type = LUNA_IR_TYPE_F64;
    EXPECT_FALSE(invalid_integer_result.Verify());
    EXPECT_NE(invalid_integer_result.Diagnostics().find(
                  "floating-to-integer conversion has invalid result type"),
              std::string::npos);
}

TEST(IrVerifierTest, RejectsOrderingComparisonOnBooleanOperands) {
    FrontendHarness harness{"module test.bool_ordering_ir;\n"
                            "fn main() -> i32 {\n"
                            "    if (true == false) { return 1; }\n"
                            "    return 0;\n"
                            "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrInstruction *comparison =
        FindInstruction(MainFunction(harness), LUNA_IR_COMPARE_EQUAL);
    ASSERT_NE(comparison, nullptr);
    comparison->opcode = LUNA_IR_COMPARE_LESS_INTEGER;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(
        harness.Diagnostics().find("ordering operand type is not integer"),
        std::string::npos);
}

TEST(IrVerifierTest, RejectsNonIntegerConversionSource) {
    FrontendHarness harness{"module test.conversion_source_ir;\n"
                            "fn widen(value: i32) -> i64 {\n"
                            "    let marker: bool = true;\n"
                            "    return value as i64;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *function = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *boolean = FindInstruction(function, LUNA_IR_CONST_BOOL);
    LunaIrInstruction *conversion =
        FindInstruction(function, LUNA_IR_CONVERT_INTEGER);
    ASSERT_NE(boolean, nullptr);
    ASSERT_NE(conversion, nullptr);
    conversion->left = boolean->result;

    EXPECT_FALSE(harness.Verify());
    EXPECT_NE(harness.Diagnostics().find(
                  "integer conversion has invalid source type"),
              std::string::npos);
}

TEST(IrVerifierTest, ValidatesTypedMemoryInstructionsAndLayouts) {
    const std::string source{"module test.memory_verifier;\n"
                             "fn main() -> i32 {\n"
                             "    var values: [2]i32 = {};\n"
                             "    values[1] = 42;\n"
                             "    let pointer: *i32 = &values[0];\n"
                             "    let text: *const u8 = \"A\";\n"
                             "    return pointer[1] + (text[0] as i32);\n"
                             "}\n"};

    FrontendHarness invalid_store{source};
    ASSERT_TRUE(invalid_store.Verify()) << invalid_store.Diagnostics();
    LunaIrFunction *store_function = MainFunction(invalid_store);
    LunaIrInstruction *store =
        FindInstruction(store_function, LUNA_IR_STORE_INDIRECT);
    ASSERT_NE(store, nullptr);
    store->memory_type = LUNA_IR_TYPE_VOID;
    EXPECT_FALSE(invalid_store.Verify());
    EXPECT_NE(invalid_store.Diagnostics().find(
                  "indirect store has invalid access type"),
              std::string::npos);

    FrontendHarness invalid_bounds{source};
    ASSERT_TRUE(invalid_bounds.Verify()) << invalid_bounds.Diagnostics();
    LunaIrInstruction *bounds =
        FindInstruction(MainFunction(invalid_bounds), LUNA_IR_BOUNDS_CHECK);
    ASSERT_NE(bounds, nullptr);
    bounds->immediate = 0U;
    EXPECT_FALSE(invalid_bounds.Verify());
    EXPECT_NE(invalid_bounds.Diagnostics().find(
                  "bounds check requires a positive bound"),
              std::string::npos);

    FrontendHarness invalid_global{source};
    ASSERT_TRUE(invalid_global.Verify()) << invalid_global.Diagnostics();
    LunaIrInstruction *global =
        FindInstruction(MainFunction(invalid_global), LUNA_IR_GLOBAL_ADDRESS);
    ASSERT_NE(global, nullptr);
    global->global = LUNA_IR_INVALID_ID;
    EXPECT_FALSE(invalid_global.Verify());
    EXPECT_NE(invalid_global.Diagnostics().find("global id is out of range"),
              std::string::npos);

    FrontendHarness invalid_layout{source};
    ASSERT_TRUE(invalid_layout.Verify()) << invalid_layout.Diagnostics();
    LunaIrFunction *layout_function = MainFunction(invalid_layout);
    LunaIrSlot *slot =
        static_cast<LunaIrSlot *>(luna_vector_at(&layout_function->slots, 0U));
    ASSERT_NE(slot, nullptr);
    slot->alignment_bytes = 3U;
    EXPECT_FALSE(invalid_layout.Verify());
    EXPECT_NE(invalid_layout.Diagnostics().find("invalid slot layout"),
              std::string::npos);
}

}
