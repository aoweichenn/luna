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
#include <stdint.h>
#include <stdio.h>

static bool luna_compiler_select_module_units(
    LunaProgram *const *programs, uint32_t program_count,
    LunaDiagnosticEngine *diagnostics, const LunaProgram **interface_unit,
    const LunaProgram **implementation_unit) {
    *interface_unit = NULL;
    *implementation_unit = NULL;

    for (uint32_t index = 0U; index < program_count; index += 1U) {
        const LunaProgram *program = programs[index];
        const LunaProgram **selected =
            program->is_interface ? interface_unit : implementation_unit;
        if (*selected != NULL) {
            luna_diagnostic_error(
                diagnostics, program->module_span,
                "module compilation received more than one %s unit",
                program->is_interface ? "interface" : "implementation");
            luna_diagnostic_note(diagnostics, (*selected)->module_span,
                                 "the first %s unit is here",
                                 program->is_interface ? "interface"
                                                       : "implementation");
            continue;
        }
        *selected = program;
    }

    if (*implementation_unit == NULL && *interface_unit != NULL &&
        luna_diagnostic_error_count(diagnostics) == 0U) {
        luna_diagnostic_error(
            diagnostics, (*interface_unit)->module_span,
            "module interface requires a matching implementation source unit");
    }

    return *implementation_unit != NULL &&
           luna_diagnostic_error_count(diagnostics) == 0U;
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

    if (options->input_count == 0U ||
        options->input_count > (uint32_t)LUNA_COMPILER_MAX_SOURCE_UNITS) {
        luna_diagnostic_error_plain(
            &diagnostics, "compiler requires one or two source units");
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
    luna_arena_init(&arena, 32U * 1024U);

    LunaSourceFile sources[LUNA_COMPILER_MAX_SOURCE_UNITS] = {0};
    LunaProgram *programs[LUNA_COMPILER_MAX_SOURCE_UNITS] = {0};
    uint32_t loaded_count = 0U;
    bool success = true;
    for (uint32_t index = 0U; index < options->input_count; index += 1U) {
        if (!luna_source_load(options->input_paths[index], &sources[index])) {
            luna_diagnostic_error_plain(&diagnostics,
                                        "cannot read input file '%s'",
                                        options->input_paths[index]);
            success = false;
            break;
        }
        loaded_count += 1U;
    }

    if (success) {
        for (uint32_t index = 0U; index < options->input_count; index += 1U) {
            LunaParser parser;
            luna_parser_init(&parser, &sources[index], &diagnostics, &arena);
            programs[index] = luna_parser_parse_program(&parser);
        }
        success = luna_diagnostic_error_count(&diagnostics) == 0U;
    }

    LunaIrModule module;
    luna_ir_module_init(&module, options->target);

    const LunaProgram *interface_unit = NULL;
    const LunaProgram *implementation_unit = NULL;
    if (success) {
        for (uint32_t index = 0U; index < options->input_count; index += 1U) {
            if (programs[index] == NULL) {
                success = false;
            }
        }
    }

    if (success) {
        success = luna_compiler_select_module_units(
            programs, options->input_count, &diagnostics, &interface_unit,
            &implementation_unit);
    }

    if (success) {
        success = luna_sema_lower_module(interface_unit, implementation_unit,
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
    for (uint32_t index = 0U; index < loaded_count; index += 1U) {
        luna_source_destroy(&sources[index]);
    }

    return success && luna_diagnostic_error_count(&diagnostics) == 0U ? 0 : 1;
}
