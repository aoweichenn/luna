#ifndef LUNA_DIAGNOSTIC_H
#define LUNA_DIAGNOSTIC_H

#include "luna/attributes.h"
#include "luna/source.h"

#include <stddef.h>
#include <stdio.h>

typedef struct LunaDiagnosticEngine {
    FILE *stream;
    size_t error_count;
} LunaDiagnosticEngine;

void luna_diagnostic_init(LunaDiagnosticEngine *diagnostics, FILE *stream);
void luna_diagnostic_error(LunaDiagnosticEngine *diagnostics,
                           LunaSourceSpan span, const char *format, ...)
    LUNA_PRINTF_LIKE(3, 4);
void luna_diagnostic_error_plain(LunaDiagnosticEngine *diagnostics,
                                 const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);
size_t luna_diagnostic_error_count(const LunaDiagnosticEngine *diagnostics);

#endif
