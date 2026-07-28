#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import random

from run_minimum_standard_library import (
    Module,
    compile_metadata,
    compile_object,
    dependency_paths,
    ensure_sysroot,
    module_graph,
    require_tool,
    run,
    target_runner,
)


def required_object(graph: dict[str, Module], key: str) -> pathlib.Path:
    object_file = graph[key].object_file
    if object_file is None:
        raise AssertionError(f"{key} has no separately linked object")
    return object_file


def compile_driver(
    compiler: pathlib.Path,
    source: pathlib.Path,
    output: pathlib.Path,
    metadata: list[pathlib.Path],
) -> None:
    run(
        [
            str(compiler),
            "--emit",
            "obj",
            "-o",
            str(output),
            str(source),
            *(str(path) for path in metadata),
        ]
    )


def reproduce_backend(
    compiler: pathlib.Path,
    graph: dict[str, Module],
    output_root: pathlib.Path,
) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    generated_metadata: dict[str, pathlib.Path] = {}
    for key in (
        "bootstrap_x86_64_text",
        "bootstrap_x86_64_abi",
        "bootstrap_x86_64_frame",
        "bootstrap_x86_64_codegen",
    ):
        module = graph[key]
        metadata = output_root / f"{key}.lmi"
        object_file = output_root / f"{key}.o"
        dependencies = dependency_paths(
            graph, module, generated_metadata
        )
        compile_metadata(compiler, module, metadata, dependencies)
        compile_object(
            compiler,
            module,
            metadata,
            object_file,
            dependencies,
        )
        generated_metadata[key] = metadata
        if metadata.read_bytes() != module.metadata.read_bytes():
            raise AssertionError(f"{module.name} metadata is stale")
        if module.object_file is None:
            raise AssertionError(f"{module.name} has no object")
        if object_file.read_bytes() != module.object_file.read_bytes():
            raise AssertionError(f"{module.name} object is stale")


def link(
    linker: pathlib.Path,
    output: pathlib.Path,
    objects: list[pathlib.Path],
) -> None:
    run(
        [
            str(linker),
            "-o",
            str(output),
            *(str(path) for path in objects),
        ]
    )


def scalar_program() -> tuple[bytes, int]:
    return (
        b"module generated.scalar;\n"
        b"fn adjust(value: i32) -> i32 { return value * 3 - 2; }\n"
        b"fn main() -> i32 {\n"
        b"    var total: i32 = 0;\n"
        b"    for (var index: i32 = 0; index < 5; index += 1) {\n"
        b"        total += adjust(index);\n"
        b"    }\n"
        b"    let real: f64 = 8.75;\n"
        b"    let converted: i32 = real as i32;\n"
        b"    return total + converted + 14;\n"
        b"}\n",
        42,
    )


def aggregate_program() -> tuple[bytes, int]:
    return (
        b"module generated.aggregate;\n"
        b"struct Small { integer: i64; real: f64; }\n"
        b"struct Big { first: i64; second: i64; third: i64; }\n"
        b"fn change(value: *Small) -> i64 {\n"
        b"    (*value).integer = 99;\n"
        b"    return 1;\n"
        b"}\n"
        b"fn snapshot(value: Small, ignored: i64) -> i64 {\n"
        b"    return value.integer + ignored;\n"
        b"}\n"
        b"fn make_small() -> Small {\n"
        b"    return { integer = 8, real = 2.5 };\n"
        b"}\n"
        b"fn make_big(value: i64) -> Big {\n"
        b"    return { first = value, second = value + 1, "
        b"third = value + 2 };\n"
        b"}\n"
        b"fn stack_sum(a: i64, b: i64, c: i64, d: i64, e: i64, "
        b"f: i64, g: i64, h: i64) -> i64 {\n"
        b"    return a + b + c + d + e + f + g + h;\n"
        b"}\n"
        b"fn main() -> i32 {\n"
        b"    var original: Small = { integer = 7, real = 1.5 };\n"
        b"    let preserved: i64 = snapshot(original, change(&original));\n"
        b"    let small: Small = make_small();\n"
        b"    let big: Big = make_big(3);\n"
        b"    let stacked: i64 = stack_sum(1, 2, 3, 4, 5, 6, 7, 8);\n"
        b"    return (preserved + small.integer + big.first + "
        b"big.second + big.third + stacked - 22) as i32;\n"
        b"}\n",
        42,
    )


