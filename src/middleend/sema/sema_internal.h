#ifndef LUNA_SEMA_INTERNAL_H
#define LUNA_SEMA_INTERNAL_H

#include "luna/middleend/sema/sema.h"

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int LunaSemaTypeId;

typedef struct LunaSemaFunction {
    const LunaFunction *syntax;
    LunaStringView module_name;
    LunaSemaTypeId return_type;
    LunaVector parameter_types;
    LunaIrFunctionId ir_id;
    bool is_exported;
} LunaSemaFunction;

typedef enum LunaSemaLayoutState {
    LUNA_SEMA_LAYOUT_UNRESOLVED,
    LUNA_SEMA_LAYOUT_RESOLVING,
    LUNA_SEMA_LAYOUT_RESOLVED,
    LUNA_SEMA_LAYOUT_INVALID
} LunaSemaLayoutState;

typedef struct LunaSemaType {
    LunaTypeKind kind;
    LunaSemaTypeId element_type;
    uint64_t array_count;
    bool is_read_only;
    const LunaTypeDeclaration *declaration;
    uint64_t size_bytes;
    uint32_t alignment_bytes;
    LunaSemaLayoutState layout_state;
} LunaSemaType;

typedef struct LunaSemaNamedType {
    const LunaTypeDeclaration *syntax;
    LunaStringView module_name;
    LunaSemaTypeId type;
    bool is_exported;
} LunaSemaNamedType;

typedef struct LunaSemaField {
    const LunaField *syntax;
    LunaSemaTypeId owner_type;
    LunaSemaTypeId type;
    uint64_t offset;
} LunaSemaField;

typedef struct LunaSemaEnumMember {
    const LunaEnumMember *syntax;
    LunaSemaTypeId owner_type;
    uint64_t value;
} LunaSemaEnumMember;

typedef struct LunaSemaLocal {
    LunaStringView name;
    LunaSemaTypeId type;
    LunaIrSlotId slot;
    uint32_t scope_depth;
    bool is_mutable;
} LunaSemaLocal;

typedef struct LunaSemaControlFrame {
    LunaIrBlockId break_block;
    LunaIrBlockId continue_block;
    bool has_live_break;
    bool has_live_continue;
} LunaSemaControlFrame;

typedef struct LunaSemaSwitchArm {
    const LunaSwitchArm *syntax;
    LunaIrBlockId body_block;
} LunaSemaSwitchArm;

typedef struct LunaSemaSwitchLabel {
    uint64_t value;
    LunaSourceSpan span;
    LunaIrBlockId body_block;
} LunaSemaSwitchLabel;

typedef struct LunaCheckedValue {
    LunaIrValueId id;
    LunaSemaTypeId type;
} LunaCheckedValue;

typedef struct LunaSemaCallArgument {
    LunaIrValueId value;
    LunaIrSlotId preserved_slot;
    LunaSemaTypeId type;
    bool is_aggregate;
} LunaSemaCallArgument;

typedef enum LunaSemaLvalueStorage {
    LUNA_SEMA_LVALUE_INVALID,
    LUNA_SEMA_LVALUE_SLOT,
    LUNA_SEMA_LVALUE_ADDRESS
} LunaSemaLvalueStorage;

typedef struct LunaCheckedLvalue {
    LunaSemaTypeId type;
    LunaSemaLvalueStorage storage;
    LunaIrSlotId slot;
    LunaIrValueId address;
    bool is_mutable;
    bool requires_null_check;
} LunaCheckedLvalue;

typedef struct LunaSemaContext {
    const LunaProgram *interface_unit;
    const LunaProgram *implementation_unit;
    LunaDiagnosticEngine *diagnostics;
    LunaIrModule *module;
    LunaVector functions;
    LunaVector visible_functions;
    LunaVector types;
    LunaVector named_types;
    LunaVector visible_named_types;
    LunaVector fields;
    LunaVector enum_members;
    LunaVector locals;
    LunaVector control_frames;
    LunaIrFunction *current_function;
    LunaSemaFunction *current_semantic_function;
    const LunaFunction *current_syntax_function;
    LunaIrBlockId current_block;
    uint32_t scope_depth;
    uint64_t current_metadata_content_hash;
    bool reachable;
    bool checking_dead_code;
    bool allocation_failed;
    bool failed;
    bool current_interface_is_metadata;
} LunaSemaContext;

const LunaSemaType *luna_sema_type(const LunaSemaContext *context,
                                   LunaSemaTypeId type);
