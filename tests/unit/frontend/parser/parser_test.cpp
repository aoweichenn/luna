#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

namespace luna::test {

TEST(ParserTest, BuildsModuleImportsAndFunctionShape) {
    FrontendHarness harness{"export module compiler.frontend;\n"
                            "import core.text;\n"
                            "import core.io;\n"
                            "export fn add(left: i32, right: i32) -> i32 {\n"
                            "    return left + right;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    LunaProgram *program = harness.Program();
    ASSERT_NE(program, nullptr);
    EXPECT_TRUE(program->is_interface);
    EXPECT_EQ(
        std::string(program->module_name.data, program->module_name.length),
        "compiler.frontend");
    ASSERT_NE(program->first_import, nullptr);
    ASSERT_NE(program->first_import->next, nullptr);
    EXPECT_EQ(program->first_import->next->next, nullptr);
    ASSERT_NE(program->first_function, nullptr);
    EXPECT_TRUE(program->first_function->is_exported);
    EXPECT_EQ(program->first_function->parameter_count, 2U);
    EXPECT_EQ(program->first_function->return_type.kind, LUNA_TYPE_I32);
}

TEST(ParserTest, BuildsExternalFunctionDeclarations) {
    FrontendHarness harness{
        "module test.external_syntax;\n"
        "export extern fn c_mix(value: i16, pointer: *void) -> i64;\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_TRUE(function->is_exported);
    EXPECT_TRUE(function->is_external);
    EXPECT_TRUE(function->is_declaration);
    EXPECT_EQ(function->body, nullptr);
    EXPECT_EQ(function->parameter_count, 2U);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_I64);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_I16);
    ASSERT_NE(function->first_parameter->next, nullptr);
    EXPECT_EQ(function->first_parameter->next->type.kind, LUNA_TYPE_POINTER);
}

TEST(ParserTest, BuildsAggregateEnumAndMemberAccessNodes) {
    FrontendHarness harness{"module test.aggregate_syntax;\n"
                            "export enum Kind: u8 { empty, ready = 7, }\n"
                            "struct Payload { byte: u8; kind: Kind; }\n"
                            "union Storage { payload: Payload; bits: u64; }\n"
                            "fn main() -> i32 {\n"
                            "    var storage: Storage = {};\n"
                            "    let pointer: *Storage = &storage;\n"
                            "    pointer->payload.kind = Kind.ready;\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaTypeDeclaration *enumeration =
        harness.Program()->first_type_declaration;
    ASSERT_NE(enumeration, nullptr);
    EXPECT_EQ(enumeration->kind, LUNA_TYPE_ENUM);
    EXPECT_TRUE(enumeration->is_exported);
    EXPECT_EQ(enumeration->as.enumeration.underlying_type.kind, LUNA_TYPE_U8);
    EXPECT_EQ(enumeration->as.enumeration.member_count, 2U);
    ASSERT_NE(enumeration->as.enumeration.first_member, nullptr);
    ASSERT_NE(enumeration->as.enumeration.first_member->next, nullptr);
    ASSERT_NE(enumeration->as.enumeration.first_member->next->initializer,
              nullptr);

    const LunaTypeDeclaration *structure = enumeration->next;
    ASSERT_NE(structure, nullptr);
    EXPECT_EQ(structure->kind, LUNA_TYPE_STRUCT);
    EXPECT_EQ(structure->as.aggregate.field_count, 2U);
    ASSERT_NE(structure->as.aggregate.first_field, nullptr);
    EXPECT_EQ(structure->as.aggregate.first_field->type.kind, LUNA_TYPE_U8);
    ASSERT_NE(structure->as.aggregate.first_field->next, nullptr);
    EXPECT_EQ(structure->as.aggregate.first_field->next->type.kind,
              LUNA_TYPE_NAMED);

    const LunaTypeDeclaration *union_declaration = structure->next;
    ASSERT_NE(union_declaration, nullptr);
    EXPECT_EQ(union_declaration->kind, LUNA_TYPE_UNION);
    EXPECT_EQ(union_declaration->next, nullptr);

    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);
    const LunaStatement *assignment = function->body->first;
    ASSERT_NE(assignment, nullptr);
    assignment = assignment->next;
    ASSERT_NE(assignment, nullptr);
    assignment = assignment->next;
    ASSERT_NE(assignment, nullptr);
    ASSERT_EQ(assignment->kind, LUNA_STATEMENT_ASSIGNMENT);
    const LunaExpression *target = assignment->as.assignment.target;
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target->kind, LUNA_EXPRESSION_MEMBER);
    EXPECT_EQ(target->as.member.operator_kind, LUNA_TOKEN_DOT);
    ASSERT_NE(target->as.member.base, nullptr);
    ASSERT_EQ(target->as.member.base->kind, LUNA_EXPRESSION_MEMBER);
    EXPECT_EQ(target->as.member.base->as.member.operator_kind,
              LUNA_TOKEN_ARROW);
    const LunaExpression *value = assignment->as.assignment.value;
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->kind, LUNA_EXPRESSION_MEMBER);
    EXPECT_EQ(value->as.member.operator_kind, LUNA_TOKEN_DOT);
}