def memory_program() -> tuple[bytes, int]:
    return (
        b"module generated.memory;\n"
        b"struct Pair { left: i32; right: i32; }\n"
        b"fn main() -> i32 {\n"
        b"    var values: [4]i32 = {};\n"
        b"    values[0 as usize] = 9;\n"
        b"    values[1 as usize] = 11;\n"
        b"    let pointer: *i32 = &values[1 as usize];\n"
        b"    *pointer += 1;\n"
        b"    var first: Pair = { left = values[0 as usize], "
        b"right = values[1 as usize] };\n"
        b"    var second: Pair = {};\n"
        b"    second = first;\n"
        b"    let message: *const u8 = \"A\";\n"
        b"    let contiguous: bool = "
        b"((&values[1 as usize]) as usize - "
        b"(&values[0 as usize]) as usize) == sizeof(i32);\n"
        b"    let result: i32 = second.left + second.right + "
        b"message[0 as usize] as i32 - 44;\n"
        b"    return contiguous ? result : 1;\n"
        b"}\n",
        42,
    )


def numeric_program() -> tuple[bytes, int]:
    return (
        b"module generated.numeric;\n"
        b"fn main() -> i32 {\n"
        b"    let narrow: i8 = -7;\n"
        b"    let widened: i64 = narrow as i64;\n"
        b"    let real: f64 = narrow as f64;\n"
        b"    let round_trip: i8 = real as i8;\n"
        b"    let shifted: i8 = (-8 as i8) >> 2;\n"
        b"    let shifted_left_32: i32 = 3 << 5;\n"
        b"    let shifted_left_64: u64 = 3 as u64 << 5 as u64;\n"
        b"    let nan: f64 = 0.0 / 0.0;\n"
        b"    let maximum: u64 = 18446744073709551615;\n"
        b"    let high_real: f64 = maximum as f64;\n"
        b"    let high_source: u64 = 9223372036854775808;\n"
        b"    let high_value: f64 = high_source as f64;\n"
        b"    let high_integer: u64 = high_value as u64;\n"
        b"    let small_real: f32 = (-3 as i16) as f32;\n"
        b"    let small_integer: i16 = small_real as i16;\n"
        b"    var status: i32 = 0;\n"
        b"    if (widened == -7) { status += 1; }\n"
        b"    if (round_trip == -7) { status += 2; }\n"
        b"    if (shifted == -2 && shifted_left_32 == 96 && "
        b"shifted_left_64 == 96 as u64) { status += 4; }\n"
        b"    if (nan != nan) { status += 8; }\n"
        b"    if (high_real > 0.0) { status += 16; }\n"
        b"    if (high_integer == 9223372036854775808) { status += 32; }\n"
        b"    if (small_integer == -3) { status += 64; }\n"
        b"    if (!false) { status += 128; }\n"
        b"    return status;\n"
        b"}\n",
        255,
    )


def external_abi_program() -> tuple[bytes, int]:
    return (
        b"module generated.external_abi;\n"
        b"struct Mixed { integer: i64; real: f64; }\n"
        b"struct Big { first: i64; second: i64; third: i64; }\n"
        b"extern fn foreign_mixed(value: Mixed) -> i64;\n"
        b"extern fn foreign_make() -> Mixed;\n"
        b"extern fn foreign_big(value: i64) -> Big;\n"
        b"fn main() -> i32 {\n"
        b"    let direct: i64 = foreign_mixed("
        b"{ integer = 19, real = 23.0 });\n"
        b"    let mixed: Mixed = foreign_make();\n"
        b"    let big: Big = foreign_big(3);\n"
        b"    return (direct + mixed.integer + mixed.real as i64 + "
        b"big.first + big.second + big.third - 24) as i32;\n"
        b"}\n",
        42,
    )


