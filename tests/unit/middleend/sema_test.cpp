#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace luna::test {

TEST(SemaTest, LowersExternalDeclarationsAndCallsToTypedIr) {
    FrontendHarness harness{
        "module test.external_sema;\n"
        "extern fn c_mix(value: i16, scale: f64, pointer: *i32) -> i64;\n"
        "fn main() -> i32 {\n"
        "    var value: i32 = 7;\n"
        "    return c_mix(-12, 1.5, &value) as i32;\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrModule *module = harness.Module();
    ASSERT_EQ(module->functions.length, 2U);

    LunaIrFunction *external = luna_ir_module_function(module, 0U);
    ASSERT_NE(external, nullptr);
    EXPECT_EQ(external->linkage, LUNA_IR_LINKAGE_EXTERNAL_C);
    EXPECT_EQ(external->return_type, LUNA_IR_TYPE_I64);
    EXPECT_EQ(external->parameter_types.length, 3U);
    EXPECT_EQ(external->slots.length, 0U);
    EXPECT_EQ(external->value_types.length, 0U);
    EXPECT_EQ(external->arguments.length, 0U);
    EXPECT_EQ(external->blocks.length, 0U);

    LunaIrFunction *main_function = luna_ir_module_function(module, 1U);
    ASSERT_NE(main_function, nullptr);
    EXPECT_EQ(main_function->linkage, LUNA_IR_LINKAGE_INTERNAL);
    LunaIrInstruction *call = FindInstruction(main_function, LUNA_IR_CALL);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->callee, 0U);
    EXPECT_EQ(call->argument_count, 3U);
    EXPECT_EQ(call->type, LUNA_IR_TYPE_I64);
}

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
        FindInstruction(main_function, LUNA_IR_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->immediate, std::uint64_t{1} << 31U);
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

