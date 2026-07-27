#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include "luna/target/target.h"

#include <stdint.h>
#include <stdio.h>

enum { LUNA_COMPILER_MAX_SOURCE_UNITS = 2 };

typedef enum LunaEmitKind {
    LUNA_EMIT_CHECK,
    LUNA_EMIT_IR,
    LUNA_EMIT_ASSEMBLY
} LunaEmitKind;

typedef struct LunaCompilerOptions {
    const char *input_paths[LUNA_COMPILER_MAX_SOURCE_UNITS];
    uint32_t input_count;
    const char *output_path;
    LunaEmitKind emit_kind;
    const LunaTargetInfo *target;
} LunaCompilerOptions;

int luna_compile(const LunaCompilerOptions *options, FILE *diagnostic_stream);

#endif
