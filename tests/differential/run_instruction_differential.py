#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import platform
import random
import re
import shutil
import subprocess
import sys

from machine_ir_reference import (
    ALL_MACHINE_OPCODES,
    MachineTrap,
    ReferenceInterpreter,
    exit_code,
    parse_module,
)


FIXTURE_CASES = (
    "aggregate_by_value.luna",
    "floating_operations.luna",
    "i64_operations.luna",
    "i64_recursive_factorial.luna",
    "memory_operations.luna",
    "scalar_conversions.luna",
    "structured_control_flow.luna",
    "unsigned_operations.luna",
)
TRAP_CASES = (
    "division_by_zero.luna",
    "division_overflow.luna",
    "null_dereference.luna",
    "array_out_of_bounds.luna",
    "float_to_integer_nan_trap.luna",
    "float_to_integer_unsigned_range_trap.luna",
)


def run(
    command: list[str],
    *,
    expected_code: int = 0,
    timeout: int = 30,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if result.returncode != expected_code:
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected_code}: "
            f"{' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        print(f"SKIP: required instruction-differential tool is missing: {name}")
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


def emit(
    compiler: pathlib.Path,
    emit_kind: str,
    output: pathlib.Path,
    sources: tuple[pathlib.Path, ...],
) -> None:
    run(
        [
            str(compiler),
            "--emit",
            emit_kind,
            "-o",
            str(output),
            *(str(source) for source in sources),
        ]
    )


def assemble_and_link(
    llvm_mc: str,
    linker: str,
    assembly: pathlib.Path,
    object_file: pathlib.Path,
    executable: pathlib.Path,
) -> None:
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


def evaluate_reference(machine_ir: pathlib.Path) -> tuple[int, set[str]]:
    module = parse_module(machine_ir.read_text(encoding="utf-8"))
    interpreter = ReferenceInterpreter(module)
    result = interpreter.run_main()
    return exit_code(result), interpreter.executed_opcodes


def evaluate_reference_trap(machine_ir: pathlib.Path) -> tuple[int, set[str]]:
    module = parse_module(machine_ir.read_text(encoding="utf-8"))
    interpreter = ReferenceInterpreter(module)
    try:
        interpreter.run_main()
    except MachineTrap as trap:
        return -trap.signal_number, interpreter.executed_opcodes
    raise AssertionError("reference interpreter expected a trap")


def execute_case(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    target_runner: list[str],
    sources: tuple[pathlib.Path, ...],
    case_dir: pathlib.Path,
    *,
    expected_code: int | None,
    expects_trap: bool = False,
    capture_rewrite: bool = False,
) -> tuple[set[str], str]:
    case_dir.mkdir(parents=True, exist_ok=True)
    machine_ir = case_dir / "program.mir"
    rewrite = case_dir / "program.rewrite"
    assembly = case_dir / "program.s"
    oracle_object = case_dir / "program.mc.o"
    oracle_executable = case_dir / "program.mc"
    object_file = case_dir / "program.o"
    executable = case_dir / "program"

    emit(compiler, "mir", machine_ir, sources)
    if expects_trap:
        reference_code, opcodes = evaluate_reference_trap(machine_ir)
    else:
        reference_code, opcodes = evaluate_reference(machine_ir)
    if expected_code is not None and reference_code != expected_code:
        rendered_sources = "\n".join(
            source.read_text(encoding="utf-8") for source in sources
        )
        raise AssertionError(
            f"reference result {reference_code} does not match independent "
            f"oracle {expected_code}\n{rendered_sources}"
        )

    rewrite_text = ""
    if capture_rewrite:
        emit(compiler, "rewrite", rewrite, sources)
        rewrite_text = rewrite.read_text(encoding="utf-8")
    emit(compiler, "asm", assembly, sources)
    assemble_and_link(
        llvm_mc,
        linker,
        assembly,
        oracle_object,
        oracle_executable,
    )
    emit(compiler, "obj", object_file, sources)
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
    run(
        [*target_runner, str(oracle_executable)],
        expected_code=reference_code,
    )
    run(
        [*target_runner, str(executable)],
        expected_code=reference_code,
    )
    return opcodes, rewrite_text


