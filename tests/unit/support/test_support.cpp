#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_ARENA_BLOCK_SIZE = std::size_t{32U} * 1024U;
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

FrontendHarness::FrontendHarness(std::string_view source_text,
                                 const LunaTargetInfo *target) {
    luna_arena_init(&this->arena_, LUNA_TEST_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&this->module_, target);
    luna_string_builder_init(&this->machine_ir_);
    luna_string_builder_init(&this->abi_);
    luna_string_builder_init(&this->liveness_);
    luna_string_builder_init(&this->register_allocation_);
    luna_string_builder_init(&this->instruction_rewrite_);
    luna_string_builder_init(&this->assembly_);

    this->diagnostic_file_ = std::tmpfile();
    if (this->diagnostic_file_ == nullptr) {
        return;
    }

    luna_diagnostic_init(&this->diagnostics_, this->diagnostic_file_);
    this->ready_ = luna_source_from_bytes("<test>", source_text.data(),
                                          source_text.size(), &this->source_);
}

FrontendHarness::FrontendHarness(std::string_view interface_text,
                                 std::string_view implementation_text,
                                 const LunaTargetInfo *target)
    : FrontendHarness(implementation_text, target) {
    this->has_interface_ = true;
    this->ready_ =
        this->ready_ &&
        luna_source_from_bytes("<interface>", interface_text.data(),
                               interface_text.size(), &this->interface_source_);
}

FrontendHarness::~FrontendHarness() {
    luna_string_builder_destroy(&this->assembly_);
    luna_string_builder_destroy(&this->instruction_rewrite_);
    luna_string_builder_destroy(&this->register_allocation_);
    luna_string_builder_destroy(&this->liveness_);
    luna_string_builder_destroy(&this->abi_);
    luna_string_builder_destroy(&this->machine_ir_);
    luna_ir_module_destroy(&this->module_);
    luna_arena_destroy(&this->arena_);
    luna_source_destroy(&this->interface_source_);
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

    if (this->has_interface_) {
        LunaParser interface_parser{};
        luna_parser_init(&interface_parser, &this->interface_source_,
                         &this->diagnostics_, &this->arena_);
        this->interface_program_ = luna_parser_parse_program(&interface_parser);
    }

    LunaParser implementation_parser{};
    luna_parser_init(&implementation_parser, &this->source_,
                     &this->diagnostics_, &this->arena_);
    this->program_ = luna_parser_parse_program(&implementation_parser);
    this->parse_succeeded_ =
        this->program_ != nullptr &&
        (!this->has_interface_ || this->interface_program_ != nullptr) &&
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
        (this->has_interface_
             ? luna_sema_lower_module(this->interface_program_, this->program_,
                                      &this->diagnostics_, &this->module_)
             : luna_sema_lower(this->program_, &this->diagnostics_,
                               &this->module_)) &&
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

bool FrontendHarness::EmitMachineIr() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_machine_ir(&this->module_, &this->diagnostics_,
                                       &this->machine_ir_);
}

bool FrontendHarness::EmitAbi() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_abi(&this->module_, &this->diagnostics_,
                                &this->abi_);
}

bool FrontendHarness::EmitLiveness() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_liveness(&this->module_, &this->diagnostics_,
                                     &this->liveness_);
}

bool FrontendHarness::EmitRegisterAllocation() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_register_allocation(
        &this->module_, &this->diagnostics_, &this->register_allocation_);
}

bool FrontendHarness::EmitInstructionRewrite() {
    if (!this->Verify()) {
        return false;
    }

    return luna_x86_64_emit_instruction_rewrite(
        &this->module_, &this->diagnostics_, &this->instruction_rewrite_);
}

std::size_t FrontendHarness::ErrorCount() const noexcept {
    return luna_diagnostic_error_count(&this->diagnostics_);
}

std::string FrontendHarness::Diagnostics() const {
    return ReadStream(this->diagnostic_file_);
}

std::string FrontendHarness::MachineIr() const {
    return std::string{luna_string_builder_data(&this->machine_ir_),
                       this->machine_ir_.length};
}

std::string FrontendHarness::Abi() const {
    return std::string{luna_string_builder_data(&this->abi_),
                       this->abi_.length};
}

std::string FrontendHarness::Liveness() const {
    return std::string{luna_string_builder_data(&this->liveness_),
                       this->liveness_.length};
}

std::string FrontendHarness::RegisterAllocation() const {
    return std::string{luna_string_builder_data(&this->register_allocation_),
                       this->register_allocation_.length};
}

