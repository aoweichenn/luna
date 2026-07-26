#include "luna_c_api.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

constexpr std::size_t LUNA_FUZZ_MAX_INPUT_SIZE = 64U * 1024U;
constexpr std::size_t LUNA_FUZZ_ARENA_BLOCK_SIZE = 32U * 1024U;

[[nodiscard]] bool RunFrontend(const std::uint8_t *data, std::size_t size) {
    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }

    LunaSourceFile source{};
    LunaArena arena{};
    LunaIrModule module{};
    LunaStringBuilder assembly{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&module);
    luna_string_builder_init(&assembly);

    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_file);

    bool invariant_holds = true;
    if (luna_source_from_bytes("<fuzz>", reinterpret_cast<const char *>(data),
                               size, &source)) {
        LunaParser parser{};
        luna_parser_init(&parser, &source, &diagnostics, &arena);
        LunaProgram *program = luna_parser_parse_program(&parser);
        if (program != nullptr &&
            luna_diagnostic_error_count(&diagnostics) == 0U &&
            luna_sema_lower(program, &diagnostics, &module) &&
            luna_diagnostic_error_count(&diagnostics) == 0U) {
            invariant_holds =
                luna_ir_verify(&module, diagnostic_file) &&
                luna_x86_64_emit_assembly(&module, &diagnostics, &assembly);
        }
    }

    luna_string_builder_destroy(&assembly);
    luna_ir_module_destroy(&module);
    luna_arena_destroy(&arena);
    luna_source_destroy(&source);
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
    if (size <= LUNA_FUZZ_MAX_INPUT_SIZE && !RunFrontend(data, size)) {
        __builtin_trap();
    }
    return 0;
}
