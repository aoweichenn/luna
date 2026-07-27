#ifndef LUNA_SEMA_H
#define LUNA_SEMA_H

#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/middleend/ir/ir.h"

#include <stdbool.h>

bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module);
bool luna_sema_lower_module(const LunaProgram *interface_unit,
                            const LunaProgram *implementation_unit,
                            LunaDiagnosticEngine *diagnostics,
                            LunaIrModule *module);

#endif
