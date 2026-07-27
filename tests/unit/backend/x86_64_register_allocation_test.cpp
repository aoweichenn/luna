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

constexpr std::uint32_t LUNA_TEST_REGISTER_PRESSURE_VALUE_COUNT = 24U;

struct FileCloser final {
    void operator()(std::FILE *file) const noexcept {
        if (file != nullptr) {
            static_cast<void>(std::fclose(file));
        }
    }
};

class RegisterAllocationOwner final {
  public:
    explicit RegisterAllocationOwner(const LunaTargetInfo *target) {
        luna_x86_64_machine_module_init(&this->machine_module_, target);
        luna_x86_64_liveness_init(&this->liveness_);
        luna_x86_64_register_allocation_init(&this->allocation_);
    }

    ~RegisterAllocationOwner() {
        luna_x86_64_register_allocation_destroy(&this->allocation_);
        luna_x86_64_liveness_destroy(&this->liveness_);
        luna_x86_64_machine_module_destroy(&this->machine_module_);
    }

    RegisterAllocationOwner(const RegisterAllocationOwner &) = delete;
    RegisterAllocationOwner &
    operator=(const RegisterAllocationOwner &) = delete;
    RegisterAllocationOwner(RegisterAllocationOwner &&) = delete;
    RegisterAllocationOwner &operator=(RegisterAllocationOwner &&) = delete;

    [[nodiscard]] bool Allocate(FrontendHarness &harness) {
        return luna_x86_64_machine_lower(harness.Module(),
                                         harness.DiagnosticEngine(),
                                         &this->machine_module_) &&
               luna_x86_64_liveness_analyze(&this->machine_module_,
                                            &this->liveness_, nullptr) &&
               luna_x86_64_register_allocate(&this->machine_module_,
                                             &this->liveness_,
                                             &this->allocation_, nullptr);
    }

    [[nodiscard]] LunaX8664MachineModule *MachineModule() noexcept {
        return &this->machine_module_;
    }

    [[nodiscard]] LunaX8664ModuleLiveness *Liveness() noexcept {
        return &this->liveness_;
    }

    [[nodiscard]] LunaX8664ModuleRegisterAllocation *Allocation() noexcept {
        return &this->allocation_;
    }

  private:
    LunaX8664MachineModule machine_module_{};
    LunaX8664ModuleLiveness liveness_{};
    LunaX8664ModuleRegisterAllocation allocation_{};
};

[[nodiscard]] std::uint32_t
FindFunctionIndex(const LunaX8664MachineModule *module, std::string_view name) {
    for (std::uint32_t function_index = 0U;
         function_index < module->functions.length; function_index += 1U) {
        const LunaX8664MachineFunction *function =
            static_cast<const LunaX8664MachineFunction *>(
                luna_vector_at_const(&module->functions, function_index));
        if (function != nullptr &&
            std::string_view{function->name.data, function->name.length} ==
                name) {
            return function_index;
        }
    }
    return LUNA_X86_64_MACHINE_INVALID_ID;
}

[[nodiscard]] LunaX8664FunctionRegisterAllocation *
FunctionAllocation(LunaX8664ModuleRegisterAllocation *allocation,
                   std::uint32_t function_index) {
    return static_cast<LunaX8664FunctionRegisterAllocation *>(
        luna_vector_at(&allocation->functions, function_index));
}

[[nodiscard]] const LunaX8664MachineType *
ValueType(const LunaX8664MachineFunction *function, std::uint32_t value_index) {
    return static_cast<const LunaX8664MachineType *>(
        luna_vector_at_const(&function->value_types, value_index));
}