TEST(SemaTest, ContextuallyTypesI64LiteralsAndOperators) {
    FrontendHarness harness{
        "module test.i64_context;\n"
        "fn calculate(value: i64) -> i64 {\n"
        "    return (value + 4294967296) * 2;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    if (calculate(5) == 8589934602) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *calculate = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *constant =
        FindInstruction(calculate, LUNA_IR_CONST_INTEGER);
    LunaIrInstruction *addition =
        FindInstruction(calculate, LUNA_IR_ADD_INTEGER);
    LunaIrInstruction *multiplication =
        FindInstruction(calculate, LUNA_IR_MUL_INTEGER);
    ASSERT_NE(constant, nullptr);
    ASSERT_NE(addition, nullptr);
    ASSERT_NE(multiplication, nullptr);
    EXPECT_EQ(constant->type, LUNA_IR_TYPE_I64);
    EXPECT_EQ(addition->type, LUNA_IR_TYPE_I64);
    EXPECT_EQ(multiplication->type, LUNA_IR_TYPE_I64);
}

TEST(SemaTest, AcceptsMinimumI64Literal) {
    FrontendHarness harness{"module test.minimum_i64;\n"
                            "fn minimum() -> i64 {\n"
                            "    return -9223372036854775808;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrInstruction *constant = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->immediate, std::uint64_t{1} << 63U);
}

TEST(SemaTest, PreservesPointerSizedIntegerTypesInTypedIr) {
    FrontendHarness harness{
        "module test.pointer_sized_ir;\n"
        "fn signed_calculate(left: isize, right: isize) -> isize {\n"
        "    return (left + right) >> 65;\n"
        "}\n"
        "fn unsigned_calculate(left: usize, right: usize) -> usize {\n"
        "    return (left * right) / 3;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let signed: isize = signed_calculate(-16, 8);\n"
        "    let unsigned: usize = unsigned_calculate(7, 9);\n"
        "    if (signed == -4 && unsigned == 21) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *signed_function =
        luna_ir_module_function(harness.Module(), 0U);
    LunaIrFunction *unsigned_function =
        luna_ir_module_function(harness.Module(), 1U);
    ASSERT_NE(signed_function, nullptr);
    ASSERT_NE(unsigned_function, nullptr);
    EXPECT_EQ(signed_function->return_type, LUNA_IR_TYPE_ISIZE);
    EXPECT_EQ(unsigned_function->return_type, LUNA_IR_TYPE_USIZE);

    LunaIrInstruction *signed_shift =
        FindInstruction(signed_function, LUNA_IR_SHIFT_RIGHT_INTEGER);
    LunaIrInstruction *unsigned_division =
        FindInstruction(unsigned_function, LUNA_IR_DIV_INTEGER);
    ASSERT_NE(signed_shift, nullptr);
    ASSERT_NE(unsigned_division, nullptr);
    EXPECT_EQ(signed_shift->type, LUNA_IR_TYPE_ISIZE);
    EXPECT_EQ(unsigned_division->type, LUNA_IR_TYPE_USIZE);
}

TEST(SemaTest, UsesTheTargetPointerWidthForIntegerLiteralBounds) {
    LunaTargetInfo target32 = *luna_target_info_default();
    target32.triple = "test32-unknown-none";
    target32.architecture = LUNA_TARGET_ARCHITECTURE_UNKNOWN;
    target32.operating_system = LUNA_TARGET_OPERATING_SYSTEM_UNKNOWN;
    target32.abi = LUNA_TARGET_ABI_UNKNOWN;
    target32.data_layout.pointer.size_bits = 32U;
    target32.data_layout.pointer.abi_alignment_bits = 32U;
    ASSERT_TRUE(luna_data_layout_is_valid(&target32.data_layout));

    FrontendHarness valid{
        "module test.pointer_width_32;\n"
        "fn signed_minimum() -> isize { return -2147483648; }\n"
        "fn signed_maximum() -> isize { return 2147483647; }\n"
        "fn unsigned_maximum() -> usize { return 4294967295; }\n"
        "fn main() -> i32 { return 0; }\n",
        &target32};
    ASSERT_TRUE(valid.ParseAndLower()) << valid.Diagnostics();
    ASSERT_TRUE(valid.Verify()) << valid.Diagnostics();

    FrontendHarness signed_overflow{
        "module test.isize_overflow_32;\n"
        "fn value() -> isize { return 2147483648; }\n"
        "fn main() -> i32 { return 0; }\n",
        &target32};
    EXPECT_FALSE(signed_overflow.ParseAndLower());
    EXPECT_NE(signed_overflow.Diagnostics().find(
                  "integer literal does not fit in isize"),
              std::string::npos);

    FrontendHarness unsigned_overflow{
        "module test.usize_overflow_32;\n"
        "fn value() -> usize { return 4294967296; }\n"
        "fn main() -> i32 { return 0; }\n",
        &target32};
    EXPECT_FALSE(unsigned_overflow.ParseAndLower());
    EXPECT_NE(unsigned_overflow.Diagnostics().find(
                  "integer literal does not fit in usize"),
              std::string::npos);
}

TEST(SemaTest, RejectsImplicitMixingWithStorageEquivalentIntegers) {
    FrontendHarness signed_harness{
        "module test.isize_i64_mix;\n"
        "fn combine(left: isize, right: i64) -> isize {\n"
        "    return left + right;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(signed_harness.ParseAndLower());
    EXPECT_NE(signed_harness.Diagnostics().find("expected isize, found i64"),
              std::string::npos);

    FrontendHarness unsigned_harness{
        "module test.usize_u64_mix;\n"
        "fn combine(left: usize, right: u64) -> usize {\n"
        "    return left + right;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(unsigned_harness.ParseAndLower());
    EXPECT_NE(unsigned_harness.Diagnostics().find("expected usize, found u64"),
              std::string::npos);
}

TEST(SemaTest, AcceptsMinimumNarrowSignedLiterals) {
    FrontendHarness harness{"module test.minimum_narrow;\n"
                            "fn minimum_i8() -> i8 { return -128; }\n"
                            "fn minimum_i16() -> i16 { return -32768; }\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrInstruction *minimum_i8 = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_INTEGER);
    LunaIrInstruction *minimum_i16 = FindInstruction(
        luna_ir_module_function(harness.Module(), 1U), LUNA_IR_CONST_INTEGER);
    ASSERT_NE(minimum_i8, nullptr);
    ASSERT_NE(minimum_i16, nullptr);
    EXPECT_EQ(minimum_i8->type, LUNA_IR_TYPE_I8);
    EXPECT_EQ(minimum_i8->immediate, std::uint64_t{1} << 7U);
    EXPECT_EQ(minimum_i16->type, LUNA_IR_TYPE_I16);
    EXPECT_EQ(minimum_i16->immediate, std::uint64_t{1} << 15U);
}

TEST(SemaTest, RejectsNarrowIntegerLiteralOverflow) {
    struct OverflowCase final {
        std::string_view type_name;
        std::string_view literal;
    };
    constexpr std::array<OverflowCase, 6U> LUNA_TEST_OVERFLOW_CASES = {{
        {"i8", "128"},
        {"i8", "-129"},
        {"i16", "32768"},
        {"i16", "-32769"},
        {"u8", "256"},
        {"u16", "65536"},
    }};

    for (const OverflowCase &test_case : LUNA_TEST_OVERFLOW_CASES) {
        std::string source{"module test.narrow_overflow;\nfn value() -> "};
        source.append(test_case.type_name);
        source.append(" { return ");
        source.append(test_case.literal);
        source.append("; }\nfn main() -> i32 { return 0; }\n");

        FrontendHarness harness{source};
        EXPECT_FALSE(harness.ParseAndLower())
            << test_case.type_name << ' ' << test_case.literal;
        EXPECT_NE(
            harness.Diagnostics().find("integer literal does not fit in " +
                                       std::string{test_case.type_name}),
            std::string::npos)
            << harness.Diagnostics();
    }
}

TEST(SemaTest, RejectsMixedI32AndI64Operands) {
    FrontendHarness harness{"module test.mixed_integer_types;\n"
                            "fn combine(wide: i64, narrow: i32) -> i64 {\n"
                            "    return wide + narrow;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("expected i64, found i32"),
              std::string::npos);
}

TEST(SemaTest, ContextuallyTypesUnsignedLiteralsAndOperators) {
    FrontendHarness harness{
        "module test.unsigned_context;\n"
        "fn calculate(left: u64, right: u64) -> u64 {\n"
        "    return ((left + 18446744073709551615) >> right) / 3;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    if (calculate(2, 1) == 0) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *calculate = luna_ir_module_function(harness.Module(), 0U);
    LunaIrInstruction *constant =
        FindInstruction(calculate, LUNA_IR_CONST_INTEGER);
    LunaIrInstruction *shift =
        FindInstruction(calculate, LUNA_IR_SHIFT_RIGHT_INTEGER);
    LunaIrInstruction *division =
        FindInstruction(calculate, LUNA_IR_DIV_INTEGER);
    ASSERT_NE(constant, nullptr);
    ASSERT_NE(shift, nullptr);
    ASSERT_NE(division, nullptr);
    EXPECT_EQ(constant->type, LUNA_IR_TYPE_U64);
    EXPECT_EQ(constant->immediate, UINT64_MAX);
    EXPECT_EQ(shift->type, LUNA_IR_TYPE_U64);
    EXPECT_EQ(division->type, LUNA_IR_TYPE_U64);
}

TEST(SemaTest, RejectsImplicitSignedUnsignedMixing) {
    FrontendHarness harness{"module test.signed_unsigned_mix;\n"
                            "fn combine(left: u64, right: i64) -> u64 {\n"
                            "    return left + right;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("expected u64, found i64"),
              std::string::npos);
}

TEST(SemaTest, LowersExplicitIntegerConversions) {
    FrontendHarness harness{"module test.conversions;\n"
                            "fn convert(value: i32) -> i32 {\n"
                            "    return (value as i64) as i32;\n"
                            "}\n"
                            "fn main() -> i32 { return convert(-1); }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *convert = luna_ir_module_function(harness.Module(), 0U);
    ASSERT_NE(convert, nullptr);
    std::uint64_t conversion_count = 0U;
    for (std::size_t block_index = 0U; block_index < convert->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block = static_cast<const LunaIrBlock *>(
            luna_vector_at_const(&convert->blocks, block_index));
        ASSERT_NE(block, nullptr);
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *instruction =
                static_cast<const LunaIrInstruction *>(luna_vector_at_const(
                    &block->instructions, instruction_index));
            ASSERT_NE(instruction, nullptr);
            if (instruction->opcode == LUNA_IR_CONVERT_INTEGER) {
                conversion_count += 1U;
            }
        }
    }
    EXPECT_EQ(conversion_count, 2U);
}

TEST(SemaTest, LowersWidthAndSignednessIntegerConversions) {
    FrontendHarness harness{
        "module test.unsigned_conversions;\n"
        "fn convert(signed_value: i32, unsigned_value: u32) -> u64 {\n"
        "    let reinterpreted: u32 = signed_value as u32;\n"
        "    let widened: u64 = unsigned_value as u64;\n"
        "    return (reinterpreted as i64) as u64;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *convert = luna_ir_module_function(harness.Module(), 0U);
    ASSERT_NE(convert, nullptr);
    std::uint64_t conversion_count = 0U;
    for (std::size_t block_index = 0U; block_index < convert->blocks.length;
         block_index += 1U) {
        const LunaIrBlock *block = static_cast<const LunaIrBlock *>(
            luna_vector_at_const(&convert->blocks, block_index));
        ASSERT_NE(block, nullptr);
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const LunaIrInstruction *instruction =
                static_cast<const LunaIrInstruction *>(luna_vector_at_const(
                    &block->instructions, instruction_index));
            ASSERT_NE(instruction, nullptr);
            if (instruction->opcode == LUNA_IR_CONVERT_INTEGER) {
                conversion_count += 1U;
            }
        }
    }
    EXPECT_EQ(conversion_count, 4U);
}

TEST(SemaTest, LowersEveryIntegerConversionPair) {
    constexpr std::array<std::string_view, 10U> LUNA_TEST_TYPE_NAMES = {
        "i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize",
    };
    constexpr std::array<LunaIrType, 10U> LUNA_TEST_IR_TYPES = {
        LUNA_IR_TYPE_I8,    LUNA_IR_TYPE_I16,   LUNA_IR_TYPE_I32,
        LUNA_IR_TYPE_I64,   LUNA_IR_TYPE_ISIZE, LUNA_IR_TYPE_U8,
        LUNA_IR_TYPE_U16,   LUNA_IR_TYPE_U32,   LUNA_IR_TYPE_U64,
        LUNA_IR_TYPE_USIZE,
    };

    std::string source{"module test.conversion_matrix;\n"};
    for (const std::string_view source_type : LUNA_TEST_TYPE_NAMES) {
        for (const std::string_view target_type : LUNA_TEST_TYPE_NAMES) {
            source.append("fn convert_");
            source.append(source_type);
            source.append("_to_");
            source.append(target_type);
            source.append("(value: ");
            source.append(source_type);
            source.append(") -> ");
            source.append(target_type);
            source.append(" { return value as ");
            source.append(target_type);
            source.append("; }\n");
        }
    }
    source.append("fn main() -> i32 { return 0; }\n");

    FrontendHarness harness{source};
    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_EQ(harness.Module()->functions.length, 101U);

    std::size_t function_index = 0U;
    for (std::size_t source_index = 0U;
         source_index < LUNA_TEST_IR_TYPES.size(); source_index += 1U) {
        for (std::size_t target_index = 0U;
             target_index < LUNA_TEST_IR_TYPES.size(); target_index += 1U) {
            LunaIrFunction *function = luna_ir_module_function(
                harness.Module(),
                static_cast<LunaIrFunctionId>(function_index));
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->return_type, LUNA_TEST_IR_TYPES[target_index]);
            const auto *parameter_type = static_cast<const LunaIrType *>(
                luna_vector_at_const(&function->parameter_types, 0U));
            ASSERT_NE(parameter_type, nullptr);
            EXPECT_EQ(*parameter_type, LUNA_TEST_IR_TYPES[source_index]);

            LunaIrInstruction *conversion =
                FindInstruction(function, LUNA_IR_CONVERT_INTEGER);
            if (source_index == target_index) {
                EXPECT_EQ(conversion, nullptr);
            } else {
                ASSERT_NE(conversion, nullptr);
                EXPECT_EQ(conversion->type, LUNA_TEST_IR_TYPES[target_index]);
                const LunaIrType *operand_type =
                    static_cast<const LunaIrType *>(luna_vector_at_const(
                        &function->value_types, conversion->left));
                ASSERT_NE(operand_type, nullptr);
                EXPECT_EQ(*operand_type, LUNA_TEST_IR_TYPES[source_index]);
            }
            function_index += 1U;
        }
    }
}

TEST(SemaTest, LowersEveryRemainingNumericConversionPair) {
    constexpr std::array<std::string_view, 12U>
        LUNA_TEST_SCALAR_CONVERSION_TYPE_NAMES = {
            "i8",  "i16", "i32", "i64",   "isize", "u8",
            "u16", "u32", "u64", "usize", "f32",   "f64",
        };
    constexpr std::array<LunaIrType, 12U> LUNA_TEST_SCALAR_CONVERSION_IR_TYPES =
        {
            LUNA_IR_TYPE_I8,    LUNA_IR_TYPE_I16,   LUNA_IR_TYPE_I32,
            LUNA_IR_TYPE_I64,   LUNA_IR_TYPE_ISIZE, LUNA_IR_TYPE_U8,
            LUNA_IR_TYPE_U16,   LUNA_IR_TYPE_U32,   LUNA_IR_TYPE_U64,
            LUNA_IR_TYPE_USIZE, LUNA_IR_TYPE_F32,   LUNA_IR_TYPE_F64,
        };
    constexpr std::size_t LUNA_TEST_SCALAR_CONVERSION_INTEGER_TYPE_COUNT = 10U;
    constexpr std::size_t LUNA_TEST_SCALAR_CONVERSION_FUNCTION_COUNT = 44U;

    std::string source{"module test.scalar_conversion_matrix;\n"};
    for (std::size_t source_index = 0U;
         source_index < LUNA_TEST_SCALAR_CONVERSION_TYPE_NAMES.size();
         source_index += 1U) {
        for (std::size_t target_index = 0U;
             target_index < LUNA_TEST_SCALAR_CONVERSION_TYPE_NAMES.size();
             target_index += 1U) {
            if (source_index < LUNA_TEST_SCALAR_CONVERSION_INTEGER_TYPE_COUNT &&
                target_index < LUNA_TEST_SCALAR_CONVERSION_INTEGER_TYPE_COUNT) {
                continue;
            }
            const std::string_view source_type =
                LUNA_TEST_SCALAR_CONVERSION_TYPE_NAMES[source_index];
            const std::string_view target_type =
                LUNA_TEST_SCALAR_CONVERSION_TYPE_NAMES[target_index];
            source.append("fn convert_");
            source.append(source_type);
            source.append("_to_");
            source.append(target_type);
            source.append("(value: ");
            source.append(source_type);
            source.append(") -> ");
            source.append(target_type);
            source.append(" { return value as ");
            source.append(target_type);
            source.append("; }\n");
        }
    }
    source.append("fn main() -> i32 { return 0; }\n");

    FrontendHarness harness{source};
    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_EQ(harness.Module()->functions.length,
              LUNA_TEST_SCALAR_CONVERSION_FUNCTION_COUNT + 1U);

    std::size_t function_index = 0U;
    for (std::size_t source_index = 0U;
         source_index < LUNA_TEST_SCALAR_CONVERSION_IR_TYPES.size();
         source_index += 1U) {
        for (std::size_t target_index = 0U;
             target_index < LUNA_TEST_SCALAR_CONVERSION_IR_TYPES.size();
             target_index += 1U) {
            const bool source_is_integer =
                source_index < LUNA_TEST_SCALAR_CONVERSION_INTEGER_TYPE_COUNT;
            const bool target_is_integer =
                target_index < LUNA_TEST_SCALAR_CONVERSION_INTEGER_TYPE_COUNT;
            if (source_is_integer && target_is_integer) {
                continue;
            }

            LunaIrFunction *function = luna_ir_module_function(
                harness.Module(),
                static_cast<LunaIrFunctionId>(function_index));
            ASSERT_NE(function, nullptr);
            const LunaIrType source_type =
                LUNA_TEST_SCALAR_CONVERSION_IR_TYPES[source_index];
            const LunaIrType target_type =
                LUNA_TEST_SCALAR_CONVERSION_IR_TYPES[target_index];
            EXPECT_EQ(function->return_type, target_type);

            LunaIrOpcode expected_opcode = LUNA_IR_CONVERT_FLOAT;
            if (source_is_integer) {
                expected_opcode = LUNA_IR_CONVERT_INTEGER_TO_FLOAT;
            } else if (target_is_integer) {
                expected_opcode = LUNA_IR_CONVERT_FLOAT_TO_INTEGER;
            }
            LunaIrInstruction *conversion =
                FindInstruction(function, expected_opcode);
            if (source_type == target_type) {
                EXPECT_EQ(conversion, nullptr);
            } else {
                ASSERT_NE(conversion, nullptr);
                EXPECT_EQ(conversion->type, target_type);
                const LunaIrType *operand_type =
                    static_cast<const LunaIrType *>(luna_vector_at_const(
                        &function->value_types, conversion->left));
                ASSERT_NE(operand_type, nullptr);
                EXPECT_EQ(*operand_type, source_type);
            }
            function_index += 1U;
        }
    }
    EXPECT_EQ(function_index, LUNA_TEST_SCALAR_CONVERSION_FUNCTION_COUNT);
}

TEST(SemaTest, PreservesLiteralCategoryAcrossExplicitConversions) {
    FrontendHarness harness{"module test.scalar_literal_conversions;\n"
                            "fn negative() -> i32 { return (-42.75) as i32; }\n"
                            "fn positive() -> f64 { return 42 as f64; }\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *negative = luna_ir_module_function(harness.Module(), 0U);
    LunaIrFunction *positive = luna_ir_module_function(harness.Module(), 1U);
    ASSERT_NE(negative, nullptr);
    ASSERT_NE(positive, nullptr);
    LunaIrInstruction *float_to_integer =
        FindInstruction(negative, LUNA_IR_CONVERT_FLOAT_TO_INTEGER);
    LunaIrInstruction *integer_to_float =
        FindInstruction(positive, LUNA_IR_CONVERT_INTEGER_TO_FLOAT);
    ASSERT_NE(float_to_integer, nullptr);
    ASSERT_NE(integer_to_float, nullptr);
    EXPECT_EQ(float_to_integer->type, LUNA_IR_TYPE_I32);
    EXPECT_EQ(integer_to_float->type, LUNA_IR_TYPE_F64);
}

TEST(SemaTest, RejectsImplicitNarrowIntegerMixing) {
    FrontendHarness harness{"module test.narrow_mixing;\n"
                            "fn combine(left: i8, right: u8) -> i8 {\n"
                            "    return left + right;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("expected i8, found u8"),
              std::string::npos);
}

TEST(SemaTest, RejectsNonNumericExplicitConversions) {
    FrontendHarness harness{"module test.invalid_conversion;\n"
                            "fn main() -> i32 { return true as i32; }\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find(
                  "explicit conversion requires numeric source and target"),
              std::string::npos);
}

TEST(SemaTest, ContextuallyTypesFloatingLiteralsAndOperators) {
    FrontendHarness harness{"module test.float_context;\n"
                            "fn single(value: f32) -> f32 {\n"
                            "    return -(value + 1.5) * 2.0 / 4.0;\n"
                            "}\n"
                            "fn ordered(left: f64, right: f64) -> bool {\n"
                            "    return left >= right;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    LunaIrFunction *single = luna_ir_module_function(harness.Module(), 0U);
    LunaIrFunction *ordered = luna_ir_module_function(harness.Module(), 1U);
    ASSERT_NE(single, nullptr);
    ASSERT_NE(ordered, nullptr);
    EXPECT_EQ(single->return_type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(ordered->return_type, LUNA_IR_TYPE_BOOL);

    LunaIrInstruction *constant = FindInstruction(single, LUNA_IR_CONST_FLOAT);
    LunaIrInstruction *addition = FindInstruction(single, LUNA_IR_ADD_FLOAT);
    LunaIrInstruction *negation = FindInstruction(single, LUNA_IR_NEG_FLOAT);
    LunaIrInstruction *multiplication =
        FindInstruction(single, LUNA_IR_MUL_FLOAT);
    LunaIrInstruction *division = FindInstruction(single, LUNA_IR_DIV_FLOAT);
    LunaIrInstruction *comparison =
        FindInstruction(ordered, LUNA_IR_COMPARE_GREATER_EQUAL_FLOAT);
    ASSERT_NE(constant, nullptr);
    ASSERT_NE(addition, nullptr);
    ASSERT_NE(negation, nullptr);
    ASSERT_NE(multiplication, nullptr);
    ASSERT_NE(division, nullptr);
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(constant->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(constant->immediate, UINT64_C(0x3fc00000));
    EXPECT_EQ(addition->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(negation->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(multiplication->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(division->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(comparison->type, LUNA_IR_TYPE_BOOL);
}

TEST(SemaTest, RoundsFloatingLiteralsDirectlyToTheirContextType) {
    FrontendHarness harness{
        "module test.float_rounding;\n"
        "fn single() -> f32 {\n"
        "    return 1.0000000596046447753906251;\n"
        "}\n"
        "fn double() -> f64 { return 9007199254740993.0; }\n"
        "fn default_width() -> bool { return 1.0 == 1.0; }\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrInstruction *single = FindInstruction(
        luna_ir_module_function(harness.Module(), 0U), LUNA_IR_CONST_FLOAT);
    LunaIrInstruction *double_value = FindInstruction(
        luna_ir_module_function(harness.Module(), 1U), LUNA_IR_CONST_FLOAT);
    LunaIrInstruction *default_value = FindInstruction(
        luna_ir_module_function(harness.Module(), 2U), LUNA_IR_CONST_FLOAT);
    ASSERT_NE(single, nullptr);
    ASSERT_NE(double_value, nullptr);
    ASSERT_NE(default_value, nullptr);
    EXPECT_EQ(single->type, LUNA_IR_TYPE_F32);
    EXPECT_EQ(single->immediate, UINT64_C(0x3f800001));
    EXPECT_EQ(double_value->type, LUNA_IR_TYPE_F64);
    EXPECT_EQ(double_value->immediate, UINT64_C(0x4340000000000000));
    EXPECT_EQ(default_value->type, LUNA_IR_TYPE_F64);
}

TEST(SemaTest, RejectsImplicitFloatingPointTypeMixing) {
    FrontendHarness widths{
        "module test.float_width_mix;\n"
        "fn combine(left: f32, right: f64) -> f32 { return left + right; }\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(widths.ParseAndLower());
    EXPECT_NE(widths.Diagnostics().find("expected f32, found f64"),
              std::string::npos);

    FrontendHarness categories{
        "module test.float_integer_mix;\n"
        "fn combine(left: f64, right: i32) -> f64 { return left + right; }\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(categories.ParseAndLower());
    EXPECT_NE(categories.Diagnostics().find("expected f64, found i32"),
              std::string::npos);
}

TEST(SemaTest, EnforcesIndependentScalarArgumentRegisterLimits) {
    FrontendHarness valid{
        "module test.mixed_registers;\n"
        "fn mixed(a: i32, b: f32, c: i64, d: f64, e: u32, f: f32,\n"
        "         g: u64, h: f64, i: isize, j: f32, k: usize, l: f64,\n"
        "         m: f32, n: f64) -> bool {\n"
        "    return true;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_TRUE(valid.Verify()) << valid.Diagnostics();

    FrontendHarness invalid{
        "module test.too_many_float_registers;\n"
        "fn too_many(a: f64, b: f64, c: f64, d: f64, e: f64,\n"
        "            f: f64, g: f64, h: f64, i: f64) -> f64 {\n"
        "    return a;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(invalid.ParseAndLower());
    EXPECT_NE(invalid.Diagnostics().find("eight floating-point arguments"),
              std::string::npos);

    FrontendHarness too_many_integers{
        "module test.too_many_integer_registers;\n"
        "fn too_many(a: i8, b: i16, c: i32, d: i64, e: u32, f: usize,\n"
        "            g: isize) -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(too_many_integers.ParseAndLower());
    EXPECT_NE(too_many_integers.Diagnostics().find(
                  "six integer-class and eight floating-point arguments"),
              std::string::npos);
}

TEST(SemaTest, LowersConditionalAndStructuredControlFlowToValidIr) {
    FrontendHarness harness{
        "module test.structured_lowering;\n"
        "fn select(condition: bool, left: i64, right: i64) -> i64 {\n"
        "    return condition ? left : right;\n"
        "}\n"
        "fn classify(value: u8) -> i32 {\n"
        "    switch (value) {\n"
        "        case -1 { return 1; }\n"
        "        case 0, 1 { return 2; }\n"
        "        default { return 3; }\n"
        "    }\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    var total: i32 = 0;\n"
        "    do { total += 1; } while (total < 2);\n"
        "    for (var index: i32 = 0; index < 4; index += 1) {\n"
        "        switch (index) {\n"
        "            case 1 { continue; }\n"
        "            case 2 { break; }\n"
        "            default { total += index; }\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *select = luna_ir_module_function(harness.Module(), 0U);
    LunaIrFunction *main_function =
        luna_ir_module_function(harness.Module(), 2U);
    ASSERT_NE(select, nullptr);
    ASSERT_NE(main_function, nullptr);
    EXPECT_GT(select->blocks.length, 1U);
    EXPECT_GT(main_function->blocks.length, 8U);
    EXPECT_NE(FindInstruction(select, LUNA_IR_BRANCH), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_COMPARE_EQUAL), nullptr);
}

TEST(SemaTest, KeepsDetachedLoopClausesWellTypedAndValid) {
    FrontendHarness harness{
        "module test.detached_loop_clauses;\n"
        "fn do_value() -> i32 {\n"
        "    do { return 7; } while (false);\n"
        "}\n"
        "fn for_value() -> i32 {\n"
        "    for (var index: i32 = 0; index < 1; index += 1) {\n"
        "        return 9;\n"
        "    }\n"
        "    return 11;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};

    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(SemaTest, RejectsInvalidConditionalOperands) {
    FrontendHarness condition{"module test.bad_conditional_condition;\n"
                              "fn main() -> i32 { return 1 ? 2 : 3; }\n"};
    EXPECT_FALSE(condition.ParseAndLower());
    EXPECT_NE(condition.Diagnostics().find("expected bool, found i32"),
              std::string::npos);

    FrontendHarness operands{"module test.bad_conditional_operands;\n"
                             "fn main() -> i32 {\n"
                             "    let choose: bool = true;\n"
                             "    let value: i32 = choose ? 1 : false;\n"
                             "    return value;\n"
                             "}\n"};
    EXPECT_FALSE(operands.ParseAndLower());
    EXPECT_NE(operands.Diagnostics().find("expected i32, found bool"),
              std::string::npos);
}

TEST(SemaTest, RejectsInvalidSwitchForms) {
    FrontendHarness non_integer{"module test.non_integer_switch;\n"
                                "fn main() -> i32 {\n"
                                "    switch (1.0) { default { return 0; } }\n"
                                "}\n"};
    EXPECT_FALSE(non_integer.ParseAndLower());
    EXPECT_NE(non_integer.Diagnostics().find(
                  "switch expression requires an integer type"),
              std::string::npos);

    FrontendHarness duplicate{"module test.duplicate_switch_case;\n"
                              "fn main() -> i32 {\n"
                              "    let value: u8 = 0;\n"
                              "    switch (value) {\n"
                              "        case -1 { return 1; }\n"
                              "        case 255 { return 2; }\n"
                              "        default { return 3; }\n"
                              "    }\n"
                              "}\n"};
    EXPECT_FALSE(duplicate.ParseAndLower());
    EXPECT_NE(duplicate.Diagnostics().find("duplicate switch case value"),
              std::string::npos);

    FrontendHarness duplicate_default{"module test.duplicate_switch_default;\n"
                                      "fn main() -> i32 {\n"
                                      "    switch (0) {\n"
                                      "        default { return 1; }\n"
                                      "        default { return 2; }\n"
                                      "    }\n"
                                      "}\n"};
    EXPECT_FALSE(duplicate_default.ParseAndLower());
    EXPECT_NE(duplicate_default.Diagnostics().find(
                  "switch has more than one default arm"),
              std::string::npos);

    FrontendHarness overflow{"module test.switch_label_overflow;\n"
                             "fn main() -> i32 {\n"
                             "    let value: i8 = 0;\n"
                             "    switch (value) {\n"
                             "        case 128 { return 1; }\n"
                             "        default { return 0; }\n"
                             "    }\n"
                             "}\n"};
    EXPECT_FALSE(overflow.ParseAndLower());
    EXPECT_NE(
        overflow.Diagnostics().find("switch case label does not fit in i8"),
        std::string::npos);
}

TEST(SemaTest, LowersPointersArraysAndStringsToTypedMemoryIr) {
    FrontendHarness harness{"module test.memory_ir;\n"
                            "fn update(pointer: *i32) -> i32 {\n"
                            "    *pointer += 1;\n"
                            "    return *pointer;\n"
                            "}\n"
                            "fn main() -> i32 {\n"
                            "    var values: [3]i32 = {};\n"
                            "    values[1] = 41;\n"
                            "    let pointer: *i32 = &values[1];\n"
                            "    let bits: usize = pointer as usize;\n"
                            "    let round_trip: *i32 = bits as *i32;\n"
                            "    let text: *const u8 = \"A\";\n"
                            "    if (round_trip != null && text[0] == 65) {\n"
                            "        return update(round_trip);\n"
                            "    }\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrModule *module = harness.Module();
    ASSERT_EQ(module->globals.length, 1U);
    const LunaIrGlobal *global = luna_ir_module_global(module, 0U);
    ASSERT_NE(global, nullptr);
    EXPECT_TRUE(global->is_read_only);
    ASSERT_EQ(global->bytes.length, 2U);
    EXPECT_EQ(*static_cast<const std::uint8_t *>(
                  luna_vector_at_const(&global->bytes, 0U)),
              static_cast<std::uint8_t>('A'));
    EXPECT_EQ(*static_cast<const std::uint8_t *>(
                  luna_vector_at_const(&global->bytes, 1U)),
              0U);

    LunaIrFunction *main_function =
        luna_ir_module_function(module, module->entry_function);
    ASSERT_NE(main_function, nullptr);
    ASSERT_FALSE(main_function->slots.length == 0U);
    const LunaIrSlot *array_slot = static_cast<const LunaIrSlot *>(
        luna_vector_at_const(&main_function->slots, 0U));
    ASSERT_NE(array_slot, nullptr);
    EXPECT_FALSE(array_slot->is_scalar);
    EXPECT_EQ(array_slot->size_bytes, 12U);
    EXPECT_EQ(array_slot->alignment_bytes, 4U);

    EXPECT_NE(FindInstruction(main_function, LUNA_IR_ZERO_SLOT), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_ADDRESS_OF_SLOT), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_BOUNDS_CHECK), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_POINTER_OFFSET), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_STORE_INDIRECT), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_LOAD_INDIRECT), nullptr);
    EXPECT_NE(
        FindInstruction(main_function, LUNA_IR_CONVERT_POINTER_TO_INTEGER),
        nullptr);
    EXPECT_NE(
        FindInstruction(main_function, LUNA_IR_CONVERT_INTEGER_TO_POINTER),
        nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_GLOBAL_ADDRESS), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_CONST_NULL), nullptr);
}

TEST(SemaTest, LowersAggregatesAndScopedEnumsWithExactTargetLayouts) {
    FrontendHarness harness{
        "module test.aggregate_ir;\n"
        "enum Kind: u8 { empty, ready = 7, complete, }\n"
        "struct Inner { byte: u8; value: u32; }\n"
        "union Bits { wide: u64; inner: Inner; }\n"
        "struct Outer { kind: Kind; bits: Bits; tail: u16; next: *Outer; }\n"
        "fn pass(kind: Kind) -> Kind { return kind; }\n"
        "fn main() -> i32 {\n"
        "    var outer: Outer = {};\n"
        "    outer.kind = pass(Kind.ready);\n"
        "    outer.bits.inner.value = 42;\n"
        "    outer.tail = 9;\n"
        "    let pointer: *Outer = &outer;\n"
        "    pointer->next = pointer;\n"
        "    let raw: u8 = pointer->kind as u8;\n"
        "    let restored: Kind = raw as Kind;\n"
        "    if (sizeof(Outer) != 32 || alignof(Outer) != 8 ||\n"
        "        offsetof(Outer, kind) != 0 ||\n"
        "        offsetof(Outer, bits) != 8 ||\n"
        "        offsetof(Outer, tail) != 16 ||\n"
        "        offsetof(Outer, next) != 24 ||\n"
        "        sizeof(Kind) != 1 || offsetof(Bits, inner) != 0) {\n"
        "        return 2;\n"
        "    }\n"
        "    switch (restored) {\n"
        "        case Kind.ready { return pointer->bits.inner.value as i32; }\n"
        "        default { return 1; }\n"
        "    }\n"
        "}\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *main_function = luna_ir_module_function(
        harness.Module(), harness.Module()->entry_function);
    ASSERT_NE(main_function, nullptr);
    ASSERT_GT(main_function->slots.length, 0U);
    const auto *outer_slot = static_cast<const LunaIrSlot *>(
        luna_vector_at_const(&main_function->slots, 0U));
    ASSERT_NE(outer_slot, nullptr);
    EXPECT_FALSE(outer_slot->is_scalar);
    EXPECT_EQ(outer_slot->size_bytes, 32U);
    EXPECT_EQ(outer_slot->alignment_bytes, 8U);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_ZERO_SLOT), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_MEMBER_ADDRESS), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_NULL_CHECK), nullptr);

    bool saw_inner_value_offset = false;
    bool saw_tail_offset = false;
    bool saw_next_offset = false;
    for (std::size_t block_index = 0U;
         block_index < main_function->blocks.length; block_index += 1U) {
        const auto *block = static_cast<const LunaIrBlock *>(
            luna_vector_at_const(&main_function->blocks, block_index));
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const auto *instruction = static_cast<const LunaIrInstruction *>(
                luna_vector_at_const(&block->instructions, instruction_index));
            if (instruction->opcode != LUNA_IR_MEMBER_ADDRESS) {
                continue;
            }
            saw_inner_value_offset =
                saw_inner_value_offset || instruction->immediate == 4U;
            saw_tail_offset = saw_tail_offset || instruction->immediate == 16U;
            saw_next_offset = saw_next_offset || instruction->immediate == 24U;
        }
    }
    EXPECT_TRUE(saw_inner_value_offset);
    EXPECT_TRUE(saw_tail_offset);
    EXPECT_TRUE(saw_next_offset);
}

TEST(SemaTest, LowersNamedAggregateInitializationAndExactMemoryCopies) {
    FrontendHarness harness{
        "module test.aggregate_initialization;\n"
        "struct Inner { tag: u8; value: i32; }\n"
        "union Choice { integer: i64; inner: Inner; }\n"
        "struct Outer {\n"
        "    head: u8;\n"
        "    inner: Inner;\n"
        "    choice: Choice;\n"
        "    values: [2]i32;\n"
        "    tail: u16;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    var seed: Inner = { value = 40, };\n"
        "    var first: Outer = {\n"
        "        tail = 7,\n"
        "        inner = seed,\n"
        "        choice = { integer = 99, },\n"
        "        head = 1,\n"
        "    };\n"
        "    var second: Outer = first;\n"
        "    let pointer: *Outer = &second;\n"
        "    pointer->inner = {\n"
        "        value = true ? 42 : 0,\n"
        "        tag = {},\n"
        "    };\n"
        "    var left: [2]i32 = {};\n"
        "    left[0] = 20;\n"
        "    left[1] = 22;\n"
        "    var right: [2]i32 = left;\n"
        "    right = left;\n"
        "    if (first.inner.tag != 0 || first.choice.integer != 99 ||\n"
        "        first.values[0] != 0 || second.inner.tag != 0 ||\n"
        "        right[0] + right[1] != 42) {\n"
        "        return 1;\n"
        "    }\n"
        "    return pointer->inner.value;\n"
        "}\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    LunaIrFunction *main_function = luna_ir_module_function(
        harness.Module(), harness.Module()->entry_function);
    ASSERT_NE(main_function, nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_ZERO_SLOT), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_MEMBER_ADDRESS), nullptr);
    EXPECT_NE(FindInstruction(main_function, LUNA_IR_MEMORY_COPY), nullptr);

    std::size_t copy_count = 0U;
    for (std::size_t block_index = 0U;
         block_index < main_function->blocks.length; block_index += 1U) {
        const auto *block = static_cast<const LunaIrBlock *>(
            luna_vector_at_const(&main_function->blocks, block_index));
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            const auto *instruction = static_cast<const LunaIrInstruction *>(
                luna_vector_at_const(&block->instructions, instruction_index));
            if (instruction->opcode == LUNA_IR_MEMORY_COPY) {
                copy_count += 1U;
                EXPECT_GT(instruction->immediate, 0U);
            }
        }
    }
    EXPECT_GE(copy_count, 5U);
}

TEST(SemaTest, AcceptsSignedEnumBoundariesAndImplicitSuccessors) {
    FrontendHarness harness{
        "module test.signed_enum;\n"
        "enum Signed: i8 { minimum = -128, next, maximum = 127, }\n"
        "fn identity(value: Signed) -> Signed { return value; }\n"
        "fn main() -> i32 {\n"
        "    let value: Signed = identity(Signed.next);\n"
        "    if ((value as i8) != -127) { return 1; }\n"
        "    return value == Signed.next ? 42 : 2;\n"
        "}\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(SemaTest, SupportsForwardAggregateGraphsAndEnumLayoutExpressions) {
    FrontendHarness harness{
        "module test.forward_aggregate_graph;\n"
        "struct First { second: Second; }\n"
        "struct Second { value: i32; owner: *First; }\n"
        "enum ByteCount: usize { zero, }\n"
        "fn main() -> i32 {\n"
        "    var first: First = {};\n"
        "    first.second.value = 42;\n"
        "    first.second.owner = &first;\n"
        "    var values: [2]First = {};\n"
        "    values[1].second.value = first.second.value;\n"
        "    let count: ByteCount = sizeof([2][3][4]First) as ByteCount;\n"
        "    let empty: ByteCount = true ? {} : {};\n"
        "    if ((count as usize) != 384 || empty != ByteCount.zero) {\n"
        "        return 1;\n"
        "    }\n"
        "    return values[1].second.value;\n"
        "}\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(SemaTest, RejectsInvalidAggregateAndScopedEnumPrograms) {
    struct InvalidProgram {
        std::string_view source;
        std::string_view diagnostic;
    };
    constexpr std::array<InvalidProgram, 33U> LUNA_TEST_INVALID_PROGRAMS = {{
        {"module test.duplicate_type;\n"
         "struct Item { value: i32; }\n"
         "union Item { value: i32; }\n"
         "fn main() -> i32 { return 0; }\n",
         "duplicate type declaration 'Item'"},
        {"module test.duplicate_field;\n"
         "struct Item { value: i32; value: u32; }\n"
         "fn main() -> i32 { return 0; }\n",
         "duplicate field 'value'"},
        {"module test.unknown_type;\n"
         "fn main() -> i32 { var value: Missing = {}; return 0; }\n",
         "unknown type 'Missing'"},
        {"module test.void_field;\n"
         "struct Item { value: void; }\n"
         "fn main() -> i32 { return 0; }\n",
         "aggregate field cannot have type void"},
        {"module test.empty_struct;\n"
         "struct Empty {}\n"
         "fn main() -> i32 { return 0; }\n",
         "must declare a field"},
        {"module test.recursive_struct;\n"
         "struct Node { next: Node; }\n"
         "fn main() -> i32 { return 0; }\n",
         "contains itself by value"},
        {"module test.mutual_recursion;\n"
         "struct Left { right: Right; }\n"
         "struct Right { left: Left; }\n"
         "fn main() -> i32 { return 0; }\n",
         "contains itself by value"},
        {"module test.enum_underlying;\n"
         "enum Kind: f32 { value, }\n"
         "fn main() -> i32 { return 0; }\n",
         "enum underlying type must be a built-in integer type"},
        {"module test.enum_overflow;\n"
         "enum Kind: u8 { value = 256, }\n"
         "fn main() -> i32 { return 0; }\n",
         "enum member value does not fit in u8"},
        {"module test.enum_nonliteral;\n"
         "enum Kind: u8 { value = 1 + 2, }\n"
         "fn main() -> i32 { return 0; }\n",
         "enum member value must be an integer literal"},
        {"module test.enum_implicit_overflow;\n"
         "enum Kind: u8 { maximum = 255, overflow, }\n"
         "fn main() -> i32 { return 0; }\n",
         "implicit enum member value overflows u8"},
        {"module test.duplicate_enum_member;\n"
         "enum Kind: u8 { value, value, }\n"
         "fn main() -> i32 { return 0; }\n",
         "duplicate enum member 'value'"},
        {"module test.unknown_enum_member;\n"
         "enum Kind: u8 { value, }\n"
         "fn main() -> i32 { return Kind.missing as i32; }\n",
         "enum 'Kind' has no member named 'missing'"},
        {"module test.type_as_value;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 { return Item.value; }\n",
         "type 'Item' is not a value"},
        {"module test.empty_enum;\n"
         "enum Kind: u8 {}\n"
         "fn main() -> i32 { return 0; }\n",
         "must declare a member"},
        {"module test.unknown_field;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var item: Item = {}; return item.missing;\n"
         "}\n",
         "has no field named 'missing'"},
        {"module test.dot_scalar;\n"
         "fn main() -> i32 { var value: i32 = 0; return value.field; }\n",
         "'.' member access requires a struct or union lvalue"},
        {"module test.arrow_scalar;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 { var item: Item = {}; return item->value; }\n",
         "'->' member access requires a pointer"},
        {"module test.immutable_field;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 { let item: Item = {}; item.value = 1; return 0; "
         "}\n",
         "cannot assign through an immutable lvalue"},
        {"module test.readonly_field;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var item: Item = {};\n"
         " let pointer: *const Item = (&item) as *const Item;\n"
         " pointer->value = 1; return 0;\n"
         "}\n",
         "cannot assign through an immutable lvalue"},
        {"module test.enum_integer;\n"
         "enum Kind: u8 { value, }\n"
         "fn main() -> i32 { let kind: Kind = 0; return 0; }\n",
         "expected Kind, found i32"},
        {"module test.enum_mixing;\n"
         "enum Left: u8 { value, }\n"
         "enum Right: u8 { value, }\n"
         "fn main() -> i32 {\n"
         " return Left.value == Right.value ? 1 : 0;\n"
         "}\n",
         "expected Left, found Right"},
        {"module test.enum_switch_label;\n"
         "enum Kind: u8 { value, }\n"
         "fn main() -> i32 {\n"
         " let kind: Kind = Kind.value;\n"
         " switch (kind) { case 0 { return 1; } default { return 0; } }\n"
         "}\n",
         "enum switch case label must be a member"},
        {"module test.aggregate_parameter;\n"
         "struct Item { value: i32; }\n"
         "fn take(item: Item) -> i32 { return 0; }\n"
         "fn main() -> i32 { return 0; }\n",
         "aggregate types cannot be passed by value"},
        {"module test.aggregate_return;\n"
         "struct Item { value: i32; }\n"
         "fn make() -> Item { var item: Item = {}; return item; }\n"
         "fn main() -> i32 { return 0; }\n",
         "aggregate types cannot be returned by value"},
        {"module test.aggregate_initializer;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 { var item: Item = 0; return 0; }\n",
         "aggregate initialization requires braces"},
        {"module test.aggregate_assignment;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var left: Item = {}; var right: Item = {}; left += right; return "
         "0;\n"
         "}\n",
         "compound assignment requires a scalar numeric type"},
        {"module test.sizeof_void;\n"
         "fn main() -> i32 { let size: usize = sizeof(void); return 0; }\n",
         "layout query requires a type with a valid target layout"},
        {"module test.offsetof_scalar;\n"
         "fn main() -> i32 { let offset: usize = offsetof(i32, value); "
         "return 0; }\n",
         "offsetof requires a struct or union type"},
        {"module test.offsetof_unknown;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " let offset: usize = offsetof(Item, missing); return 0;\n"
         "}\n",
         "offsetof names an unknown field 'missing'"},
        {"module test.enum_arithmetic;\n"
         "enum Kind: u8 { first, second, }\n"
         "fn main() -> i32 {\n"
         " let value: Kind = Kind.first + Kind.second; return 0;\n"
         "}\n",
         "arithmetic and bitwise operators require numeric operands"},
        {"module test.enum_conversion;\n"
         "enum Kind: u8 { value, }\n"
         "fn main() -> i32 { let kind: Kind = (0 as i16) as Kind; return 0; "
         "}\n",
         "enum conversion requires the enum's exact underlying integer type"},
        {"module test.enum_duplicate_switch_value;\n"
         "enum Kind: u8 { first = 7, second = 7, }\n"
         "fn main() -> i32 {\n"
         " let kind: Kind = Kind.first;\n"
         " switch (kind) {\n"
         "  case Kind.first { return 1; }\n"
         "  case Kind.second { return 2; }\n"
         " }\n"
         "}\n",
         "duplicate switch case value"},
    }};

    for (const InvalidProgram &program : LUNA_TEST_INVALID_PROGRAMS) {
        FrontendHarness harness{program.source};
        EXPECT_FALSE(harness.ParseAndLower()) << program.source;
        EXPECT_NE(harness.Diagnostics().find(program.diagnostic),
                  std::string::npos)
            << program.source << '\n'
            << harness.Diagnostics();
    }
}

TEST(SemaTest, RejectsInvalidNamedInitializersAndMemoryCopies) {
    struct InvalidProgram {
        std::string_view source;
        std::string_view diagnostic;
    };
    constexpr std::array<InvalidProgram, 10U> LUNA_TEST_INVALID_PROGRAMS = {{
        {"module test.duplicate_initializer;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var item: Item = { value = missing(), value = 1, }; return 0;\n"
         "}\n",
         "duplicate initializer field 'value'"},
        {"module test.unknown_initializer_field;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var item: Item = { missing = missing(), }; return 0;\n"
         "}\n",
         "type 'Item' has no field named 'missing'"},
        {"module test.union_initializer;\n"
         "union Number { integer: i64; real: f64; }\n"
         "fn main() -> i32 {\n"
         " var number: Number = { integer = 1, real = 2.0, }; return 0;\n"
         "}\n",
         "union initializer may name at most one field"},
        {"module test.scalar_initializer;\n"
         "fn main() -> i32 {\n"
         " let value: i32 = { field = 1, }; return value;\n"
         "}\n",
         "named aggregate initializer requires an aggregate destination"},
        {"module test.array_initializer;\n"
         "fn main() -> i32 {\n"
         " var values: [2]i32 = { first = 1, }; return 0;\n"
         "}\n",
         "named aggregate initializer requires a struct or union context"},
        {"module test.field_type;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var item: Item = { value = true, }; return 0;\n"
         "}\n",
         "expected i32, found bool"},
        {"module test.copy_type;\n"
         "struct Left { value: i32; }\n"
         "struct Right { value: i32; }\n"
         "fn main() -> i32 {\n"
         " var left: Left = {}; var right: Right = left; return 0;\n"
         "}\n",
         "expected Right, found Left"},
        {"module test.immutable_copy;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 {\n"
         " let left: Item = {}; let right: Item = {}; left = right; return 0;\n"
         "}\n",
         "cannot assign to immutable local 'left'"},
        {"module test.nested_union_initializer;\n"
         "union Number { integer: i64; real: f64; }\n"
         "struct Box { number: Number; }\n"
         "fn main() -> i32 {\n"
         " var box: Box = { number = { integer = 1, real = 2.0, }, };\n"
         " return 0;\n"
         "}\n",
         "union initializer may name at most one field"},
        {"module test.self_copy_initializer;\n"
         "struct Item { value: i32; }\n"
         "fn main() -> i32 { var item: Item = item; return 0; }\n",
         "unknown local variable 'item'"},
    }};

    for (const InvalidProgram &program : LUNA_TEST_INVALID_PROGRAMS) {
        FrontendHarness harness{program.source};
        EXPECT_FALSE(harness.ParseAndLower()) << program.source;
        EXPECT_NE(harness.Diagnostics().find(program.diagnostic),
                  std::string::npos)
            << program.source << '\n'
            << harness.Diagnostics();
    }
}

TEST(SemaTest, PreservesSequencedMemoryValuesAcrossControlFlowExpressions) {
    FrontendHarness harness{
        "module test.memory_control_flow;\n"
        "fn combine(first: i32, second: i32) -> i32 {\n"
        "    return first + second;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    var values: [2]i32 = {};\n"
        "    values[true ? 0 : 1] = 10 + (false ? 32 : 31);\n"
        "    let pointer: *i32 = &values[0];\n"
        "    pointer[false ? 1 : 0] += true ? 1 : 2;\n"
        "    let computed: i32 = 1 + (true ? 2 : 3);\n"
        "    let called: i32 = combine(4 + computed, false ? 5 : 6);\n"
        "    if (called != 13) { return 1; }\n"
        "    return values[0];\n"
        "}\n"};

    ASSERT_TRUE(harness.ParseAndLower()) << harness.Diagnostics();
    EXPECT_TRUE(harness.Verify()) << harness.Diagnostics();
}

TEST(SemaTest, RejectsUnsafePointerAndArrayOperations) {
    FrontendHarness read_only{"module test.read_only;\n"
                              "fn main() -> i32 {\n"
                              "    let value: i32 = 1;\n"
                              "    let pointer: *const i32 = &value;\n"
                              "    *pointer = 2;\n"
                              "    return value;\n"
                              "}\n"};
    EXPECT_FALSE(read_only.ParseAndLower());
    EXPECT_NE(read_only.Diagnostics().find(
                  "cannot assign through an immutable lvalue"),
              std::string::npos);

    FrontendHarness invalid_index{"module test.invalid_index;\n"
                                  "fn main() -> i32 {\n"
                                  "    var values: [2]i32 = {};\n"
                                  "    let index: i32 = 0;\n"
                                  "    return values[index];\n"
                                  "}\n"};
    EXPECT_FALSE(invalid_index.ParseAndLower());
    EXPECT_NE(invalid_index.Diagnostics().find("expected usize, found i32"),
              std::string::npos);

    FrontendHarness array_parameter{
        "module test.array_parameter;\n"
        "fn invalid(values: [2]i32) -> i32 { return values[0]; }\n"
        "fn main() -> i32 { return 0; }\n"};
    EXPECT_FALSE(array_parameter.ParseAndLower());
    EXPECT_NE(array_parameter.Diagnostics().find(
                  "fixed arrays cannot be passed by value"),
              std::string::npos);

    FrontendHarness nested_read_only_cast{
        "module test.nested_read_only_cast;\n"
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    var inner: *const i32 = (&value) as *const i32;\n"
        "    let nested: **const i32 = &inner;\n"
        "    let invalid: **i32 = nested as **i32;\n"
        "    return **invalid;\n"
        "}\n"};
    EXPECT_FALSE(nested_read_only_cast.ParseAndLower());
    EXPECT_NE(nested_read_only_cast.Diagnostics().find(
                  "pointer conversion cannot remove read-only qualification"),
              std::string::npos);

    FrontendHarness void_laundering{
        "module test.void_laundering;\n"
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    var inner: *const i32 = (&value) as *const i32;\n"
        "    let nested: **const i32 = &inner;\n"
        "    let invalid: *void = nested as *void;\n"
        "    return 0;\n"
        "}\n"};
    EXPECT_FALSE(void_laundering.ParseAndLower());
    EXPECT_NE(void_laundering.Diagnostics().find(
                  "pointer conversion cannot remove read-only qualification"),
              std::string::npos);

    FrontendHarness array_laundering{
        "module test.array_laundering;\n"
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    var entries: [1]*const i32 = {};\n"
        "    entries[0] = (&value) as *const i32;\n"
        "    let source: *[1]*const i32 = &entries;\n"
        "    let invalid: *[1]*i32 = source as *[1]*i32;\n"
        "    return 0;\n"
        "}\n"};
    EXPECT_FALSE(array_laundering.ParseAndLower());
    EXPECT_NE(array_laundering.Diagnostics().find(
                  "pointer conversion cannot remove read-only qualification"),
              std::string::npos);
}

}
