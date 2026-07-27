#include "luna/frontend/compiler/compiler.h"
#include "luna/frontend/support/buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void luna_print_usage(FILE *stream) {
    (void)fputs("usage: lunac [--target triple] "
                "[--emit check|ir|mir|liveness|allocation|asm|metadata] "
                "[--compile-module name] [-o path] input...\n"
                "\n"
                "targets:\n"
                "  x86_64-unknown-linux-gnu (default)\n"
                "\n"
                "options:\n"
                "  --target name  select the compilation target\n"
                "  --emit check   parse, type-check and verify IR\n"
                "  --emit ir      write textual Luna IR\n"
                "  --emit mir     write verified x86-64 machine IR\n"
                "  --emit liveness  write verified x86-64 live sets\n"
                "  --emit allocation  write verified x86-64 register "
                "allocation\n"
                "  --emit asm     write x86-64 GNU assembly (default)\n"
                "  --emit metadata  write deterministic .lmi metadata\n"
                "  --compile-module name  compile one module without _start\n"
                "  -o path        output path; '-' writes to stdout\n"
                "  inputs         executable root, every transitive module "
                "source or compiled .lmi dependency, in any order\n"
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
    if (strcmp(name, "mir") == 0) {
        *emit_kind = LUNA_EMIT_MACHINE_IR;
        return true;
    }
    if (strcmp(name, "liveness") == 0) {
        *emit_kind = LUNA_EMIT_LIVENESS;
        return true;
    }
    if (strcmp(name, "allocation") == 0) {
        *emit_kind = LUNA_EMIT_REGISTER_ALLOCATION;
        return true;
    }
    if (strcmp(name, "asm") == 0) {
        *emit_kind = LUNA_EMIT_ASSEMBLY;
        return true;
    }
    if (strcmp(name, "metadata") == 0) {
        *emit_kind = LUNA_EMIT_METADATA;
        return true;
    }
    return false;
}

int main(int argument_count, char **arguments) {
    LunaVector input_paths;
    luna_vector_init(&input_paths, sizeof(const char *));

    LunaCompilerOptions options = {
        .input_paths = NULL,
        .input_count = 0U,
        .output_path = NULL,
        .emit_kind = LUNA_EMIT_ASSEMBLY,
        .target = luna_target_info_default(),
    };
    int exit_code = 2;

    for (int index = 1; index < argument_count; index += 1) {
        const char *argument = arguments[index];

        if (strcmp(argument, "--help") == 0) {
            luna_print_usage(stdout);
            exit_code = 0;
            goto cleanup;
        }

        if (strcmp(argument, "--version") == 0) {
            (void)puts("lunac 0.1.0-dev");
            exit_code = 0;
            goto cleanup;
        }

        if (strcmp(argument, "--emit") == 0) {
            if (index + 1 >= argument_count ||
                !luna_parse_emit_kind(arguments[index + 1],
                                      &options.emit_kind)) {
                (void)fputs("error: --emit requires check, ir, mir, liveness, "
                            "allocation, asm or metadata\n",
                            stderr);
                goto cleanup;
            }
            index += 1;
            continue;
        }

        if (strcmp(argument, "--target") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("error: --target requires a target triple\n",
                            stderr);
                goto cleanup;
            }
            options.target = luna_target_info_from_triple(arguments[index + 1]);
            if (options.target == NULL) {
                (void)fprintf(stderr, "error: unsupported target '%s'\n",
                              arguments[index + 1]);
                goto cleanup;
            }
            index += 1;
            continue;
        }

        if (strcmp(argument, "--compile-module") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("error: --compile-module requires a module name\n",
                            stderr);
                goto cleanup;
            }
            if (options.separate_module_name != NULL) {
                (void)fputs(
                    "error: --compile-module may be specified only once\n",
                    stderr);
                goto cleanup;
            }
            options.separate_module_name = arguments[index + 1];
            index += 1;
            continue;
        }

        if (strcmp(argument, "-o") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("error: -o requires an output path\n", stderr);
                goto cleanup;
            }
            options.output_path = arguments[index + 1];
            index += 1;
            continue;
        }

        if (argument[0] == '-') {
            (void)fprintf(stderr, "error: unknown option '%s'\n", argument);
            goto cleanup;
        }

        if (!luna_vector_push(&input_paths, (const void *)&argument)) {
            (void)fputs("error: out of memory while recording source units\n",
                        stderr);
            goto cleanup;
        }
    }

    if (input_paths.length == 0U) {
        luna_print_usage(stderr);
        goto cleanup;
    }
    if (input_paths.length > UINT32_MAX) {
        (void)fputs("error: too many source units\n", stderr);
        goto cleanup;
    }

    if (options.emit_kind != LUNA_EMIT_CHECK && options.output_path == NULL) {
        if (options.emit_kind == LUNA_EMIT_IR) {
            options.output_path = "a.lir";
        } else if (options.emit_kind == LUNA_EMIT_MACHINE_IR) {
            options.output_path = "a.mir";
        } else if (options.emit_kind == LUNA_EMIT_LIVENESS) {
            options.output_path = "a.live";
        } else if (options.emit_kind == LUNA_EMIT_REGISTER_ALLOCATION) {
            options.output_path = "a.alloc";
        } else if (options.emit_kind == LUNA_EMIT_METADATA) {
            options.output_path = "a.lmi";
        } else {
            options.output_path = "a.s";
        }
    }

    options.input_paths = (const char *const *)input_paths.data;
    options.input_count = (uint32_t)input_paths.length;
    exit_code = luna_compile(&options, stderr);

cleanup:
    luna_vector_destroy(&input_paths);
    return exit_code;
}
