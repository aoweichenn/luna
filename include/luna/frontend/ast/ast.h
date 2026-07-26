#ifndef LUNA_AST_H
#define LUNA_AST_H

#include "luna/frontend/source/source.h"
#include "luna/frontend/support/string_view.h"
#include "luna/frontend/token/token.h"
#include "luna/frontend/type/type.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LunaTypeRef {
    LunaTypeKind kind;
    LunaSourceSpan span;
} LunaTypeRef;

typedef enum LunaExpressionKind {
    LUNA_EXPRESSION_INTEGER,
    LUNA_EXPRESSION_BOOLEAN,
    LUNA_EXPRESSION_NAME,
    LUNA_EXPRESSION_UNARY,
    LUNA_EXPRESSION_BINARY,
    LUNA_EXPRESSION_CALL
} LunaExpressionKind;

typedef struct LunaExpression LunaExpression;

struct LunaExpression {
    LunaExpressionKind kind;
    LunaSourceSpan span;
    LunaTypeKind checked_type;
    LunaExpression *next;

    union {
        int64_t integer;
        bool boolean;
        LunaStringView name;

        struct {
            LunaTokenKind operator_kind;
            LunaExpression *operand;
        } unary;

        struct {
            LunaTokenKind operator_kind;
            LunaExpression *left;
            LunaExpression *right;
        } binary;

        struct {
            LunaStringView name;
            LunaExpression *first_argument;
            uint32_t argument_count;
        } call;
    } as;
};

typedef struct LunaBlock LunaBlock;
typedef struct LunaStatement LunaStatement;

typedef enum LunaStatementKind {
    LUNA_STATEMENT_BLOCK,
    LUNA_STATEMENT_DECLARATION,
    LUNA_STATEMENT_ASSIGNMENT,
    LUNA_STATEMENT_EXPRESSION,
    LUNA_STATEMENT_IF,
    LUNA_STATEMENT_WHILE,
    LUNA_STATEMENT_BREAK,
    LUNA_STATEMENT_CONTINUE,
    LUNA_STATEMENT_RETURN
} LunaStatementKind;

struct LunaBlock {
    LunaSourceSpan span;
    LunaStatement *first;
    LunaStatement *last;
};

struct LunaStatement {
    LunaStatementKind kind;
    LunaSourceSpan span;
    LunaStatement *next;

    union {
        LunaBlock block;

        struct {
            bool is_mutable;
            LunaStringView name;
            LunaTypeRef type;
            LunaExpression *initializer;
        } declaration;

        struct {
            LunaStringView name;
            LunaTokenKind operator_kind;
            LunaExpression *value;
        } assignment;

        LunaExpression *expression;

        struct {
            LunaExpression *condition;
            LunaBlock *then_block;
            LunaStatement *else_branch;
        } if_statement;

        struct {
            LunaExpression *condition;
            LunaBlock *body;
        } while_statement;

        LunaExpression *return_value;
    } as;
};

typedef struct LunaParameter {
    LunaStringView name;
    LunaTypeRef type;
    LunaSourceSpan span;
    struct LunaParameter *next;
} LunaParameter;

typedef struct LunaFunction {
    LunaStringView name;
    LunaSourceSpan span;
    bool is_exported;
    bool is_declaration;
    LunaParameter *first_parameter;
    uint32_t parameter_count;
    LunaTypeRef return_type;
    LunaBlock *body;
    struct LunaFunction *next;
} LunaFunction;

typedef struct LunaImport {
    LunaStringView module_name;
    LunaSourceSpan span;
    struct LunaImport *next;
} LunaImport;

typedef struct LunaProgram {
    const LunaSourceFile *source;
    LunaStringView module_name;
    LunaSourceSpan module_span;
    bool is_interface;
    LunaImport *first_import;
    LunaFunction *first_function;
} LunaProgram;

const char *luna_type_kind_name(LunaTypeKind kind);

#endif
