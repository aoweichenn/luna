#ifndef LUNA_TEST_SUPPORT_HPP
#define LUNA_TEST_SUPPORT_HPP

#include "luna_c_api.hpp"

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace luna::test {

class FrontendHarness final {
  public:
    explicit FrontendHarness(
        std::string_view source_text,
        const LunaTargetInfo *target = luna_target_info_default());
    FrontendHarness(std::string_view interface_text,
                    std::string_view implementation_text,
                    const LunaTargetInfo *target = luna_target_info_default());
    ~FrontendHarness();

    FrontendHarness(const FrontendHarness &) = delete;
    FrontendHarness &operator=(const FrontendHarness &) = delete;
    FrontendHarness(FrontendHarness &&) = delete;
    FrontendHarness &operator=(FrontendHarness &&) = delete;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool Parse();
    [[nodiscard]] bool Lower();
    [[nodiscard]] bool ParseAndLower();
    [[nodiscard]] bool Verify();
    [[nodiscard]] bool EmitMachineIr();
    [[nodiscard]] bool EmitAbi();
    [[nodiscard]] bool EmitLiveness();
    [[nodiscard]] bool EmitRegisterAllocation();
    [[nodiscard]] bool EmitInstructionRewrite();
    [[nodiscard]] bool EmitAssembly();

    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::string Diagnostics() const;
    [[nodiscard]] std::string MachineIr() const;
    [[nodiscard]] std::string Abi() const;
    [[nodiscard]] std::string Liveness() const;
    [[nodiscard]] std::string RegisterAllocation() const;
    [[nodiscard]] std::string InstructionRewrite() const;
    [[nodiscard]] std::string Assembly() const;
    [[nodiscard]] const LunaSourceFile *Source() const noexcept;
    [[nodiscard]] LunaDiagnosticEngine *DiagnosticEngine() noexcept;
    [[nodiscard]] LunaProgram *Program() noexcept;
    [[nodiscard]] LunaProgram *InterfaceProgram() noexcept;
    [[nodiscard]] LunaIrModule *Module() noexcept;

  private:
    LunaSourceFile source_{};
    LunaSourceFile interface_source_{};
    LunaArena arena_{};
    std::FILE *diagnostic_file_{nullptr};
    LunaDiagnosticEngine diagnostics_{};
    LunaProgram *program_{nullptr};
    LunaProgram *interface_program_{nullptr};
    LunaIrModule module_{};
    LunaStringBuilder machine_ir_{};
    LunaStringBuilder abi_{};
    LunaStringBuilder liveness_{};
    LunaStringBuilder register_allocation_{};
    LunaStringBuilder instruction_rewrite_{};
    LunaStringBuilder assembly_{};
    bool ready_{false};
    bool has_interface_{false};
    bool parse_attempted_{false};
    bool parse_succeeded_{false};
    bool lower_attempted_{false};
    bool lower_succeeded_{false};
};

class CompilationHarness final {
  public:
    explicit CompilationHarness(
        std::initializer_list<std::string_view> source_texts,
        const LunaTargetInfo *target = luna_target_info_default());
    ~CompilationHarness();

    CompilationHarness(const CompilationHarness &) = delete;
    CompilationHarness &operator=(const CompilationHarness &) = delete;
    CompilationHarness(CompilationHarness &&) = delete;
    CompilationHarness &operator=(CompilationHarness &&) = delete;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool Parse();
    [[nodiscard]] bool Lower();
    [[nodiscard]] bool ParseAndLower();
    [[nodiscard]] bool Verify();
    [[nodiscard]] bool EmitMachineIr();
    [[nodiscard]] bool EmitAbi();
    [[nodiscard]] bool EmitLiveness();
    [[nodiscard]] bool EmitRegisterAllocation();
    [[nodiscard]] bool EmitInstructionRewrite();
    [[nodiscard]] bool EmitAssembly();
    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::string Diagnostics() const;
    [[nodiscard]] std::string MachineIr() const;
    [[nodiscard]] std::string Abi() const;
    [[nodiscard]] std::string Liveness() const;
    [[nodiscard]] std::string RegisterAllocation() const;
    [[nodiscard]] std::string InstructionRewrite() const;
    [[nodiscard]] std::string Assembly() const;
    [[nodiscard]] LunaIrModule *Module() noexcept;

  private:
    std::vector<LunaSourceFile> sources_;
    std::vector<const LunaProgram *> programs_;
    LunaArena arena_{};
    std::FILE *diagnostic_file_{nullptr};
    LunaDiagnosticEngine diagnostics_{};
    LunaIrModule module_{};
    LunaStringBuilder machine_ir_{};
    LunaStringBuilder abi_{};
    LunaStringBuilder liveness_{};
    LunaStringBuilder register_allocation_{};
    LunaStringBuilder instruction_rewrite_{};
    LunaStringBuilder assembly_{};
    bool ready_{false};
    bool parse_attempted_{false};
    bool parse_succeeded_{false};
    bool lower_attempted_{false};
    bool lower_succeeded_{false};
};

[[nodiscard]] LunaIrInstruction *FindInstruction(LunaIrFunction *function,
                                                 LunaIrOpcode opcode) noexcept;

}

#endif
