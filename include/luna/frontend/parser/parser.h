#ifndef LUNA_PARSER_H
#define LUNA_PARSER_H

#include "luna/frontend/support/arena.h"
#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/lexer/lexer.h"

#include <stdint.h>

typedef struct LunaParser {
    LunaLexer lexer;
    LunaToken current;
    LunaToken previous;
    LunaDiagnosticEngine *diagnostics;
    LunaArena *arena;
    uint32_t nesting_depth;
} LunaParser;

void luna_parser_init(LunaParser *parser, const LunaSourceFile *source,
                      LunaDiagnosticEngine *diagnostics, LunaArena *arena);
LunaProgram *luna_parser_parse_program(LunaParser *parser);

#endif
