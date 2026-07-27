#include "luna/frontend/compiler/compiler.h"

#include "luna/backend/x86_64/x86_64.h"
#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/parser/parser.h"
#include "luna/frontend/source/source.h"
#include "luna/frontend/support/arena.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
#include "luna/middleend/ir/ir.h"
#include "luna/middleend/module/metadata.h"
#include "luna/middleend/module/module.h"
#include "luna/middleend/sema/sema.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const size_t luna_compiler_arena_block_size = (size_t)32U * 1024U;

static bool luna_compiler_is_metadata_path(const char *path) {
    static const char suffix[] = ".lmi";
    const size_t path_length = strlen(path);
    const size_t suffix_length = sizeof(suffix) - 1U;
    return path_length >= suffix_length &&
           memcmp(path + path_length - suffix_length, suffix, suffix_length) ==
               0;
}

static const LunaProgram *
luna_compiler_find_interface(const LunaModuleInput *inputs,
                             uint32_t input_count, LunaStringView module_name) {
    for (uint32_t index = 0U; index < input_count; index += 1U) {
        const LunaProgram *program = inputs[index].program;
        if (program != NULL && program->is_interface &&
            luna_string_view_equal(program->module_name, module_name)) {
            return program;
        }
    }
    return NULL;
}

static bool luna_compiler_collect_metadata_dependencies(
    const LunaProgram *interface_unit, const LunaModuleInput *inputs,
    uint32_t input_count, LunaDiagnosticEngine *diagnostics,
    LunaVector *dependencies) {
    for (const LunaImport *import = interface_unit->first_import;
         import != NULL; import = import->next) {
        const LunaModuleInput *matched = NULL;
        for (uint32_t index = 0U; index < input_count; index += 1U) {
            const LunaProgram *program = inputs[index].program;
            if (inputs[index].is_metadata && program != NULL &&
                luna_string_view_equal(program->module_name,
                                       import->module_name)) {
                matched = &inputs[index];
                break;
            }
        }
        if (matched == NULL) {
            luna_diagnostic_error(
                diagnostics, import->span,
                "cannot emit metadata without compiled dependency '%.*s'",
                (int)import->module_name.length, import->module_name.data);
            return false;
        }

        const LunaModuleMetadataDependency dependency = {
            .module_name = import->module_name,
            .content_hash = matched->metadata_content_hash,
        };
        if (!luna_vector_push(dependencies, &dependency)) {
            luna_diagnostic_error_plain(
                diagnostics,
                "out of memory while recording metadata dependencies");
            return false;
        }
    }
    return true;
}

static bool luna_compiler_write_output(const char *path,
                                       const LunaStringBuilder *output,
                                       LunaDiagnosticEngine *diagnostics) {
    FILE *file = stdout;
    const bool use_stdout = path != NULL && path[0] == '-' && path[1] == '\0';

    if (!use_stdout) {
        file = fopen(path, "wb");
        if (file == NULL) {
            luna_diagnostic_error_plain(diagnostics,
                                        "cannot open output file '%s'", path);
            return false;
        }
    }

    const size_t written =
        fwrite(luna_string_builder_data(output), 1U, output->length, file);
    bool success = written == output->length;

    if (!use_stdout && fclose(file) != 0) {
        success = false;
    }

    if (!success) {
        luna_diagnostic_error_plain(diagnostics,
                                    "failed to write output file '%s'", path);
    }
    return success;
}

