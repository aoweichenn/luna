#ifndef LUNA_SEMA_H
#define LUNA_SEMA_H

#include "luna/ast.h"
#include "luna/diagnostic.h"
#include "luna/ir.h"

#include <stdbool.h>

bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module);

#endif
