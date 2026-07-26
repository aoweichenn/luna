#include "luna/frontend/compiler/compiler.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void luna_print_usage(FILE *stream) {
    (void)fputs("usage: lunac [--emit check|ir|asm] [-o path] input.luna\n"
                "\n"
                "targets:\n"
                "  x86_64-unknown-linux-gnu (the only M0 target)\n"
                "\n"
                "options:\n"
                "  --emit check   parse, type-check and verify IR\n"
                "  --emit ir      write textual Luna IR\n"
                "  --emit asm     write x86-64 GNU assembly (default)\n"
                "  -o path        output path; '-' writes to stdout\n"
                "  --version      print compiler version\n"
                "  --help         print this help\n",
                stream);
}

static bool luna_parse_emit_kind(const char *name, LunaEmitKind *emit_kind) {
    if (strcmp(name, "check") == 0) {
        *emit_kind = LUNA_EMIT_CHECK;
        return true;
    }
    if (strcmp(name, "ir") == 0) {
        *emit_kind = LUNA_EMIT_IR;
        return true;
    }
    if (strcmp(name, "asm") == 0) {
        *emit_kind = LUNA_EMIT_ASSEMBLY;
        return true;
    }
    return false;
}

int main(int argument_count, char **arguments) {
    LunaCompilerOptions options = {
        .input_path = NULL,
        .output_path = NULL,
        .emit_kind = LUNA_EMIT_ASSEMBLY,
    };

    for (int index = 1; index < argument_count; index += 1) {
        const char *argument = arguments[index];

        if (strcmp(argument, "--help") == 0) {
            luna_print_usage(stdout);
            return 0;
        }

        if (strcmp(argument, "--version") == 0) {
            (void)puts("lunac 0.1.0-dev");
            return 0;
        }

        if (strcmp(argument, "--emit") == 0) {
            if (index + 1 >= argument_count ||
                !luna_parse_emit_kind(arguments[index + 1],
                                      &options.emit_kind)) {
                (void)fputs("error: --emit requires check, ir or asm\n",
                            stderr);
                return 2;
            }
            index += 1;
            continue;
        }

        if (strcmp(argument, "-o") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("error: -o requires an output path\n", stderr);
                return 2;
            }
            options.output_path = arguments[index + 1];
            index += 1;
            continue;
        }

        if (argument[0] == '-') {
            (void)fprintf(stderr, "error: unknown option '%s'\n", argument);
            return 2;
        }

        if (options.input_path != NULL) {
            (void)fputs("error: milestone M0 accepts exactly one source unit\n",
                        stderr);
            return 2;
        }
        options.input_path = argument;
    }

    if (options.input_path == NULL) {
        luna_print_usage(stderr);
        return 2;
    }

    if (options.emit_kind != LUNA_EMIT_CHECK && options.output_path == NULL) {
        options.output_path =
            options.emit_kind == LUNA_EMIT_IR ? "a.lir" : "a.s";
    }

    return luna_compile(&options, stderr);
}