def random_program(
    case_index: int, generator: random.Random
) -> tuple[bytes, int]:
    values = [generator.randrange(0, 8) for _ in range(8)]
    multiplier = generator.randrange(1, 5)
    bias = generator.randrange(0, 11)
    loop_limit = generator.randrange(0, 8)
    total = sum(values) * multiplier + bias
    for index in range(loop_limit):
        total += index if index % 2 == 0 else -index
    expected = total % 200
    arguments = ", ".join(str(value) for value in values)
    source = (
        f"module generated.random_{case_index};\n"
        "fn combine(a: i32, b: i32, c: i32, d: i32, e: i32, "
        "f: i32, g: i32, h: i32) -> i32 {\n"
        f"    return (a + b + c + d + e + f + g + h) * {multiplier}"
        f" + {bias};\n"
        "}\n"
        "fn main() -> i32 {\n"
        f"    var result: i32 = combine({arguments});\n"
        f"    for (var index: i32 = 0; index < {loop_limit}; "
        "index += 1) {\n"
        "        if ((index & 1) == 0) { result += index; } "
        "else { result -= index; }\n"
        "    }\n"
        f"    return result % 200;\n"
        "}\n"
    )
    return source.encode(), expected


def generate_assembly(
    runner: list[str],
    driver: pathlib.Path,
    work_dir: pathlib.Path,
    source: bytes,
) -> pathlib.Path:
    input_path = work_dir / "bootstrap-backend-input.luna"
    assembly_path = work_dir / "bootstrap-backend-output.s"
    input_path.write_bytes(source)
    assembly_path.unlink(missing_ok=True)
    run(
        [*runner, str(driver)],
        expected_code=42,
        cwd=work_dir,
        timeout=60,
    )
    if not assembly_path.is_file() or not assembly_path.read_bytes():
        raise AssertionError("bootstrap backend emitted no assembly")
    return assembly_path


