#ifndef LUNA_LEXER_H
#define LUNA_LEXER_H

#include "luna/diagnostic.h"
#include "luna/token.h"

#include <stddef.h>
#include <stdint.h>

typedef struct LunaLexer {
    const LunaSourceFile *source;
    LunaDiagnosticEngine *diagnostics;
    size_t offset;
    uint32_t line;
    uint32_t column;
} LunaLexer;

void luna_lexer_init(LunaLexer *lexer, const LunaSourceFile *source,
                     LunaDiagnosticEngine *diagnostics);
LunaToken luna_lexer_next(LunaLexer *lexer);

#endif
