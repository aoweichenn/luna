#ifndef LUNA_SEMA_H
#define LUNA_SEMA_H

#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/middleend/ir/ir.h"

#include <stdbool.h>
#include <stdint.h>

bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module);
bool luna_sema_lower_module(const LunaProgram *interface_unit,
                            const LunaProgram *implementation_unit,
                            LunaDiagnosticEngine *diagnostics,
                            LunaIrModule *module);

typedef struct LunaSemaImport {
    const LunaProgram *interface_unit;
    LunaSourceSpan span;
} LunaSemaImport;

typedef struct LunaSemaModule {
    const LunaProgram *interface_unit;
    const LunaProgram *implementation_unit;
    const LunaSemaImport *interface_imports;
    uint32_t interface_import_count;
    const LunaSemaImport *implementation_imports;
    uint32_t implementation_import_count;
    bool is_precompiled;
    bool is_compilation_root;
    bool is_executable_root;
    bool has_metadata_interface;
    uint64_t metadata_content_hash;
} LunaSemaModule;

bool luna_sema_lower_modules(const LunaSemaModule *modules,
                             uint32_t module_count,
                             LunaDiagnosticEngine *diagnostics,
                             LunaIrModule *module);

#endif
