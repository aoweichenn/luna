#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import random

from run_minimum_standard_library import (
    Module,
    compile_metadata,
    compile_object,
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


def compile_application(
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


def reproduce_middleend(
    compiler: pathlib.Path,
    graph: dict[str, Module],
    output_root: pathlib.Path,
) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    generated_metadata: dict[str, pathlib.Path] = {}
    for key in ("bootstrap_type", "bootstrap_ir", "bootstrap_sema"):
        module = graph[key]
        metadata = output_root / f"{key}.lmi"
        object_file = output_root / f"{key}.o"
        dependencies = [
            generated_metadata.get(dependency, graph[dependency].metadata)
            for dependency in module.dependencies
        ]
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
            raise AssertionError(f"{module.name} unexpectedly has no object")
        if object_file.read_bytes() != module.object_file.read_bytes():
            raise AssertionError(f"{module.name} object is stale")


def link_application(
    linker: pathlib.Path,
    application_object: pathlib.Path,
    output: pathlib.Path,
    objects: list[pathlib.Path],
) -> None:
    run(
        [
            str(linker),
            "-o",
            str(output),
            str(application_object),
            *(str(path) for path in objects),
        ]
    )


def run_quiet(
    runner: list[str],
    executable: pathlib.Path,
    expected_code: int,
    work_dir: pathlib.Path,
) -> None:
    result = run(
        [*runner, str(executable)],
        expected_code=expected_code,
        cwd=work_dir,
        timeout=60,
    )
    if result.stdout or result.stderr:
        raise AssertionError(
            f"{executable.name} unexpectedly produced output"
        )


def generated_valid_program(
    case_index: int, generator: random.Random
) -> bytes:
    term_count = generator.randrange(1, 10)
    terms = [
        str(generator.randrange(0, 100_000))
        for _ in range(term_count)
    ]
    expression = terms[0]
    for term in terms[1:]:
        operation = generator.choice(("+", "-", "*", "^", "|", "&"))
        expression = f"({expression} {operation} {term})"
    loop_limit = generator.randrange(0, 12)
    return (
        f"module random.valid_{case_index};\n"
        "struct Pair { left: i64; right: i64; }\n"
        "fn combine(pair: Pair) -> i64 {\n"
        "    return pair.left + pair.right;\n"
        "}\n"
        "fn main() -> i32 {\n"
        f"    var pair: Pair = {{ left = {expression}, right = 0 }};\n"
        f"    for (var index: i64 = 0; index < {loop_limit}; "
        "index += 1) {\n"
        "        if ((index & 1) == 0) { pair.right += index; } "
        "else { pair.right -= index; }\n"
        "    }\n"
        "    let selected: i64 = pair.left > pair.right ? "
        "pair.left : pair.right;\n"
        "    return (combine(pair) + selected) as i32;\n"
        "}\n"
    ).encode()


def generated_invalid_program(
    case_index: int, generator: random.Random
) -> bytes:
    left_type, right_type, initializer = generator.choice(
        (
            ("i32", "bool", "true"),
            ("u64", "f64", "1.25"),
            ("bool", "i32", "7"),
            ("f32", "u32", "9"),
        )
    )
    operation = generator.choice(("+", "-", "*", "^", "&", "|"))
    return (
        f"module random.invalid_{case_index};\n"
        "fn main() -> i32 {\n"
        f"    let left: {left_type} = 1;\n"
        f"    let right: {right_type} = {initializer};\n"
        f"    let broken: {left_type} = left {operation} right;\n"
        "    return broken as i32;\n"
        "}\n"
    ).encode()


def run_corpus(
    runner: list[str],
    executable: pathlib.Path,
    work_dir: pathlib.Path,
) -> None:
    input_path = work_dir / "bootstrap-middleend-input.luna"
    fixed_cases: list[tuple[str, bytes, int]] = [
        (
            "unknown-type",
            b"module negative.unknown_type;\n"
            b"fn main() -> i32 { let value: Missing = {}; return 0; }\n",
            64 + 12,
        ),
        (
            "recursive-type",
            b"module negative.recursive;\n"
            b"struct Node { next: Node; }\n"
            b"fn main() -> i32 { return 0; }\n",
            64 + 14,
        ),
        (
            "duplicate-local",
            b"module negative.duplicate_local;\n"
            b"fn main() -> i32 { let value: i32 = 1; "
            b"let value: i32 = 2; return value; }\n",
            64 + 27,
        ),
        (
            "immutable-assignment",
            b"module negative.immutable;\n"
            b"fn main() -> i32 { let value: i32 = 1; "
            b"value = 2; return value; }\n",
            64 + 28,
        ),
        (
            "type-mismatch",
            b"module negative.type_mismatch;\n"
            b"fn main() -> i32 { let value: i32 = true; return value; }\n",
            64 + 29,
        ),
        (
            "invalid-cast",
            b"module negative.invalid_cast;\n"
            b"fn main() -> i32 { let value: bool = true; "
            b"return value as i32; }\n",
            64 + 32,
        ),
        (
            "break-outside",
            b"module negative.break_outside;\n"
            b"fn main() -> i32 { break; return 0; }\n",
            64 + 42,
        ),
        (
            "missing-return",
            b"module negative.missing_return;\n"
            b"fn main() -> i32 { let value: i32 = 1; }\n",
            64 + 45,
        ),
    ]
    for label, source, expected_code in fixed_cases:
        input_path.write_bytes(source)
        try:
            run_quiet(
                runner,
                executable,
                expected_code,
                work_dir,
            )
        except AssertionError as error:
            raise AssertionError(f"{label}: {error}") from error

    generator = random.Random(0x4C554E4153454D41)
    for case_index in range(64):
        input_path.write_bytes(
            generated_valid_program(case_index, generator)
        )
        run_quiet(runner, executable, 42, work_dir)
    for case_index in range(64):
        input_path.write_bytes(
            generated_invalid_program(case_index, generator)
        )
        run_quiet(runner, executable, 64 + 29, work_dir)


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument(
        "--compiler", type=pathlib.Path, required=True
    )
    argument_parser.add_argument(
        "--linker", type=pathlib.Path, required=True
    )
    argument_parser.add_argument(
        "--sysroot", type=pathlib.Path, required=True
    )
    argument_parser.add_argument(
        "--source-root", type=pathlib.Path, required=True
    )
    argument_parser.add_argument(
        "--work-dir", type=pathlib.Path, required=True
    )
    arguments = argument_parser.parse_args()
    arguments.compiler = arguments.compiler.resolve()
    arguments.linker = arguments.linker.resolve()
    arguments.sysroot = arguments.sysroot.resolve()
    arguments.source_root = arguments.source_root.resolve()
    arguments.work_dir = arguments.work_dir.resolve()

    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    graph = module_graph(arguments.source_root, arguments.sysroot)
    ensure_sysroot(arguments.compiler, graph)
    reproduce_middleend(
        arguments.compiler,
        graph,
        arguments.work_dir / "reproduction-one",
    )
    reproduce_middleend(
        arguments.compiler,
        graph,
        arguments.work_dir / "reproduction-two",
    )

    metadata = [
        graph[key].metadata
        for key in (
            "runtime",
            "bytes",
            "text",
            "bootstrap_lexer",
            "bootstrap_parser",
            "bootstrap_type",
            "bootstrap_ir",
            "bootstrap_sema",
        )
    ]
    objects = [
        required_object(graph, key)
        for key in (
            "runtime",
            "memory",
            "bytes",
            "text",
            "bootstrap_lexer",
            "bootstrap_parser",
            "bootstrap_type",
            "bootstrap_ir",
            "bootstrap_sema",
        )
    ]
    cases = arguments.source_root / "tests" / "integration" / "cases"
    runner = target_runner()

    for source_name in (
        "bootstrap_middleend_driver.luna",
        "bootstrap_middleend_modules_driver.luna",
    ):
        object_path = arguments.work_dir / f"{source_name}.o"
        executable = arguments.work_dir / source_name.removesuffix(".luna")
        compile_application(
            arguments.compiler,
            cases / source_name,
            object_path,
            metadata,
        )
        link_application(
            arguments.linker,
            object_path,
            executable,
            objects,
        )
        run_quiet(runner, executable, 42, arguments.work_dir)

    corpus_metadata = [
        *metadata,
        graph["path"].metadata,
        graph["io"].metadata,
    ]
    corpus_objects = [
        *objects,
        required_object(graph, "path"),
        required_object(graph, "io"),
    ]
    corpus_object = arguments.work_dir / "bootstrap-corpus.o"
    corpus_executable = arguments.work_dir / "bootstrap-corpus"
    compile_application(
        arguments.compiler,
        cases / "bootstrap_middleend_corpus_driver.luna",
        corpus_object,
        corpus_metadata,
    )
    link_application(
        arguments.linker,
        corpus_object,
        corpus_executable,
        corpus_objects,
    )
    run_corpus(runner, corpus_executable, arguments.work_dir)

    program_headers = run(
        [read_elf, "--program-headers", str(corpus_executable)]
    ).stdout
    if "INTERP" in program_headers or "DYNAMIC" in program_headers:
        raise AssertionError("bootstrap middleend gained a dynamic loader")
    symbols = run(
        [read_elf, "--wide", "--symbols", str(corpus_executable)]
    ).stdout
    if " UND " in symbols:
        raise AssertionError("bootstrap middleend has unresolved symbols")
    for key in ("bootstrap_type", "bootstrap_ir", "bootstrap_sema"):
        module_symbols = run(
            [
                read_elf,
                "--wide",
                "--symbols",
                str(required_object(graph, key)),
            ]
        ).stdout
        if "_start" in module_symbols or "luna_linux_syscall" in module_symbols:
            raise AssertionError(
                f"{graph[key].name} crossed its freestanding boundary"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
