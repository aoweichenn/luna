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
constexpr std::size_t LUNA_FUZZ_MAX_ABI_COMPONENTS = 32U;
constexpr std::string_view LUNA_FUZZ_MODULE_SEPARATOR =
    "\n===LUNA_SOURCE_UNIT===\n";

[[nodiscard]] bool RunAggregateAbiClassification(const std::uint8_t *data,
                                                 std::size_t size) {
    if (size == 0U) {
        return true;
    }

    const std::uint64_t eightbyte_count =
        1U + static_cast<std::uint64_t>(data[0] % 4U);
    const std::uint64_t layout_size =
        eightbyte_count * LUNA_X86_64_ABI_EIGHTBYTE_SIZE;
    LunaX8664AbiAggregateLayout layout{};
    luna_x86_64_abi_aggregate_layout_init(&layout, layout_size,
                                          LUNA_X86_64_ABI_EIGHTBYTE_SIZE);

    std::size_t component_count = (size - 1U) / 2U;
    if (component_count > LUNA_FUZZ_MAX_ABI_COMPONENTS) {
        component_count = LUNA_FUZZ_MAX_ABI_COMPONENTS;
    }
    bool invariant_holds = true;
    if (component_count == 0U) {
        invariant_holds = luna_x86_64_abi_aggregate_layout_add_component(
            &layout, 0U, 1U, 1U, LUNA_X86_64_ABI_CLASS_INTEGER);
    }
    for (std::size_t index = 0U; invariant_holds && index < component_count;
         index += 1U) {
        const std::uint8_t shape = data[1U + (index * 2U)];
        const bool is_sse = (shape & 0x80U) != 0U;
        const std::uint32_t size_exponent =
            is_sse ? 2U + static_cast<std::uint32_t>(shape & 1U)
                   : static_cast<std::uint32_t>(shape & 3U);
        const std::uint64_t component_size = UINT64_C(1) << size_exponent;
        const std::uint64_t maximum_offset = layout_size - component_size;
        const std::uint64_t candidate =
            static_cast<std::uint64_t>(data[2U + (index * 2U)]) %
            (maximum_offset + 1U);
        const std::uint64_t component_offset =
            candidate & ~(component_size - 1U);
        const LunaX8664AbiClass abi_class =
            is_sse ? LUNA_X86_64_ABI_CLASS_SSE : LUNA_X86_64_ABI_CLASS_INTEGER;
        invariant_holds = luna_x86_64_abi_aggregate_layout_add_component(
            &layout, component_offset, component_size,
            static_cast<std::uint32_t>(component_size), abi_class);
    }

    LunaX8664AbiAggregateClassification classification{};
    invariant_holds =
        invariant_holds &&
        luna_x86_64_abi_classify_aggregate(&layout, &classification) &&
        luna_x86_64_abi_aggregate_classification_verify(&layout,
                                                        &classification);
    if (invariant_holds) {
        LunaX8664AbiAggregateClassification corrupted = classification;
        corrupted.eightbyte_count = 0U;
        invariant_holds = !luna_x86_64_abi_aggregate_classification_verify(
            &layout, &corrupted);
    }
    luna_x86_64_abi_aggregate_layout_destroy(&layout);
    return invariant_holds;
}

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
    LunaStringBuilder abi{};
    LunaStringBuilder liveness{};
    LunaStringBuilder register_allocation{};
    LunaStringBuilder instruction_rewrite{};
    LunaStringBuilder assembly{};
    LunaStringBuilder object{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&module, luna_target_info_default());
    luna_string_builder_init(&machine_ir);
    luna_string_builder_init(&abi);
    luna_string_builder_init(&liveness);
    luna_string_builder_init(&register_allocation);
    luna_string_builder_init(&instruction_rewrite);
    luna_string_builder_init(&assembly);
    luna_string_builder_init(&object);

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
                luna_x86_64_emit_abi(&module, &diagnostics, &abi) &&
                luna_x86_64_emit_liveness(&module, &diagnostics, &liveness) &&
                luna_x86_64_emit_register_allocation(&module, &diagnostics,
                                                     &register_allocation) &&
                luna_x86_64_emit_instruction_rewrite(&module, &diagnostics,
                                                     &instruction_rewrite) &&
                luna_x86_64_emit_assembly(&module, &diagnostics, &assembly) &&
                luna_x86_64_emit_object(&module, &diagnostics, &object) &&
                luna_x86_64_elf_object_verify(
                    LunaStringView{
                        .data = luna_string_builder_data(&object),
                        .length = object.length,
                    },
                    diagnostic_file);
        }
    }

    luna_string_builder_destroy(&object);
    luna_string_builder_destroy(&assembly);
    luna_string_builder_destroy(&instruction_rewrite);
    luna_string_builder_destroy(&register_allocation);
    luna_string_builder_destroy(&liveness);
    luna_string_builder_destroy(&abi);
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
    LunaStringBuilder abi{};
    LunaStringBuilder liveness{};
    LunaStringBuilder register_allocation{};
    LunaStringBuilder instruction_rewrite{};
    LunaStringBuilder assembly{};
    LunaStringBuilder object{};
    luna_arena_init(&arena, LUNA_FUZZ_ARENA_BLOCK_SIZE);
    luna_ir_module_init(&module, luna_target_info_default());
    luna_string_builder_init(&machine_ir);
    luna_string_builder_init(&abi);
    luna_string_builder_init(&liveness);
    luna_string_builder_init(&register_allocation);
    luna_string_builder_init(&instruction_rewrite);
    luna_string_builder_init(&assembly);
    luna_string_builder_init(&object);

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
                luna_x86_64_emit_abi(&module, &diagnostics, &abi) &&
                luna_x86_64_emit_liveness(&module, &diagnostics, &liveness) &&
                luna_x86_64_emit_register_allocation(&module, &diagnostics,
                                                     &register_allocation) &&
                luna_x86_64_emit_instruction_rewrite(&module, &diagnostics,
                                                     &instruction_rewrite) &&
                luna_x86_64_emit_assembly(&module, &diagnostics, &assembly) &&
                luna_x86_64_emit_object(&module, &diagnostics, &object) &&
                luna_x86_64_elf_object_verify(
                    LunaStringView{
                        .data = luna_string_builder_data(&object),
                        .length = object.length,
                    },
                    diagnostic_file);
            for (const LunaProgram *program : programs) {
                if (invariant_holds && program->is_interface) {
                    invariant_holds =
                        RoundTripMetadata(program, &diagnostics, &arena);
                    break;
                }
            }
        }
    }

    luna_string_builder_destroy(&object);
    luna_string_builder_destroy(&assembly);
    luna_string_builder_destroy(&instruction_rewrite);
    luna_string_builder_destroy(&register_allocation);
    luna_string_builder_destroy(&liveness);
    luna_string_builder_destroy(&abi);
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