bool luna_sema_initialize_types(LunaSemaContext *context);
const LunaSemaNamedType *
luna_sema_find_named_type(const LunaSemaContext *context, LunaStringView name);
const LunaSemaField *luna_sema_find_field(const LunaSemaContext *context,
                                          LunaSemaTypeId owner_type,
                                          LunaStringView name);
const LunaSemaEnumMember *
luna_sema_find_enum_member(const LunaSemaContext *context,
                           LunaSemaTypeId owner_type, LunaStringView name);
LunaTypeKind luna_sema_type_kind(const LunaSemaContext *context,
                                 LunaSemaTypeId type);
bool luna_sema_is_pointer_type(const LunaSemaContext *context,
                               LunaSemaTypeId type);
bool luna_sema_is_array_type(const LunaSemaContext *context,
                             LunaSemaTypeId type);
bool luna_sema_is_record_type(const LunaSemaContext *context,
                              LunaSemaTypeId type);
bool luna_sema_is_memory_type(const LunaSemaContext *context,
                              LunaSemaTypeId type);
bool luna_sema_is_enum_type(const LunaSemaContext *context,
                            LunaSemaTypeId type);
LunaIrType luna_sema_ir_type(const LunaSemaContext *context,
                             LunaSemaTypeId type);
void luna_sema_report_allocation_failure(LunaSemaContext *context);
void luna_sema_fail(LunaSemaContext *context, LunaSourceSpan span,
                    const char *format, ...) LUNA_PRINTF_LIKE(3, 4);
void luna_sema_fail_plain(LunaSemaContext *context, const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);
bool luna_sema_append_type_name(const LunaSemaContext *context,
                                LunaSemaTypeId type, LunaStringBuilder *output);
bool luna_sema_type_layout(LunaSemaContext *context, LunaSemaTypeId type,
                           uint64_t *size_bytes, uint32_t *alignment_bytes);
bool luna_sema_build_aggregate_layout(LunaSemaContext *context,
                                      LunaSemaTypeId type,
                                      LunaIrAggregateLayout *layout);
LunaSemaTypeId luna_sema_resolve_type(LunaSemaContext *context,
                                      const LunaTypeRef *type_ref);
LunaSemaTypeId luna_sema_pointer_type(LunaSemaContext *context,
                                      LunaSemaTypeId pointee,
                                      bool is_read_only);
bool luna_sema_validate_exported_type(LunaSemaContext *context,
                                      LunaSemaTypeId type, LunaSourceSpan span,
                                      LunaStringView exported_name);
bool luna_sema_collect_named_types(LunaSemaContext *context,
                                   const LunaProgram *program);
bool luna_sema_validate_exported_type_declarations(LunaSemaContext *context,
                                                   size_t first_named_type);
bool luna_sema_resolve_type_declarations(LunaSemaContext *context,
                                         size_t first_named_type);
bool luna_sema_pointer_conversion_removes_read_only(
    const LunaSemaContext *context, LunaSemaTypeId source_type,
    LunaSemaTypeId target_type);
LunaIrInstruction luna_sema_instruction(LunaIrOpcode opcode,
                                        LunaSourceSpan span);
void luna_sema_append_instruction(LunaSemaContext *context,
                                  const LunaIrInstruction *instruction);
LunaIrBlockId luna_sema_add_block(LunaSemaContext *context);
void luna_sema_emit_jump(LunaSemaContext *context, LunaIrBlockId target,
                         LunaSourceSpan span);
void luna_sema_emit_branch(LunaSemaContext *context, LunaIrValueId condition,
                           LunaIrBlockId true_block, LunaIrBlockId false_block,
                           LunaSourceSpan span);
void luna_sema_set_block(LunaSemaContext *context, LunaIrBlockId block_id);
LunaIrValueId luna_sema_emit_value_instruction(LunaSemaContext *context,
                                               LunaIrInstruction *instruction,
                                               LunaSemaTypeId type);
LunaIrSlotId luna_sema_preserve_value(LunaSemaContext *context,
                                      LunaCheckedValue value,
                                      LunaSourceSpan span);
LunaCheckedValue luna_sema_reload_value(LunaSemaContext *context,
                                        LunaIrSlotId slot, LunaSemaTypeId type,
                                        LunaSourceSpan span);
LunaSemaFunction *luna_sema_find_function(LunaSemaContext *context,
                                          LunaStringView name);
bool luna_sema_add_imported_scope(LunaSemaContext *context,
                                  const LunaSemaImport *imports,
                                  uint32_t import_count);
