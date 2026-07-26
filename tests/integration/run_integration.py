#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import platform
import shutil
import subprocess
import sys


def run(
    command: list[str],
    *,
    expected_code: int = 0,
    timeout: int = 20,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if result.returncode != expected_code:
        rendered = " ".join(command)
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected_code}: "
            f"{rendered}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        print(f"SKIP: required integration tool is missing: {name}")
        raise SystemExit(77)
    return path


def require_target_runner() -> list[str]:
    qemu = shutil.which("qemu-x86_64-static")
    if qemu is not None:
        return [qemu]
    if platform.system() == "Linux" and platform.machine().lower() in (
        "x86_64",
        "amd64",
    ):
        return []
    print("SKIP: qemu-x86_64-static is required on this host")
    raise SystemExit(77)


def compile_and_run(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    target_runner: list[str],
    source: pathlib.Path,
    work_dir: pathlib.Path,
    expected_code: int,
) -> None:
    stem = source.stem
    assembly = work_dir / f"{stem}.s"
    object_file = work_dir / f"{stem}.o"
    executable = work_dir / stem

    run(
        [
            str(compiler),
            "--emit",
            "asm",
            "-o",
            str(assembly),
            str(source),
        ]
    )
    run(
        [
            llvm_mc,
            "--triple=x86_64-unknown-linux-gnu",
            "--filetype=obj",
            "-o",
            str(object_file),
            str(assembly),
        ]
    )
    run(
        [
            linker,
            "-static",
            "-e",
            "_start",
            "-o",
            str(executable),
            str(object_file),
        ]
    )
    run([*target_runner, str(executable)], expected_code=expected_code)


def convert_integer(
    value: int,
    source_width: int,
    source_signed: bool,
    target_width: int,
    target_signed: bool,
) -> int:
    source_modulus = 2**source_width
    source_bits = value % source_modulus
    if target_width > source_width and source_signed:
        source_value = (
            source_bits - source_modulus
            if source_bits >= 2 ** (source_width - 1)
            else source_bits
        )
        target_bits = source_value % (2**target_width)
    else:
        target_bits = source_bits % (2**target_width)

    if target_signed and target_bits >= 2 ** (target_width - 1):
        return target_bits - 2**target_width
    return target_bits


