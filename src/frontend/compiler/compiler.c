#include "luna/frontend/compiler/compiler.h"

#include "luna/backend/x86_64/x86_64.h"
#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/parser/parser.h"
#include "luna/frontend/source/source.h"
#include "luna/frontend/support/arena.h"
#include "luna/frontend/support/buffer.h"
#include "luna/middleend/ir/ir.h"
#include "luna/middleend/module/module.h"
#include "luna/middleend/sema/sema.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static const size_t luna_compiler_arena_block_size = (size_t)32U * 1024U;

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
        options->emit_kind > LUNA_EMIT_ASSEMBLY) {
        luna_diagnostic_error_plain(&diagnostics, "invalid output kind");
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
    LunaVector programs;
    luna_vector_init(&sources, sizeof(LunaSourceFile));
    luna_vector_init(&programs, sizeof(const LunaProgram *));

    bool success = true;
    for (uint32_t index = 0U; index < options->input_count; index += 1U) {
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
            LunaParser parser;
            luna_parser_init(&parser, source, &diagnostics, &arena);
            const LunaProgram *program = luna_parser_parse_program(&parser);
            if (!luna_vector_push(&programs, (const void *)&program)) {
                luna_diagnostic_error_plain(
                    &diagnostics,
                    "out of memory while recording parsed source units");
                success = false;
                break;
            }
        }
        success = success && luna_diagnostic_error_count(&diagnostics) == 0U;
    }

    LunaIrModule module;
    luna_ir_module_init(&module, options->target);

    if (success) {
        for (size_t index = 0U; index < programs.length; index += 1U) {
            const LunaProgram *const *program =
                (const LunaProgram *const *)luna_vector_at_const(&programs,
                                                                 index);
            if (*program == NULL) {
                success = false;
            }
        }
    }

    if (success) {
        success = luna_module_lower_programs(
            (const LunaProgram *const *)programs.data, options->input_count,
            &diagnostics, &module);
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
    } else if (success && options->emit_kind == LUNA_EMIT_ASSEMBLY) {
        success = luna_x86_64_emit_assembly(&module, &diagnostics, &output);
    }

    if (success && options->emit_kind != LUNA_EMIT_CHECK) {
        success = luna_compiler_write_output(options->output_path, &output,
                                             &diagnostics);
    }

    luna_string_builder_destroy(&output);
    luna_ir_module_destroy(&module);
    luna_arena_destroy(&arena);
    for (size_t index = 0U; index < sources.length; index += 1U) {
        LunaSourceFile *source = luna_vector_at(&sources, index);
        luna_source_destroy(source);
    }
    luna_vector_destroy(&programs);
    luna_vector_destroy(&sources);

    return success && luna_diagnostic_error_count(&diagnostics) == 0U ? 0 : 1;
}