LunaSemaLocal *luna_sema_find_local(LunaSemaContext *context,
                                    LunaStringView name);
LunaSemaLocal *luna_sema_find_local_in_current_scope(LunaSemaContext *context,
                                                     LunaStringView name);
bool luna_sema_add_local(LunaSemaContext *context, LunaStringView name,
                         LunaSemaTypeId type, LunaIrSlotId slot,
                         bool is_mutable);
void luna_sema_enter_scope(LunaSemaContext *context);
void luna_sema_leave_scope(LunaSemaContext *context);
void luna_sema_require_type(LunaSemaContext *context, LunaCheckedValue value,
                            LunaSemaTypeId expected, LunaSourceSpan span);
LunaCheckedValue luna_sema_invalid_value(void);
bool luna_sema_is_integer_type(LunaSemaTypeId type);
bool luna_sema_is_float_type(LunaSemaTypeId type);
bool luna_sema_is_numeric_type(LunaSemaTypeId type);
LunaIrOpcode luna_sema_binary_integer_opcode(LunaTokenKind operator_kind);
bool luna_sema_binary_float_opcode(LunaTokenKind operator_kind,
                                   LunaIrOpcode *opcode);
uint64_t luna_sema_integer_maximum(const LunaSemaContext *context,
                                   LunaSemaTypeId type);
LunaCheckedValue luna_sema_lower_expression(LunaSemaContext *context,
                                            const LunaExpression *expression);
const LunaSemaEnumMember *
luna_sema_scoped_enum_member(LunaSemaContext *context,
                             const LunaExpression *expression,
                             LunaSemaTypeId *enum_type);
bool luna_sema_expression_can_branch(const LunaExpression *expression);
LunaIrValueId luna_sema_lvalue_address(LunaSemaContext *context,
                                       const LunaCheckedLvalue *lvalue,
                                       LunaSourceSpan span);
LunaCheckedLvalue luna_sema_lower_lvalue(LunaSemaContext *context,
                                         const LunaExpression *expression);
LunaCheckedValue luna_sema_load_lvalue(LunaSemaContext *context,
                                       const LunaCheckedLvalue *lvalue,
                                       LunaSourceSpan span);
void luna_sema_store_lvalue(LunaSemaContext *context,
                            const LunaCheckedLvalue *lvalue,
                            LunaCheckedValue value, LunaSourceSpan span);
bool luna_sema_is_brace_initializer(const LunaExpression *expression);
LunaIrSlotId luna_sema_allocate_memory_slot(LunaSemaContext *context,
                                            LunaSemaTypeId type,
                                            LunaSourceSpan span);
void luna_sema_emit_memory_copy(LunaSemaContext *context,
                                const LunaCheckedLvalue *destination,
                                const LunaCheckedLvalue *source,
                                LunaSourceSpan span);
LunaCheckedLvalue
luna_sema_lower_memory_source(LunaSemaContext *context,
                              const LunaExpression *expression,
                              LunaSemaTypeId expected_type);
bool luna_sema_initialize_slot_value(LunaSemaContext *context,
                                     LunaIrSlotId slot,
                                     LunaSemaTypeId root_type,
                                     LunaSemaTypeId value_type, uint64_t offset,
                                     const LunaExpression *initializer);
void luna_sema_zero_memory_slot(LunaSemaContext *context, LunaIrSlotId slot,
                                LunaSourceSpan span);
LunaCheckedLvalue
luna_sema_lower_memory_assignment_value(LunaSemaContext *context,
                                        const LunaExpression *initializer,
                                        LunaSemaTypeId expected_type);
LunaCheckedValue
luna_sema_lower_expression_expected(LunaSemaContext *context,
                                    const LunaExpression *expression,
                                    LunaSemaTypeId expected_type);
void luna_sema_lower_block(LunaSemaContext *context, const LunaBlock *block,
                           bool create_scope);
bool luna_sema_lower_modules(const LunaSemaModule *modules,
                             uint32_t module_count,
                             LunaDiagnosticEngine *diagnostics,
                             LunaIrModule *module);
bool luna_sema_lower_module(const LunaProgram *interface_unit,
                            const LunaProgram *implementation_unit,
                            LunaDiagnosticEngine *diagnostics,
                            LunaIrModule *module);
bool luna_sema_lower(const LunaProgram *program,
                     LunaDiagnosticEngine *diagnostics, LunaIrModule *module);

#endif
