#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import random
import sys

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

INTENTIONALLY_MALFORMED_SOURCES = {
    "external_body.luna",
    "malformed_float_literal.luna",
    "parse_error.luna",
}


def required_object(
    graph: dict[str, Module], module_key: str
) -> pathlib.Path:
    module = graph[module_key]
    object_file = module.object_file
    if object_file is None:
        raise AssertionError(f"{module_key} has no separately linked object")
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


def reproduce_frontend(
    compiler: pathlib.Path,
    graph: dict[str, Module],
    output_root: pathlib.Path,
) -> None:
    generated_metadata: dict[str, pathlib.Path] = {}
    for key in ("bootstrap_lexer", "bootstrap_parser"):
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


def generated_program(case_index: int, generator: random.Random) -> bytes:
    term_count = generator.randrange(1, 9)
    terms = [
        str(generator.randrange(0, 1_000_000))
        for _ in range(term_count)
    ]
    operators = [
        generator.choice(("+", "-", "*", "^", "|", "&"))
        for _ in range(term_count - 1)
    ]
    expression = terms[0]
    for operator, term in zip(operators, terms[1:], strict=True):
        expression = f"({expression} {operator} {term})"
    loop_limit = generator.randrange(0, 12)
    return (
        f"module generated.case_{case_index};\n"
        "fn evaluate(seed: i64) -> i64 {\n"
        f"    var result: i64 = {expression};\n"
        f"    for (var index: i64 = 0; index < {loop_limit}; "
        "index += 1) {\n"
        "        if ((index & 1) == 0) { result += seed; } "
        "else { result -= index; }\n"
        "    }\n"
        "    return result;\n"
        "}\n"
    ).encode()


def malformed_programs(generator: random.Random) -> list[tuple[str, bytes]]:
    base = bytearray(
        b"module recovery.sample;\n"
        b"fn evaluate(value: i32) -> i32 {\n"
        b"    var result: i32 = value + 42;\n"
        b"    if (result > 0) { return result; }\n"
        b"    return 0;\n"
        b"}\n"
    )
    insertions = (
        b"@",
        b"{",
        b")",
        b";",
        b"0x_",
        b"1e+",
        b"case",
        b"extern",
        b"\"unterminated\n",
        b"/* unterminated",
    )
    cases: list[tuple[str, bytes]] = [
        ("empty-source", b""),
        (
            "nesting-limit",
            b"module recovery.deep; fn f() -> i32 { return "
            + (b"(" * 300)
            + b"1"
            + (b")" * 300)
            + b"; }\n",
        ),
    ]
    for case_index in range(64):
        mutated = bytearray(base)
        mutation_count = generator.randrange(1, 5)
        for _ in range(mutation_count):
            if generator.randrange(2) == 0 and mutated:
                start = generator.randrange(len(mutated))
                end = min(
                    len(mutated),
                    start + generator.randrange(1, 9),
                )
                del mutated[start:end]
            else:
                position = generator.randrange(len(mutated) + 1)
                mutated[position:position] = generator.choice(insertions)
        mutated[:0] = b"@"
        cases.append((f"malformed-{case_index}", bytes(mutated)))
    return cases