TEST(X8664RegisterAllocationTest,
     DescribesTheSystemVRegisterClassesAndPreservation) {
    EXPECT_FALSE(luna_x86_64_physical_register_is_allocatable(
        LUNA_X86_64_PHYSICAL_REGISTER_RAX));
    EXPECT_FALSE(luna_x86_64_physical_register_is_allocatable(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM15));
    EXPECT_TRUE(luna_x86_64_physical_register_is_allocatable(
        LUNA_X86_64_PHYSICAL_REGISTER_RBX));
    EXPECT_TRUE(luna_x86_64_physical_register_is_allocatable(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM8));
    EXPECT_FALSE(luna_x86_64_physical_register_is_allocatable(
        LUNA_X86_64_PHYSICAL_REGISTER_INVALID));
    EXPECT_EQ(
        luna_x86_64_physical_register_class(LUNA_X86_64_PHYSICAL_REGISTER_R10),
        LUNA_X86_64_MACHINE_REGISTER_GENERAL);
    EXPECT_EQ(
        luna_x86_64_physical_register_class(LUNA_X86_64_PHYSICAL_REGISTER_XMM8),
        LUNA_X86_64_MACHINE_REGISTER_FLOAT);
    EXPECT_TRUE(luna_x86_64_physical_register_is_callee_saved(
        LUNA_X86_64_PHYSICAL_REGISTER_RBX));
    EXPECT_TRUE(luna_x86_64_physical_register_is_callee_saved(
        LUNA_X86_64_PHYSICAL_REGISTER_R15));
    EXPECT_FALSE(luna_x86_64_physical_register_is_callee_saved(
        LUNA_X86_64_PHYSICAL_REGISTER_R11));
    EXPECT_FALSE(luna_x86_64_physical_register_is_callee_saved(
        LUNA_X86_64_PHYSICAL_REGISTER_XMM15));
    EXPECT_STREQ(
        luna_x86_64_physical_register_name(LUNA_X86_64_PHYSICAL_REGISTER_XMM12),
        "xmm12");
}

TEST(X8664RegisterAllocationTest, AllocatesExactOverlappingIntervals) {
    FrontendHarness harness{"module test.allocation;\n"
                            "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    RegisterAllocationOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Allocate(harness)) << harness.Diagnostics();
    ASSERT_TRUE(luna_x86_64_register_allocation_verify(
        owner.MachineModule(), owner.Liveness(), owner.Allocation(), nullptr));

    LunaX8664FunctionRegisterAllocation *function =
        FunctionAllocation(owner.Allocation(), 0U);
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->intervals.length, 3U);
    ASSERT_EQ(function->allocations.length, 3U);
    EXPECT_EQ(function->instruction_count, 4U);
    EXPECT_EQ(function->spill_slot_count, 0U);

    constexpr std::uint64_t LUNA_TEST_EXPECTED_STARTS[] = {0U, 1U, 2U};
    constexpr std::uint64_t LUNA_TEST_EXPECTED_ENDS[] = {2U, 2U, 3U};
    constexpr LunaX8664PhysicalRegister LUNA_TEST_EXPECTED_REGISTERS[] = {
        LUNA_X86_64_PHYSICAL_REGISTER_RBX,
        LUNA_X86_64_PHYSICAL_REGISTER_R12,
        LUNA_X86_64_PHYSICAL_REGISTER_R13,
    };
    for (std::uint32_t value_index = 0U; value_index < 3U; value_index += 1U) {
        const LunaX8664LiveInterval *interval =
            static_cast<const LunaX8664LiveInterval *>(
                luna_vector_at_const(&function->intervals, value_index));
        const LunaX8664VirtualRegisterAllocation *location =
            static_cast<const LunaX8664VirtualRegisterAllocation *>(
                luna_vector_at_const(&function->allocations, value_index));
        ASSERT_NE(interval, nullptr);
        ASSERT_NE(location, nullptr);
        EXPECT_EQ(interval->start, LUNA_TEST_EXPECTED_STARTS[value_index]);
        EXPECT_EQ(interval->end, LUNA_TEST_EXPECTED_ENDS[value_index]);
        EXPECT_FALSE(interval->crosses_call);
        EXPECT_EQ(location->kind, LUNA_X86_64_ALLOCATION_REGISTER);
        EXPECT_EQ(location->physical_register,
                  LUNA_TEST_EXPECTED_REGISTERS[value_index]);
        EXPECT_EQ(location->spill_slot, LUNA_X86_64_MACHINE_INVALID_ID);
    }
}

