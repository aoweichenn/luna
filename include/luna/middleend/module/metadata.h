#ifndef LUNA_MODULE_METADATA_H
#define LUNA_MODULE_METADATA_H

#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/support/arena.h"
#include "luna/frontend/support/buffer.h"
#include "luna/target/target.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LunaModuleMetadataDependency {
    LunaStringView module_name;
    uint64_t content_hash;
} LunaModuleMetadataDependency;

typedef struct LunaModuleMetadata {
    LunaSourceFile diagnostic_source;
    const LunaProgram *interface_unit;
    LunaVector dependencies;
    uint64_t content_hash;
} LunaModuleMetadata;

bool luna_module_metadata_encode(
    const LunaProgram *interface_unit, const LunaTargetInfo *target,
    const LunaModuleMetadataDependency *dependencies, uint32_t dependency_count,
    LunaDiagnosticEngine *diagnostics, LunaStringBuilder *output);
bool luna_module_metadata_decode(const LunaSourceFile *source,
                                 const LunaTargetInfo *target, LunaArena *arena,
                                 LunaDiagnosticEngine *diagnostics,
                                 LunaModuleMetadata *metadata);
void luna_module_metadata_destroy(LunaModuleMetadata *metadata);

#endif