def generate_integer_conversion_matrix(work_dir: pathlib.Path) -> pathlib.Path:
    integer_types = (
        ("i8", 8, True, -85),
        ("i16", 16, True, -21846),
        ("i32", 32, True, -1431655766),
        ("i64", 64, True, -6148914691236517206),
        ("isize", 64, True, -6148914691236517206),
        ("u8", 8, False, 171),
        ("u16", 16, False, 43690),
        ("u32", 32, False, 2863311530),
        ("u64", 64, False, 12297829382473034410),
        ("usize", 64, False, 12297829382473034410),
    )
    lines = ["module test.all_integer_conversions;", ""]
    for source_name, _, _, _ in integer_types:
        for target_name, _, _, _ in integer_types:
            lines.extend(
                (
                    f"fn convert_{source_name}_to_{target_name}("
                    f"value: {source_name}) -> {target_name} {{",
                    f"    return value as {target_name};",
                    "}",
                    "",
                )
            )

    lines.append("fn main() -> i32 {")
    for (
        source_name,
        source_width,
        source_signed,
        source_value,
    ) in integer_types:
        for target_name, target_width, target_signed, _ in integer_types:
            expected = convert_integer(
                source_value,
                source_width,
                source_signed,
                target_width,
                target_signed,
            )
            lines.extend(
                (
                    f"    if (convert_{source_name}_to_{target_name}("
                    f"{source_value}) != {expected}) {{",
                    "        return 1;",
                    "    }",
                )
            )
    lines.extend(("    return 42;", "}", ""))

    source = work_dir / "all_integer_conversions.luna"
    source.write_text("\n".join(lines), encoding="utf-8")
    return source


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    llvm_mc = require_tool("llvm-mc")
    linker = require_tool("ld.lld")
    target_runner = require_target_runner()

    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    case_dir = arguments.source_root / "tests" / "integration" / "cases"

    help_result = run([str(arguments.compiler), "--help"])
    if "usage: lunac" not in help_result.stdout:
        raise AssertionError("--help did not print the compiler usage")
    if "x86_64-unknown-linux-gnu" not in help_result.stdout:
        raise AssertionError("--help did not list the supported target")
    version_result = run([str(arguments.compiler), "--version"])
    if "lunac 0.1.0-dev" not in version_result.stdout:
        raise AssertionError("--version did not print the compiler version")
    run([str(arguments.compiler)], expected_code=2)
    run(
        [str(arguments.compiler), "--emit", "invalid", "input.luna"],
        expected_code=2,
    )
    run([str(arguments.compiler), "--target"], expected_code=2)
    unsupported_target = run(
        [
            str(arguments.compiler),
            "--target",
            "aarch64-unknown-linux-gnu",
            str(case_dir / "return_42.luna"),
        ],
        expected_code=2,
    )
    if "unsupported target" not in unsupported_target.stderr:
        raise AssertionError("unsupported target diagnostic is missing")
    run(
        [
            str(arguments.compiler),
            "--target",
            "x86_64-unknown-linux-gnu",
            "--emit",
            "check",
            str(case_dir / "return_42.luna"),
        ]
    )
    run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(case_dir / "does_not_exist.luna"),
        ],
        expected_code=1,
    )
    print("PASS compiler command-line contract")

    executable_cases = {
        "return_42.luna": 42,
        "arithmetic.luna": 42,
        "function_call.luna": 42,
        "if_else.luna": 17,
        "while_loop.luna": 45,
        "short_circuit.luna": 42,
        "nested_call.luna": 42,
        "recursive_factorial.luna": 120,
        "break_continue.luna": 25,
        "six_arguments.luna": 21,
        "signed_arithmetic.luna": 36,
        "defined_i32_semantics.luna": 42,
        "defined_i64_semantics.luna": 42,
        "division_by_zero.luna": -8,
        "division_overflow.luna": -8,
        "void_call.luna": 42,
        "bool_call.luna": 42,
        "i64_operations.luna": 42,
        "i64_boundaries.luna": 42,
        "i64_six_arguments.luna": 42,
        "i64_recursive_factorial.luna": 42,
        "i64_division_by_zero.luna": -8,
        "i64_division_overflow.luna": -8,
        "mixed_width_arguments.luna": 42,
        "fixed_width_arguments.luna": 42,
        "integer_conversions.luna": 42,
        "conversion_round_trip.luna": 255,
        "unsigned_operations.luna": 42,
        "unsigned_conversions.luna": 42,
        "remaining_integer_conversions.luna": 42,
        "narrow_integer_operations.luna": 42,
        "i8_division_by_zero.luna": -8,
        "i16_division_by_zero.luna": -8,
        "u8_division_by_zero.luna": -8,
        "u16_division_by_zero.luna": -8,
        "i8_division_overflow.luna": -8,
        "i16_remainder_overflow.luna": -8,
        "u32_division_by_zero.luna": -8,
        "u64_division_by_zero.luna": -8,
        "pointer_sized_integer_operations.luna": 42,
        "isize_division_by_zero.luna": -8,
        "isize_division_overflow.luna": -8,
        "isize_remainder_overflow.luna": -8,
        "usize_division_by_zero.luna": -8,
    }

    for case_name, expected_code in executable_cases.items():
        compile_and_run(
            arguments.compiler,
            llvm_mc,
            linker,
            target_runner,
            case_dir / case_name,
            arguments.work_dir,
            expected_code,
        )
        print(f"PASS executable: {case_name}")

    conversion_matrix = generate_integer_conversion_matrix(arguments.work_dir)
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        conversion_matrix,
        arguments.work_dir,
        42,
    )
    print("PASS executable: all 100 integer conversion pairs")

    negative_cases = {
        "type_error.luna": "expected bool, found i32",
        "immutable_assignment.luna": "cannot assign to immutable local",
        "missing_return.luna": "not every path",
        "parse_error.luna": "expected ';'",
        "unknown_function.luna": "unknown function 'missing'",
        "wrong_arity.luna": "expects 1 arguments, found 2",
        "duplicate_local.luna": "duplicate local variable 'answer'",
        "break_outside_loop.luna": "break is only valid inside a loop",
        "integer_overflow.luna": "integer literal does not fit in i32",
        "module_interface_pending.luna": "module interface compilation",
        "import_pending.luna": "cross-module import resolution",
        "duplicate_function.luna": "duplicate function 'main'",
        "unreachable_type_error.luna": "expected bool, found i32",
        "void_local.luna": "local variables cannot have type void",
        "wrong_return_type.luna": "expected i32, found bool",
        "continue_outside_loop.luna": "continue is only valid inside a loop",
        "duplicate_parameter.luna": "duplicate parameter 'value'",
        "i64_positive_overflow.luna": "integer literal does not fit in i64",
        "i64_mixed_types.luna": "expected i64, found i32",
        "u32_positive_overflow.luna": "integer literal does not fit in u32",
        "i8_positive_overflow.luna": "integer literal does not fit in i8",
        "i16_positive_overflow.luna": "integer literal does not fit in i16",
        "u8_positive_overflow.luna": "integer literal does not fit in u8",
        "u16_positive_overflow.luna": "integer literal does not fit in u16",
        "narrow_mixed_types.luna": "expected i8, found u8",
        "signed_unsigned_mixed.luna": "expected u64, found i64",
        "invalid_bool_conversion.luna": (
            "explicit conversion requires integer source and target types"
        ),
        "invalid_void_conversion.luna": (
            "explicit conversion requires integer source and target types"
        ),
        "isize_positive_overflow.luna": (
            "integer literal does not fit in isize"
        ),
        "pointer_sized_mixed_types.luna": "expected isize, found i64",
    }

    for case_name, expected_diagnostic in negative_cases.items():
        result = run(
            [
                str(arguments.compiler),
                "--emit",
                "check",
                str(case_dir / case_name),
            ],
            expected_code=1,
        )
        if expected_diagnostic not in result.stderr:
            raise AssertionError(
                f"{case_name}: expected diagnostic "
                f"{expected_diagnostic!r}\nstderr:\n{result.stderr}"
            )
        print(f"PASS negative: {case_name}")

    for snapshot_name in (
        "function_call",
        "i64_six_arguments",
        "conversion_round_trip",
        "unsigned_conversions",
        "narrow_ir",
        "pointer_sized_integer_ir",
    ):
        ir_output = arguments.work_dir / f"{snapshot_name}.lir"
        run(
            [
                str(arguments.compiler),
                "--emit",
                "ir",
                "-o",
                str(ir_output),
                str(case_dir / f"{snapshot_name}.luna"),
            ]
        )
        expected_ir = (
            arguments.source_root
            / "tests"
            / "integration"
            / "golden"
            / f"{snapshot_name}.lir"
        ).read_text(encoding="utf-8")
        actual_ir = ir_output.read_text(encoding="utf-8")
        if actual_ir.rstrip("\n") != expected_ir.rstrip("\n"):
            raise AssertionError(
                f"IR snapshot mismatch for {snapshot_name}.luna\n"
                f"expected:\n{expected_ir}\nactual:\n{actual_ir}"
            )
        print(f"PASS IR snapshot: {snapshot_name}.luna")

    for deterministic_name in (
        "recursive_factorial",
        "i64_operations",
        "unsigned_operations",
        "narrow_integer_operations",
        "pointer_sized_integer_operations",
    ):
        deterministic_first = (
            arguments.work_dir / f"{deterministic_name}_first.s"
        )
        deterministic_second = (
            arguments.work_dir / f"{deterministic_name}_second.s"
        )
        for output in (deterministic_first, deterministic_second):
            run(
                [
                    str(arguments.compiler),
                    "--emit",
                    "asm",
                    "-o",
                    str(output),
                    str(case_dir / f"{deterministic_name}.luna"),
                ]
            )
        if deterministic_first.read_bytes() != deterministic_second.read_bytes():
            raise AssertionError(
                f"assembly output is not deterministic: {deterministic_name}"
            )
        print(f"PASS deterministic assembly: {deterministic_name}.luna")

    print("all integration tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
