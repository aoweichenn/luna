#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace luna::test {
namespace {

class AggregateLayoutOwner final {
  public:
    AggregateLayoutOwner(std::uint64_t size_bytes,
                         std::uint32_t alignment_bytes) {
        luna_x86_64_abi_aggregate_layout_init(&this->layout_, size_bytes,
                                              alignment_bytes);
    }

    ~AggregateLayoutOwner() {
        luna_x86_64_abi_aggregate_layout_destroy(&this->layout_);
    }

    AggregateLayoutOwner(const AggregateLayoutOwner &) = delete;
    AggregateLayoutOwner &operator=(const AggregateLayoutOwner &) = delete;
    AggregateLayoutOwner(AggregateLayoutOwner &&) = delete;
    AggregateLayoutOwner &operator=(AggregateLayoutOwner &&) = delete;

    [[nodiscard]] bool Add(std::uint64_t offset_bytes, std::uint64_t size_bytes,
                           std::uint32_t alignment_bytes,
                           LunaX8664AbiClass abi_class) {
        return luna_x86_64_abi_aggregate_layout_add_component(
            &this->layout_, offset_bytes, size_bytes, alignment_bytes,
            abi_class);
    }

    [[nodiscard]] const LunaX8664AbiAggregateLayout *Layout() const noexcept {
        return &this->layout_;
    }

  private:
    LunaX8664AbiAggregateLayout layout_{};
};

class MachineAbiOwner final {
  public:
    explicit MachineAbiOwner(const LunaTargetInfo *target) {
        luna_x86_64_machine_module_init(&this->machine_module_, target);
        luna_x86_64_abi_init(&this->abi_);
    }

    ~MachineAbiOwner() {
        luna_x86_64_abi_destroy(&this->abi_);
        luna_x86_64_machine_module_destroy(&this->machine_module_);
    }

    MachineAbiOwner(const MachineAbiOwner &) = delete;
    MachineAbiOwner &operator=(const MachineAbiOwner &) = delete;
    MachineAbiOwner(MachineAbiOwner &&) = delete;
    MachineAbiOwner &operator=(MachineAbiOwner &&) = delete;

    [[nodiscard]] bool Analyze(FrontendHarness &harness) {
        return luna_x86_64_machine_lower(harness.Module(),
                                         harness.DiagnosticEngine(),
                                         &this->machine_module_) &&
               luna_x86_64_machine_verify(&this->machine_module_, nullptr) &&
               luna_x86_64_abi_analyze(&this->machine_module_, &this->abi_,
                                       nullptr);
    }

    [[nodiscard]] LunaX8664MachineModule *MachineModule() noexcept {
        return &this->machine_module_;
    }

    [[nodiscard]] LunaX8664ModuleAbi *Abi() noexcept {
        return &this->abi_;
    }

  private:
    LunaX8664MachineModule machine_module_{};
    LunaX8664ModuleAbi abi_{};
};

[[nodiscard]] const LunaX8664AbiParameterLocation *
ParameterLocation(const LunaX8664FunctionAbi *function,
                  std::size_t parameter_index) {
    return static_cast<const LunaX8664AbiParameterLocation *>(
        luna_vector_at_const(&function->parameter_locations, parameter_index));
}

TEST(X8664AbiTest, ClassifiesIntegerSseAndMixedAggregateEightbytes) {
    AggregateLayoutOwner integer_layout{8U, 4U};
    ASSERT_TRUE(integer_layout.Add(0U, 4U, 4U, LUNA_X86_64_ABI_CLASS_INTEGER));
    ASSERT_TRUE(integer_layout.Add(4U, 4U, 4U, LUNA_X86_64_ABI_CLASS_INTEGER));
    LunaX8664AbiAggregateClassification integer_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(integer_layout.Layout(),
                                                   &integer_classification));
    EXPECT_EQ(integer_classification.eightbyte_count, 1U);
    EXPECT_EQ(integer_classification.eightbytes[0],
              LUNA_X86_64_ABI_CLASS_INTEGER);
    EXPECT_TRUE(luna_x86_64_abi_aggregate_classification_verify(
        integer_layout.Layout(), &integer_classification));