TEST(ParserTest, BuildsNestedNamedAggregateInitializersInSourceOrder) {
    FrontendHarness harness{"module test.aggregate_initializer_syntax;\n"
                            "struct Inner { first: i32; second: i32; }\n"
                            "struct Outer { inner: Inner; tail: u8; }\n"
                            "fn main() -> i32 {\n"
                            "    var value: Outer = {\n"
                            "        tail = 7,\n"
                            "        inner = { second = 2, first = 1, },\n"
                            "    };\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaStatement *declaration =
        harness.Program()->first_function->body->first;
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->kind, LUNA_STATEMENT_DECLARATION);
    const LunaExpression *initializer = declaration->as.declaration.initializer;
    ASSERT_NE(initializer, nullptr);
    ASSERT_EQ(initializer->kind, LUNA_EXPRESSION_AGGREGATE_INITIALIZER);
    EXPECT_EQ(initializer->as.aggregate_initializer.field_count, 2U);

    const LunaInitializerField *tail =
        initializer->as.aggregate_initializer.first_field;
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(std::string(tail->name.data, tail->name.length), "tail");
    ASSERT_NE(tail->value, nullptr);
    EXPECT_EQ(tail->value->kind, LUNA_EXPRESSION_INTEGER);

    const LunaInitializerField *inner = tail->next;
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(std::string(inner->name.data, inner->name.length), "inner");
    ASSERT_NE(inner->value, nullptr);
    ASSERT_EQ(inner->value->kind, LUNA_EXPRESSION_AGGREGATE_INITIALIZER);
    EXPECT_EQ(inner->value->as.aggregate_initializer.field_count, 2U);
    ASSERT_NE(inner->value->as.aggregate_initializer.first_field, nullptr);
    const LunaStringView first_inner_name =
        inner->value->as.aggregate_initializer.first_field->name;
    EXPECT_EQ(std::string(first_inner_name.data, first_inner_name.length),
              "second");
    EXPECT_EQ(inner->next, nullptr);
}