std::string FrontendHarness::InstructionRewrite() const {
    return std::string{luna_string_builder_data(&this->instruction_rewrite_),
                       this->instruction_rewrite_.length};
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

LunaProgram *FrontendHarness::InterfaceProgram() noexcept {
    return this->interface_program_;
}

LunaIrModule *FrontendHarness::Module() noexcept {
    return &this->module_;
}

CompilationHarness::CompilationHarness(
    std::initializer_list<std::string_view> source_texts,
    const LunaTargetInfo *target)
    : sources_(source_texts.size()) {
    luna_arena_init(&this->arena_, LUNA_TEST_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&this->module_, target);
    luna_string_builder_init(&this->machine_ir_);
    luna_string_builder_init(&this->abi_);
    luna_string_builder_init(&this->liveness_);
    luna_string_builder_init(&this->register_allocation_);
    luna_string_builder_init(&this->instruction_rewrite_);
    luna_string_builder_init(&this->assembly_);

    this->diagnostic_file_ = std::tmpfile();
    if (this->diagnostic_file_ == nullptr || source_texts.size() == 0U ||
        source_texts.size() > static_cast<std::size_t>(
                                  std::numeric_limits<std::uint32_t>::max())) {
        return;
    }

    luna_diagnostic_init(&this->diagnostics_, this->diagnostic_file_);
    this->ready_ = true;
    std::size_t source_index = 0U;
    for (const std::string_view source_text : source_texts) {
        if (!luna_source_from_bytes("<module-test>", source_text.data(),
                                    source_text.size(),
                                    &this->sources_[source_index])) {
            this->ready_ = false;
        }
        source_index += 1U;
    }
}

CompilationHarness::~CompilationHarness() {
    luna_string_builder_destroy(&this->assembly_);
    luna_string_builder_destroy(&this->instruction_rewrite_);
    luna_string_builder_destroy(&this->register_allocation_);
    luna_string_builder_destroy(&this->liveness_);
    luna_string_builder_destroy(&this->abi_);
    luna_string_builder_destroy(&this->machine_ir_);
    luna_ir_module_destroy(&this->module_);
    luna_arena_destroy(&this->arena_);
    for (LunaSourceFile &source : this->sources_) {
        luna_source_destroy(&source);
    }
    if (this->diagnostic_file_ != nullptr) {
        static_cast<void>(std::fclose(this->diagnostic_file_));
    }
}

bool CompilationHarness::IsReady() const noexcept {
    return this->ready_;
}

bool CompilationHarness::Parse() {
    if (this->parse_attempted_) {
        return this->parse_succeeded_;
    }
    this->parse_attempted_ = true;
    if (!this->ready_) {
        return false;
    }

    this->programs_.reserve(this->sources_.size());
    for (LunaSourceFile &source : this->sources_) {
        LunaParser parser{};
        luna_parser_init(&parser, &source, &this->diagnostics_, &this->arena_);
        LunaProgram *program = luna_parser_parse_program(&parser);
        this->programs_.push_back(program);
    }
    this->parse_succeeded_ =
        luna_diagnostic_error_count(&this->diagnostics_) == 0U;
    for (const LunaProgram *program : this->programs_) {
        if (program == nullptr) {
            this->parse_succeeded_ = false;
        }
    }
    return this->parse_succeeded_;
}

bool CompilationHarness::Lower() {
    if (this->lower_attempted_) {
        return this->lower_succeeded_;
    }
    this->lower_attempted_ = true;
    if (!this->Parse()) {
        return false;
    }

    this->lower_succeeded_ =
        luna_module_lower_programs(
            this->programs_.data(),
            static_cast<std::uint32_t>(this->programs_.size()),
            &this->diagnostics_, &this->module_) &&
        luna_diagnostic_error_count(&this->diagnostics_) == 0U;
    return this->lower_succeeded_;
}

bool CompilationHarness::ParseAndLower() {
    return this->Lower();
}

bool CompilationHarness::Verify() {
    return this->Lower() &&
           luna_ir_verify(&this->module_, this->diagnostic_file_);
}

bool CompilationHarness::EmitAssembly() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_assembly(&this->module_, &this->diagnostics_,
                                     &this->assembly_);
}

bool CompilationHarness::EmitMachineIr() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_machine_ir(&this->module_, &this->diagnostics_,
                                       &this->machine_ir_);
}

bool CompilationHarness::EmitAbi() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_abi(&this->module_, &this->diagnostics_,
                                &this->abi_);
}

bool CompilationHarness::EmitLiveness() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_liveness(&this->module_, &this->diagnostics_,
                                     &this->liveness_);
}

bool CompilationHarness::EmitRegisterAllocation() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_register_allocation(
        &this->module_, &this->diagnostics_, &this->register_allocation_);
}

bool CompilationHarness::EmitInstructionRewrite() {
    if (!this->Verify()) {
        return false;
    }
    return luna_x86_64_emit_instruction_rewrite(
        &this->module_, &this->diagnostics_, &this->instruction_rewrite_);
}

std::size_t CompilationHarness::ErrorCount() const noexcept {
    return luna_diagnostic_error_count(&this->diagnostics_);
}

std::string CompilationHarness::Diagnostics() const {
    return ReadStream(this->diagnostic_file_);
}

std::string CompilationHarness::MachineIr() const {
    return std::string{luna_string_builder_data(&this->machine_ir_),
                       this->machine_ir_.length};
}

std::string CompilationHarness::Abi() const {
    return std::string{luna_string_builder_data(&this->abi_),
                       this->abi_.length};
}

std::string CompilationHarness::Liveness() const {
    return std::string{luna_string_builder_data(&this->liveness_),
                       this->liveness_.length};
}

std::string CompilationHarness::RegisterAllocation() const {
    return std::string{luna_string_builder_data(&this->register_allocation_),
                       this->register_allocation_.length};
}

std::string CompilationHarness::InstructionRewrite() const {
    return std::string{luna_string_builder_data(&this->instruction_rewrite_),
                       this->instruction_rewrite_.length};
}

std::string CompilationHarness::Assembly() const {
    return std::string{luna_string_builder_data(&this->assembly_),
                       this->assembly_.length};
}

LunaIrModule *CompilationHarness::Module() noexcept {
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