    AggregateLayoutOwner sse_layout{16U, 8U};
    ASSERT_TRUE(sse_layout.Add(0U, 8U, 8U, LUNA_X86_64_ABI_CLASS_SSE));
    ASSERT_TRUE(sse_layout.Add(8U, 8U, 8U, LUNA_X86_64_ABI_CLASS_SSE));
    LunaX8664AbiAggregateClassification sse_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(sse_layout.Layout(),
                                                   &sse_classification));
    EXPECT_EQ(sse_classification.eightbyte_count, 2U);
    EXPECT_EQ(sse_classification.eightbytes[0], LUNA_X86_64_ABI_CLASS_SSE);
    EXPECT_EQ(sse_classification.eightbytes[1], LUNA_X86_64_ABI_CLASS_SSE);

    AggregateLayoutOwner mixed_layout{16U, 8U};
    ASSERT_TRUE(mixed_layout.Add(0U, 4U, 4U, LUNA_X86_64_ABI_CLASS_INTEGER));
    ASSERT_TRUE(mixed_layout.Add(8U, 8U, 8U, LUNA_X86_64_ABI_CLASS_SSE));
    LunaX8664AbiAggregateClassification mixed_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(mixed_layout.Layout(),
                                                   &mixed_classification));
    EXPECT_EQ(mixed_classification.eightbytes[0],
              LUNA_X86_64_ABI_CLASS_INTEGER);
    EXPECT_EQ(mixed_classification.eightbytes[1], LUNA_X86_64_ABI_CLASS_SSE);
}

TEST(X8664AbiTest, AppliesUnionMergeUnalignedAndMemoryRules) {
    AggregateLayoutOwner union_layout{8U, 8U};
    ASSERT_TRUE(union_layout.Add(0U, 8U, 8U, LUNA_X86_64_ABI_CLASS_SSE));
    ASSERT_TRUE(union_layout.Add(0U, 4U, 4U, LUNA_X86_64_ABI_CLASS_INTEGER));
    LunaX8664AbiAggregateClassification union_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(union_layout.Layout(),
                                                   &union_classification));
    EXPECT_EQ(union_classification.eightbytes[0],
              LUNA_X86_64_ABI_CLASS_INTEGER);

    AggregateLayoutOwner unaligned_layout{12U, 4U};
    ASSERT_TRUE(
        unaligned_layout.Add(4U, 8U, 8U, LUNA_X86_64_ABI_CLASS_INTEGER));
    LunaX8664AbiAggregateClassification unaligned_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(unaligned_layout.Layout(),
                                                   &unaligned_classification));
    EXPECT_EQ(unaligned_classification.eightbyte_count, 1U);
    EXPECT_EQ(unaligned_classification.eightbytes[0],
              LUNA_X86_64_ABI_CLASS_MEMORY);

    AggregateLayoutOwner large_layout{24U, 8U};
    ASSERT_TRUE(large_layout.Add(0U, 8U, 8U, LUNA_X86_64_ABI_CLASS_INTEGER));
    ASSERT_TRUE(large_layout.Add(8U, 8U, 8U, LUNA_X86_64_ABI_CLASS_INTEGER));
    ASSERT_TRUE(large_layout.Add(16U, 8U, 8U, LUNA_X86_64_ABI_CLASS_INTEGER));
    LunaX8664AbiAggregateClassification large_classification{};
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(large_layout.Layout(),
                                                   &large_classification));
    EXPECT_EQ(large_classification.eightbytes[0], LUNA_X86_64_ABI_CLASS_MEMORY);
}

TEST(X8664AbiTest, RejectsMalformedLayoutsAndCorruptedClassifications) {
    AggregateLayoutOwner empty_layout{8U, 8U};
    LunaX8664AbiAggregateClassification classification{};
    EXPECT_FALSE(luna_x86_64_abi_classify_aggregate(empty_layout.Layout(),
                                                    &classification));

    AggregateLayoutOwner layout{8U, 4U};
    ASSERT_TRUE(layout.Add(0U, 8U, 3U, LUNA_X86_64_ABI_CLASS_INTEGER));
    EXPECT_FALSE(
        luna_x86_64_abi_classify_aggregate(layout.Layout(), &classification));

    AggregateLayoutOwner unnatural_layout{8U, 4U};
    ASSERT_TRUE(
        unnatural_layout.Add(0U, 8U, 4U, LUNA_X86_64_ABI_CLASS_INTEGER));
    EXPECT_FALSE(luna_x86_64_abi_classify_aggregate(unnatural_layout.Layout(),
                                                    &classification));

    AggregateLayoutOwner valid_layout{8U, 8U};
    ASSERT_TRUE(valid_layout.Add(0U, 8U, 8U, LUNA_X86_64_ABI_CLASS_INTEGER));
    ASSERT_TRUE(luna_x86_64_abi_classify_aggregate(valid_layout.Layout(),
                                                   &classification));
    classification.eightbytes[0] = LUNA_X86_64_ABI_CLASS_SSE;
    EXPECT_FALSE(luna_x86_64_abi_aggregate_classification_verify(
        valid_layout.Layout(), &classification));
}