[[nodiscard]] bool RunObjectAssembler(const std::uint8_t *data,
                                      std::size_t size) {
    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }

    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_file);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    const LunaStringView assembly = {
        .data = reinterpret_cast<const char *>(data),
        .length = size,
    };
    bool invariant_holds = true;
    if (luna_x86_64_assemble_elf_object(assembly, &diagnostics, &object)) {
        invariant_holds = luna_x86_64_elf_object_verify(
            LunaStringView{
                .data = luna_string_builder_data(&object),
                .length = object.length,
            },
            diagnostic_file);
        LunaStringBuilder executable{};
        luna_string_builder_init(&executable);
        const LunaX8664ElfLinkInput input = {
            .name = luna_string_view_from_c_string("<fuzz-object>"),
            .object =
                {
                    .data = luna_string_builder_data(&object),
                    .length = object.length,
                },
        };
        if (invariant_holds &&
            luna_x86_64_link_elf_executable(
                &input, 1U, luna_string_view_from_c_string("_start"),
                diagnostic_file, &executable)) {
            invariant_holds = luna_x86_64_elf_executable_verify(
                LunaStringView{
                    .data = luna_string_builder_data(&executable),
                    .length = executable.length,
                },
                diagnostic_file);
        }
        luna_string_builder_destroy(&executable);
    }

    luna_string_builder_destroy(&object);
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

[[nodiscard]] bool RunObjectLinker(const std::uint8_t *data, std::size_t size) {
    std::FILE *diagnostic_file = std::fopen("/dev/null", "wb");
    if (diagnostic_file == nullptr) {
        return true;
    }
    const LunaX8664ElfLinkInput input = {
        .name = luna_string_view_from_c_string("<fuzz-raw-object>"),
        .object =
            {
                .data = reinterpret_cast<const char *>(data),
                .length = size,
            },
    };
    LunaStringBuilder executable{};
    luna_string_builder_init(&executable);
    bool invariant_holds = true;
    if (luna_x86_64_link_elf_executable(
            &input, 1U, luna_string_view_from_c_string("_start"),
            diagnostic_file, &executable)) {
        invariant_holds = luna_x86_64_elf_executable_verify(
            LunaStringView{
                .data = luna_string_builder_data(&executable),
                .length = executable.length,
            },
            diagnostic_file);
    }
    luna_string_builder_destroy(&executable);
    static_cast<void>(std::fclose(diagnostic_file));
    return invariant_holds;
}

[[nodiscard]] bool RunSyscallAbiVerifier(const std::uint8_t *data,
                                         std::size_t size) {
    const LunaStringView object = {
        .data = reinterpret_cast<const char *>(data),
        .length = size,
    };
    const bool first =
        luna_x86_64_linux_syscall_abi_verify_object(object, nullptr);
    const bool second =
        luna_x86_64_linux_syscall_abi_verify_object(object, nullptr);
    return first == second;
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
    if (size <= LUNA_FUZZ_MAX_INPUT_SIZE &&
        (!RunAggregateAbiClassification(data, size) ||
         !RunFrontend(data, size) || !RunModuleCompilation(data, size) ||
         !RunMetadataDecoder(data, size) || !RunObjectAssembler(data, size) ||
         !RunObjectLinker(data, size) || !RunSyscallAbiVerifier(data, size))) {
        __builtin_trap();
    }
    return 0;
}