def run_driver_corpus(
    runner: list[str],
    executable: pathlib.Path,
    work_dir: pathlib.Path,
    sources: list[tuple[str, bytes]],
) -> None:
    input_path = work_dir / "bootstrap-input.luna"
    for label, source in sources:
        input_path.write_bytes(source)
        try:
            result = run(
                [*runner, str(executable)],
                expected_code=42,
                cwd=work_dir,
                timeout=60,
            )
        except AssertionError as error:
            raise AssertionError(f"{label}: {error}") from error
        if result.stdout or result.stderr:
            raise AssertionError(
                f"bootstrap parser produced output for {label}"
            )


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

    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    graph = module_graph(arguments.source_root, arguments.sysroot)
    ensure_sysroot(arguments.compiler, graph)
    reproduce_frontend(
        arguments.compiler,
        graph,
        arguments.work_dir / "reproduction-one",
    )
    reproduce_frontend(
        arguments.compiler,
        graph,
        arguments.work_dir / "reproduction-two",
    )
    parser_module = graph["bootstrap_parser"]
    missing_metadata = run(
        [
            str(arguments.compiler),
            "--compile-module",
            parser_module.name,
            "--emit",
            "metadata",
            "-o",
            str(arguments.work_dir / "missing-lexer-metadata.lmi"),
            str(parser_module.source_stem.with_suffix(".interface.luna")),
            str(parser_module.source_stem.with_suffix(".luna")),
            str(graph["runtime"].metadata),
            str(graph["bytes"].metadata),
            str(graph["text"].metadata),
        ],
        expected_code=1,
    )
    if "was not supplied" not in missing_metadata.stderr:
        raise AssertionError(
            "missing lexer metadata lacked a precise dependency error"
        )

    frontend_metadata = [
        graph[key].metadata
        for key in (
            "runtime",
            "bytes",
            "text",
            "bootstrap_lexer",
            "bootstrap_parser",
        )
    ]
    driver_metadata = [
        *frontend_metadata,
        graph["path"].metadata,
        graph["io"].metadata,
    ]
    frontend_objects = [
        required_object(graph, key)
        for key in (
            "runtime",
            "memory",
            "bytes",
            "text",
            "bootstrap_lexer",
            "bootstrap_parser",
        )
    ]
    driver_objects = [
        *frontend_objects,
        required_object(graph, "path"),
        required_object(graph, "io"),
    ]

    cases = arguments.source_root / "tests" / "integration" / "cases"
    contract_object = arguments.work_dir / "bootstrap-contract.o"
    compile_application(
        arguments.compiler,
        cases / "bootstrap_frontend_contract.luna",
        contract_object,
        frontend_metadata,
    )
    contract_executable = arguments.work_dir / "bootstrap-contract"
    run(
        [
            str(arguments.linker),
            "-o",
            str(contract_executable),
            str(contract_object),
            *map(str, frontend_objects),
        ]
    )

    runner = target_runner()
    contract_result = run(
        [*runner, str(contract_executable)],
        expected_code=42,
        cwd=arguments.work_dir,
        timeout=60,
    )
    if contract_result.stdout or contract_result.stderr:
        raise AssertionError("bootstrap contract test produced output")

    driver_object = arguments.work_dir / "bootstrap-driver.o"
    compile_application(
        arguments.compiler,
        cases / "bootstrap_frontend_driver.luna",
        driver_object,
        driver_metadata,
    )
    driver_executable = arguments.work_dir / "bootstrap-driver"
    run(
        [
            str(arguments.linker),
            "-o",
            str(driver_executable),
            str(driver_object),
            *map(str, driver_objects),
        ]
    )

    recovery_object = arguments.work_dir / "bootstrap-recovery-driver.o"
    compile_application(
        arguments.compiler,
        cases / "bootstrap_frontend_recovery_driver.luna",
        recovery_object,
        driver_metadata,
    )
    recovery_executable = arguments.work_dir / "bootstrap-recovery-driver"
    run(
        [
            str(arguments.linker),
            "-o",
            str(recovery_executable),
            str(recovery_object),
            *map(str, driver_objects),
        ]
    )

    program_headers = run(
        [read_elf, "--program-headers", str(driver_executable)]
    ).stdout
    if "INTERP" in program_headers or "DYNAMIC" in program_headers:
        raise AssertionError("bootstrap frontend gained a dynamic loader")
    executable_symbols = run(
        [read_elf, "--wide", "--symbols", str(driver_executable)]
    ).stdout
    if " UND " in executable_symbols:
        raise AssertionError("bootstrap frontend has unresolved symbols")

    for key in ("bootstrap_lexer", "bootstrap_parser"):
        module = graph[key]
        object_file = required_object(graph, key)
        symbols = run(
            [read_elf, "--wide", "--symbols", str(object_file)]
        ).stdout
        if "_start" in symbols or "luna_linux_syscall" in symbols:
            raise AssertionError(
                f"{module.name} crossed its freestanding module boundary"
            )

    runtime_sources = [
        (
            str(path.relative_to(arguments.source_root)),
            path.read_bytes(),
        )
        for path in sorted(
            (arguments.source_root / "runtime" / "luna").rglob("*.luna")
        )
    ]
    integration_sources = [
        (
            str(path.relative_to(arguments.source_root)),
            path.read_bytes(),
        )
        for path in sorted(
            (arguments.source_root / "tests" / "integration" / "cases").glob(
                "*.luna"
            )
        )
        if path.name not in INTENTIONALLY_MALFORMED_SOURCES
    ]
    real_sources = [*runtime_sources, *integration_sources]
    generator = random.Random(0x4C554E4150415253)
    random_sources = [
        (
            f"generated-{case_index}",
            generated_program(case_index, generator),
        )
        for case_index in range(64)
    ]
    malformed_sources = malformed_programs(generator)
    run_driver_corpus(
        runner,
        recovery_executable,
        arguments.work_dir,
        malformed_sources,
    )
    run_driver_corpus(
        runner,
        driver_executable,
        arguments.work_dir,
        [*real_sources, *random_sources],
    )

    for omitted_key in ("bootstrap_lexer", "bootstrap_parser"):
        omitted = required_object(graph, omitted_key)
        missing = run(
            [
                str(arguments.linker),
                "-o",
                str(arguments.work_dir / f"missing-{omitted_key}"),
                str(driver_object),
                *(str(path) for path in driver_objects if path != omitted),
            ],
            expected_code=1,
        )
        if "undefined symbol" not in missing.stderr:
            raise AssertionError(
                f"missing {omitted_key} object lacked a precise link error"
            )

    print(
        "PASS Luna bootstrap lexer/parser: "
        f"{len(real_sources)} real modules and "
        f"{len(random_sources)} generated programs plus "
        f"{len(malformed_sources)} recovery cases"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
