#ifndef LUNA_X86_64_H
#define LUNA_X86_64_H

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/middleend/ir/ir.h"

#include <stdbool.h>

bool luna_x86_64_emit_assembly(const LunaIrModule *module,
                               LunaDiagnosticEngine *diagnostics,
                               LunaStringBuilder *output);

#endif
