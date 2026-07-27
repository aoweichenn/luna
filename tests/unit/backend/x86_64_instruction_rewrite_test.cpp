#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace luna::test {
namespace {

class InstructionRewriteOwner final {
  public:
    explicit InstructionRewriteOwner(const LunaTargetInfo *target) {
        luna_x86_64_machine_module_init(&this->machine_module_, target);
        luna_x86_64_abi_init(&this->abi_);
        luna_x86_64_liveness_init(&this->liveness_);
        luna_x86_64_register_allocation_init(&this->allocation_);
        luna_x86_64_instruction_rewrite_init(&this->rewrite_);
    }

    ~InstructionRewriteOwner() {
        luna_x86_64_instruction_rewrite_destroy(&this->rewrite_);
        luna_x86_64_register_allocation_destroy(&this->allocation_);
        luna_x86_64_liveness_destroy(&this->liveness_);
        luna_x86_64_abi_destroy(&this->abi_);
        luna_x86_64_machine_module_destroy(&this->machine_module_);
    }

    InstructionRewriteOwner(const InstructionRewriteOwner &) = delete;
    InstructionRewriteOwner &
    operator=(const InstructionRewriteOwner &) = delete;
    InstructionRewriteOwner(InstructionRewriteOwner &&) = delete;
    InstructionRewriteOwner &operator=(InstructionRewriteOwner &&) = delete;

    [[nodiscard]] bool Build(FrontendHarness &harness) {
        return luna_x86_64_machine_lower(harness.Module(),
                                         harness.DiagnosticEngine(),
                                         &this->machine_module_) &&
               luna_x86_64_abi_analyze(&this->machine_module_, &this->abi_,
                                       nullptr) &&
               luna_x86_64_liveness_analyze(&this->machine_module_,
                                            &this->liveness_, nullptr) &&
               luna_x86_64_register_allocate(&this->machine_module_,
                                             &this->liveness_,
                                             &this->allocation_, nullptr) &&
               luna_x86_64_instruction_rewrite_build(
                   &this->machine_module_, &this->abi_, &this->liveness_,
                   &this->allocation_, &this->rewrite_, nullptr);
    }

    [[nodiscard]] bool Verify() const {
        return luna_x86_64_instruction_rewrite_verify(
            &this->machine_module_, &this->abi_, &this->liveness_,
            &this->allocation_, &this->rewrite_, nullptr);
    }

    [[nodiscard]] LunaX8664ModuleInstructionRewrite *Rewrite() noexcept {
        return &this->rewrite_;
    }

