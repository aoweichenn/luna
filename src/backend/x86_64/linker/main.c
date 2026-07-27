#define _POSIX_C_SOURCE 200809L

#include "luna/backend/x86_64/elf_linker.h"

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    LUNA_LINKER_READ_CHUNK_SIZE = 16384,
    LUNA_LINKER_TEMPORARY_ATTEMPTS = 100
};

typedef struct LunaLinkerFile {
    const char *path;
    LunaStringBuilder contents;
} LunaLinkerFile;

static void luna_linker_print_usage(FILE *stream) {
    (void)fputs(
        "usage: lunalink [-e symbol] [-o path] input.o...\n"
        "\n"
        "options:\n"
        "  -e, --entry symbol  executable entry symbol (default: _start)\n"
        "  -o path             output path (default: a.out)\n"
        "  --static, -static   accepted for linker-driver compatibility\n"
        "  --version           print linker version\n"
        "  --help              print this help\n"
        "\n"
        "The output is always a static x86-64 Linux ELF executable and has no\n"
        "dynamic interpreter or implicit C runtime. The project-owned direct\n"
        "Linux syscall ABI object is included automatically.\n",
        stream);
}

static bool luna_linker_read_file(const char *path,
                                  LunaStringBuilder *contents) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr, "lunalink: error: cannot open '%s': %s\n", path,
                      strerror(errno));
        return false;
    }

    char buffer[LUNA_LINKER_READ_CHUNK_SIZE];
    bool success = true;
    for (;;) {
        const size_t read_count = fread(buffer, 1U, sizeof(buffer), file);
        if (read_count > 0U) {
            if (contents->length > LUNA_X86_64_ELF_LINK_MAX_OBJECT_SIZE ||
                read_count > (size_t)LUNA_X86_64_ELF_LINK_MAX_OBJECT_SIZE -
                                 contents->length) {
                (void)fprintf(stderr,
                              "lunalink: error: input '%s' exceeds the "
                              "supported size limit\n",
                              path);
                success = false;
                break;
            }
            if (!luna_string_builder_append(contents, buffer, read_count)) {
                (void)fprintf(
                    stderr,
                    "lunalink: error: out of memory while reading '%s'\n",
                    path);
                success = false;
                break;
            }
        }
        if (read_count != sizeof(buffer)) {
            if (ferror(file) != 0) {
                (void)fprintf(stderr, "lunalink: error: failed to read '%s'\n",
                              path);
                success = false;
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        (void)fprintf(stderr, "lunalink: error: failed to close '%s'\n", path);
        success = false;
    }
    return success;
}

static bool luna_linker_write_all(int descriptor, const char *data,
                                  size_t length) {
    size_t written = 0U;
    while (written < length) {
        const ssize_t result =
            write(descriptor, data + written, length - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            if (result == 0) {
                errno = EIO;
            }
            return false;
        }
        written += (size_t)result;
    }
    return true;
}

static bool luna_linker_make_temporary_path(const char *output_path,
                                            uint32_t attempt,
                                            LunaStringBuilder *temporary) {
    return luna_string_builder_append_format(
        temporary, "%s.lunalink.tmp.%jd.%" PRIu32, output_path,
        (intmax_t)getpid(), attempt);
}

static bool luna_linker_write_output(const char *output_path,
                                     const LunaStringBuilder *output) {
    for (uint32_t attempt = 0U; attempt < LUNA_LINKER_TEMPORARY_ATTEMPTS;
         attempt += 1U) {
        LunaStringBuilder temporary;
        luna_string_builder_init(&temporary);
        if (!luna_linker_make_temporary_path(output_path, attempt,
                                             &temporary)) {
            luna_string_builder_destroy(&temporary);
            (void)fputs(
                "lunalink: error: out of memory while creating output path\n",
                stderr);
            return false;
        }

        const char *temporary_path = luna_string_builder_data(&temporary);
        const int descriptor = open(temporary_path, O_WRONLY | O_CREAT | O_EXCL,
                                    S_IRUSR | S_IWUSR);
        if (descriptor < 0 && errno == EEXIST) {
            luna_string_builder_destroy(&temporary);
            continue;
        }
        if (descriptor < 0) {
            (void)fprintf(stderr, "lunalink: error: cannot create '%s': %s\n",
                          temporary_path, strerror(errno));
            luna_string_builder_destroy(&temporary);
            return false;
        }

        bool success =
            luna_linker_write_all(descriptor, luna_string_builder_data(output),
                                  output->length) &&
            fsync(descriptor) == 0 &&
            fchmod(descriptor, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP |
                                   S_IROTH | S_IXOTH) == 0;
        if (close(descriptor) != 0) {
            success = false;
        }
        if (success && rename(temporary_path, output_path) != 0) {
            success = false;
        }
        if (!success) {
            const int saved_error = errno;
            (void)unlink(temporary_path);
            (void)fprintf(stderr, "lunalink: error: failed to write '%s': %s\n",
                          output_path, strerror(saved_error));
        }
        luna_string_builder_destroy(&temporary);
        return success;
    }
    (void)fprintf(
        stderr, "lunalink: error: cannot allocate a temporary path for '%s'\n",
        output_path);
    return false;
}

static void luna_linker_destroy_files(LunaVector *files) {
    for (size_t index = 0U; index < files->length; index += 1U) {
        LunaLinkerFile *file = luna_vector_at(files, index);
        if (file != NULL) {
            luna_string_builder_destroy(&file->contents);
        }
    }
    luna_vector_destroy(files);
}

int main(int argument_count, char **arguments) {
    const char *output_path = "a.out";
    const char *entry_symbol = "_start";
    LunaVector input_paths;
    luna_vector_init(&input_paths, sizeof(const char *));
    int exit_code = 2;

    for (int index = 1; index < argument_count; index += 1) {
        const char *argument = arguments[index];
        if (strcmp(argument, "--help") == 0) {
            luna_linker_print_usage(stdout);
            exit_code = 0;
            goto cleanup_paths;
        }
        if (strcmp(argument, "--version") == 0) {
            (void)puts("lunalink 0.1.0-dev");
            exit_code = 0;
            goto cleanup_paths;
        }
        if (strcmp(argument, "--static") == 0 ||
            strcmp(argument, "-static") == 0) {
            continue;
        }
        if (strcmp(argument, "-o") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("lunalink: error: -o requires a path\n", stderr);
                goto cleanup_paths;
            }
            output_path = arguments[index + 1];
            index += 1;
            continue;
        }
        if (strcmp(argument, "-e") == 0 || strcmp(argument, "--entry") == 0) {
            if (index + 1 >= argument_count) {
                (void)fputs("lunalink: error: entry option requires a symbol\n",
                            stderr);
                goto cleanup_paths;
            }
            entry_symbol = arguments[index + 1];
            index += 1;
            continue;
        }
        if (argument[0] == '-') {
            (void)fprintf(stderr, "lunalink: error: unknown option '%s'\n",
                          argument);
            goto cleanup_paths;
        }
        if (!luna_vector_push(&input_paths, (const void *)&argument)) {
            (void)fputs(
                "lunalink: error: out of memory while recording inputs\n",
                stderr);
            goto cleanup_paths;
        }
    }

    if (input_paths.length == 0U) {
        luna_linker_print_usage(stderr);
        goto cleanup_paths;
    }
    if (input_paths.length > LUNA_X86_64_ELF_LINK_MAX_INPUT_COUNT) {
        (void)fputs("lunalink: error: too many input objects\n", stderr);
        goto cleanup_paths;
    }
    const size_t entry_symbol_length = strlen(entry_symbol);
    if (entry_symbol_length == 0U ||
        entry_symbol_length > LUNA_X86_64_ELF_LINK_MAX_NAME_LENGTH) {
        (void)fputs("lunalink: error: invalid entry symbol\n", stderr);
        goto cleanup_paths;
    }

    LunaVector files;
    LunaVector inputs;
    luna_vector_init(&files, sizeof(LunaLinkerFile));
    luna_vector_init(&inputs, sizeof(LunaX8664ElfLinkInput));
    bool success = luna_vector_reserve(&files, input_paths.length) &&
                   luna_vector_reserve(&inputs, input_paths.length);
    if (!success) {
        (void)fputs(
            "lunalink: error: out of memory while preparing input objects\n",
            stderr);
    }
    for (size_t index = 0U; success && index < input_paths.length;
         index += 1U) {
        const char *const *path =
            (const char *const *)luna_vector_at_const(&input_paths, index);
        LunaLinkerFile file = {
            .path = path == NULL ? NULL : *path,
        };
        luna_string_builder_init(&file.contents);
        if (file.path == NULL ||
            strlen(file.path) > LUNA_X86_64_ELF_LINK_MAX_NAME_LENGTH) {
            (void)fputs("lunalink: error: invalid input path\n", stderr);
            luna_string_builder_destroy(&file.contents);
            success = false;
            break;
        }
        if (!luna_linker_read_file(file.path, &file.contents)) {
            luna_string_builder_destroy(&file.contents);
            success = false;
            break;
        }
        if (!luna_vector_push(&files, &file)) {
            (void)fputs("lunalink: error: out of memory while storing inputs\n",
                        stderr);
            luna_string_builder_destroy(&file.contents);
            success = false;
            break;
        }
        LunaLinkerFile *stored = luna_vector_at(&files, files.length - 1U);
        const LunaX8664ElfLinkInput input = {
            .name = luna_string_view_from_c_string(stored->path),
            .object =
                {
                    .data = luna_string_builder_data(&stored->contents),
                    .length = stored->contents.length,
                },
        };
        if (!luna_vector_push(&inputs, &input)) {
            (void)fputs(
                "lunalink: error: out of memory while storing link inputs\n",
                stderr);
            success = false;
        }
    }
    if (!success) {
        luna_vector_destroy(&inputs);
        luna_linker_destroy_files(&files);
        exit_code = 1;
        goto cleanup_paths;
    }

    LunaStringBuilder executable;
    luna_string_builder_init(&executable);
    success =
        luna_x86_64_link_elf_executable(
            (const LunaX8664ElfLinkInput *)inputs.data, (uint32_t)inputs.length,
            luna_string_view_from_c_string(entry_symbol), stderr,
            &executable) &&
        luna_linker_write_output(output_path, &executable);
    luna_string_builder_destroy(&executable);
    luna_vector_destroy(&inputs);
    luna_linker_destroy_files(&files);
    exit_code = success ? 0 : 1;

cleanup_paths:
    luna_vector_destroy(&input_paths);
    return exit_code;
}