TEST(X8664RegisterAllocationTest,
     PreservesGeneralValuesAndSpillsFloatValuesAcrossCalls) {
    FrontendHarness harness{
        "module test.allocation_calls;\n"
        "extern fn c_integer() -> i32;\n"
        "extern fn c_float() -> f64;\n"
        "fn integer_value() -> i32 { return 20 + c_integer(); }\n"
        "fn float_value() -> f64 { return 1.0 + c_float(); }\n"
        "fn main() -> i32 {\n"
        "    return integer_value() + (float_value() as i32);\n"
        "}\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    RegisterAllocationOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Allocate(harness)) << harness.Diagnostics();

    const std::uint32_t integer_function_index =
        FindFunctionIndex(owner.MachineModule(), "integer_value");
    const std::uint32_t float_function_index =
        FindFunctionIndex(owner.MachineModule(), "float_value");
    ASSERT_NE(integer_function_index, LUNA_X86_64_MACHINE_INVALID_ID);
    ASSERT_NE(float_function_index, LUNA_X86_64_MACHINE_INVALID_ID);

    bool found_cross_call_integer = false;
    bool found_cross_call_float = false;
    for (const std::uint32_t function_index :
         {integer_function_index, float_function_index}) {
        const LunaX8664MachineFunction *machine_function =
            luna_x86_64_machine_module_function_const(owner.MachineModule(),
                                                      function_index);
        LunaX8664FunctionRegisterAllocation *allocation =
            FunctionAllocation(owner.Allocation(), function_index);
        ASSERT_NE(machine_function, nullptr);
        ASSERT_NE(allocation, nullptr);
        for (std::uint32_t value_index = 0U;
             value_index < allocation->intervals.length; value_index += 1U) {
            const LunaX8664LiveInterval *interval =
                static_cast<const LunaX8664LiveInterval *>(
                    luna_vector_at_const(&allocation->intervals, value_index));
            const LunaX8664VirtualRegisterAllocation *location =
                static_cast<const LunaX8664VirtualRegisterAllocation *>(
                    luna_vector_at_const(&allocation->allocations,
                                         value_index));
            const LunaX8664MachineType *type =
                ValueType(machine_function, value_index);
            ASSERT_NE(interval, nullptr);
            ASSERT_NE(location, nullptr);
            ASSERT_NE(type, nullptr);
            if (!interval->crosses_call) {
                continue;
            }
            if (luna_x86_64_machine_type_is_float(*type)) {
                found_cross_call_float = true;
                EXPECT_EQ(location->kind, LUNA_X86_64_ALLOCATION_SPILL);
            } else {
                found_cross_call_integer = true;
                ASSERT_EQ(location->kind, LUNA_X86_64_ALLOCATION_REGISTER);
                EXPECT_TRUE(luna_x86_64_physical_register_is_callee_saved(
                    location->physical_register));
            }
        }
    }
    EXPECT_TRUE(found_cross_call_integer);
    EXPECT_TRUE(found_cross_call_float);
}

TEST(X8664RegisterAllocationTest,
     SpillsFurthestEndingIntervalsUnderRegisterPressure) {
    std::string expression =
        std::to_string(LUNA_TEST_REGISTER_PRESSURE_VALUE_COUNT);
    for (std::uint32_t value = LUNA_TEST_REGISTER_PRESSURE_VALUE_COUNT - 1U;
         value > 0U; value -= 1U) {
        expression = std::to_string(value) + " + (" + expression + ")";
    }
    const std::string source = "module test.allocation_pressure;\n"
                               "fn main() -> i32 { return " +
                               expression + "; }\n";
    FrontendHarness harness{std::string_view(source)};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    RegisterAllocationOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Allocate(harness)) << harness.Diagnostics();

    LunaX8664FunctionRegisterAllocation *function =
        FunctionAllocation(owner.Allocation(), 0U);
    ASSERT_NE(function, nullptr);
    EXPECT_GT(function->spill_slot_count, 0U);
    EXPECT_TRUE(luna_x86_64_register_allocation_verify(
        owner.MachineModule(), owner.Liveness(), owner.Allocation(), nullptr));

    bool spilled_long_interval = false;
    for (std::uint32_t value_index = 0U;
         value_index < LUNA_TEST_REGISTER_PRESSURE_VALUE_COUNT;
         value_index += 1U) {
        const LunaX8664LiveInterval *interval =
            static_cast<const LunaX8664LiveInterval *>(
                luna_vector_at_const(&function->intervals, value_index));
        const LunaX8664VirtualRegisterAllocation *location =
            static_cast<const LunaX8664VirtualRegisterAllocation *>(
                luna_vector_at_const(&function->allocations, value_index));
        ASSERT_NE(interval, nullptr);
        ASSERT_NE(location, nullptr);
        if (location->kind == LUNA_X86_64_ALLOCATION_SPILL &&
            interval->end - interval->start > 10U) {
            spilled_long_interval = true;
        }
    }
    EXPECT_TRUE(spilled_long_interval);
}

