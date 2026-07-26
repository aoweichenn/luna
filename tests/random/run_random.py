#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import platform
import random
import shutil
import subprocess
import sys


I32_MIN = -(2**31)
I32_MAX = 2**31 - 1
I64_MIN = -(2**63)


@dataclasses.dataclass(frozen=True)
class Expression:
    text: str
    value: int


def wrap_i32(value: int) -> int:
    return ((value + 2**31) % 2**32) - 2**31


def wrap_i64(value: int) -> int:
    return ((value + 2**63) % 2**64) - 2**63


def wrap_signed(value: int, width: int) -> int:
    return ((value + 2 ** (width - 1)) % (2**width)) - 2 ** (width - 1)


def wrap_unsigned(value: int, width: int) -> int:
    return value % (2**width)


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


def generate_i64_expression(
    engine: random.Random,
    variables: dict[str, int],
    depth: int,
) -> Expression:
    if depth == 0 or engine.randrange(5) == 0:
        if engine.randrange(2) == 0:
            name = engine.choice(tuple(variables))
            return Expression(name, variables[name])
        value = 2**32 + engine.randrange(0, 4096)
        return Expression(str(value), value)

    left = generate_i64_expression(engine, variables, depth - 1)
    operation = engine.choice(
        ("+", "-", "*", "&", "|", "^", "<<", ">>", "/", "%")
    )

    if operation in ("<<", ">>"):
        shift = engine.randrange(0, 80)
        effective_shift = shift & 63
        if operation == "<<":
            value = wrap_i64(left.value << effective_shift)
        else:
            value = left.value >> effective_shift
        return Expression(f"({left.text} {operation} {shift})", value)

    if operation in ("/", "%"):
        divisor = engine.randrange(1, 64)
        if left.value == I64_MIN and divisor == -1:
            divisor = 1
        quotient = truncate_division(left.value, divisor)
        value = quotient if operation == "/" else left.value - quotient * divisor
        return Expression(f"({left.text} {operation} {divisor})", value)

    right = generate_i64_expression(engine, variables, depth - 1)
    if operation == "+":
        value = wrap_i64(left.value + right.value)
    elif operation == "-":
        value = wrap_i64(left.value - right.value)
    elif operation == "*":
        value = wrap_i64(left.value * right.value)
    elif operation == "&":
        value = wrap_i64(left.value & right.value)
    elif operation == "|":
        value = wrap_i64(left.value | right.value)
    else:
        value = wrap_i64(left.value ^ right.value)
    return Expression(f"({left.text} {operation} {right.text})", value)


def generate_i64_case(
    engine: random.Random,
    case_index: int,
    type_name: str = "i64",
) -> tuple[str, int]:
    arguments = {
        "first": 2**32 + engine.randrange(0, 4096),
        "second": 2**33 + engine.randrange(0, 4096),
        "third": 2**34 + engine.randrange(0, 4096),
    }
    expression = generate_i64_expression(engine, arguments, 4)
    narrow_value = engine.randrange(I32_MIN, I32_MAX + 1)
    result_value = wrap_i64(expression.value + narrow_value)
    truncated_value = wrap_i32(result_value)
    expected_code = result_value & 255
    failure_code = (expected_code + 1) & 255
    source = (
        f"module random.wide_case{case_index};\n"
        "\n"
        f"fn calculate(first: {type_name}, second: {type_name}, "
        f"third: {type_name}, narrow: i32) -> {type_name} {{\n"
        f"    return {expression.text} + (narrow as {type_name});\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let result: {type_name} = calculate("
        f"{arguments['first']}, {arguments['second']}, {arguments['third']}, "
        f"{narrow_value});\n"
        "    let truncated: i32 = result as i32;\n"
        f"    if (result == {result_value} && truncated == {truncated_value}) "
        f"{{ return {expected_code}; }}\n"
        f"    return {failure_code};\n"
        "}\n"
    )
    return source, expected_code


def generate_signed_narrow_expression(
    engine: random.Random,
    variables: dict[str, int],
    depth: int,
    width: int,
) -> Expression:
    if depth == 0 or engine.randrange(5) == 0:
        if engine.randrange(2) == 0:
            name = engine.choice(tuple(variables))
            return Expression(name, variables[name])
        minimum = -(2 ** (width - 1))
        maximum = 2 ** (width - 1) - 1
        value = engine.randrange(minimum, maximum + 1)
        return Expression(str(value), value)

    left = generate_signed_narrow_expression(
        engine, variables, depth - 1, width
    )
    operation = engine.choice(
        ("+", "-", "*", "&", "|", "^", "<<", ">>", "/", "%")
    )

    if operation in ("<<", ">>"):
        shift = engine.randrange(0, width + 16)
        effective_shift = shift & (width - 1)
        if operation == "<<":
            value = wrap_signed(left.value << effective_shift, width)
        else:
            value = left.value >> effective_shift
        return Expression(f"({left.text} {operation} {shift})", value)

    if operation in ("/", "%"):
        divisor = engine.randrange(1, min(64, 2 ** (width - 1)))
        quotient = truncate_division(left.value, divisor)
        value = quotient if operation == "/" else left.value - quotient * divisor
        return Expression(f"({left.text} {operation} {divisor})", value)

    right = generate_signed_narrow_expression(
        engine, variables, depth - 1, width
    )
    if operation == "+":
        value = left.value + right.value
    elif operation == "-":
        value = left.value - right.value
    elif operation == "*":
        value = left.value * right.value
    elif operation == "&":
        value = left.value & right.value
    elif operation == "|":
        value = left.value | right.value
    else:
        value = left.value ^ right.value
    return Expression(
        f"({left.text} {operation} {right.text})",
        wrap_signed(value, width),
    )


