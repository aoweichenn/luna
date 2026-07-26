#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import random
import shutil
import subprocess
import sys


I32_MIN = -(2**31)
I32_MAX = 2**31 - 1


@dataclasses.dataclass(frozen=True)
class Expression:
    text: str
    value: int


def wrap_i32(value: int) -> int:
    return ((value + 2**31) % 2**32) - 2**31


def truncate_division(left: int, right: int) -> int:
    quotient = abs(left) // abs(right)
    return -quotient if (left < 0) != (right < 0) else quotient


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
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected_code}: "
            f"{' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        print(f"SKIP: required random-test tool is missing: {name}")
        raise SystemExit(77)
    return path


def generate_expression(
    engine: random.Random,
    variables: dict[str, int],
    depth: int,
) -> Expression:
    if depth == 0 or engine.randrange(5) == 0:
        if engine.randrange(2) == 0:
            name = engine.choice(tuple(variables))
            return Expression(name, variables[name])
        value = engine.randrange(0, 256)
        return Expression(str(value), value)

    left = generate_expression(engine, variables, depth - 1)
    operation = engine.choice(
        ("+", "-", "*", "&", "|", "^", "<<", ">>", "/", "%")
    )

    if operation in ("<<", ">>"):
        shift = engine.randrange(0, 16)
        if operation == "<<":
            value = wrap_i32(left.value << shift)
        else:
            value = left.value >> shift
        return Expression(f"({left.text} {operation} {shift})", value)

    if operation in ("/", "%"):
        divisor = engine.randrange(1, 32)
        if left.value == I32_MIN and divisor == -1:
            divisor = 1
        quotient = truncate_division(left.value, divisor)
        value = quotient if operation == "/" else left.value - quotient * divisor
        return Expression(f"({left.text} {operation} {divisor})", value)

    right = generate_expression(engine, variables, depth - 1)
    if operation == "+":
        value = wrap_i32(left.value + right.value)
    elif operation == "-":
        value = wrap_i32(left.value - right.value)
    elif operation == "*":
        value = wrap_i32(left.value * right.value)
    elif operation == "&":
        value = wrap_i32(left.value & right.value)
    elif operation == "|":
        value = wrap_i32(left.value | right.value)
    else:
        value = wrap_i32(left.value ^ right.value)
    return Expression(f"({left.text} {operation} {right.text})", value)


def generate_case(engine: random.Random, case_index: int) -> tuple[str, int]:
    arguments = {
        "first": engine.randrange(0, 256),
        "second": engine.randrange(0, 256),
        "third": engine.randrange(0, 256),
    }
    expression = generate_expression(engine, arguments, 4)
    result = expression.value
    loop_count = engine.randrange(1, 8)
    even_delta = engine.randrange(1, 32)
    odd_delta = engine.randrange(1, 32)
    threshold = engine.randrange(0, 16)

    for _ in range(loop_count):
        if (result & 15) <= threshold and result != 0:
            result = wrap_i32(result + even_delta)
        else:
            result = wrap_i32(result - odd_delta)

    expected_code = result & 255
    source = (
        f"module random.case{case_index};\n"
        "\n"
        "fn identity(value: i32) -> i32 {\n"
        "    return value;\n"
        "}\n"
        "\n"
        "fn calculate(first: i32, second: i32, third: i32) -> i32 {\n"
        f"    var result: i32 = {expression.text};\n"
        "    var index: i32 = 0;\n"
        f"    while (index < {loop_count}) {{\n"
        f"        if ((result & 15) <= {threshold} && result != 0) {{\n"
        f"            result += {even_delta};\n"
        "        } else {\n"
        f"            result -= {odd_delta};\n"
        "        }\n"
        "        index += 1;\n"
        "    }\n"
        "    return result & 255;\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        "    return calculate("
        f"identity({arguments['first']}), "
        f"{arguments['second']}, {arguments['third']});\n"
        "}\n"
    )
    return source, expected_code


def compile_and_run(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    qemu: str,
    source_text: str,
    expected_code: int,
    case_index: int,
    work_dir: pathlib.Path,
) -> None:
    source = work_dir / f"case_{case_index}.luna"
    assembly = work_dir / f"case_{case_index}.s"
    object_file = work_dir / f"case_{case_index}.o"
    executable = work_dir / f"case_{case_index}"
    source.write_text(source_text, encoding="utf-8")

    try:
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
    except (AssertionError, subprocess.TimeoutExpired) as error:
        raise AssertionError(
            f"random case {case_index} failed; expected exit "
            f"{expected_code}\nsource:\n{source_text}\n{error}"
        ) from error


def parse_seed(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    parser.add_argument("--seed", type=parse_seed, required=True)
    parser.add_argument("--cases", type=int, default=64)
    arguments = parser.parse_args()

    if arguments.cases <= 0:
        parser.error("--cases must be positive")

    llvm_mc = require_tool("llvm-mc")
    linker = require_tool("ld.lld")
    qemu = require_tool("qemu-x86_64-static")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    engine = random.Random(arguments.seed)
    for case_index in range(arguments.cases):
        source, expected_code = generate_case(engine, case_index)
        compile_and_run(
            arguments.compiler,
            llvm_mc,
            linker,
            qemu,
            source,
            expected_code,
            case_index,
            arguments.work_dir,
        )

    print(
        f"PASS: {arguments.cases} differential random programs "
        f"(seed={arguments.seed:#x})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
