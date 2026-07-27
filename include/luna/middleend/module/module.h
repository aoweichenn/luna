#ifndef LUNA_MODULE_H
#define LUNA_MODULE_H

#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/middleend/ir/ir.h"

#include <stdbool.h>
#include <stdint.h>

bool luna_module_lower_programs(const LunaProgram *const *programs,
                                uint32_t program_count,
                                LunaDiagnosticEngine *diagnostics,
                                LunaIrModule *module);

#endif
