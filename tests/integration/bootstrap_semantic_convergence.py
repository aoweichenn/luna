from __future__ import annotations

import dataclasses
import random


@dataclasses.dataclass(frozen=True)
class SourceUnit:
    name: str
    content: bytes | None = None


@dataclasses.dataclass(frozen=True)
class ExecutionCase:
    name: str
    units: tuple[SourceUnit, ...]
    expected_code: int
    features: frozenset[str]
    verify_reversed_order: bool = False


@dataclasses.dataclass(frozen=True)
class RejectionCase:
    name: str
    units: tuple[SourceUnit, ...]
    diagnostic_kind: int


REQUIRED_EXECUTION_FEATURES = frozenset(
    (
        "aggregate-abi",
        "aggregate-initialization",
        "aggregate-layout",
        "array",
        "call-abi",
        "conditional",
        "control-flow",
        "enum",
        "floating-point",
        "integer-conversion",
        "module-graph",
        "pointer",
        "pointer-sized-integer",
        "recursion",
        "runtime-trap",
        "short-circuit",
        "signed-integer",
        "source-order",
        "string",
        "switch",
        "union",
        "unsigned-integer",
        "whole-object-copy",
    )
)


def _file(name: str) -> SourceUnit:
    return SourceUnit(name)


def _inline(name: str, content: str) -> SourceUnit:
    return SourceUnit(name, content.encode("utf-8"))


def _execution_case(
    name: str,
    source_name: str,
    expected_code: int,
    *features: str,
) -> ExecutionCase:
    return ExecutionCase(
        name,
        (_file(source_name),),
        expected_code,
        frozenset(features),
    )


def fixed_execution_cases() -> tuple[ExecutionCase, ...]:
    cases = (
        _execution_case(
            "signed-i32",
            "defined_i32_semantics.luna",
            42,
            "signed-integer",
        ),
        _execution_case(
            "signed-i64",
            "defined_i64_semantics.luna",
            42,
            "signed-integer",
        ),
        _execution_case(
            "narrow-integers",
            "narrow_integer_operations.luna",
            42,
            "signed-integer",
            "unsigned-integer",
        ),
        _execution_case(
            "unsigned-integers",
            "unsigned_operations.luna",
            42,
            "unsigned-integer",
        ),
        _execution_case(
            "pointer-sized-integers",
            "pointer_sized_integer_operations.luna",
            42,
            "pointer-sized-integer",
        ),
        _execution_case(
            "scalar-conversions",
            "scalar_conversions.luna",
            42,
            "floating-point",
            "integer-conversion",
        ),
        _execution_case(
            "floating-point",
            "floating_operations.luna",
            42,
            "floating-point",
        ),
        _execution_case(
            "scalar-zero-initializers",
            "zero_initializer_scalars.luna",
            42,
            "enum",
            "floating-point",
            "pointer",
            "signed-integer",
            "unsigned-integer",
        ),
        _execution_case(
            "structured-control",
            "structured_control_flow.luna",
            42,
            "conditional",
            "control-flow",
            "short-circuit",
            "switch",
        ),
        _execution_case(
            "memory",
            "memory_operations.luna",
            42,
            "array",
            "pointer",
            "string",
        ),
        _execution_case(
            "memory-evaluation-order",
            "memory_control_flow.luna",
            42,
            "array",
            "control-flow",
            "pointer",
        ),
        _execution_case(
            "aggregate-layout",
            "aggregate_types.luna",
            42,
            "aggregate-layout",
            "array",
            "enum",
            "pointer",
            "switch",
            "union",
        ),
        _execution_case(
            "aggregate-initialization",
            "aggregate_initialization.luna",
            42,
            "aggregate-initialization",
            "array",
            "enum",
            "union",
            "whole-object-copy",
        ),
        _execution_case(
            "aggregate-by-value",
            "aggregate_by_value.luna",
            42,
            "aggregate-abi",
            "array",
            "floating-point",
            "union",
            "whole-object-copy",
        ),
        _execution_case(
            "overlap-copy",
            "memory_copy_overlap.luna",
            42,
            "array",
            "whole-object-copy",
        ),
        _execution_case(
            "integer-call-abi",
            "stack_arguments.luna",
            42,
            "call-abi",
        ),
        _execution_case(
            "floating-call-abi",
            "too_many_float_arguments.luna",
            42,
            "call-abi",
            "floating-point",
        ),
        _execution_case(
            "recursion",
            "recursive_factorial.luna",
            120,
            "recursion",
        ),
        _execution_case(
            "division-trap",
            "division_by_zero.luna",
            -8,
            "runtime-trap",
        ),
        _execution_case(
            "null-trap",
            "null_dereference.luna",
            -4,
            "pointer",
            "runtime-trap",
        ),
        _execution_case(
            "bounds-trap",
            "array_out_of_bounds.luna",
            -4,
            "array",
            "runtime-trap",
        ),
        _execution_case(
            "conversion-trap",
            "float_to_integer_nan_trap.luna",
            -4,
            "floating-point",
            "integer-conversion",
            "runtime-trap",
        ),
        ExecutionCase(
            "module-graph",
            (
                _file("module_import_app.luna"),
                _file("module_import_math_implementation.luna"),
                _file("module_import_core_interface.luna"),
                _file("module_import_math_interface.luna"),
                _file("module_import_core_implementation.luna"),
            ),
            42,
            frozenset(("module-graph", "source-order")),
            True,
        ),
    )
    covered = frozenset(
        feature for case in cases for feature in case.features
    )
    missing = REQUIRED_EXECUTION_FEATURES - covered
    if missing:
        raise AssertionError(
            "semantic convergence execution corpus is missing features: "
            + ", ".join(sorted(missing))
        )
    return cases


