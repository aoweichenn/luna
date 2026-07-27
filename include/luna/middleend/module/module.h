#ifndef LUNA_MODULE_H
#define LUNA_MODULE_H

#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/middleend/ir/ir.h"
#include "luna/middleend/module/metadata.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum LunaModuleCompilationKind {
    LUNA_MODULE_COMPILE_EXECUTABLE,
    LUNA_MODULE_COMPILE_SEPARATE
} LunaModuleCompilationKind;

typedef struct LunaModuleInput {
    const LunaProgram *program;
    bool is_metadata;
    const LunaModuleMetadataDependency *metadata_dependencies;
    uint32_t metadata_dependency_count;
    uint64_t metadata_content_hash;
} LunaModuleInput;

typedef struct LunaModuleOptions {
    LunaModuleCompilationKind compilation_kind;
    LunaStringView root_module_name;
    bool require_compiled_root_interface;
} LunaModuleOptions;

bool luna_module_lower_inputs(const LunaModuleInput *inputs,
                              uint32_t input_count,
                              const LunaModuleOptions *options,
                              LunaDiagnosticEngine *diagnostics,
                              LunaIrModule *module);
bool luna_module_lower_programs(const LunaProgram *const *programs,
                                uint32_t program_count,
                                LunaDiagnosticEngine *diagnostics,
                                LunaIrModule *module);

#endif
