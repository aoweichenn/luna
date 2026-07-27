#ifndef LUNA_AST_H
#define LUNA_AST_H

#include "luna/frontend/source/source.h"
#include "luna/frontend/support/string_view.h"
#include "luna/frontend/token/token.h"
#include "luna/frontend/type/type.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LunaTypeRef LunaTypeRef;

struct LunaTypeRef {
    LunaTypeKind kind;
    LunaSourceSpan span;

    union {
        struct {
            LunaTypeRef *pointee;
            bool is_read_only;
        } pointer;

        struct {
            LunaTypeRef *element;
            uint64_t count;
        } array;

        LunaStringView name;
    } as;
};

typedef enum LunaExpressionKind {
    LUNA_EXPRESSION_INTEGER,
    LUNA_EXPRESSION_FLOAT,
    LUNA_EXPRESSION_BOOLEAN,
    LUNA_EXPRESSION_STRING,
    LUNA_EXPRESSION_NULL,
    LUNA_EXPRESSION_ZERO_INITIALIZER,
    LUNA_EXPRESSION_AGGREGATE_INITIALIZER,
    LUNA_EXPRESSION_NAME,
    LUNA_EXPRESSION_UNARY,
    LUNA_EXPRESSION_BINARY,
    LUNA_EXPRESSION_CONDITIONAL,
    LUNA_EXPRESSION_INDEX,
    LUNA_EXPRESSION_MEMBER,
    LUNA_EXPRESSION_SIZEOF,
    LUNA_EXPRESSION_ALIGNOF,
    LUNA_EXPRESSION_OFFSETOF,
    LUNA_EXPRESSION_CALL,
    LUNA_EXPRESSION_CAST
} LunaExpressionKind;

typedef struct LunaExpression LunaExpression;
typedef struct LunaInitializerField LunaInitializerField;

struct LunaInitializerField {
    LunaStringView name;
    LunaExpression *value;
    LunaSourceSpan span;
    LunaInitializerField *next;
};

struct LunaExpression {
    LunaExpressionKind kind;
    LunaSourceSpan span;
    LunaTypeKind checked_type;
    LunaExpression *next;

    union {
        uint64_t integer;
        LunaStringView floating;
        bool boolean;
        LunaStringView string;
        LunaStringView name;

        struct {
            LunaInitializerField *first_field;
            uint32_t field_count;
        } aggregate_initializer;

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
            LunaExpression *condition;
            LunaExpression *then_expression;
            LunaExpression *else_expression;
        } conditional;

        struct {
            LunaExpression *base;
            LunaExpression *index;
        } index;

        struct {
            LunaExpression *base;
            LunaStringView name;
            LunaTokenKind operator_kind;
        } member;

        struct {
            LunaTypeRef type;
            LunaStringView member_name;
        } type_query;

        struct {
            LunaStringView name;
            LunaExpression *first_argument;
            uint32_t argument_count;
        } call;

        struct {
            LunaExpression *operand;
            LunaTypeRef target_type;
        } cast;
    } as;
};

typedef struct LunaBlock LunaBlock;
typedef struct LunaStatement LunaStatement;
typedef struct LunaSwitchArm LunaSwitchArm;

typedef enum LunaStatementKind {
    LUNA_STATEMENT_BLOCK,
    LUNA_STATEMENT_DECLARATION,
    LUNA_STATEMENT_ASSIGNMENT,
    LUNA_STATEMENT_EXPRESSION,
    LUNA_STATEMENT_IF,
    LUNA_STATEMENT_WHILE,
    LUNA_STATEMENT_DO,
    LUNA_STATEMENT_FOR,
    LUNA_STATEMENT_SWITCH,
    LUNA_STATEMENT_BREAK,
    LUNA_STATEMENT_CONTINUE,
    LUNA_STATEMENT_RETURN
} LunaStatementKind;

struct LunaBlock {
    LunaSourceSpan span;
    LunaStatement *first;
    LunaStatement *last;
};

struct LunaSwitchArm {
    LunaSourceSpan span;
    bool is_default;
    LunaExpression *first_label;
    uint32_t label_count;
    LunaBlock *body;
    LunaSwitchArm *next;
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
            LunaExpression *target;
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

        struct {
            LunaBlock *body;
            LunaExpression *condition;
        } do_statement;

        struct {
            LunaStatement *initializer;
            LunaExpression *condition;
            LunaStatement *update;
            LunaBlock *body;
        } for_statement;

        struct {
            LunaExpression *expression;
            LunaSwitchArm *first_arm;
            uint32_t arm_count;
        } switch_statement;

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
    bool is_external;
    bool is_declaration;
    LunaParameter *first_parameter;
    uint32_t parameter_count;
    LunaTypeRef return_type;
    LunaBlock *body;
    struct LunaFunction *next;
} LunaFunction;

typedef struct LunaField {
    LunaStringView name;
    LunaTypeRef type;
    LunaSourceSpan span;
    struct LunaField *next;
} LunaField;

typedef struct LunaEnumMember {
    LunaStringView name;
    LunaExpression *initializer;
    LunaSourceSpan span;
    struct LunaEnumMember *next;
} LunaEnumMember;

typedef struct LunaTypeDeclaration {
    LunaTypeKind kind;
    LunaStringView name;
    LunaSourceSpan span;
    bool is_exported;

    union {
        struct {
            LunaField *first_field;
            uint32_t field_count;
        } aggregate;

        struct {
            LunaTypeRef underlying_type;
            LunaEnumMember *first_member;
            uint32_t member_count;
        } enumeration;
    } as;

    struct LunaTypeDeclaration *next;
} LunaTypeDeclaration;

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
    LunaTypeDeclaration *first_type_declaration;
    LunaFunction *first_function;
} LunaProgram;

#endif