def generated_execution_cases(
    count: int = 32,
) -> tuple[ExecutionCase, ...]:
    generator = random.Random(0x4C554E41434F4E56)
    cases: list[ExecutionCase] = []
    for case_index in range(count):
        values = [generator.randrange(0, 21) for _ in range(4)]
        bias = generator.randrange(20, 61)
        multiplier = generator.randrange(1, 4)
        delta = generator.randrange(0, 5)
        rounds = generator.randrange(1, 13)
        mask = generator.randrange(1, 1 << 16)

        expected = bias
        for index in range(rounds):
            expected += values[index % len(values)] * multiplier
            expected += delta if index % 2 == 0 else -delta
        remainder = expected & 3
        expected += 1 if remainder == 0 else 2 if remainder == 1 else 3
        expected %= 200

        assignments = "\n".join(
            f"    values[{index}] = {value};"
            for index, value in enumerate(values)
        )
        source = (
            f"module convergence.random_{case_index};\n"
            "struct State {\n"
            "    total: i32;\n"
            "    values: [4]i32;\n"
            "}\n"
            "fn fold(state: *State, rounds: i32) -> i32 {\n"
            "    for (var index: i32 = 0; index < rounds; index += 1) {\n"
            "        let slot: usize = (index % 4) as usize;\n"
            f"        state->total += state->values[slot] * {multiplier};\n"
            "        if ((index & 1) == 0) {\n"
            f"            state->total += {delta};\n"
            "        } else {\n"
            f"            state->total -= {delta};\n"
            "        }\n"
            "    }\n"
            "    return state->total;\n"
            "}\n"
            "fn main() -> i32 {\n"
            "    var values: [4]i32 = {};\n"
            f"{assignments}\n"
            f"    var state: State = {{ total = {bias}, values = values }};\n"
            f"    let folded: i32 = fold(&state, {rounds});\n"
            "    let wide: i64 = folded as i64;\n"
            f"    let encoded: u64 = (wide as u64) ^ {mask} as u64;\n"
            f"    let decoded: i32 = (encoded ^ {mask} as u64) as i32;\n"
            "    let real: f64 = decoded as f64;\n"
            "    var result: i32 = real as i32;\n"
            "    switch (result & 3) {\n"
            "        case 0 { result += 1; }\n"
            "        case 1 { result += 2; }\n"
            "        default { result += 3; }\n"
            "    }\n"
            "    return result % 200;\n"
            "}\n"
        )
        cases.append(
            ExecutionCase(
                f"generated-{case_index}",
                (
                    _inline(
                        f"convergence_random_{case_index}.luna",
                        source,
                    ),
                ),
                expected,
                frozenset(
                    (
                        "aggregate-initialization",
                        "array",
                        "control-flow",
                        "floating-point",
                        "integer-conversion",
                        "pointer",
                        "switch",
                    )
                ),
            )
        )
    return tuple(cases)