def generate_signed_narrow_case(
    engine: random.Random,
    case_index: int,
    width: int,
) -> tuple[str, int]:
    minimum = -(2 ** (width - 1))
    maximum = 2 ** (width - 1) - 1
    type_name = f"i{width}"
    arguments = {
        "first": engine.randrange(minimum, maximum + 1),
        "second": engine.randrange(minimum, maximum + 1),
        "third": engine.randrange(minimum, maximum + 1),
    }
    expression = generate_signed_narrow_expression(
        engine, arguments, 4, width
    )
    expected_value = expression.value
    expected_code = expected_value & 255
    failure_code = (expected_code + 1) & 255
    source = (
        f"module random.signed_narrow_case{case_index};\n"
        "\n"
        f"fn calculate(first: {type_name}, second: {type_name}, "
        f"third: {type_name}) -> {type_name} {{\n"
        f"    return {expression.text};\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let result: {type_name} = calculate("
        f"{arguments['first']}, {arguments['second']}, {arguments['third']});\n"
        f"    if (result == {expected_value}) {{ return {expected_code}; }}\n"
        f"    return {failure_code};\n"
        "}\n"
    )
    return source, expected_code


def generate_unsigned_expression(
    engine: random.Random,
    variables: dict[str, int],
    depth: int,
    width: int,
) -> Expression:
    if depth == 0 or engine.randrange(5) == 0:
        if engine.randrange(2) == 0:
            name = engine.choice(tuple(variables))
            return Expression(name, variables[name])
        modulus = 2**width
        value = modulus - 1 - engine.randrange(0, min(4096, modulus))
        return Expression(str(value), value)

    left = generate_unsigned_expression(engine, variables, depth - 1, width)
    operation = engine.choice(
        ("+", "-", "*", "&", "|", "^", "<<", ">>", "/", "%")
    )

    if operation in ("<<", ">>"):
        shift = engine.randrange(0, width + 16)
        effective_shift = shift & (width - 1)
        if operation == "<<":
            value = wrap_unsigned(left.value << effective_shift, width)
        else:
            value = left.value >> effective_shift
        return Expression(f"({left.text} {operation} {shift})", value)

    if operation in ("/", "%"):
        divisor = engine.randrange(1, 64)
        value = (
            left.value // divisor
            if operation == "/"
            else left.value % divisor
        )
        return Expression(f"({left.text} {operation} {divisor})", value)

    right = generate_unsigned_expression(engine, variables, depth - 1, width)
    if operation == "+":
        value = left.value + right.value
    elif operation == "-":
        value = left.value - right.value
    elif operation == "*":
        value = left.value * right.value
    elif operation == "&":
        value = left.value & right.value
    elif operation == "|":
        value = left.value | right.value
    else:
        value = left.value ^ right.value
    return Expression(
        f"({left.text} {operation} {right.text})",
        wrap_unsigned(value, width),
    )


def generate_unsigned_case(
    engine: random.Random,
    case_index: int,
    width: int,
    type_name: str | None = None,
) -> tuple[str, int]:
    modulus = 2**width
    if type_name is None:
        type_name = f"u{width}"
    spread = max(1, min(4096, modulus // 4))
    arguments = {
        "first": modulus - 1 - engine.randrange(0, spread),
        "second": modulus // 2 + engine.randrange(0, spread),
        "third": modulus // 4 + engine.randrange(0, spread),
    }
    expression = generate_unsigned_expression(engine, arguments, 4, width)
    expected_value = expression.value
    expected_code = expected_value & 255
    failure_code = (expected_code + 1) & 255
    source = (
        f"module random.unsigned_case{case_index};\n"
        "\n"
        f"fn calculate(first: {type_name}, second: {type_name}, "
        f"third: {type_name}) -> {type_name} {{\n"
        f"    return {expression.text};\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let result: {type_name} = calculate("
        f"{arguments['first']}, {arguments['second']}, {arguments['third']});\n"
        f"    if (result == {expected_value}) {{ return {expected_code}; }}\n"
        f"    return {failure_code};\n"
        "}\n"
    )
    return source, expected_code


def compile_and_run(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    target_runner: list[str],
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
        run([*target_runner, str(executable)], expected_code=expected_code)
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
    target_runner = require_target_runner()
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    engine = random.Random(arguments.seed)
    for case_index in range(arguments.cases):
        case_kind = case_index % 10
        if case_kind == 0:
            source, expected_code = generate_case(engine, case_index)
        elif case_kind == 1:
            source, expected_code = generate_i64_case(engine, case_index)
        elif case_kind == 2:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 32
            )
        elif case_kind == 3:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 64
            )
        elif case_kind == 4:
            source, expected_code = generate_signed_narrow_case(
                engine, case_index, 8
            )
        elif case_kind == 5:
            source, expected_code = generate_signed_narrow_case(
                engine, case_index, 16
            )
        elif case_kind == 6:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 8
            )
        elif case_kind == 7:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 16
            )
        elif case_kind == 8:
            source, expected_code = generate_i64_case(
                engine, case_index, "isize"
            )
        else:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 64, "usize"
            )
        compile_and_run(
            arguments.compiler,
            llvm_mc,
            linker,
            target_runner,
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