TEST(ParserTest, BuildsTypeOnlyLayoutQueryNodes) {
    FrontendHarness harness{"module test.layout_queries;\n"
                            "struct Record { tag: u8; value: u64; }\n"
                            "fn main() -> i32 {\n"
                            "    let size: usize = sizeof(Record);\n"
                            "    let alignment: usize = alignof([4]u16);\n"
                            "    let offset: usize = offsetof(Record, value);\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);

    const LunaStatement *size_declaration = function->body->first;
    ASSERT_NE(size_declaration, nullptr);
    ASSERT_EQ(size_declaration->kind, LUNA_STATEMENT_DECLARATION);
    const LunaExpression *size = size_declaration->as.declaration.initializer;
    ASSERT_NE(size, nullptr);
    ASSERT_EQ(size->kind, LUNA_EXPRESSION_SIZEOF);
    EXPECT_EQ(size->as.type_query.type.kind, LUNA_TYPE_NAMED);
    EXPECT_EQ(std::string(size->as.type_query.type.as.name.data,
                          size->as.type_query.type.as.name.length),
              "Record");

    const LunaStatement *alignment_declaration = size_declaration->next;
    ASSERT_NE(alignment_declaration, nullptr);
    const LunaExpression *alignment =
        alignment_declaration->as.declaration.initializer;
    ASSERT_NE(alignment, nullptr);
    ASSERT_EQ(alignment->kind, LUNA_EXPRESSION_ALIGNOF);
    ASSERT_EQ(alignment->as.type_query.type.kind, LUNA_TYPE_ARRAY);
    EXPECT_EQ(alignment->as.type_query.type.as.array.count, 4U);
    ASSERT_NE(alignment->as.type_query.type.as.array.element, nullptr);
    EXPECT_EQ(alignment->as.type_query.type.as.array.element->kind,
              LUNA_TYPE_U16);

    const LunaStatement *offset_declaration = alignment_declaration->next;
    ASSERT_NE(offset_declaration, nullptr);
    const LunaExpression *offset =
        offset_declaration->as.declaration.initializer;
    ASSERT_NE(offset, nullptr);
    ASSERT_EQ(offset->kind, LUNA_EXPRESSION_OFFSETOF);
    EXPECT_EQ(offset->as.type_query.type.kind, LUNA_TYPE_NAMED);
    EXPECT_EQ(std::string(offset->as.type_query.member_name.data,
                          offset->as.type_query.member_name.length),
              "value");
}

TEST(ParserTest, RejectsAnExternalFunctionBody) {
    FrontendHarness harness{"module test.external_body;\n"
                            "extern fn c_value() -> i32 { return 42; }\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.Parse());
    EXPECT_NE(harness.Diagnostics().find(
                  "external function declaration must end with ';'"),
              std::string::npos);
}

TEST(ParserTest, ReportsMissingDelimiterWithoutHanging) {
    FrontendHarness harness{"module test.bad;\n"
                            "fn main() -> i32 {\n"
                            "    let answer: i32 = 42\n"
                            "    return answer;\n"
                            "}\n"};

    EXPECT_FALSE(harness.Parse());
    EXPECT_GT(harness.ErrorCount(), 0U);
    EXPECT_NE(harness.Diagnostics().find("expected ';'"), std::string::npos);
}

TEST(ParserTest, ReportsMalformedNamedAggregateInitializers) {
    FrontendHarness missing_equal{"module test.missing_initializer_equal;\n"
                                  "struct Item { value: i32; }\n"
                                  "fn main() -> i32 {\n"
                                  "    var item: Item = { value 42, };\n"
                                  "    return 0;\n"
                                  "}\n"};
    EXPECT_FALSE(missing_equal.Parse());
    EXPECT_NE(missing_equal.Diagnostics().find(
                  "expected '=' after aggregate initializer field name"),
              std::string::npos);

    FrontendHarness missing_comma{
        "module test.missing_initializer_comma;\n"
        "struct Item { first: i32; second: i32; }\n"
        "fn main() -> i32 {\n"
        "    var item: Item = { first = 1 second = 2 };\n"
        "    return 0;\n"
        "}\n"};
    EXPECT_FALSE(missing_comma.Parse());
    EXPECT_NE(missing_comma.Diagnostics().find(
                  "expected '}' after aggregate initializer"),
              std::string::npos);
}

TEST(ParserTest, RejectsInvalidIntegerSeparators) {
    for (const std::string literal : {"1_", "1__2", "0x", "0b_", "0o_"}) {
        FrontendHarness harness{"module test.integer;\n"
                                "fn main() -> i32 { return " +
                                literal + "; }\n"};
        EXPECT_FALSE(harness.Parse()) << literal;
        EXPECT_GT(harness.ErrorCount(), 0U) << literal;
    }
}

TEST(ParserTest, RejectsPathologicalNestingWithoutStackOverflow) {
    std::string source{"module test.depth;\nfn main() -> i32 { return "};
    source.append(300U, '(');
    source += "1";
    source.append(300U, ')');
    source += "; }\n";

    FrontendHarness harness{source};
    EXPECT_FALSE(harness.Parse());
    EXPECT_NE(harness.Diagnostics().find("maximum parser nesting depth"),
              std::string::npos);
}

TEST(ParserTest, ParsesI64FunctionAndLocalTypes) {
    FrontendHarness harness{"module test.i64_types;\n"
                            "fn widen(value: i64) -> i64 {\n"
                            "    let copy: i64 = value;\n"
                            "    return copy;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_I64);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_I64);
}

TEST(ParserTest, ParsesUnsignedFunctionTypesAndMaximumLiteral) {
    FrontendHarness harness{"module test.unsigned_types;\n"
                            "fn widen(value: u32) -> u64 {\n"
                            "    let maximum: u64 = 18446744073709551615;\n"
                            "    return maximum;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_U64);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_U32);
    ASSERT_NE(function->body, nullptr);
    const LunaStatement *declaration = function->body->first;
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->kind, LUNA_STATEMENT_DECLARATION);
    ASSERT_NE(declaration->as.declaration.initializer, nullptr);
    EXPECT_EQ(declaration->as.declaration.initializer->as.integer, UINT64_MAX);
}

TEST(ParserTest, ParsesAllNarrowIntegerTypes) {
    FrontendHarness harness{"module test.narrow_types;\n"
                            "fn signed_pair(left: i8, right: i16) -> i16 {\n"
                            "    return left as i16;\n"
                            "}\n"
                            "fn unsigned_pair(left: u8, right: u16) -> u16 {\n"
                            "    return left as u16;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *signed_function = harness.Program()->first_function;
    ASSERT_NE(signed_function, nullptr);
    EXPECT_EQ(signed_function->return_type.kind, LUNA_TYPE_I16);
    ASSERT_NE(signed_function->first_parameter, nullptr);
    EXPECT_EQ(signed_function->first_parameter->type.kind, LUNA_TYPE_I8);
    ASSERT_NE(signed_function->first_parameter->next, nullptr);
    EXPECT_EQ(signed_function->first_parameter->next->type.kind, LUNA_TYPE_I16);

    const LunaFunction *unsigned_function = signed_function->next;
    ASSERT_NE(unsigned_function, nullptr);
    EXPECT_EQ(unsigned_function->return_type.kind, LUNA_TYPE_U16);
    ASSERT_NE(unsigned_function->first_parameter, nullptr);
    EXPECT_EQ(unsigned_function->first_parameter->type.kind, LUNA_TYPE_U8);
    ASSERT_NE(unsigned_function->first_parameter->next, nullptr);
    EXPECT_EQ(unsigned_function->first_parameter->next->type.kind,
              LUNA_TYPE_U16);
}

TEST(ParserTest, ParsesPointerSizedIntegerTypes) {
    FrontendHarness harness{
        "module test.pointer_sized_types;\n"
        "fn convert(offset: isize, size: usize) -> usize {\n"
        "    let adjusted: isize = offset;\n"
        "    return adjusted as usize;\n"
        "}\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_USIZE);
    ASSERT_NE(function->first_parameter, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_ISIZE);
    ASSERT_NE(function->first_parameter->next, nullptr);
    EXPECT_EQ(function->first_parameter->next->type.kind, LUNA_TYPE_USIZE);
    ASSERT_NE(function->body, nullptr);
    ASSERT_NE(function->body->first, nullptr);
    EXPECT_EQ(function->body->first->as.declaration.type.kind, LUNA_TYPE_ISIZE);
}

TEST(ParserTest, RejectsIntegerMagnitudeBeyondU64Maximum) {
    FrontendHarness harness{"module test.integer_magnitude;\n"
                            "fn value() -> u64 {\n"
                            "    return 18446744073709551616;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    EXPECT_FALSE(harness.Parse());
    EXPECT_NE(harness.Diagnostics().find("integer literal is too large"),
              std::string::npos);
}

TEST(ParserTest, BuildsChainedExplicitConversionNodes) {
    FrontendHarness harness{"module test.conversion_syntax;\n"
                            "fn main() -> i32 {\n"
                            "    return (1 as i64) as i32;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);
    const LunaStatement *statement = function->body->first;
    ASSERT_NE(statement, nullptr);
    ASSERT_EQ(statement->kind, LUNA_STATEMENT_RETURN);
    const LunaExpression *outer = statement->as.return_value;
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(outer->kind, LUNA_EXPRESSION_CAST);
    EXPECT_EQ(outer->as.cast.target_type.kind, LUNA_TYPE_I32);
    const LunaExpression *inner = outer->as.cast.operand;
    ASSERT_NE(inner, nullptr);
    ASSERT_EQ(inner->kind, LUNA_EXPRESSION_CAST);
    EXPECT_EQ(inner->as.cast.target_type.kind, LUNA_TYPE_I64);
}

TEST(ParserTest, ParsesFloatingTypesAndPreservesLiteralSpelling) {
    FrontendHarness harness{"module test.floating_syntax;\n"
                            "fn blend(left: f32, right: f64) -> f64 {\n"
                            "    let value: f64 = 1_2.5_0e-1;\n"
                            "    return value;\n"
                            "}\n"
                            "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->first_parameter, nullptr);
    ASSERT_NE(function->first_parameter->next, nullptr);
    EXPECT_EQ(function->first_parameter->type.kind, LUNA_TYPE_F32);
    EXPECT_EQ(function->first_parameter->next->type.kind, LUNA_TYPE_F64);
    EXPECT_EQ(function->return_type.kind, LUNA_TYPE_F64);
    ASSERT_NE(function->body, nullptr);
    ASSERT_NE(function->body->first, nullptr);
    ASSERT_EQ(function->body->first->kind, LUNA_STATEMENT_DECLARATION);
    const LunaExpression *initializer =
        function->body->first->as.declaration.initializer;
    ASSERT_NE(initializer, nullptr);
    EXPECT_EQ(initializer->kind, LUNA_EXPRESSION_FLOAT);
    EXPECT_EQ(std::string(initializer->as.floating.data,
                          initializer->as.floating.length),
              "1_2.5_0e-1");
}

TEST(ParserTest, RejectsMalformedFloatingPointLiterals) {
    for (const std::string literal : {"1e", "1e+", "1_.0", "1.0_", "1.0e_2"}) {
        FrontendHarness harness{"module test.bad_float;\n"
                                "fn value() -> f64 { return " +
                                literal +
                                "; }\n"
                                "fn main() -> i32 { return 0; }\n"};
        EXPECT_FALSE(harness.Parse()) << literal;
        EXPECT_NE(harness.Diagnostics().find("invalid floating-point literal"),
                  std::string::npos)
            << literal << '\n'
            << harness.Diagnostics();
    }
}

TEST(ParserTest, BuildsConditionalAndStructuredControlFlowNodes) {
    FrontendHarness harness{
        "module test.structured_syntax;\n"
        "fn main() -> i32 {\n"
        "    let selected: i32 = true ? 1 : false ? 2 : 3;\n"
        "    do { return selected; } while (false);\n"
        "    for (var index: i32 = 0; index < 4; index += 1) {\n"
        "        switch (index) {\n"
        "            case -1, 0 { break; }\n"
        "            default { continue; }\n"
        "        }\n"
        "    }\n"
        "    return selected;\n"
        "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *function = harness.Program()->first_function;
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->body, nullptr);

    const LunaStatement *declaration = function->body->first;
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->kind, LUNA_STATEMENT_DECLARATION);
    const LunaExpression *conditional = declaration->as.declaration.initializer;
    ASSERT_NE(conditional, nullptr);
    ASSERT_EQ(conditional->kind, LUNA_EXPRESSION_CONDITIONAL);
    ASSERT_NE(conditional->as.conditional.else_expression, nullptr);
    EXPECT_EQ(conditional->as.conditional.else_expression->kind,
              LUNA_EXPRESSION_CONDITIONAL);

    const LunaStatement *do_statement = declaration->next;
    ASSERT_NE(do_statement, nullptr);
    EXPECT_EQ(do_statement->kind, LUNA_STATEMENT_DO);
    ASSERT_NE(do_statement->as.do_statement.body, nullptr);
    ASSERT_NE(do_statement->as.do_statement.condition, nullptr);

    const LunaStatement *for_statement = do_statement->next;
    ASSERT_NE(for_statement, nullptr);
    ASSERT_EQ(for_statement->kind, LUNA_STATEMENT_FOR);
    ASSERT_NE(for_statement->as.for_statement.initializer, nullptr);
    EXPECT_EQ(for_statement->as.for_statement.initializer->kind,
              LUNA_STATEMENT_DECLARATION);
    ASSERT_NE(for_statement->as.for_statement.condition, nullptr);
    ASSERT_NE(for_statement->as.for_statement.update, nullptr);
    EXPECT_EQ(for_statement->as.for_statement.update->kind,
              LUNA_STATEMENT_ASSIGNMENT);

    const LunaBlock *for_body = for_statement->as.for_statement.body;
    ASSERT_NE(for_body, nullptr);
    const LunaStatement *switch_statement = for_body->first;
    ASSERT_NE(switch_statement, nullptr);
    ASSERT_EQ(switch_statement->kind, LUNA_STATEMENT_SWITCH);
    EXPECT_EQ(switch_statement->as.switch_statement.arm_count, 2U);
    const LunaSwitchArm *first_arm =
        switch_statement->as.switch_statement.first_arm;
    ASSERT_NE(first_arm, nullptr);
    EXPECT_FALSE(first_arm->is_default);
    EXPECT_EQ(first_arm->label_count, 2U);
    ASSERT_NE(first_arm->next, nullptr);
    EXPECT_TRUE(first_arm->next->is_default);
}

TEST(ParserTest, AcceptsEmptyForClauses) {
    FrontendHarness harness{"module test.empty_for_clauses;\n"
                            "fn main() -> i32 {\n"
                            "    for (;;) { break; }\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaStatement *statement =
        harness.Program()->first_function->body->first;
    ASSERT_NE(statement, nullptr);
    ASSERT_EQ(statement->kind, LUNA_STATEMENT_FOR);
    EXPECT_EQ(statement->as.for_statement.initializer, nullptr);
    EXPECT_EQ(statement->as.for_statement.condition, nullptr);
    EXPECT_EQ(statement->as.for_statement.update, nullptr);
}

TEST(ParserTest, BuildsPointerArrayStringAndLvalueNodes) {
    FrontendHarness harness{"module test.memory_syntax;\n"
                            "fn inspect(input: *const [3]u8) -> *const u8 {\n"
                            "    return \"ok\\n\";\n"
                            "}\n"
                            "fn main() -> i32 {\n"
                            "    var matrix: [2][3]i16 = {};\n"
                            "    matrix[1][2] = -7;\n"
                            "    let pointer: *i16 = &matrix[1][2];\n"
                            "    let absent: *i16 = null;\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();
    const LunaFunction *inspect = harness.Program()->first_function;
    ASSERT_NE(inspect, nullptr);
    ASSERT_NE(inspect->first_parameter, nullptr);
    const LunaTypeRef &pointer_type = inspect->first_parameter->type;
    ASSERT_EQ(pointer_type.kind, LUNA_TYPE_POINTER);
    EXPECT_TRUE(pointer_type.as.pointer.is_read_only);
    ASSERT_NE(pointer_type.as.pointer.pointee, nullptr);
    const LunaTypeRef &array_type = *pointer_type.as.pointer.pointee;
    ASSERT_EQ(array_type.kind, LUNA_TYPE_ARRAY);
    EXPECT_EQ(array_type.as.array.count, 3U);
    ASSERT_NE(array_type.as.array.element, nullptr);
    EXPECT_EQ(array_type.as.array.element->kind, LUNA_TYPE_U8);
    ASSERT_NE(inspect->body, nullptr);
    ASSERT_NE(inspect->body->first, nullptr);
    ASSERT_NE(inspect->body->first->as.return_value, nullptr);
    EXPECT_EQ(inspect->body->first->as.return_value->kind,
              LUNA_EXPRESSION_STRING);

    const LunaFunction *main_function = inspect->next;
    ASSERT_NE(main_function, nullptr);
    const LunaStatement *declaration = main_function->body->first;
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->kind, LUNA_STATEMENT_DECLARATION);
    ASSERT_EQ(declaration->as.declaration.type.kind, LUNA_TYPE_ARRAY);
    ASSERT_NE(declaration->as.declaration.type.as.array.element, nullptr);
    EXPECT_EQ(declaration->as.declaration.type.as.array.element->kind,
              LUNA_TYPE_ARRAY);
    EXPECT_EQ(declaration->as.declaration.initializer->kind,
              LUNA_EXPRESSION_ZERO_INITIALIZER);

    const LunaStatement *assignment = declaration->next;
    ASSERT_NE(assignment, nullptr);
    ASSERT_EQ(assignment->kind, LUNA_STATEMENT_ASSIGNMENT);
    ASSERT_NE(assignment->as.assignment.target, nullptr);
    ASSERT_EQ(assignment->as.assignment.target->kind, LUNA_EXPRESSION_INDEX);
    ASSERT_NE(assignment->as.assignment.target->as.index.base, nullptr);
    EXPECT_EQ(assignment->as.assignment.target->as.index.base->kind,
              LUNA_EXPRESSION_INDEX);

    const LunaStatement *pointer_declaration = assignment->next;
    ASSERT_NE(pointer_declaration, nullptr);
    ASSERT_NE(pointer_declaration->as.declaration.initializer, nullptr);
    EXPECT_EQ(pointer_declaration->as.declaration.initializer->kind,
              LUNA_EXPRESSION_UNARY);
    EXPECT_EQ(
        pointer_declaration->as.declaration.initializer->as.unary.operator_kind,
        LUNA_TOKEN_AMPERSAND);

    const LunaStatement *null_declaration = pointer_declaration->next;
    ASSERT_NE(null_declaration, nullptr);
    ASSERT_NE(null_declaration->as.declaration.initializer, nullptr);
    EXPECT_EQ(null_declaration->as.declaration.initializer->kind,
              LUNA_EXPRESSION_NULL);
}

}