def assemble_and_run(
    llvm_mc: str,
    assembler: pathlib.Path,
    linker: pathlib.Path,
    runner: list[str],
    assembly: pathlib.Path,
    expected_code: int,
    work_dir: pathlib.Path,
    case_name: str,
    support_object: pathlib.Path,
) -> None:
    object_path = work_dir / f"{case_name}.o"
    oracle_object = work_dir / f"{case_name}-llvm.o"
    executable = work_dir / case_name
    run([str(assembler), str(assembly), str(object_path)], timeout=60)
    run(
        [
            llvm_mc,
            "-filetype=obj",
            "-triple=x86_64-unknown-linux-gnu",
            str(assembly),
            "-o",
            str(oracle_object),
        ],
        timeout=60,
    )
    link(linker, executable, [object_path, support_object])
    result = run(
        [*runner, str(executable)],
        expected_code=expected_code,
        cwd=work_dir,
        timeout=60,
    )
    if result.stdout or result.stderr:
        raise AssertionError(f"{case_name} unexpectedly produced output")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--assembler", type=pathlib.Path, required=True)
    parser.add_argument("--sysroot", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    compiler = arguments.compiler.resolve()
    linker = arguments.linker.resolve()
    assembler = arguments.assembler.resolve()
    sysroot = arguments.sysroot.resolve()
    source_root = arguments.source_root.resolve()
    work_dir = arguments.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)

    llvm_mc = require_tool("llvm-mc")
    read_elf = require_tool("llvm-readelf")
    graph = module_graph(source_root, sysroot)
    ensure_sysroot(compiler, graph)
    reproduce_backend(compiler, graph, work_dir / "reproduction-one")
    reproduce_backend(compiler, graph, work_dir / "reproduction-two")
    metadata_keys = (
        "runtime",
        "bytes",
        "text",
        "path",
        "io",
        "bootstrap_lexer",
        "bootstrap_parser",
        "bootstrap_type",
        "bootstrap_ir",
        "bootstrap_sema",
        "bootstrap_x86_64_abi",
        "bootstrap_x86_64_frame",
        "bootstrap_x86_64_codegen",
    )
    object_keys = (
        "runtime",
        "memory",
        "bytes",
        "text",
        "path",
        "io",
        "bootstrap_lexer",
        "bootstrap_parser",
        "bootstrap_type",
        "bootstrap_ir",
        "bootstrap_sema",
        "bootstrap_x86_64_text",
        "bootstrap_x86_64_abi",
        "bootstrap_x86_64_frame",
        "bootstrap_x86_64_codegen",
    )
    driver_object = work_dir / "backend-driver.o"
    driver = work_dir / "backend-driver"
    compile_driver(
        compiler,
        source_root
        / "tests"
        / "integration"
        / "cases"
        / "bootstrap_x86_64_backend_driver.luna",
        driver_object,
        [graph[key].metadata for key in metadata_keys],
    )
    link(
        linker,
        driver,
        [
            driver_object,
            *(required_object(graph, key) for key in object_keys),
        ],
    )
    text_resource_object = work_dir / "text-resource.o"
    text_resource = work_dir / "text-resource"
    compile_driver(
        compiler,
        source_root
        / "tests"
        / "integration"
        / "cases"
        / "bootstrap_x86_64_text_resource.luna",
        text_resource_object,
        [
            graph[key].metadata
            for key in (
                "runtime",
                "bytes",
                "text",
                "bootstrap_x86_64_text",
            )
        ],
    )
    link(
        linker,
        text_resource,
        [
            text_resource_object,
            required_object(graph, "runtime"),
            required_object(graph, "memory"),
            required_object(graph, "bytes"),
            required_object(graph, "text"),
            required_object(graph, "bootstrap_x86_64_text"),
        ],
    )
    support_assembly = work_dir / "foreign-support.s"
    support_object = work_dir / "foreign-support.o"
    support_assembly.write_text(
        "    .text\n"
        "    .globl foreign_mixed\n"
        "foreign_mixed:\n"
        "    cvttsd2siq %xmm0, %rax\n"
        "    addq %rdi, %rax\n"
        "    ret\n"
        "    .globl foreign_make\n"
        "foreign_make:\n"
        "    movq $5, %rax\n"
        "    movabsq $0x401c000000000000, %rdx\n"
        "    movq %rdx, %xmm0\n"
        "    ret\n"
        "    .globl foreign_big\n"
        "foreign_big:\n"
        "    movq %rdi, %rax\n"
        "    movq %rsi, (%rdi)\n"
        "    leaq 1(%rsi), %rdx\n"
        "    movq %rdx, 8(%rdi)\n"
        "    leaq 2(%rsi), %rdx\n"
        "    movq %rdx, 16(%rdi)\n"
        "    ret\n"
        "    .section .note.GNU-stack,\"\",@progbits\n"
    )
    run(
        [
            llvm_mc,
            "-filetype=obj",
            "-triple=x86_64-unknown-linux-gnu",
            str(support_assembly),
            "-o",
            str(support_object),
        ]
    )
    runner = target_runner()
    text_resource_result = run(
        [*runner, str(text_resource)],
        expected_code=42,
        cwd=work_dir,
        timeout=120,
    )
    if text_resource_result.stdout or text_resource_result.stderr:
        raise AssertionError("x86-64 text resource test produced output")
    cases = [
        scalar_program(),
        aggregate_program(),
        memory_program(),
        numeric_program(),
        external_abi_program(),
    ]
    generator = random.Random(0x4C554E4158383634)
    cases.extend(random_program(index, generator) for index in range(48))
    for case_index, (source, expected_code) in enumerate(cases):
        assembly = generate_assembly(runner, driver, work_dir, source)
        assemble_and_run(
            llvm_mc,
            assembler,
            linker,
            runner,
            assembly,
            expected_code,
            work_dir,
            f"generated-{case_index}",
            support_object,
        )
    driver_headers = run(
        [read_elf, "--program-headers", str(driver)]
    ).stdout
    if "INTERP" in driver_headers or "DYNAMIC" in driver_headers:
        raise AssertionError("bootstrap backend driver gained a loader")
    driver_symbols = run(
        [read_elf, "--wide", "--symbols", str(driver)]
    ).stdout
    if " UND " in driver_symbols:
        raise AssertionError("bootstrap backend driver has unresolved symbols")
    for key in (
        "bootstrap_x86_64_text",
        "bootstrap_x86_64_abi",
        "bootstrap_x86_64_frame",
        "bootstrap_x86_64_codegen",
    ):
        symbols = run(
            [
                read_elf,
                "--wide",
                "--symbols",
                str(required_object(graph, key)),
            ]
        ).stdout
        if "_start" in symbols or "luna_linux_syscall" in symbols:
            raise AssertionError(
                f"{graph[key].name} crossed its freestanding boundary"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
