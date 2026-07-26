#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import math
import pathlib
import platform
import random
import shutil
import struct
import subprocess
import sys


I32_MIN = -(2**31)
I32_MAX = 2**31 - 1
I64_MIN = -(2**63)


@dataclasses.dataclass(frozen=True)
class Expression:
    text: str
    value: int


@dataclasses.dataclass(frozen=True)
class FloatExpression:
    text: str
    value: float


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


def round_f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def round_float(value: float, type_name: str) -> float:
    return round_f32(value) if type_name == "f32" else value


def format_float_literal(value: float, type_name: str) -> str:
    if not math.isfinite(value):
        raise AssertionError(
            "random floating-point generator produced infinity or NaN"
        )
    precision = 9 if type_name == "f32" else 17
    text = format(value, f".{precision}g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text


def generate_float_expression(
    engine: random.Random,
    variables: dict[str, float],
    depth: int,
    type_name: str,
) -> FloatExpression:
    if depth == 0 or engine.randrange(5) == 0:
        if engine.randrange(2) == 0:
            name = engine.choice(tuple(variables))
            return FloatExpression(name, variables[name])
        value = round_float(engine.randrange(-1024, 1025) / 8.0, type_name)
        return FloatExpression(
            format_float_literal(value, type_name),
            value,
        )

    if engine.randrange(8) == 0:
        operand = generate_float_expression(
            engine, variables, depth - 1, type_name
        )
        return FloatExpression(f"(-{operand.text})", -operand.value)

    left = generate_float_expression(engine, variables, depth - 1, type_name)
    operation = engine.choice(("+", "-", "*", "/"))
    if operation == "/":
        right_value = engine.choice(
            (-8.0, -4.0, -2.0, -0.5, 0.5, 2.0, 4.0, 8.0)
        )
        right = FloatExpression(
            format_float_literal(right_value, type_name),
            right_value,
        )
    else:
        right = generate_float_expression(
            engine, variables, depth - 1, type_name
        )

    if operation == "+":
        value = left.value + right.value
    elif operation == "-":
        value = left.value - right.value
    elif operation == "*":
        value = left.value * right.value
    else:
        value = left.value / right.value
    value = round_float(value, type_name)
    return FloatExpression(f"({left.text} {operation} {right.text})", value)


def generate_float_case(
    engine: random.Random,
    case_index: int,
    type_name: str,
) -> tuple[str, int]:
    arguments = {
        "first": round_float(engine.randrange(-1024, 1025) / 8.0, type_name),
        "second": round_float(engine.randrange(-1024, 1025) / 8.0, type_name),
        "third": round_float(engine.randrange(-1024, 1025) / 8.0, type_name),
    }
    expression = generate_float_expression(engine, arguments, 4, type_name)
    adjustment = round_float(engine.randrange(-64, 65) / 8.0, type_name)
    expected_value = round_float(expression.value + adjustment, type_name)
    expected_value = round_float(expected_value - adjustment, type_name)
    expected_literal = format_float_literal(expected_value, type_name)
    source = (
        f"module random.float_case{case_index};\n"
        "\n"
        f"fn calculate(first: {type_name}, second: {type_name}, "
        f"third: {type_name}) -> {type_name} {{\n"
        f"    var result: {type_name} = {expression.text};\n"
        f"    result += {format_float_literal(adjustment, type_name)};\n"
        f"    result -= {format_float_literal(adjustment, type_name)};\n"
        "    return -(-result);\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let result: {type_name} = calculate("
        f"{format_float_literal(arguments['first'], type_name)}, "
        f"{format_float_literal(arguments['second'], type_name)}, "
        f"{format_float_literal(arguments['third'], type_name)});\n"
        f"    if (result == {expected_literal} && "
        f"result <= {expected_literal} && result >= {expected_literal}) {{\n"
        "        return 42;\n"
        "    }\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_signed_scalar_conversion_case(
    engine: random.Random,
    case_index: int,
    type_name: str,
) -> tuple[str, int]:
    magnitude_bits = 40 if type_name == "f32" else 62
    source_value = engine.randrange(-(2**magnitude_bits), 2**magnitude_bits)
    converted_value = round_float(float(source_value), type_name)
    expected_value = int(converted_value)
    source = (
        f"module random.signed_scalar_conversion{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let source: i64 = {source_value};\n"
        f"    let converted: {type_name} = source as {type_name};\n"
        "    let round_trip: i64 = converted as i64;\n"
        f"    if (round_trip == {expected_value}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_unsigned_scalar_conversion_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    source_value = engine.randrange(2**63, 2**64 - 8192)
    expected_value = int(float(source_value))
    source = (
        f"module random.unsigned_scalar_conversion{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let source: u64 = {source_value};\n"
        "    let converted: f64 = source as f64;\n"
        "    let round_trip: u64 = converted as u64;\n"
        f"    if (round_trip == {expected_value}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_fractional_scalar_conversion_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    is_signed = engine.randrange(2) == 0
    source_type = engine.choice(("f32", "f64"))
    if is_signed:
        integral = engine.randrange(-32767, 32767)
        source_value = integral + (-0.75 if integral < 0 else 0.75)
        target_type = "i16"
    else:
        integral = engine.randrange(0, 65535)
        source_value = integral + 0.75
        target_type = "u16"
    expected_value = int(round_float(source_value, source_type))
    source_literal = format_float_literal(source_value, source_type)
    source = (
        f"module random.fractional_scalar_conversion{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let source: {source_type} = {source_literal};\n"
        f"    let converted: {target_type} = source as {target_type};\n"
        f"    if (converted == {expected_value}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_float_width_conversion_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    source_value = engine.randrange(-(2**30), 2**30) / 10.0
    expected_value = round_f32(source_value)
    source_literal = format_float_literal(source_value, "f64")
    expected_literal = format_float_literal(expected_value, "f64")
    source = (
        f"module random.float_width_conversion{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let source: f64 = {source_literal};\n"
        "    let narrowed: f32 = source as f32;\n"
        "    let widened: f64 = narrowed as f64;\n"
        f"    if (widened == {expected_literal}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_conditional_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    first_condition = engine.randrange(2) == 0
    second_condition = engine.randrange(2) == 0
    first_value = engine.randrange(-100000, 100001)
    second_value = engine.randrange(-100000, 100001)
    third_value = engine.randrange(-100000, 100001)
    expected_value = (
        first_value
        if first_condition
        else second_value if second_condition else third_value
    )
    first_text = "true" if first_condition else "false"
    second_text = "true" if second_condition else "false"
    source = (
        f"module random.conditional_case{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    let selected: i32 = {first_text} ? {first_value} : "
        f"{second_text} ? {second_value} : {third_value};\n"
        "    let safe: i32 = true ? selected : 1 / 0;\n"
        f"    if (safe == {expected_value}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_structured_control_flow_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    initial_value = engine.randrange(-50, 51)
    do_limit = engine.randrange(1, 9)
    for_limit = engine.randrange(4, 12)
    selected_labels = engine.sample(range(for_limit), 3)
    first_skip = selected_labels[0]
    second_skip = selected_labels[1]
    switch_break = selected_labels[2]

    expected_value = initial_value
    for iteration in range(1, do_limit + 1):
        expected_value = wrap_i32(expected_value + iteration)
        if iteration % 2 != 0:
            expected_value = wrap_i32(expected_value - 1)

    for index in range(for_limit):
        if index == first_skip or index == second_skip:
            continue
        if index != switch_break:
            expected_value = wrap_i32(expected_value + index)
        expected_value = wrap_i32(expected_value + 1)

    source = (
        f"module random.structured_control_flow_case{case_index};\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    var total: i32 = {initial_value};\n"
        "    var iteration: i32 = 0;\n"
        "    do {\n"
        "        iteration += 1;\n"
        "        total += iteration;\n"
        "        if ((iteration % 2) == 0) { continue; }\n"
        "        total -= 1;\n"
        f"    }} while (iteration < {do_limit});\n"
        f"    for (var index: i32 = 0; index < {for_limit}; index += 1) {{\n"
        "        switch (index) {\n"
        f"            case {first_skip}, {second_skip} {{ continue; }}\n"
        f"            case {switch_break} {{ break; }}\n"
        "            default { total += index; }\n"
        "        }\n"
        "        total += 1;\n"
        "    }\n"
        f"    if (total == {expected_value}) {{ return 42; }}\n"
        "    return 1;\n"
        "}\n"
    )
    return source, 42


def generate_memory_case(
    engine: random.Random,
    case_index: int,
) -> tuple[str, int]:
    length = engine.randrange(2, 13)
    values = [engine.randrange(0, 1000) for _ in range(length)]
    update_index = engine.randrange(length)
    delta = engine.randrange(-100, 101)
    updated_value = wrap_i32(values[update_index] + delta)
    values[update_index] = updated_value
    first_index = engine.randrange(length)
    second_index = engine.randrange(length)
    select_first = engine.randrange(2) == 0
    selected_index = first_index if select_first else second_index
    selected_value = values[selected_index]
    row = engine.randrange(2)
    column = engine.randrange(3)
    matrix_value = engine.randrange(-30000, 30001)
    condition = "true" if select_first else "false"

    assignments = "".join(
        f"    values[{index}] = {value};\n"
        for index, value in enumerate(values)
        if index != update_index
    )
    source = (
        f"module random.memory_case{case_index};\n"
        "\n"
        "fn update(base: *i32, index: usize, delta: i32) -> i32 {\n"
        "    base[index] += delta;\n"
        "    return base[index];\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        f"    var values: [{length}]i32 = {{}};\n"
        f"{assignments}"
        f"    values[{update_index}] = "
        f"{updated_value - delta};\n"
        "    let base: *i32 = &values[0];\n"
        f"    if (update(base, {update_index}, {delta}) != "
        f"{updated_value}) {{ return 1; }}\n"
        "    let address: usize = base as usize;\n"
        "    let round_trip: *i32 = address as *i32;\n"
        "    let read_only: *const i32 = round_trip as *const i32;\n"
        f"    let selected: *i32 = {condition} ? &values[{first_index}] : "
        f"&values[{second_index}];\n"
        f"    if (*selected != {selected_value} || "
        f"read_only[{update_index}] != {updated_value}) {{ return 2; }}\n"
        "    var matrix: [2][3]i16 = {};\n"
        f"    matrix[{row}][{column}] = {matrix_value};\n"
        f"    if (matrix[{row}][{column}] != {matrix_value}) "
        "{ return 3; }\n"
        "    let text: *const u8 = \"random\\n\";\n"
        "    if (text[6] != 10 || text[7] != 0) { return 4; }\n"
        "    return 42;\n"
        "}\n"
    )
    return source, 42


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
        case_kind = case_index % 19
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
        elif case_kind == 9:
            source, expected_code = generate_unsigned_case(
                engine, case_index, 64, "usize"
            )
        elif case_kind == 10:
            source, expected_code = generate_float_case(
                engine, case_index, "f32"
            )
        elif case_kind == 11:
            source, expected_code = generate_float_case(
                engine, case_index, "f64"
            )
        elif case_kind == 12:
            source, expected_code = generate_signed_scalar_conversion_case(
                engine, case_index, "f32"
            )
        elif case_kind == 13:
            source, expected_code = generate_signed_scalar_conversion_case(
                engine, case_index, "f64"
            )
        elif case_kind == 14:
            source, expected_code = generate_unsigned_scalar_conversion_case(
                engine, case_index
            )
        elif case_kind == 15:
            if engine.randrange(2) == 0:
                source, expected_code = (
                    generate_fractional_scalar_conversion_case(
                        engine, case_index
                    )
                )
            else:
                source, expected_code = generate_float_width_conversion_case(
                    engine, case_index
                )
        elif case_kind == 16:
            source, expected_code = generate_conditional_case(
                engine, case_index
            )
        elif case_kind == 17:
            source, expected_code = generate_structured_control_flow_case(
                engine, case_index
            )
        else:
            source, expected_code = generate_memory_case(engine, case_index)
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