TEST(X8664AbiTest, AssignsIndependentRegisterBanksAndDenseStackSlots) {
    FrontendHarness harness{
        "module test.abi_stack;\n"
        "fn consume(a0: i32, a1: i32, a2: i32, a3: i32, a4: i32,\n"
        "           a5: i32, a6: i32, f0: f64, f1: f64, f2: f64,\n"
        "           f3: f64, f4: f64, f5: f64, f6: f64, f7: f64,\n"
        "           f8: f64) -> i32 { return a6; }\n"
        "fn main() -> i32 {\n"
        "    return consume(0, 1, 2, 3, 4, 5, 42,\n"
        "                   0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0);\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineAbiOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();
    ASSERT_TRUE(luna_x86_64_abi_verify(analysis.MachineModule(), analysis.Abi(),
                                       nullptr));

    const LunaX8664FunctionAbi *consume =
        static_cast<const LunaX8664FunctionAbi *>(
            luna_vector_at_const(&analysis.Abi()->functions, 0U));
    ASSERT_NE(consume, nullptr);
    EXPECT_EQ(consume->general_register_count, 6U);
    EXPECT_EQ(consume->vector_register_count, 8U);
    EXPECT_EQ(consume->stack_argument_size_bytes, 16U);
    EXPECT_EQ(consume->call_frame_size_bytes, 16U);

    const LunaX8664AbiParameterLocation *sixth_integer =
        ParameterLocation(consume, 5U);
    const LunaX8664AbiParameterLocation *seventh_integer =
        ParameterLocation(consume, 6U);
    const LunaX8664AbiParameterLocation *first_float =
        ParameterLocation(consume, 7U);
    const LunaX8664AbiParameterLocation *ninth_float =
        ParameterLocation(consume, 15U);
    ASSERT_NE(sixth_integer, nullptr);
    ASSERT_NE(seventh_integer, nullptr);
    ASSERT_NE(first_float, nullptr);
    ASSERT_NE(ninth_float, nullptr);
    EXPECT_EQ(sixth_integer->kind, LUNA_X86_64_ABI_LOCATION_GENERAL_REGISTER);
    EXPECT_EQ(sixth_integer->register_index, 5U);
    EXPECT_EQ(seventh_integer->kind, LUNA_X86_64_ABI_LOCATION_STACK);
    EXPECT_EQ(seventh_integer->stack_offset_bytes, 0U);
    EXPECT_EQ(first_float->kind, LUNA_X86_64_ABI_LOCATION_VECTOR_REGISTER);
    EXPECT_EQ(first_float->register_index, 0U);
    EXPECT_EQ(ninth_float->kind, LUNA_X86_64_ABI_LOCATION_STACK);
    EXPECT_EQ(ninth_float->stack_offset_bytes, 8U);

    LunaX8664FunctionAbi *mutable_consume = static_cast<LunaX8664FunctionAbi *>(
        luna_vector_at(&analysis.Abi()->functions, 0U));
    ASSERT_NE(mutable_consume, nullptr);
    mutable_consume->call_frame_size_bytes = 32U;
    EXPECT_FALSE(luna_x86_64_abi_verify(analysis.MachineModule(),
                                        analysis.Abi(),
                                        harness.DiagnosticEngine()->stream));
}

TEST(X8664AbiTest, PrintsDeterministicAuditableSnapshot) {
    FrontendHarness harness{
        "module test.abi_print;\n"
        "extern fn c_value(a: i32, b: f64, c: i32, d: i32, e: i32,\n"
        "                  f: i32, g: i32, h: i32) -> i32;\n"
        "fn main() -> i32 { return 0; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    MachineAbiOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();

    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    ASSERT_TRUE(luna_x86_64_abi_print(analysis.MachineModule(), analysis.Abi(),
                                      &output));
    const std::string text{luna_string_builder_data(&output), output.length};
    luna_string_builder_destroy(&output);

    ASSERT_TRUE(harness.EmitAbi()) << harness.Diagnostics();
    EXPECT_EQ(harness.Abi(), text);
    EXPECT_NE(text.find("x86-64-system-v-abi"), std::string::npos);
    EXPECT_NE(text.find("declare @f0 c_value parameters=8 gp=6 sse=1 "
                        "stack-bytes=8 call-frame=16"),
              std::string::npos);
    EXPECT_NE(text.find("p7 type=i32 class=integer location=stack[0]"),
              std::string::npos);
}

}
}