int luna_compile(const LunaCompilerOptions *options, FILE *diagnostic_stream) {
    LunaDiagnosticEngine diagnostics;
    luna_diagnostic_init(
        &diagnostics, diagnostic_stream == NULL ? stderr : diagnostic_stream);

    if (options == NULL) {
        luna_diagnostic_error_plain(&diagnostics,
                                    "compiler options must not be null");
        return 1;
    }

    if (options->input_paths == NULL || options->input_count == 0U) {
        luna_diagnostic_error_plain(&diagnostics,
                                    "compiler requires source units");
        return 1;
    }
    for (uint32_t index = 0U; index < options->input_count; index += 1U) {
        if (options->input_paths[index] == NULL) {
            luna_diagnostic_error_plain(&diagnostics,
                                        "input path must not be null");
            return 1;
        }
    }

    if (options->emit_kind < LUNA_EMIT_CHECK ||
        options->emit_kind > LUNA_EMIT_METADATA) {
        luna_diagnostic_error_plain(&diagnostics, "invalid output kind");
        return 1;
    }
    if (options->emit_kind == LUNA_EMIT_METADATA &&
        (options->separate_module_name == NULL ||
         options->separate_module_name[0] == '\0')) {
        luna_diagnostic_error_plain(
            &diagnostics, "metadata emission requires --compile-module");
        return 1;
    }

    if (!luna_target_info_is_supported(options->target)) {
        luna_diagnostic_error_plain(
            &diagnostics, "compiler target is missing or unsupported");
        return 1;
    }

    if (options->emit_kind != LUNA_EMIT_CHECK && options->output_path == NULL) {
        luna_diagnostic_error_plain(&diagnostics,
                                    "output path must not be null");
        return 1;
    }

    LunaArena arena;
    luna_arena_init(&arena, luna_compiler_arena_block_size);

    LunaVector sources;
    LunaVector inputs;
    LunaVector metadata_records;
    luna_vector_init(&sources, sizeof(LunaSourceFile));
    luna_vector_init(&inputs, sizeof(LunaModuleInput));
    luna_vector_init(&metadata_records, sizeof(LunaModuleMetadata));

    bool success =
        luna_vector_reserve(&sources, (size_t)options->input_count) &&
        luna_vector_reserve(&inputs, (size_t)options->input_count) &&
        luna_vector_reserve(&metadata_records, (size_t)options->input_count);
    if (!success) {
        luna_diagnostic_error_plain(
            &diagnostics, "out of memory while preparing compiler inputs");
    }
    for (uint32_t index = 0U; index < options->input_count; index += 1U) {
        if (!success) {
            break;
        }
        const LunaSourceFile empty_source = {0};
        if (!luna_vector_push(&sources, &empty_source)) {
            luna_diagnostic_error_plain(
                &diagnostics, "out of memory while loading source units");
            success = false;
            break;
        }

        LunaSourceFile *source = luna_vector_at(&sources, sources.length - 1U);
        if (!luna_source_load(options->input_paths[index], source)) {
            luna_diagnostic_error_plain(&diagnostics,
                                        "cannot read input file '%s'",
                                        options->input_paths[index]);
            success = false;
            break;
        }
    }

    if (success) {
        for (uint32_t index = 0U; index < options->input_count; index += 1U) {
            const LunaSourceFile *source =
                luna_vector_at_const(&sources, (size_t)index);
            LunaModuleInput input = {
                .is_metadata =
                    luna_compiler_is_metadata_path(options->input_paths[index]),
            };
            if (input.is_metadata) {
                const LunaModuleMetadata empty_metadata = {0};
                if (!luna_vector_push(&metadata_records, &empty_metadata)) {
                    luna_diagnostic_error_plain(
                        &diagnostics,
                        "out of memory while recording module metadata");
                    success = false;
                    break;
                }
                LunaModuleMetadata *metadata = luna_vector_at(
                    &metadata_records, metadata_records.length - 1U);
                if (!luna_module_metadata_decode(source, options->target,
                                                 &arena, &diagnostics,
                                                 metadata)) {
                    success = false;
                    break;
                }
                input.program = metadata->interface_unit;
                input.metadata_dependencies =
                    (const LunaModuleMetadataDependency *)
                        metadata->dependencies.data;
                input.metadata_dependency_count =
                    (uint32_t)metadata->dependencies.length;
                input.metadata_content_hash = metadata->content_hash;
            } else {
                LunaParser parser;
                luna_parser_init(&parser, source, &diagnostics, &arena);
                input.program = luna_parser_parse_program(&parser);
            }

            if (!luna_vector_push(&inputs, &input)) {
                luna_diagnostic_error_plain(
                    &diagnostics,
                    "out of memory while recording compiler inputs");
                success = false;
                break;
            }
        }
        success = success && luna_diagnostic_error_count(&diagnostics) == 0U;
    }

    LunaIrModule module;
    luna_ir_module_init(&module, options->target);

    if (success) {
        for (size_t index = 0U; index < inputs.length; index += 1U) {
            const LunaModuleInput *input = luna_vector_at_const(&inputs, index);
            if (input->program == NULL) {
                success = false;
            }
        }
    }

    if (success) {
        const LunaModuleOptions module_options = {
            .compilation_kind = options->separate_module_name == NULL
                                    ? LUNA_MODULE_COMPILE_EXECUTABLE
                                    : LUNA_MODULE_COMPILE_SEPARATE,
            .root_module_name = options->separate_module_name == NULL
                                    ? luna_string_view(NULL, 0U)
                                    : luna_string_view_from_c_string(
                                          options->separate_module_name),
            .require_compiled_root_interface =
                options->separate_module_name != NULL &&
                (options->emit_kind == LUNA_EMIT_IR ||
                 options->emit_kind == LUNA_EMIT_MACHINE_IR ||
                 options->emit_kind == LUNA_EMIT_LIVENESS ||
                 options->emit_kind == LUNA_EMIT_ASSEMBLY),
        };
        success = luna_module_lower_inputs(
            (const LunaModuleInput *)inputs.data, options->input_count,
            &module_options, &diagnostics, &module);
    }

    if (success && !luna_ir_verify(&module, diagnostics.stream)) {
        luna_diagnostic_error_plain(&diagnostics,
                                    "internal IR verification failed");
        success = false;
    }

    LunaStringBuilder output;
    luna_string_builder_init(&output);

    if (success && options->emit_kind == LUNA_EMIT_IR) {
        success = luna_ir_print(&module, &output);
        if (!success) {
            luna_diagnostic_error_plain(&diagnostics,
                                        "out of memory while printing IR");
        }
    } else if (success && options->emit_kind == LUNA_EMIT_MACHINE_IR) {
        success = luna_x86_64_emit_machine_ir(&module, &diagnostics, &output);
    } else if (success && options->emit_kind == LUNA_EMIT_LIVENESS) {
        success = luna_x86_64_emit_liveness(&module, &diagnostics, &output);
    } else if (success && options->emit_kind == LUNA_EMIT_ASSEMBLY) {
        success = luna_x86_64_emit_assembly(&module, &diagnostics, &output);
    } else if (success && options->emit_kind == LUNA_EMIT_METADATA) {
        const LunaStringView root_name =
            luna_string_view_from_c_string(options->separate_module_name);
        const LunaProgram *interface_unit =
            luna_compiler_find_interface((const LunaModuleInput *)inputs.data,
                                         options->input_count, root_name);
        if (interface_unit == NULL) {
            luna_diagnostic_error_plain(
                &diagnostics,
                "internal error: separate compilation root has no "
                "interface");
            success = false;
        } else {
            LunaVector dependencies;
            luna_vector_init(&dependencies,
                             sizeof(LunaModuleMetadataDependency));
            success = luna_compiler_collect_metadata_dependencies(
                interface_unit, (const LunaModuleInput *)inputs.data,
                options->input_count, &diagnostics, &dependencies);
            if (success) {
                success = luna_module_metadata_encode(
                    interface_unit, options->target,
                    (const LunaModuleMetadataDependency *)dependencies.data,
                    (uint32_t)dependencies.length, &diagnostics, &output);
            }
            luna_vector_destroy(&dependencies);
        }
    }

    if (success && options->emit_kind != LUNA_EMIT_CHECK) {
        success = luna_compiler_write_output(options->output_path, &output,
                                             &diagnostics);
    }

    luna_string_builder_destroy(&output);
    luna_ir_module_destroy(&module);
    for (size_t index = 0U; index < metadata_records.length; index += 1U) {
        LunaModuleMetadata *metadata = luna_vector_at(&metadata_records, index);
        luna_module_metadata_destroy(metadata);
    }
    for (size_t index = 0U; index < sources.length; index += 1U) {
        LunaSourceFile *source = luna_vector_at(&sources, index);
        luna_source_destroy(source);
    }
    luna_arena_destroy(&arena);
    luna_vector_destroy(&metadata_records);
    luna_vector_destroy(&inputs);
    luna_vector_destroy(&sources);

    return success && luna_diagnostic_error_count(&diagnostics) == 0U ? 0 : 1;
}