def generate_pressure_case() -> tuple[str, int]:
    value_count = 36
    expression = str(value_count)
    for value in range(value_count - 1, 0, -1):
        expression = f"{value} + ({expression})"
    expected_code = (value_count * (value_count + 1) // 2) & 0xFF
    source = (
        "module differential.register_pressure;\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    return ({expression}) & 255;\n"
        "}\n"
    )
    return source, expected_code


def generate_random_case(
    generators: object,
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    case_kind = case_index % 21
    if case_kind == 0:
        return generators.generate_case(engine, case_index)
    if case_kind == 1:
        return generators.generate_i64_case(engine, case_index)
    if case_kind == 2:
        return generators.generate_unsigned_case(engine, case_index, 32)
    if case_kind == 3:
        return generators.generate_unsigned_case(engine, case_index, 64)
    if case_kind == 4:
        return generators.generate_signed_narrow_case(engine, case_index, 8)
    if case_kind == 5:
        return generators.generate_signed_narrow_case(engine, case_index, 16)
    if case_kind == 6:
        return generators.generate_unsigned_case(engine, case_index, 8)
    if case_kind == 7:
        return generators.generate_unsigned_case(engine, case_index, 16)
    if case_kind == 8:
        return generators.generate_i64_case(engine, case_index, "isize")
    if case_kind == 9:
        return generators.generate_unsigned_case(
            engine, case_index, 64, "usize"
        )
    if case_kind == 10:
        return generators.generate_float_case(engine, case_index, "f32")
    if case_kind == 11:
        return generators.generate_float_case(engine, case_index, "f64")
    if case_kind == 12:
        return generators.generate_signed_scalar_conversion_case(
            engine, case_index, "f32"
        )
    if case_kind == 13:
        return generators.generate_signed_scalar_conversion_case(
            engine, case_index, "f64"
        )
    if case_kind == 14:
        return generators.generate_unsigned_scalar_conversion_case(
            engine, case_index
        )
    if case_kind == 15:
        if engine.randrange(2) == 0:
            return generators.generate_fractional_scalar_conversion_case(
                engine, case_index
            )
        return generators.generate_float_width_conversion_case(
            engine, case_index
        )
    if case_kind == 16:
        return generators.generate_conditional_case(engine, case_index)
    if case_kind == 17:
        return generators.generate_structured_control_flow_case(
            engine, case_index
        )
    if case_kind == 18:
        return generators.generate_memory_case(engine, case_index)
    if case_kind == 19:
        return generators.generate_aggregate_case(engine, case_index)
    return generators.generate_aggregate_value_case(engine, case_index)


def verify_rewrite_coverage(rewrite_text: str) -> None:
    required_patterns = {
        "a physical spill": r"spills=[1-9][0-9]*",
        "a spilled operand": r"uses=\[[^\]]*spill\[[0-9]+\]",
        "parallel call moves": r"parallel-moves=[1-9][0-9]*",
        "fixed RAX": r"fixed-(?:in|out)=\{[^}]*%rax",
        "fixed RCX": r"fixed-(?:in|out)=\{[^}]*%rcx",
        "fixed RDX": r"fixed-(?:in|out)=\{[^}]*%rdx",
        "an XMM constraint": r"fixed-(?:in|out)=\{[^}]*%xmm",
    }
    missing = [
        description
        for description, pattern in required_patterns.items()
        if re.search(pattern, rewrite_text) is None
    ]
    if missing:
        raise AssertionError(
            "instruction rewrite corpus lacks: " + ", ".join(missing)
        )


def parse_seed(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    parser.add_argument("--seed", type=parse_seed, required=True)
    parser.add_argument("--cases", type=int, default=42)
    arguments = parser.parse_args()
    if arguments.cases <= 0:
        parser.error("--cases must be positive")

    llvm_mc = require_tool("llvm-mc")
    linker = require_tool("ld.lld")
    target_runner = require_target_runner()
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    generator_dir = arguments.source_root / "tests" / "random"
    sys.path.insert(0, str(generator_dir))
    import run_random as generators

    observed_opcodes: set[str] = set()
    rewrite_corpus = ""
    fixture_root = arguments.source_root / "tests" / "integration" / "cases"

    for case_name in FIXTURE_CASES:
        opcodes, rewrite_text = execute_case(
            arguments.compiler,
            llvm_mc,
            linker,
            target_runner,
            (fixture_root / case_name,),
            arguments.work_dir / f"fixture-{pathlib.Path(case_name).stem}",
            expected_code=42,
            capture_rewrite=True,
        )
        observed_opcodes.update(opcodes)
        rewrite_corpus += rewrite_text
        print(f"PASS instruction fixture: {case_name}")

    pressure_source, pressure_code = generate_pressure_case()
    pressure_path = arguments.work_dir / "register-pressure.luna"
    pressure_path.write_text(pressure_source, encoding="utf-8")
    pressure_opcodes, pressure_rewrite = execute_case(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        (pressure_path,),
        arguments.work_dir / "register-pressure",
        expected_code=pressure_code,
        capture_rewrite=True,
    )
    observed_opcodes.update(pressure_opcodes)
    rewrite_corpus += pressure_rewrite
    print("PASS allocation-aware register-pressure differential")

    for case_name in TRAP_CASES:
        opcodes, _ = execute_case(
            arguments.compiler,
            llvm_mc,
            linker,
            target_runner,
            (fixture_root / case_name,),
            arguments.work_dir / f"trap-{pathlib.Path(case_name).stem}",
            expected_code=None,
            expects_trap=True,
        )
        observed_opcodes.update(opcodes)
        print(f"PASS instruction trap differential: {case_name}")

    engine = random.Random(arguments.seed)
    for case_index in range(arguments.cases):
        source, expected_code = generate_random_case(
            generators, engine, case_index
        )
        source_path = arguments.work_dir / f"random-{case_index}.luna"
        source_path.write_text(source, encoding="utf-8")
        try:
            opcodes, _ = execute_case(
                arguments.compiler,
                llvm_mc,
                linker,
                target_runner,
                (source_path,),
                arguments.work_dir / f"random-{case_index}",
                expected_code=expected_code,
            )
        except (AssertionError, subprocess.TimeoutExpired) as error:
            raise AssertionError(
                f"instruction differential random case {case_index} failed; "
                f"seed={arguments.seed:#x}\nsource:\n{source}\n{error}"
            ) from error
        observed_opcodes.update(opcodes)

    missing_opcodes = sorted(ALL_MACHINE_OPCODES - observed_opcodes)
    unexpected_opcodes = sorted(observed_opcodes - ALL_MACHINE_OPCODES)
    if missing_opcodes or unexpected_opcodes:
        raise AssertionError(
            f"machine opcode coverage mismatch; missing={missing_opcodes}, "
            f"unexpected={unexpected_opcodes}"
        )
    verify_rewrite_coverage(rewrite_corpus)

    print(
        f"PASS: all {len(ALL_MACHINE_OPCODES)} machine opcodes, "
        f"{len(FIXTURE_CASES)} fixtures, {len(TRAP_CASES)} traps, "
        f"{arguments.cases} random instruction programs "
        f"(seed={arguments.seed:#x})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
