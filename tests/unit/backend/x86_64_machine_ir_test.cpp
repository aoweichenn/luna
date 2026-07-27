#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
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

class MachineModuleOwner final {
  public:
    explicit MachineModuleOwner(const LunaTargetInfo *target) {
        luna_x86_64_machine_module_init(&this->module_, target);
    }

    ~MachineModuleOwner() {
        luna_x86_64_machine_module_destroy(&this->module_);
    }

    MachineModuleOwner(const MachineModuleOwner &) = delete;
    MachineModuleOwner &operator=(const MachineModuleOwner &) = delete;
    MachineModuleOwner(MachineModuleOwner &&) = delete;
    MachineModuleOwner &operator=(MachineModuleOwner &&) = delete;

    [[nodiscard]] LunaX8664MachineModule *Get() noexcept {
        return &this->module_;
    }

  private:
    LunaX8664MachineModule module_{};
};

[[nodiscard]] LunaX8664MachineFunction *
FindMachineFunction(LunaX8664MachineModule *module,
                    std::string_view name) noexcept {
    for (std::size_t index = 0U; index < module->functions.length;
         index += 1U) {
        LunaX8664MachineFunction *function =
            static_cast<LunaX8664MachineFunction *>(
                luna_vector_at(&module->functions, index));
        if (function != nullptr &&
            std::string_view{function->name.data, function->name.length} ==
                name) {
            return function;
        }
    }
    return nullptr;
}

