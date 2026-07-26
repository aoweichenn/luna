#ifndef LUNA_TEST_SUPPORT_HPP
#define LUNA_TEST_SUPPORT_HPP

#include "luna_c_api.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace luna::test {

class FrontendHarness final {
  public:
    explicit FrontendHarness(std::string_view source_text);
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
    [[nodiscard]] bool EmitAssembly();

    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::string Diagnostics() const;
    [[nodiscard]] std::string Assembly() const;
    [[nodiscard]] const LunaSourceFile *Source() const noexcept;
    [[nodiscard]] LunaDiagnosticEngine *DiagnosticEngine() noexcept;
    [[nodiscard]] LunaProgram *Program() noexcept;
    [[nodiscard]] LunaIrModule *Module() noexcept;

  private:
    LunaSourceFile source_{};
    LunaArena arena_{};
    std::FILE *diagnostic_file_{nullptr};
    LunaDiagnosticEngine diagnostics_{};
    LunaProgram *program_{nullptr};
    LunaIrModule module_{};
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
