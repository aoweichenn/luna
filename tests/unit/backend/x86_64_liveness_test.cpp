#include "test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

struct FileCloser final {
    void operator()(std::FILE *file) const noexcept {
        if (file != nullptr) {
            static_cast<void>(std::fclose(file));
        }
    }
};

class MachineLivenessOwner final {
  public:
    explicit MachineLivenessOwner(const LunaTargetInfo *target) {
        luna_x86_64_machine_module_init(&this->machine_module_, target);
        luna_x86_64_liveness_init(&this->liveness_);
    }

    ~MachineLivenessOwner() {
        luna_x86_64_liveness_destroy(&this->liveness_);
        luna_x86_64_machine_module_destroy(&this->machine_module_);
    }

    MachineLivenessOwner(const MachineLivenessOwner &) = delete;
    MachineLivenessOwner &operator=(const MachineLivenessOwner &) = delete;
    MachineLivenessOwner(MachineLivenessOwner &&) = delete;
    MachineLivenessOwner &operator=(MachineLivenessOwner &&) = delete;

    [[nodiscard]] bool Analyze(FrontendHarness &harness) {
        return luna_x86_64_machine_lower(harness.Module(),
                                         harness.DiagnosticEngine(),
                                         &this->machine_module_) &&
               luna_x86_64_machine_verify(&this->machine_module_, nullptr) &&
               luna_x86_64_liveness_analyze(&this->machine_module_,
                                            &this->liveness_, nullptr);
    }

    [[nodiscard]] LunaX8664MachineModule *MachineModule() noexcept {
        return &this->machine_module_;
    }

    [[nodiscard]] LunaX8664ModuleLiveness *Liveness() noexcept {
        return &this->liveness_;
    }

  private:
    LunaX8664MachineModule machine_module_{};
    LunaX8664ModuleLiveness liveness_{};
};

