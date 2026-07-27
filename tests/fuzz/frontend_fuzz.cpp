#include "luna_c_api.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t LUNA_FUZZ_MAX_INPUT_SIZE = std::size_t{64U} * 1024U;
constexpr std::size_t LUNA_FUZZ_ARENA_BLOCK_SIZE = std::size_t{32U} * 1024U;
constexpr std::size_t LUNA_FUZZ_MAX_SOURCE_UNITS = 16U;
constexpr std::string_view LUNA_FUZZ_MODULE_SEPARATOR =
    "\n===LUNA_SOURCE_UNIT===\n";

[[nodiscard]] bool RoundTripMetadata(const LunaProgram *interface_unit,
                                     LunaDiagnosticEngine *diagnostics,
                                     LunaArena *arena) {
    std::vector<LunaModuleMetadataDependency> dependencies;
    for (const LunaImport *import = interface_unit->first_import;
         import != nullptr; import = import->next) {
        dependencies.push_back(LunaModuleMetadataDependency{
            .module_name = import->module_name,
            .content_hash =
                static_cast<std::uint64_t>(dependencies.size()) + 1U,
        });
    }
    LunaStringBuilder encoded{};
    luna_string_builder_init(&encoded);
    bool invariant_holds = luna_module_metadata_encode(
        interface_unit, luna_target_info_default(), dependencies.data(),
        static_cast<std::uint32_t>(dependencies.size()), diagnostics, &encoded);

    LunaSourceFile source{};
    LunaModuleMetadata metadata{};
    if (invariant_holds) {
        invariant_holds =
            luna_source_from_bytes("<fuzz-generated.lmi>",
                                   luna_string_builder_data(&encoded),
                                   encoded.length, &source) &&
            luna_module_metadata_decode(&source, luna_target_info_default(),
                                        arena, diagnostics, &metadata);
    }

    luna_module_metadata_destroy(&metadata);
    luna_source_destroy(&source);
    luna_string_builder_destroy(&encoded);
    return invariant_holds;
}

[[nodiscard]] bool RunFrontend(const std::uint8_t *data, std::size_t size) {
    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }

    LunaSourceFile source{};
    LunaArena arena{};
    LunaIrModule module{};
    LunaStringBuilder machine_ir{};
    LunaStringBuilder assembly{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&module, luna_target_info_default());
    luna_string_builder_init(&machine_ir);
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
                luna_x86_64_emit_machine_ir(&module, &diagnostics,
                                            &machine_ir) &&
                luna_x86_64_emit_assembly(&module, &diagnostics, &assembly);
        }
    }

    luna_string_builder_destroy(&assembly);
    luna_string_builder_destroy(&machine_ir);
    luna_ir_module_destroy(&module);
    luna_arena_destroy(&arena);
    luna_source_destroy(&source);
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

[[nodiscard]] bool RunModuleCompilation(const std::uint8_t *data,
                                        std::size_t size) {
    const std::string_view input{reinterpret_cast<const char *>(data), size};
    if (input.find(LUNA_FUZZ_MODULE_SEPARATOR) == std::string_view::npos) {
        return true;
    }

    std::vector<std::string_view> source_units;
    std::size_t source_offset = 0U;
    for (;;) {
        const std::size_t separator_offset =
            input.find(LUNA_FUZZ_MODULE_SEPARATOR, source_offset);
        if (separator_offset == std::string_view::npos) {
            source_units.push_back(input.substr(source_offset));
            break;
        }
        source_units.push_back(
            input.substr(source_offset, separator_offset - source_offset));
        if (source_units.size() >= LUNA_FUZZ_MAX_SOURCE_UNITS) {
            return true;
        }
        source_offset = separator_offset + LUNA_FUZZ_MODULE_SEPARATOR.size();
    }

    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }

    std::vector<LunaSourceFile> sources(source_units.size());
    std::vector<const LunaProgram *> programs;
    programs.reserve(source_units.size());
    LunaArena arena{};
    LunaIrModule module{};
    LunaStringBuilder machine_ir{};
    LunaStringBuilder assembly{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&module, luna_target_info_default());
    luna_string_builder_init(&machine_ir);
    luna_string_builder_init(&assembly);

    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_file);

    bool invariant_holds = true;
    bool sources_ready = true;
    for (std::size_t index = 0U; index < source_units.size(); index += 1U) {
        const std::string_view source_unit = source_units[index];
        if (!luna_source_from_bytes("<fuzz-module>", source_unit.data(),
                                    source_unit.size(), &sources[index])) {
            sources_ready = false;
        }
    }
    if (sources_ready) {
        for (LunaSourceFile &source : sources) {
            LunaParser parser{};
            luna_parser_init(&parser, &source, &diagnostics, &arena);
            programs.push_back(luna_parser_parse_program(&parser));
        }

        bool programs_ready = luna_diagnostic_error_count(&diagnostics) == 0U;
        for (const LunaProgram *program : programs) {
            if (program == nullptr) {
                programs_ready = false;
            }
        }
        if (programs_ready &&
            luna_module_lower_programs(
                programs.data(), static_cast<std::uint32_t>(programs.size()),
                &diagnostics, &module) &&
            luna_diagnostic_error_count(&diagnostics) == 0U) {
            invariant_holds =
                luna_ir_verify(&module, diagnostic_file) &&
                luna_x86_64_emit_machine_ir(&module, &diagnostics,
                                            &machine_ir) &&
                luna_x86_64_emit_assembly(&module, &diagnostics, &assembly);
            for (const LunaProgram *program : programs) {
                if (invariant_holds && program->is_interface) {
                    invariant_holds =
                        RoundTripMetadata(program, &diagnostics, &arena);
                    break;
                }
            }
        }
    }

    luna_string_builder_destroy(&assembly);
    luna_string_builder_destroy(&machine_ir);
    luna_ir_module_destroy(&module);
    luna_arena_destroy(&arena);
    for (LunaSourceFile &source : sources) {
        luna_source_destroy(&source);
    }
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

[[nodiscard]] bool RunMetadataDecoder(const std::uint8_t *data,
                                      std::size_t size) {
    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }

    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_file);
    LunaArena arena{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    LunaSourceFile source{};
    LunaModuleMetadata metadata{};
    bool invariant_holds = true;
    if (luna_source_from_bytes("<fuzz.lmi>",
                               reinterpret_cast<const char *>(data), size,
                               &source) &&
        luna_module_metadata_decode(&source, luna_target_info_default(), &arena,
                                    &diagnostics, &metadata)) {
        LunaStringBuilder encoded{};
        luna_string_builder_init(&encoded);
        invariant_holds = luna_module_metadata_encode(
            metadata.interface_unit, luna_target_info_default(),
            reinterpret_cast<const LunaModuleMetadataDependency *>(
                metadata.dependencies.data),
            static_cast<std::uint32_t>(metadata.dependencies.length),
            &diagnostics, &encoded);
        luna_string_builder_destroy(&encoded);
    }

    luna_module_metadata_destroy(&metadata);
    luna_source_destroy(&source);
    luna_arena_destroy(&arena);
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
    if (size <= LUNA_FUZZ_MAX_INPUT_SIZE &&
        (!RunFrontend(data, size) || !RunModuleCompilation(data, size) ||
         !RunMetadataDecoder(data, size))) {
        __builtin_trap();
    }
    return 0;
}
