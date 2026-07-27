#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include "luna/target/target.h"

#include <stdint.h>
#include <stdio.h>

typedef enum LunaEmitKind {
    LUNA_EMIT_CHECK,
    LUNA_EMIT_IR,
    LUNA_EMIT_MACHINE_IR,
    LUNA_EMIT_LIVENESS,
    LUNA_EMIT_ASSEMBLY,
    LUNA_EMIT_METADATA
} LunaEmitKind;

typedef struct LunaCompilerOptions {
    const char *const *input_paths;
    uint32_t input_count;
    const char *output_path;
    const char *separate_module_name;
    LunaEmitKind emit_kind;
    const LunaTargetInfo *target;
} LunaCompilerOptions;

int luna_compile(const LunaCompilerOptions *options, FILE *diagnostic_stream);

#endif