[[nodiscard]] bool
LiveSetEquals(const LunaX8664LiveSet &set,
              std::initializer_list<std::uint32_t> expected_values) {
    if (luna_x86_64_live_set_count(&set) != expected_values.size()) {
        return false;
    }
    for (std::uint32_t virtual_register = 0U;
         virtual_register < set.value_count; virtual_register += 1U) {
        const bool expected =
            std::find(expected_values.begin(), expected_values.end(),
                      virtual_register) != expected_values.end();
        if (luna_x86_64_live_set_contains(&set, virtual_register) != expected) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const LunaX8664FunctionLiveness *
FunctionLiveness(const LunaX8664ModuleLiveness *liveness,
                 std::size_t function_index) {
    return static_cast<const LunaX8664FunctionLiveness *>(
        luna_vector_at_const(&liveness->functions, function_index));
}

[[nodiscard]] const LunaX8664BlockLiveness *
BlockLiveness(const LunaX8664FunctionLiveness *function,
              std::size_t block_index) {
    return static_cast<const LunaX8664BlockLiveness *>(
        luna_vector_at_const(&function->blocks, block_index));
}

[[nodiscard]] LunaX8664InstructionLiveness *
InstructionLiveness(LunaX8664BlockLiveness *block,
                    std::size_t instruction_index) {
    return static_cast<LunaX8664InstructionLiveness *>(
        luna_vector_at(&block->instructions, instruction_index));
}

TEST(X8664LivenessTest, ComputesExactInstructionTransferSets) {
    FrontendHarness harness{"module test.liveness;\n"
                            "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineLivenessOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();
    ASSERT_TRUE(luna_x86_64_liveness_verify(analysis.MachineModule(),
                                            analysis.Liveness(), nullptr));

    const LunaX8664FunctionLiveness *function =
        FunctionLiveness(analysis.Liveness(), 0U);
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->value_count, 3U);
    EXPECT_EQ(function->iteration_count, 1U);
    const LunaX8664BlockLiveness *block = BlockLiveness(function, 0U);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->instructions.length, 4U);
    EXPECT_TRUE(LiveSetEquals(block->use, {}));
    EXPECT_TRUE(LiveSetEquals(block->definition, {0U, 1U, 2U}));
    EXPECT_TRUE(LiveSetEquals(block->live_in, {}));
    EXPECT_TRUE(LiveSetEquals(block->live_out, {}));

    const LunaX8664InstructionLiveness *first =
        static_cast<const LunaX8664InstructionLiveness *>(
            luna_vector_at_const(&block->instructions, 0U));
    const LunaX8664InstructionLiveness *second =
        static_cast<const LunaX8664InstructionLiveness *>(
            luna_vector_at_const(&block->instructions, 1U));
    const LunaX8664InstructionLiveness *addition =
        static_cast<const LunaX8664InstructionLiveness *>(
            luna_vector_at_const(&block->instructions, 2U));
    const LunaX8664InstructionLiveness *return_instruction =
        static_cast<const LunaX8664InstructionLiveness *>(
            luna_vector_at_const(&block->instructions, 3U));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(addition, nullptr);
    ASSERT_NE(return_instruction, nullptr);
    EXPECT_TRUE(LiveSetEquals(first->live_before, {}));
    EXPECT_TRUE(LiveSetEquals(first->live_after, {0U}));
    EXPECT_TRUE(LiveSetEquals(second->live_before, {0U}));
    EXPECT_TRUE(LiveSetEquals(second->live_after, {0U, 1U}));
    EXPECT_TRUE(LiveSetEquals(addition->live_before, {0U, 1U}));
    EXPECT_TRUE(LiveSetEquals(addition->live_after, {2U}));
    EXPECT_TRUE(LiveSetEquals(return_instruction->live_before, {2U}));
    EXPECT_TRUE(LiveSetEquals(return_instruction->live_after, {}));
}

TEST(X8664LivenessTest, HandlesCallsDeclarationsAndControlFlowBlocks) {
    FrontendHarness harness{"module test.liveness_call;\n"
                            "extern fn c_add(left: i32, right: i32) -> i32;\n"
                            "fn main() -> i32 {\n"
                            "    if (true) { return c_add(20, 22); }\n"
                            "    return 1;\n"
                            "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineLivenessOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();

    const LunaX8664FunctionLiveness *declaration =
        FunctionLiveness(analysis.Liveness(), 0U);
    const LunaX8664FunctionLiveness *main_function =
        FunctionLiveness(analysis.Liveness(), 1U);
    ASSERT_NE(declaration, nullptr);
    ASSERT_NE(main_function, nullptr);
    EXPECT_EQ(declaration->value_count, 0U);
    EXPECT_EQ(declaration->iteration_count, 0U);
    EXPECT_EQ(declaration->blocks.length, 0U);
    EXPECT_EQ(main_function->iteration_count, 1U);
    ASSERT_GE(main_function->blocks.length, 3U);

    for (std::size_t block_index = 0U;
         block_index < main_function->blocks.length; block_index += 1U) {
        const LunaX8664BlockLiveness *block =
            BlockLiveness(main_function, block_index);
        ASSERT_NE(block, nullptr);
        EXPECT_TRUE(LiveSetEquals(block->use, {}));
        EXPECT_TRUE(LiveSetEquals(block->live_in, {}));
        EXPECT_TRUE(LiveSetEquals(block->live_out, {}));
    }

    bool found_call = false;
    const LunaX8664MachineFunction *machine_main =
        luna_x86_64_machine_module_function_const(analysis.MachineModule(), 1U);
    ASSERT_NE(machine_main, nullptr);
    for (std::size_t block_index = 0U;
         block_index < machine_main->blocks.length; block_index += 1U) {
        const LunaX8664MachineBlock *machine_block =
            static_cast<const LunaX8664MachineBlock *>(
                luna_vector_at_const(&machine_main->blocks, block_index));
        const LunaX8664BlockLiveness *block =
            BlockLiveness(main_function, block_index);
        ASSERT_NE(machine_block, nullptr);
        ASSERT_NE(block, nullptr);
        for (std::size_t instruction_index = 0U;
             instruction_index < machine_block->instructions.length;
             instruction_index += 1U) {
            const LunaX8664MachineInstruction *instruction =
                static_cast<const LunaX8664MachineInstruction *>(
                    luna_vector_at_const(&machine_block->instructions,
                                         instruction_index));
            const LunaX8664InstructionLiveness *instruction_liveness =
                static_cast<const LunaX8664InstructionLiveness *>(
                    luna_vector_at_const(&block->instructions,
                                         instruction_index));
            ASSERT_NE(instruction, nullptr);
            ASSERT_NE(instruction_liveness, nullptr);
            if (instruction->opcode != LUNA_X86_64_MACHINE_CALL) {
                continue;
            }
            found_call = true;
            EXPECT_EQ(
                luna_x86_64_live_set_count(&instruction_liveness->live_before),
                instruction->argument_count);
            for (std::uint32_t use_index = 0U;
                 use_index < instruction->argument_count; use_index += 1U) {
                const LunaX8664MachineVirtualRegister argument =
                    luna_x86_64_machine_instruction_use(machine_main,
                                                        instruction, use_index);
                EXPECT_TRUE(luna_x86_64_live_set_contains(
                    &instruction_liveness->live_before, argument));
            }
            EXPECT_TRUE(luna_x86_64_live_set_contains(
                &instruction_liveness->live_after, instruction->result));
        }
    }
    EXPECT_TRUE(found_call);
}

TEST(X8664LivenessTest, UsesMultipleWordsForLargeVirtualRegisterFiles) {
    std::string source =
        "module test.liveness_large;\nfn main() -> i32 { return ";
    for (std::uint32_t value = 1U; value <= 40U; value += 1U) {
        if (value != 1U) {
            source.append(" + ");
        }
        source.append(std::to_string(value));
    }
    source.append("; }\n");

    FrontendHarness harness{std::string_view(source)};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    MachineLivenessOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();

    const LunaX8664FunctionLiveness *function =
        FunctionLiveness(analysis.Liveness(), 0U);
    ASSERT_NE(function, nullptr);
    ASSERT_GT(function->value_count, 64U);
    const LunaX8664BlockLiveness *block = BlockLiveness(function, 0U);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(luna_x86_64_live_set_count(&block->definition),
              function->value_count);
    ASSERT_FALSE(block->instructions.length == 0U);
    const LunaX8664InstructionLiveness *return_instruction =
        static_cast<const LunaX8664InstructionLiveness *>(luna_vector_at_const(
            &block->instructions, block->instructions.length - 1U));
    ASSERT_NE(return_instruction, nullptr);
    EXPECT_EQ(luna_x86_64_live_set_count(&return_instruction->live_before), 1U);
    EXPECT_FALSE(luna_x86_64_live_set_contains(&return_instruction->live_before,
                                               function->value_count));
}

TEST(X8664LivenessTest, RejectsCorruptedInstructionAndPaddingSets) {
    FrontendHarness harness{"module test.liveness_verify;\n"
                            "fn main() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    MachineLivenessOwner analysis{luna_target_info_default()};
    ASSERT_TRUE(analysis.Analyze(harness)) << harness.Diagnostics();

    LunaX8664FunctionLiveness *function =
        static_cast<LunaX8664FunctionLiveness *>(
            luna_vector_at(&analysis.Liveness()->functions, 0U));
    ASSERT_NE(function, nullptr);
    LunaX8664BlockLiveness *block = static_cast<LunaX8664BlockLiveness *>(
        luna_vector_at(&function->blocks, 0U));
    ASSERT_NE(block, nullptr);
    LunaX8664InstructionLiveness *instruction = InstructionLiveness(block, 0U);
    ASSERT_NE(instruction, nullptr);
    std::uint64_t *word = static_cast<std::uint64_t *>(
        luna_vector_at(&instruction->live_after.words, 0U));
    ASSERT_NE(word, nullptr);

    *word ^= UINT64_C(1);
    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(luna_x86_64_liveness_verify(
        analysis.MachineModule(), analysis.Liveness(), diagnostic_file.get()));
    *word ^= UINT64_C(1);
    ASSERT_TRUE(luna_x86_64_liveness_verify(analysis.MachineModule(),
                                            analysis.Liveness(), nullptr));

    *word |= UINT64_C(1) << 63U;
    EXPECT_FALSE(luna_x86_64_liveness_verify(
        analysis.MachineModule(), analysis.Liveness(), diagnostic_file.get()));
    EXPECT_FALSE(luna_x86_64_live_set_contains(&instruction->live_after, 0U));
    EXPECT_EQ(luna_x86_64_live_set_count(&instruction->live_after), 0U);
}

TEST(X8664LivenessTest, PrintsDeterministicallyThroughTheCompilerFacade) {
    FrontendHarness first{"module test.liveness_print;\n"
                          "fn main() -> i32 { return 20 + 22; }\n"};
    FrontendHarness second{"module test.liveness_print;\n"
                           "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(first.EmitLiveness()) << first.Diagnostics();
    ASSERT_TRUE(second.EmitLiveness()) << second.Diagnostics();
    EXPECT_EQ(first.Liveness(), second.Liveness());
    EXPECT_NE(first.Liveness().find("x86-64-liveness"), std::string::npos);
    EXPECT_NE(first.Liveness().find(
                  "bb0 use={} def={%v0, %v1, %v2} live-in={} live-out={}"),
              std::string::npos);
    EXPECT_NE(first.Liveness().find("i2 before={%v0, %v1} after={%v2}"),
              std::string::npos);
}

}
}