def rejection_cases() -> tuple[RejectionCase, ...]:
    cases = (
        RejectionCase(
            "duplicate-interface",
            (
                _file("module_pair_interface.luna"),
                _file("module_pair_interface.luna"),
                _file("module_pair_implementation.luna"),
            ),
            1,
        ),
        RejectionCase(
            "duplicate-implementation",
            (
                _file("module_pair_interface.luna"),
                _file("module_pair_implementation.luna"),
                _file("module_pair_implementation.luna"),
            ),
            2,
        ),
        RejectionCase(
            "unknown-import",
            (_file("import_pending.luna"),),
            3,
        ),
        RejectionCase(
            "self-import",
            (
                _inline(
                    "self_import.luna",
                    "module convergence.self_import;\n"
                    "import convergence.self_import;\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
            ),
            4,
        ),
        RejectionCase(
            "duplicate-import",
            (
                _file("module_import_duplicate_app_implementation.luna"),
                _file("module_import_core_implementation.luna"),
                _file("module_import_duplicate_app_interface.luna"),
                _file("module_import_core_interface.luna"),
            ),
            5,
        ),
        RejectionCase(
            "import-cycle",
            (
                _file("module_import_cycle_left_implementation.luna"),
                _file("module_import_cycle_right_interface.luna"),
                _file("module_import_cycle_left_interface.luna"),
                _file("module_import_cycle_right_implementation.luna"),
            ),
            6,
        ),
        RejectionCase(
            "unreachable-module",
            (
                _file("module_import_unreachable_app.luna"),
                _file("module_import_unreachable_library.luna"),
            ),
            7,
        ),
        RejectionCase(
            "duplicate-name",
            (_file("duplicate_type_declaration.luna"),),
            8,
        ),
        RejectionCase(
            "ambiguous-import",
            (
                _inline(
                    "ambiguous_app.luna",
                    "module convergence.ambiguous.app;\n"
                    "import convergence.ambiguous.left;\n"
                    "import convergence.ambiguous.right;\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
                _inline(
                    "ambiguous_left.interface.luna",
                    "export module convergence.ambiguous.left;\n"
                    "export fn value() -> i32;\n",
                ),
                _inline(
                    "ambiguous_left.luna",
                    "module convergence.ambiguous.left;\n"
                    "fn value() -> i32 { return 1; }\n",
                ),
                _inline(
                    "ambiguous_right.interface.luna",
                    "export module convergence.ambiguous.right;\n"
                    "export fn value() -> i32;\n",
                ),
                _inline(
                    "ambiguous_right.luna",
                    "module convergence.ambiguous.right;\n"
                    "fn value() -> i32 { return 2; }\n",
                ),
            ),
            9,
        ),
        RejectionCase(
            "invalid-export",
            (
                _file("module_pair_export_interface.luna"),
                _file("module_pair_export_implementation.luna"),
            ),
            10,
        ),
        RejectionCase(
            "private-type-exposure",
            (
                _inline(
                    "private_exposure.interface.luna",
                    "export module convergence.private_exposure;\n"
                    "struct Hidden { value: i32; }\n"
                    "export struct Public { hidden: Hidden; }\n"
                    "export fn read(value: *const Public) -> i32;\n",
                ),
                _inline(
                    "private_exposure.luna",
                    "module convergence.private_exposure;\n"
                    "fn read(value: *const Public) -> i32 {\n"
                    "    return value->hidden.value;\n"
                    "}\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
            ),
            11,
        ),
        RejectionCase(
            "unknown-type",
            (_file("unknown_named_type.luna"),),
            12,
        ),
        RejectionCase(
            "duplicate-field",
            (_file("duplicate_aggregate_field.luna"),),
            13,
        ),
        RejectionCase(
            "recursive-type",
            (_file("recursive_aggregate.luna"),),
            14,
        ),
        RejectionCase(
            "incomplete-type",
            (_file("void_local.luna"),),
            15,
        ),
        RejectionCase(
            "object-too-large",
            (_file("aggregate_layout_overflow.luna"),),
            16,
        ),
        RejectionCase(
            "invalid-enum-underlying",
            (_file("invalid_enum_underlying.luna"),),
            17,
        ),
        RejectionCase(
            "invalid-enum-value",
            (
                _inline(
                    "invalid_enum_value.luna",
                    "module convergence.invalid_enum_value;\n"
                    "enum Kind: u8 { value = 1 + 2 }\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
            ),
            18,
        ),
        RejectionCase(
            "enum-value-overflow",
            (
                _inline(
                    "enum_value_overflow.luna",
                    "module convergence.enum_value_overflow;\n"
                    "enum Kind: u8 { maximum = 255, overflow }\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
            ),
            19,
        ),
        RejectionCase(
            "duplicate-enum-member",
            (
                _inline(
                    "duplicate_enum_member.luna",
                    "module convergence.duplicate_enum_member;\n"
                    "enum Kind: u8 { value, value }\n"
                    "fn main() -> i32 { return 0; }\n",
                ),
            ),
            20,
        ),
        RejectionCase(
            "duplicate-function",
            (_file("duplicate_function.luna"),),
            21,
        ),
        RejectionCase(
            "missing-definition",
            (
                _file("module_pair_missing_interface.luna"),
                _file("module_pair_missing_implementation.luna"),
            ),
            22,
        ),
        RejectionCase(
            "signature-mismatch",
            (
                _file("module_pair_signature_interface.luna"),
                _file("module_pair_signature_implementation.luna"),
            ),
            23,
        ),
        RejectionCase(
            "invalid-external",
            (_file("external_reserved_start.luna"),),
            24,
        ),
        RejectionCase(
            "duplicate-parameter",
            (_file("duplicate_parameter.luna"),),
            25,
        ),
        RejectionCase(
            "unknown-name",
            (_file("for_scope_error.luna"),),
            26,
        ),
        RejectionCase(
            "duplicate-local",
            (_file("duplicate_local.luna"),),
            27,
        ),
        RejectionCase(
            "immutable-assignment",
            (_file("immutable_assignment.luna"),),
            28,
        ),
        RejectionCase(
            "type-mismatch",
            (_file("aggregate_initializer_field_type.luna"),),
            29,
        ),
        RejectionCase(
            "invalid-condition",
            (_file("type_error.luna"),),
            30,
        ),
        RejectionCase(
            "invalid-operator",
            (_file("invalid_float_operator.luna"),),
            31,
        ),
        RejectionCase(
            "invalid-cast",
            (_file("invalid_bool_conversion.luna"),),
            32,
        ),
        RejectionCase(
            "invalid-lvalue",
            (
                _inline(
                    "invalid_lvalue.luna",
                    "module convergence.invalid_lvalue;\n"
                    "fn value() -> i32 { return 1; }\n"
                    "fn main() -> i32 { value() = 2; return 0; }\n",
                ),
            ),
            33,
        ),
        RejectionCase(
            "invalid-call",
            (
                _inline(
                    "invalid_call.luna",
                    "module convergence.invalid_call;\n"
                    "fn main() -> i32 {\n"
                    "    let value: i32 = 1;\n"
                    "    return value();\n"
                    "}\n",
                ),
            ),
            34,
        ),
        RejectionCase(
            "argument-count",
            (_file("wrong_arity.luna"),),
            35,
        ),
        RejectionCase(
            "invalid-initializer",
            (_file("aggregate_initializer.luna"),),
            36,
        ),
        RejectionCase(
            "duplicate-initializer-field",
            (_file("aggregate_initializer_duplicate.luna"),),
            37,
        ),
        RejectionCase(
            "unknown-field",
            (_file("unknown_aggregate_field.luna"),),
            38,
        ),
        RejectionCase(
            "invalid-index",
            (_file("invalid_array_index_type.luna"),),
            39,
        ),
        RejectionCase(
            "invalid-member",
            (
                _inline(
                    "invalid_member.luna",
                    "module convergence.invalid_member;\n"
                    "fn main() -> i32 {\n"
                    "    let value: i32 = 1;\n"
                    "    return value.missing;\n"
                    "}\n",
                ),
            ),
            40,
        ),
        RejectionCase(
            "invalid-layout-query",
            (_file("invalid_sizeof_type.luna"),),
            41,
        ),
        RejectionCase(
            "break-outside-control",
            (_file("break_outside_loop.luna"),),
            42,
        ),
        RejectionCase(
            "continue-outside-loop",
            (_file("continue_outside_loop.luna"),),
            43,
        ),
        RejectionCase(
            "return-type",
            (_file("wrong_return_type.luna"),),
            44,
        ),
        RejectionCase(
            "missing-return",
            (_file("missing_return.luna"),),
            45,
        ),
        RejectionCase(
            "invalid-switch",
            (_file("switch_type_error.luna"),),
            46,
        ),
        RejectionCase(
            "duplicate-case",
            (_file("switch_duplicate_case.luna"),),
            47,
        ),
        RejectionCase(
            "duplicate-default",
            (_file("switch_duplicate_default.luna"),),
            48,
        ),
        RejectionCase(
            "integer-overflow",
            (_file("integer_overflow.luna"),),
            49,
        ),
        RejectionCase(
            "floating-overflow",
            (_file("f32_literal_overflow.luna"),),
            50,
        ),
    )
    expected_kinds = set(range(1, 51))
    actual_kinds = {case.diagnostic_kind for case in cases}
    if actual_kinds != expected_kinds or len(cases) != len(expected_kinds):
        raise AssertionError(
            "semantic convergence rejection corpus must cover every "
            "user-facing diagnostic kind exactly once"
        )
    return cases