  private:
    LunaX8664MachineModule machine_module_{};
    LunaX8664ModuleAbi abi_{};
    LunaX8664ModuleLiveness liveness_{};
    LunaX8664ModuleRegisterAllocation allocation_{};
    LunaX8664ModuleInstructionRewrite rewrite_{};
};

[[nodiscard]] constexpr std::uint64_t
RegisterBit(LunaX8664PhysicalRegister physical_register) {
    return UINT64_C(1) << static_cast<std::uint32_t>(physical_register);
}

[[nodiscard]] LunaX8664RewrittenInstruction *
FindRewrittenInstruction(LunaX8664ModuleInstructionRewrite *rewrite,
                         LunaX8664MachineOpcode opcode) {
    for (std::size_t function_index = 0U;
         function_index < rewrite->functions.length; function_index += 1U) {
        LunaX8664FunctionInstructionRewrite *function =
            static_cast<LunaX8664FunctionInstructionRewrite *>(
                luna_vector_at(&rewrite->functions, function_index));
        if (function == nullptr) {
            continue;
        }
        for (std::size_t instruction_index = 0U;
             instruction_index < function->instructions.length;
             instruction_index += 1U) {
            LunaX8664RewrittenInstruction *instruction =
                static_cast<LunaX8664RewrittenInstruction *>(
                    luna_vector_at(&function->instructions, instruction_index));
            if (instruction != nullptr && instruction->opcode == opcode) {
                return instruction;
            }
        }
    }
    return nullptr;
}

TEST(X8664InstructionRewriteTest,
     ModelsDivisionShiftCallAndReservedRegisterConstraints) {
    FrontendHarness harness{
        "module test.rewrite_constraints;\n"
        "extern fn c_mix(left: i32, right: f64) -> i64;\n"
        "fn calculate(left: i64, right: i64) -> i64 {\n"
        "    return (left / right) + (left << right);\n"
        "}\n"
        "fn negate(value: f64) -> f64 { return -value; }\n"
        "fn wide() -> i64 { return 9223372036854775807; }\n"
        "fn main() -> i32 { return c_mix(42, 1.5) as i32; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    InstructionRewriteOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Build(harness)) << harness.Diagnostics();
    ASSERT_TRUE(owner.Verify());

    const std::uint64_t reserved =
        luna_x86_64_instruction_rewrite_reserved_register_mask();
    EXPECT_NE(reserved & RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RAX), 0U);
    EXPECT_NE(reserved & RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RCX), 0U);
    EXPECT_NE(reserved & RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_XMM0), 0U);
    EXPECT_EQ(reserved & RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RBX), 0U);

    const LunaX8664RewrittenInstruction *division = FindRewrittenInstruction(
        owner.Rewrite(), LUNA_X86_64_MACHINE_DIV_INTEGER);
    const LunaX8664RewrittenInstruction *shift = FindRewrittenInstruction(
        owner.Rewrite(), LUNA_X86_64_MACHINE_SHIFT_LEFT_INTEGER);
    const LunaX8664RewrittenInstruction *call =
        FindRewrittenInstruction(owner.Rewrite(), LUNA_X86_64_MACHINE_CALL);
    const LunaX8664RewrittenInstruction *wide_constant =
        FindRewrittenInstruction(owner.Rewrite(),
                                 LUNA_X86_64_MACHINE_CONST_INTEGER);
    const LunaX8664RewrittenInstruction *float_negation =
        FindRewrittenInstruction(owner.Rewrite(),
                                 LUNA_X86_64_MACHINE_NEG_FLOAT);
    ASSERT_NE(division, nullptr);
    ASSERT_NE(shift, nullptr);
    ASSERT_NE(call, nullptr);
    ASSERT_NE(wide_constant, nullptr);
    ASSERT_NE(float_negation, nullptr);
    EXPECT_NE(division->fixed_input_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RAX),
              0U);
    EXPECT_NE(division->fixed_output_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RDX),
              0U);
    EXPECT_NE(shift->fixed_input_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RCX),
              0U);
    EXPECT_EQ(call->parallel_move_count, 2U);
    EXPECT_NE(call->parallel_move_destination_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RDI),
              0U);
    EXPECT_NE(call->parallel_move_destination_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_XMM0),
              0U);
    EXPECT_NE(call->clobbered_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_XMM14),
              0U);
    EXPECT_NE(wide_constant->clobbered_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RAX),
              0U);
    EXPECT_NE(float_negation->clobbered_register_mask &
                  RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RDX),
              0U);
}

TEST(X8664InstructionRewriteTest,
     RejectsMutatedLocationsConstraintsAndFunctionSummaries) {
    FrontendHarness harness{"module test.rewrite_verify;\n"
                            "fn main() -> i32 { return (84 / 2) + 1; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    InstructionRewriteOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Build(harness)) << harness.Diagnostics();
    ASSERT_TRUE(owner.Verify());

    LunaX8664FunctionInstructionRewrite *function =
        static_cast<LunaX8664FunctionInstructionRewrite *>(
            luna_vector_at(&owner.Rewrite()->functions, 0U));
    ASSERT_NE(function, nullptr);
    LunaX8664RewrittenInstruction *instruction = FindRewrittenInstruction(
        owner.Rewrite(), LUNA_X86_64_MACHINE_DIV_INTEGER);
    ASSERT_NE(instruction, nullptr);

    instruction->clobbered_register_mask ^=
        RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RAX);
    EXPECT_FALSE(owner.Verify());
    instruction->clobbered_register_mask ^=
        RegisterBit(LUNA_X86_64_PHYSICAL_REGISTER_RAX);
    ASSERT_TRUE(owner.Verify());

    LunaX8664VirtualRegisterAllocation *value_location =
        static_cast<LunaX8664VirtualRegisterAllocation *>(
            luna_vector_at(&function->value_locations, 0U));
    ASSERT_NE(value_location, nullptr);
    const LunaX8664PhysicalRegister original_register =
        value_location->physical_register;
    value_location->physical_register = LUNA_X86_64_PHYSICAL_REGISTER_RAX;
    EXPECT_FALSE(owner.Verify());
    value_location->physical_register = original_register;
    ASSERT_TRUE(owner.Verify());

    function->spill_slot_count += 1U;
    EXPECT_FALSE(owner.Verify());
    function->spill_slot_count -= 1U;
    EXPECT_TRUE(owner.Verify());
}

