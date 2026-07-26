#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
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


def compile_and_run(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    qemu: str,
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
    run([qemu, str(executable)], expected_code=expected_code)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    llvm_mc = require_tool("llvm-mc")
    linker = require_tool("ld.lld")
    qemu = require_tool("qemu-x86_64-static")

    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    case_dir = arguments.source_root / "tests" / "cases"

    executable_cases = {
        "return_42.luna": 42,
        "arithmetic.luna": 42,
        "function_call.luna": 42,
        "if_else.luna": 17,
        "while_loop.luna": 45,
        "short_circuit.luna": 42,
    }

    for case_name, expected_code in executable_cases.items():
        compile_and_run(
            arguments.compiler,
            llvm_mc,
            linker,
            qemu,
            case_dir / case_name,
            arguments.work_dir,
            expected_code,
        )
        print(f"PASS executable: {case_name}")

    negative_cases = {
        "type_error.luna": "expected bool, found i32",
        "immutable_assignment.luna": "cannot assign to immutable local",
        "missing_return.luna": "not every path",
        "parse_error.luna": "expected ';'",
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

    ir_output = arguments.work_dir / "function_call.lir"
    run(
        [
            str(arguments.compiler),
            "--emit",
            "ir",
            "-o",
            str(ir_output),
            str(case_dir / "function_call.luna"),
        ]
    )
    expected_ir = (
        arguments.source_root / "tests" / "golden" / "function_call.lir"
    ).read_text(encoding="utf-8")
    actual_ir = ir_output.read_text(encoding="utf-8")
    if actual_ir != expected_ir:
        raise AssertionError(
            "IR snapshot mismatch for function_call.luna\n"
            f"expected:\n{expected_ir}\nactual:\n{actual_ir}"
        )
    print("PASS IR snapshot: function_call.luna")

    print("all integration tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
