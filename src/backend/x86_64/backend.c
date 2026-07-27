#include "luna/backend/x86_64/x86_64.h"

#include <stdbool.h>
#include <stddef.h>

static bool
luna_x86_64_prepare_machine_module(const LunaIrModule *source,
                                   LunaDiagnosticEngine *diagnostics,
                                   LunaX8664MachineModule *machine_module) {
    if (source == NULL || diagnostics == NULL || machine_module == NULL) {
        return false;
    }
    luna_x86_64_machine_module_init(machine_module, source->target);
    if (!luna_x86_64_machine_lower(source, diagnostics, machine_module)) {
        return false;
    }
    if (!luna_x86_64_machine_verify(machine_module, diagnostics->stream)) {
        luna_diagnostic_error_plain(diagnostics,
                                    "x86-64 machine IR verification failed");
        return false;
    }
    return true;
}

bool luna_x86_64_emit_machine_ir(const LunaIrModule *module,
                                 LunaDiagnosticEngine *diagnostics,
                                 LunaStringBuilder *output) {
    if (module == NULL || diagnostics == NULL || output == NULL ||
        output->length != 0U) {
        if (diagnostics != NULL) {
            luna_diagnostic_error_plain(
                diagnostics,
                "x86-64 machine IR emission received invalid state");
        }
        return false;
    }

    LunaX8664MachineModule machine_module;
    const bool prepared = luna_x86_64_prepare_machine_module(
        module, diagnostics, &machine_module);
    const bool success =
        prepared && luna_x86_64_machine_print(&machine_module, output);
    if (prepared && !success) {
        luna_diagnostic_error_plain(
            diagnostics, "out of memory while printing x86-64 machine IR");
    }
    luna_x86_64_machine_module_destroy(&machine_module);
    return success;
}

bool luna_x86_64_emit_assembly(const LunaIrModule *module,
                               LunaDiagnosticEngine *diagnostics,
                               LunaStringBuilder *output) {
    if (module == NULL || diagnostics == NULL || output == NULL) {
        if (diagnostics != NULL) {
            luna_diagnostic_error_plain(
                diagnostics, "x86-64 assembly emission received invalid state");
        }
        return false;
    }

    LunaX8664MachineModule machine_module;
    const bool prepared = luna_x86_64_prepare_machine_module(
        module, diagnostics, &machine_module);
    const bool success = prepared && luna_x86_64_machine_emit_assembly(
                                         &machine_module, diagnostics, output);
    luna_x86_64_machine_module_destroy(&machine_module);
    return success;
}