TEST(X8664RegisterAllocationTest,
     RejectsStaleIntervalsAndOverlappingPhysicalAssignments) {
    FrontendHarness harness{"module test.allocation_verify;\n"
                            "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    RegisterAllocationOwner owner{luna_target_info_default()};
    ASSERT_TRUE(owner.Allocate(harness)) << harness.Diagnostics();

    LunaX8664FunctionRegisterAllocation *function =
        FunctionAllocation(owner.Allocation(), 0U);
    ASSERT_NE(function, nullptr);
    LunaX8664LiveInterval *first_interval =
        static_cast<LunaX8664LiveInterval *>(
            luna_vector_at(&function->intervals, 0U));
    ASSERT_NE(first_interval, nullptr);
    first_interval->end += 1U;
    std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);
    EXPECT_FALSE(luna_x86_64_register_allocation_verify(
        owner.MachineModule(), owner.Liveness(), owner.Allocation(),
        diagnostic_file.get()));
    first_interval->end -= 1U;
    ASSERT_TRUE(luna_x86_64_register_allocation_verify(
        owner.MachineModule(), owner.Liveness(), owner.Allocation(), nullptr));

    LunaX8664VirtualRegisterAllocation *first_location =
        static_cast<LunaX8664VirtualRegisterAllocation *>(
            luna_vector_at(&function->allocations, 0U));
    LunaX8664VirtualRegisterAllocation *second_location =
        static_cast<LunaX8664VirtualRegisterAllocation *>(
            luna_vector_at(&function->allocations, 1U));
    ASSERT_NE(first_location, nullptr);
    ASSERT_NE(second_location, nullptr);
    second_location->physical_register = first_location->physical_register;
    EXPECT_FALSE(luna_x86_64_register_allocation_verify(
        owner.MachineModule(), owner.Liveness(), owner.Allocation(),
        diagnostic_file.get()));
}

TEST(X8664RegisterAllocationTest,
     PrintsDeterministicallyThroughTheCompilerFacade) {
    FrontendHarness first{"module test.allocation_print;\n"
                          "fn main() -> i32 { return 20 + 22; }\n"};
    FrontendHarness second{"module test.allocation_print;\n"
                           "fn main() -> i32 { return 20 + 22; }\n"};
    ASSERT_TRUE(first.EmitRegisterAllocation()) << first.Diagnostics();
    ASSERT_TRUE(second.EmitRegisterAllocation()) << second.Diagnostics();
    EXPECT_EQ(first.RegisterAllocation(), second.RegisterAllocation());
    EXPECT_NE(first.RegisterAllocation().find("x86-64-register-allocation"),
              std::string::npos);
    EXPECT_NE(first.RegisterAllocation().find(
                  "%v2 type=i32 class=gpr interval=[2, 3] "
                  "crosses-call=no location=%r13"),
              std::string::npos);
}

}
}
