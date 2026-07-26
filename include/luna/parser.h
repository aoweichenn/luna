#ifndef LUNA_PARSER_H
#define LUNA_PARSER_H

#include "luna/arena.h"
#include "luna/ast.h"
#include "luna/diagnostic.h"
#include "luna/lexer.h"

typedef struct LunaParser {
    LunaLexer lexer;
    LunaToken current;
    LunaToken previous;
    LunaDiagnosticEngine *diagnostics;
    LunaArena *arena;
} LunaParser;

void luna_parser_init(LunaParser *parser, const LunaSourceFile *source,
                      LunaDiagnosticEngine *diagnostics, LunaArena *arena);
LunaProgram *luna_parser_parse_program(LunaParser *parser);

#endif