[[nodiscard]] LunaX8664MachineInstruction *
FindMachineInstructionAt(LunaX8664MachineFunction *function,
                         LunaX8664MachineOpcode opcode,
                         std::size_t occurrence) noexcept {
    for (std::size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        LunaX8664MachineBlock *block = static_cast<LunaX8664MachineBlock *>(
            luna_vector_at(&function->blocks, block_index));
        if (block == nullptr) {
            continue;
        }
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            LunaX8664MachineInstruction *instruction =
                static_cast<LunaX8664MachineInstruction *>(
                    luna_vector_at(&block->instructions, instruction_index));
            if (instruction != nullptr && instruction->opcode == opcode) {
                if (occurrence == 0U) {
                    return instruction;
                }
                occurrence -= 1U;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] LunaX8664MachineInstruction *
FindMachineInstruction(LunaX8664MachineFunction *function,
                       LunaX8664MachineOpcode opcode) noexcept {
    return FindMachineInstructionAt(function, opcode, 0U);
}

TEST(X8664MachineIrTest, ResolvesTargetSizedTypesAndExposesDefUse) {
    FrontendHarness harness{
        "module test.machine_types;\n"
        "fn transform(index: usize, value: isize) -> isize {\n"
        "    let shifted: usize = index >> 1;\n"
        "    return value + (shifted as isize);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return transform(8, -2) == 2 ? 42 : 1;\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    ASSERT_TRUE(luna_x86_64_machine_verify(machine.Get(), nullptr));

    LunaX8664MachineFunction *transform =
        FindMachineFunction(machine.Get(), "transform");
    ASSERT_NE(transform, nullptr);
    ASSERT_EQ(transform->parameter_types.length, 2U);
    const LunaX8664MachineType *index_type =
        static_cast<const LunaX8664MachineType *>(
            luna_vector_at_const(&transform->parameter_types, 0U));
    const LunaX8664MachineType *value_type =
        static_cast<const LunaX8664MachineType *>(
            luna_vector_at_const(&transform->parameter_types, 1U));
    ASSERT_NE(index_type, nullptr);
    ASSERT_NE(value_type, nullptr);
    EXPECT_EQ(*index_type, LUNA_X86_64_MACHINE_TYPE_U64);
    EXPECT_EQ(*value_type, LUNA_X86_64_MACHINE_TYPE_I64);
    EXPECT_EQ(luna_x86_64_machine_type_register_class(*index_type),
              LUNA_X86_64_MACHINE_REGISTER_GENERAL);

    LunaX8664MachineInstruction *shift = FindMachineInstruction(
        transform, LUNA_X86_64_MACHINE_SHIFT_RIGHT_INTEGER);
    LunaX8664MachineInstruction *addition =
        FindMachineInstruction(transform, LUNA_X86_64_MACHINE_ADD_INTEGER);
    ASSERT_NE(shift, nullptr);
    ASSERT_NE(addition, nullptr);
    EXPECT_EQ(shift->type, LUNA_X86_64_MACHINE_TYPE_U64);
    EXPECT_EQ(addition->type, LUNA_X86_64_MACHINE_TYPE_I64);
    EXPECT_EQ(luna_x86_64_machine_instruction_use_count(addition), 2U);
    EXPECT_EQ(luna_x86_64_machine_instruction_use(transform, addition, 0U),
              addition->left);
    EXPECT_EQ(luna_x86_64_machine_instruction_use(transform, addition, 1U),
              addition->right);
    LunaX8664MachineVirtualRegister definition = LUNA_X86_64_MACHINE_INVALID_ID;
    EXPECT_TRUE(
        luna_x86_64_machine_instruction_definition(addition, &definition));
    EXPECT_EQ(definition, addition->result);
}

TEST(X8664MachineIrTest, PrintsAStableTargetSpecificBoundary) {
    FrontendHarness harness{"module test.machine_print;\n"
                            "extern fn c_adjust(value: f64) -> f64;\n"
                            "fn main() -> i32 {\n"
                            "    return c_adjust(1.5) > 1.0 ? 42 : 1;\n"
                            "}\n"};

    ASSERT_TRUE(harness.EmitMachineIr()) << harness.Diagnostics();
    const std::string machine_ir = harness.MachineIr();
    EXPECT_NE(machine_ir.find("target-machine x86_64"), std::string::npos);
    EXPECT_NE(machine_ir.find("declare @f0 c_adjust linkage=external-c"),
              std::string::npos);
    EXPECT_NE(machine_ir.find("define @f1 test.machine_print::main"),
              std::string::npos);
    EXPECT_NE(machine_ir.find("class=fpr"), std::string::npos);
    EXPECT_NE(machine_ir.find("call type=f64"), std::string::npos);
    EXPECT_NE(machine_ir.find("compare.greater.float"), std::string::npos);
}

TEST(X8664MachineIrTest, PrintsAggregateSignatureAndResultSlotContracts) {
    FrontendHarness harness{
        "module test.machine_aggregate_print;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn echo(value: Pair) -> Pair { return value; }\n"
        "fn main() -> i32 {\n"
        "    let value: Pair = echo({ left = 20, right = 22, });\n"
        "    return value.left + value.right;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitMachineIr()) << harness.Diagnostics();
    const std::string machine_ir = harness.MachineIr();
    EXPECT_NE(
        machine_ir.find("(aggregate[8,4]:memory) -> aggregate[8,4]:memory"),
        std::string::npos);
    EXPECT_NE(
        machine_ir.find("stack $s0 type=void size=8 align=4 class=memory"),
        std::string::npos);
    EXPECT_NE(machine_ir.find("result-slot=$s"), std::string::npos);
}

TEST(X8664MachineIrTest, RejectsAMissingVirtualRegisterDefinition) {
    FrontendHarness harness{"module test.machine_verify;\n"
                            "fn main() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *constant = FindMachineInstruction(
        main_function, LUNA_X86_64_MACHINE_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    constant->result = LUNA_X86_64_MACHINE_INVALID_ID;

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsAnIntegerPseudoWithBooleanType) {
    FrontendHarness harness{"module test.machine_bool;\n"
                            "fn main() -> i32 { return true ? 42 : 1; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *constant =
        FindMachineInstruction(main_function, LUNA_X86_64_MACHINE_CONST_BOOL);
    ASSERT_NE(constant, nullptr);
    constant->opcode = LUNA_X86_64_MACHINE_CONST_INTEGER;

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsANarrowConstantOutsideItsMachineWidth) {
    FrontendHarness harness{"module test.machine_constant;\n"
                            "fn narrow() -> i8 { return 1; }\n"
                            "fn main() -> i32 { return narrow() as i32; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *narrow =
        FindMachineFunction(machine.Get(), "narrow");
    ASSERT_NE(narrow, nullptr);
    LunaX8664MachineInstruction *constant =
        FindMachineInstruction(narrow, LUNA_X86_64_MACHINE_CONST_INTEGER);
    ASSERT_NE(constant, nullptr);
    ASSERT_EQ(constant->type, LUNA_X86_64_MACHINE_TYPE_I8);
    constant->immediate = UINT64_C(256);

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsAUseBeforeItsDefinition) {
    FrontendHarness harness{"module test.machine_use;\n"
                            "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *addition =
        FindMachineInstruction(main_function, LUNA_X86_64_MACHINE_ADD_INTEGER);
    ASSERT_NE(addition, nullptr);
    addition->left = addition->result;

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsOverlappingCallArgumentStorage) {
    FrontendHarness harness{
        "module test.machine_arguments;\n"
        "fn add(left: i32, right: i32) -> i32 { return left + right; }\n"
        "fn main() -> i32 { return add(1, 2) + add(3, 4); }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *first_call =
        FindMachineInstructionAt(main_function, LUNA_X86_64_MACHINE_CALL, 0U);
    LunaX8664MachineInstruction *second_call =
        FindMachineInstructionAt(main_function, LUNA_X86_64_MACHINE_CALL, 1U);
    ASSERT_NE(first_call, nullptr);
    ASSERT_NE(second_call, nullptr);
    second_call->first_argument = first_call->first_argument;

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsCorruptedAggregateCallResultStorage) {
    FrontendHarness harness{
        "module test.machine_aggregate_result;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn make() -> Pair { return { left = 20, right = 22, }; }\n"
        "fn main() -> i32 {\n"
        "    let value: Pair = make(); return value.left + value.right;\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *call =
        FindMachineInstruction(main_function, LUNA_X86_64_MACHINE_CALL);
    ASSERT_NE(call, nullptr);
    call->slot = LUNA_X86_64_MACHINE_INVALID_ID;

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsCorruptedAggregateArgumentSnapshotStorage) {
    FrontendHarness harness{
        "module test.machine_aggregate_argument;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn sum(value: Pair) -> i32 { return value.left + value.right; }\n"
        "fn main() -> i32 {\n"
        "    var value: Pair = { left = 20, right = 22, };\n"
        "    return sum(value);\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *main_function =
        FindMachineFunction(machine.Get(), "main");
    ASSERT_NE(main_function, nullptr);
    LunaX8664MachineInstruction *call =
        FindMachineInstruction(main_function, LUNA_X86_64_MACHINE_CALL);
    ASSERT_NE(call, nullptr);
    const LunaX8664MachineVirtualRegister argument =
        luna_x86_64_machine_instruction_use(main_function, call, 0U);

    LunaX8664MachineInstruction *definition = nullptr;
    for (std::size_t block_index = 0U;
         definition == nullptr && block_index < main_function->blocks.length;
         block_index += 1U) {
        auto *block = static_cast<LunaX8664MachineBlock *>(
            luna_vector_at(&main_function->blocks, block_index));
        ASSERT_NE(block, nullptr);
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            auto *candidate = static_cast<LunaX8664MachineInstruction *>(
                luna_vector_at(&block->instructions, instruction_index));
            if (candidate != nullptr && candidate->result == argument) {
                definition = candidate;
                break;
            }
        }
    }
    ASSERT_NE(definition, nullptr);
    ASSERT_EQ(definition->opcode, LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT);
    auto *snapshot = static_cast<LunaX8664MachineStackSlot *>(
        luna_vector_at(&main_function->slots, definition->slot));
    ASSERT_NE(snapshot, nullptr);
    snapshot->size_bytes = UINT64_C(4);

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

TEST(X8664MachineIrTest, RejectsCorruptedAggregateReturnSnapshotStorage) {
    FrontendHarness harness{
        "module test.machine_aggregate_return;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn make() -> Pair { return { left = 20, right = 22, }; }\n"
        "fn main() -> i32 { return make().left + make().right; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();

    MachineModuleOwner machine{luna_target_info_default()};
    ASSERT_TRUE(luna_x86_64_machine_lower(
        harness.Module(), harness.DiagnosticEngine(), machine.Get()))
        << harness.Diagnostics();
    LunaX8664MachineFunction *make = FindMachineFunction(machine.Get(), "make");
    ASSERT_NE(make, nullptr);
    LunaX8664MachineInstruction *return_instruction =
        FindMachineInstruction(make, LUNA_X86_64_MACHINE_RETURN);
    ASSERT_NE(return_instruction, nullptr);

    LunaX8664MachineInstruction *definition = nullptr;
    for (std::size_t block_index = 0U;
         definition == nullptr && block_index < make->blocks.length;
         block_index += 1U) {
        auto *block = static_cast<LunaX8664MachineBlock *>(
            luna_vector_at(&make->blocks, block_index));
        ASSERT_NE(block, nullptr);
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            auto *candidate = static_cast<LunaX8664MachineInstruction *>(
                luna_vector_at(&block->instructions, instruction_index));
            if (candidate != nullptr &&
                candidate->result == return_instruction->left) {
                definition = candidate;
                break;
            }
        }
    }
    ASSERT_NE(definition, nullptr);
    ASSERT_EQ(definition->opcode, LUNA_X86_64_MACHINE_ADDRESS_OF_SLOT);
    auto *snapshot = static_cast<LunaX8664MachineStackSlot *>(
        luna_vector_at(&make->slots, definition->slot));
    ASSERT_NE(snapshot, nullptr);
    snapshot->alignment_bytes = UINT32_C(8);

    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(
        luna_x86_64_machine_verify(machine.Get(), diagnostic_file.get()));
}

}
}
