#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include <stdio.h>

typedef enum LunaEmitKind {
    LUNA_EMIT_CHECK,
    LUNA_EMIT_IR,
    LUNA_EMIT_ASSEMBLY
} LunaEmitKind;

typedef struct LunaCompilerOptions {
    const char *input_path;
    const char *output_path;
    LunaEmitKind emit_kind;
} LunaCompilerOptions;

int luna_compile(const LunaCompilerOptions *options, FILE *diagnostic_stream);

#endif
