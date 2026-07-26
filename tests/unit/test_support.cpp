#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_ARENA_BLOCK_SIZE = 32U * 1024U;
constexpr std::size_t LUNA_TEST_READ_BUFFER_SIZE = 4096U;

[[nodiscard]] std::string ReadStream(std::FILE *stream) {
    if (stream == nullptr || std::fflush(stream) != 0) {
        return {};
    }

    std::rewind(stream);
    std::string output;
    std::array<char, LUNA_TEST_READ_BUFFER_SIZE> buffer{};
    for (;;) {
        const std::size_t read_count =
            std::fread(buffer.data(), 1U, buffer.size(), stream);
        output.append(buffer.data(), read_count);
        if (read_count != buffer.size()) {
            break;
        }
    }

    std::clearerr(stream);
    static_cast<void>(std::fseek(stream, 0L, SEEK_END));
    return output;
}

}

FrontendHarness::FrontendHarness(std::string_view source_text) {
    luna_arena_init(&this->arena_, LUNA_TEST_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&this->module_);
    luna_string_builder_init(&this->assembly_);

    this->diagnostic_file_ = std::tmpfile();
    if (this->diagnostic_file_ == nullptr) {
        return;
    }

    luna_diagnostic_init(&this->diagnostics_, this->diagnostic_file_);
    this->ready_ = luna_source_from_bytes("<test>", source_text.data(),
                                          source_text.size(), &this->source_);
}

FrontendHarness::~FrontendHarness() {
    luna_string_builder_destroy(&this->assembly_);
    luna_ir_module_destroy(&this->module_);
    luna_arena_destroy(&this->arena_);
    luna_source_destroy(&this->source_);
    if (this->diagnostic_file_ != nullptr) {
        static_cast<void>(std::fclose(this->diagnostic_file_));
    }
}

bool FrontendHarness::IsReady() const noexcept {
    return this->ready_;
}

bool FrontendHarness::Parse() {
    if (this->parse_attempted_) {
        return this->parse_succeeded_;
    }
    this->parse_attempted_ = true;

    if (!this->ready_) {
        return false;
    }

    LunaParser parser{};
    luna_parser_init(&parser, &this->source_, &this->diagnostics_,
                     &this->arena_);
    this->program_ = luna_parser_parse_program(&parser);
    this->parse_succeeded_ =
        this->program_ != nullptr &&
        luna_diagnostic_error_count(&this->diagnostics_) == 0U;
    return this->parse_succeeded_;
}

bool FrontendHarness::Lower() {
    if (this->lower_attempted_) {
        return this->lower_succeeded_;
    }
    this->lower_attempted_ = true;

    if (!this->Parse()) {
        return false;
    }

    this->lower_succeeded_ =
        luna_sema_lower(this->program_, &this->diagnostics_, &this->module_) &&
        luna_diagnostic_error_count(&this->diagnostics_) == 0U;
    return this->lower_succeeded_;
}

bool FrontendHarness::ParseAndLower() {
    return this->Lower();
}

bool FrontendHarness::Verify() {
    return this->Lower() &&
           luna_ir_verify(&this->module_, this->diagnostic_file_);
}

bool FrontendHarness::EmitAssembly() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_assembly(&this->module_, &this->diagnostics_,
                                     &this->assembly_);
}

std::size_t FrontendHarness::ErrorCount() const noexcept {
    return luna_diagnostic_error_count(&this->diagnostics_);
}

std::string FrontendHarness::Diagnostics() const {
    return ReadStream(this->diagnostic_file_);
}

std::string FrontendHarness::Assembly() const {
    return std::string{luna_string_builder_data(&this->assembly_),
                       this->assembly_.length};
}

const LunaSourceFile *FrontendHarness::Source() const noexcept {
    return &this->source_;
}

LunaDiagnosticEngine *FrontendHarness::DiagnosticEngine() noexcept {
    return &this->diagnostics_;
}

LunaProgram *FrontendHarness::Program() noexcept {
    return this->program_;
}

LunaIrModule *FrontendHarness::Module() noexcept {
    return &this->module_;
}

LunaIrInstruction *FindInstruction(LunaIrFunction *function,
                                   LunaIrOpcode opcode) noexcept {
    if (function == nullptr) {
        return nullptr;
    }

    for (std::size_t block_index = 0U; block_index < function->blocks.length;
         block_index += 1U) {
        auto *block = static_cast<LunaIrBlock *>(
            luna_vector_at(&function->blocks, block_index));
        for (std::size_t instruction_index = 0U;
             instruction_index < block->instructions.length;
             instruction_index += 1U) {
            auto *instruction = static_cast<LunaIrInstruction *>(
                luna_vector_at(&block->instructions, instruction_index));
            if (instruction->opcode == opcode) {
                return instruction;
            }
        }
    }
    return nullptr;
}

}