TEST(X8664InstructionRewriteTest,
     EmitsRegisterResidentAssemblyWithCalleeSavePreservation) {
    FrontendHarness harness{
        "module test.rewrite_assembly;\n"
        "fn add(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return add(20, 22); }\n"};
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movq %rbx, -"), std::string::npos);
    EXPECT_NE(assembly.find("(%rbp), %rbx"), std::string::npos);
    EXPECT_NE(assembly.find("movl %ebx, %eax"), std::string::npos);
    EXPECT_NE(assembly.find("movl %eax, %r13d"), std::string::npos);
}

TEST(X8664InstructionRewriteTest,
     MaterializesUniqueSpillSlotsUnderRegisterPressure) {
    constexpr std::uint32_t LUNA_TEST_REWRITE_PRESSURE_VALUE_COUNT = 24U;
    std::string expression =
        std::to_string(LUNA_TEST_REWRITE_PRESSURE_VALUE_COUNT);
    for (std::uint32_t value = LUNA_TEST_REWRITE_PRESSURE_VALUE_COUNT - 1U;
         value > 0U; value -= 1U) {
        std::string nested_expression;
        nested_expression.reserve(expression.size() + 16U);
        nested_expression.append(std::to_string(value));
        nested_expression.append(" + (");
        nested_expression.append(expression);
        nested_expression.push_back(')');
        expression = std::move(nested_expression);
    }
    const std::string source = "module test.rewrite_spills;\n"
                               "fn main() -> i32 { return " +
                               expression + "; }\n";
    FrontendHarness harness{std::string_view{source}};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    InstructionRewriteOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Build(harness)) << harness.Diagnostics();
    ASSERT_TRUE(owner.Verify());

    const LunaX8664FunctionInstructionRewrite *function =
        static_cast<const LunaX8664FunctionInstructionRewrite *>(
            luna_vector_at_const(&owner.Rewrite()->functions, 0U));
    ASSERT_NE(function, nullptr);
    EXPECT_GT(function->spill_slot_count, 0U);
    EXPECT_EQ(function->value_locations.length, 47U);
    EXPECT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
}

TEST(X8664InstructionRewriteTest,
     PrintsDeterministicallyThroughTheCompilerFacade) {
    constexpr std::string_view LUNA_TEST_SOURCE =
        "module test.rewrite_print;\n"
        "fn add(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return add(20, 22); }\n";
    FrontendHarness first{LUNA_TEST_SOURCE};
    FrontendHarness second{LUNA_TEST_SOURCE};
    ASSERT_TRUE(first.EmitInstructionRewrite()) << first.Diagnostics();
    ASSERT_TRUE(second.EmitInstructionRewrite()) << second.Diagnostics();
    EXPECT_EQ(first.InstructionRewrite(), second.InstructionRewrite());
    EXPECT_NE(first.InstructionRewrite().find("x86-64-instruction-rewrite"),
              std::string::npos);
    EXPECT_NE(first.InstructionRewrite().find("parallel-moves=2"),
              std::string::npos);
    EXPECT_NE(first.InstructionRewrite().find("clobbers={%rax"),
              std::string::npos);
}

}
}
