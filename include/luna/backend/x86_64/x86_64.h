#ifndef LUNA_X86_64_H
#define LUNA_X86_64_H

#include "luna/backend/x86_64/abi.h"
#include "luna/backend/x86_64/instruction_rewrite.h"
#include "luna/backend/x86_64/liveness.h"
#include "luna/backend/x86_64/machine_ir.h"
#include "luna/backend/x86_64/register_allocation.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/support/buffer.h"
#include "luna/middleend/ir/ir.h"

#include <stdbool.h>

bool luna_x86_64_emit_assembly(const LunaIrModule *module,
                               LunaDiagnosticEngine *diagnostics,
                               LunaStringBuilder *output);
bool luna_x86_64_emit_abi(const LunaIrModule *module,
                          LunaDiagnosticEngine *diagnostics,
                          LunaStringBuilder *output);
bool luna_x86_64_emit_machine_ir(const LunaIrModule *module,
                                 LunaDiagnosticEngine *diagnostics,
                                 LunaStringBuilder *output);
bool luna_x86_64_emit_liveness(const LunaIrModule *module,
                               LunaDiagnosticEngine *diagnostics,
                               LunaStringBuilder *output);
bool luna_x86_64_emit_register_allocation(const LunaIrModule *module,
                                          LunaDiagnosticEngine *diagnostics,
                                          LunaStringBuilder *output);
bool luna_x86_64_emit_instruction_rewrite(const LunaIrModule *module,
                                          LunaDiagnosticEngine *diagnostics,
                                          LunaStringBuilder *output);
bool luna_x86_64_machine_emit_assembly(
    const LunaX8664MachineModule *module, const LunaX8664ModuleAbi *abi,
    const LunaX8664ModuleLiveness *liveness,
    const LunaX8664ModuleRegisterAllocation *allocation,
    const LunaX8664ModuleInstructionRewrite *rewrite,
    LunaDiagnosticEngine *diagnostics, LunaStringBuilder *output);

#endif
