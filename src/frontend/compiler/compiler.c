#include "luna/frontend/compiler/compiler.h"

#include "luna/backend/x86_64/x86_64.h"
#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/parser/parser.h"
#include "luna/frontend/source/source.h"
#include "luna/frontend/support/arena.h"
#include "luna/frontend/support/buffer.h"
#include "luna/middleend/ir/ir.h"
#include "luna/middleend/sema/sema.h"

#include <stdbool.h>
#include <stdio.h>

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

    if (options->input_path == NULL) {
        luna_diagnostic_error_plain(&diagnostics,
                                    "input path must not be null");
        return 1;
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

    LunaSourceFile source;
    if (!luna_source_load(options->input_path, &source)) {
        luna_diagnostic_error_plain(&diagnostics, "cannot read input file '%s'",
                                    options->input_path);
        return 1;
    }

    LunaArena arena;
    luna_arena_init(&arena, 32U * 1024U);

    LunaParser parser;
    luna_parser_init(&parser, &source, &diagnostics, &arena);
    LunaProgram *program = luna_parser_parse_program(&parser);

    LunaIrModule module;
    luna_ir_module_init(&module, options->target);

    bool success =
        program != NULL && luna_diagnostic_error_count(&diagnostics) == 0U;

    if (success) {
        success = luna_sema_lower(program, &diagnostics, &module);
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
    luna_source_destroy(&source);

    return success && luna_diagnostic_error_count(&diagnostics) == 0U ? 0 : 1;
}
