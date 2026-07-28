#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import fractions
import pathlib
import random
import re
import shutil

from bootstrap_semantic_convergence import (
    ExecutionCase,
    RejectionCase,
    SourceUnit,
    fixed_execution_cases,
    generated_execution_cases,
    rejection_cases,
)
from run_minimum_standard_library import (
    Module,
    ensure_sysroot,
    module_graph,
    require_tool,
    run,
    target_runner,
)


STAGE_LIBRARY_KEYS = (
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

STAGE_INTERFACE_KEYS = ("syscall", *STAGE_LIBRARY_KEYS)

STAGE_DRIVER_METADATA_KEYS = (
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
    "bootstrap_x86_64_text",
    "bootstrap_x86_64_abi",
    "bootstrap_x86_64_frame",
    "bootstrap_x86_64_codegen",
)

STAGE_NATIVE_TIMEOUT_SECONDS = 300
STAGE_EMULATED_TIMEOUT_SECONDS = 1200


@dataclasses.dataclass(frozen=True)
class StageArtifacts:
    root: pathlib.Path
    compiler: pathlib.Path
    assemblies: dict[str, pathlib.Path]
    objects: dict[str, pathlib.Path]


@dataclasses.dataclass(frozen=True)
class BinaryFloatFormat:
    name: str
    fraction_bits: int
    exponent_bits: int
    exponent_bias: int

    @property
    def maximum_bits(self) -> int:
        exponent_mask = (1 << self.exponent_bits) - 1
        return ((exponent_mask - 1) << self.fraction_bits) | (
            (1 << self.fraction_bits) - 1
        )


BINARY_FLOAT_FORMATS = (
    BinaryFloatFormat("f32", 23, 8, 127),
    BinaryFloatFormat("f64", 52, 11, 1023),
)


def required_object(graph: dict[str, Module], key: str) -> pathlib.Path:
    object_file = graph[key].object_file
    if object_file is None:
        raise AssertionError(f"{key} has no separately linked object")
    return object_file


def compile_stage_one_driver(
    compiler: pathlib.Path,
    source: pathlib.Path,
    output: pathlib.Path,
    graph: dict[str, Module],
) -> None:
    run(
        [
            str(compiler),
            "--emit",
            "obj",
            "-o",
            str(output),
            str(source),
            *(
                str(graph[key].metadata)
                for key in STAGE_DRIVER_METADATA_KEYS
            ),
        ],
        timeout=120,
    )


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
        ],
        timeout=120,
    )


def verify_static_executable(read_elf: str, executable: pathlib.Path) -> None:
    headers = run(
        [read_elf, "--program-headers", str(executable)],
        timeout=60,
    ).stdout
    if "INTERP" in headers or "DYNAMIC" in headers:
        raise AssertionError(f"{executable} gained a dynamic loader")
    symbols = run(
        [read_elf, "--wide", "--symbols", str(executable)],
        timeout=60,
    ).stdout
    if " UND " in symbols:
        raise AssertionError(f"{executable} has unresolved symbols")


def verify_relocatable_object(
    read_elf: str,
    object_file: pathlib.Path,
    defines_entry: bool,
) -> None:
    header = run(
        [read_elf, "--file-header", str(object_file)],
        timeout=60,
    ).stdout
    if "REL (Relocatable file)" not in header:
        raise AssertionError(f"{object_file} is not an ELF relocatable object")
    symbols = run(
        [read_elf, "--wide", "--symbols", str(object_file)],
        timeout=60,
    ).stdout
    has_entry = re.search(r"\b_start\b", symbols) is not None
    if has_entry != defines_entry:
        expectation = "define" if defines_entry else "not define"
        raise AssertionError(
            f"{object_file} must {expectation} the bootstrap entry point"
        )


def dependency_closure(
    graph: dict[str, Module],
    key: str,
) -> tuple[str, ...]:
    keys_by_name = {
        module.name: candidate for candidate, module in graph.items()
    }
    required: set[str] = set(graph[key].dependencies)

    def interface_dependencies(current: str) -> tuple[str, ...]:
        source = graph[current].source_stem.with_suffix(
            ".interface.luna"
        ).read_text(encoding="utf-8")
        names = re.findall(
            r"^[ \t]*import[ \t]+([A-Za-z0-9_.]+)[ \t]*;[ \t]*$",
            source,
            flags=re.MULTILINE,
        )
        dependencies: list[str] = []
        for name in names:
            if name not in keys_by_name:
                raise AssertionError(
                    f"{graph[current].name} imports unknown module {name}"
                )
            dependencies.append(keys_by_name[name])
        return tuple(dependencies)

    pending = list(required)
    while pending:
        current = pending.pop()
        for dependency in interface_dependencies(current):
            if dependency not in required:
                required.add(dependency)
                pending.append(dependency)
    return tuple(
        candidate
        for candidate in STAGE_INTERFACE_KEYS
        if candidate in required
    )


def module_sources(
    graph: dict[str, Module],
    key: str,
) -> list[pathlib.Path]:
    module = graph[key]
    return [
        module.source_stem.with_suffix(".luna"),
        module.source_stem.with_suffix(".interface.luna"),
        *(
            graph[dependency].source_stem.with_suffix(".interface.luna")
            for dependency in dependency_closure(graph, key)
        ),
    ]


def driver_sources(
    driver_source: pathlib.Path,
    graph: dict[str, Module],
) -> list[pathlib.Path]:
    return [
        driver_source,
        *(
            graph[key].source_stem.with_suffix(".interface.luna")
            for key in STAGE_DRIVER_METADATA_KEYS
        ),
    ]


def prepare_invocation(
    invocation_root: pathlib.Path,
    executable: bool,
    sources: list[pathlib.Path],
) -> None:
    if invocation_root.exists():
        shutil.rmtree(invocation_root)
    invocation_root.mkdir(parents=True)
    (invocation_root / "bootstrap-stage-version").write_bytes(
        b"LUNA-STAGE/1 LUNA/1\n"
    )
    (invocation_root / "bootstrap-stage-mode").write_bytes(
        b"E\n" if executable else b"L\n"
    )
    for index, source in enumerate(sources):
        destination = (
            invocation_root / f"bootstrap-stage-unit-{index}.luna"
        )
        destination.write_bytes(source.read_bytes())


def invoke_stage_compiler(
    compiler: pathlib.Path,
    runner: list[str],
    invocation_root: pathlib.Path,
    executable: bool,
    sources: list[pathlib.Path],
    output: pathlib.Path,
) -> None:
    prepare_invocation(invocation_root, executable, sources)
    generated = invocation_root / "bootstrap-stage-output.s"
    run(
        [*runner, str(compiler)],
        expected_code=42,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=invocation_root,
    )
    if not generated.is_file() or not generated.read_bytes():
        raise AssertionError(
            f"{compiler} produced no assembly for {output.name}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(generated.read_bytes())


def assemble(
    assembler: pathlib.Path,
    assembly: pathlib.Path,
    output: pathlib.Path,
) -> None:
    run(
        [str(assembler), str(assembly), str(output)],
        timeout=180,
    )


def build_stage(
    stage_name: str,
    producing_compiler: pathlib.Path,
    runner: list[str],
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    graph: dict[str, Module],
    driver_source: pathlib.Path,
    work_dir: pathlib.Path,
) -> StageArtifacts:
    stage_root = work_dir / stage_name
    if stage_root.exists():
        shutil.rmtree(stage_root)
    assembly_root = stage_root / "assembly"
    object_root = stage_root / "objects"
    invocation_root = stage_root / "invocations"
    assembly_root.mkdir(parents=True)
    object_root.mkdir(parents=True)
    invocation_root.mkdir(parents=True)

    assemblies: dict[str, pathlib.Path] = {}
    objects: dict[str, pathlib.Path] = {}
    for key in STAGE_LIBRARY_KEYS:
        assembly = assembly_root / f"{key}.s"
        object_file = object_root / f"{key}.o"
        invoke_stage_compiler(
            producing_compiler,
            runner,
            invocation_root / key,
            False,
            module_sources(graph, key),
            assembly,
        )
        assemble(assembler, assembly, object_file)
        verify_relocatable_object(read_elf, object_file, False)
        assemblies[key] = assembly
        objects[key] = object_file

    driver_assembly = assembly_root / "stage_compiler.s"
    driver_object = object_root / "stage_compiler.o"
    invoke_stage_compiler(
        producing_compiler,
        runner,
        invocation_root / "stage_compiler",
        True,
        driver_sources(driver_source, graph),
        driver_assembly,
    )
    assemble(assembler, driver_assembly, driver_object)
    verify_relocatable_object(read_elf, driver_object, True)
    assemblies["stage_compiler"] = driver_assembly
    objects["stage_compiler"] = driver_object

    stage_compiler = stage_root / "luna-stage-compiler"
    link(
        linker,
        stage_compiler,
        [
            driver_object,
            *(objects[key] for key in STAGE_LIBRARY_KEYS),
        ],
    )
    verify_static_executable(read_elf, stage_compiler)
    return StageArtifacts(
        root=stage_root,
        compiler=stage_compiler,
        assemblies=assemblies,
        objects=objects,
    )


def compare_files(
    left: pathlib.Path,
    right: pathlib.Path,
    description: str,
) -> None:
    left_bytes = left.read_bytes()
    right_bytes = right.read_bytes()
    if left_bytes != right_bytes:
        mismatch = min(len(left_bytes), len(right_bytes))
        for index, (left_byte, right_byte) in enumerate(
            zip(left_bytes, right_bytes)
        ):
            if left_byte != right_byte:
                mismatch = index
                break
        raise AssertionError(
            f"{description} is not reproducible: "
            f"{left} ({len(left_bytes)} bytes) != "
            f"{right} ({len(right_bytes)} bytes), "
            f"first mismatch at byte {mismatch}"
        )


def compare_stages(
    stage_two: StageArtifacts,
    stage_three: StageArtifacts,
) -> None:
    for key in (*STAGE_LIBRARY_KEYS, "stage_compiler"):
        compare_files(
            stage_two.assemblies[key],
            stage_three.assemblies[key],
            f"{key} assembly",
        )
        compare_files(
            stage_two.objects[key],
            stage_three.objects[key],
            f"{key} ELF object",
        )
    compare_files(
        stage_two.compiler,
        stage_three.compiler,
        "stage compiler executable",
    )


def padded_module(name: str, size: int) -> bytes:
    prefix = f"module {name};\n/*".encode("ascii")
    suffix = b"*/"
    if size < len(prefix) + len(suffix):
        raise AssertionError("padded module size is too small")
    return prefix + b"x" * (size - len(prefix) - len(suffix)) + suffix


def run_negative_driver_tests(
    stage_one: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
) -> None:
    missing_version = work_dir / "negative-missing-version"
    if missing_version.exists():
        shutil.rmtree(missing_version)
    missing_version.mkdir(parents=True)
    run(
        [*runner, str(stage_one)],
        expected_code=9,
        cwd=missing_version,
    )

    for name, version in (
        ("mismatched", b"LUNA-STAGE/1 LUNA/0\n"),
        ("truncated", b"LUNA-STAGE/1 LUNA/1"),
        ("extended", b"LUNA-STAGE/1 LUNA/1\nx"),
    ):
        invalid_version = work_dir / f"negative-{name}-version"
        if invalid_version.exists():
            shutil.rmtree(invalid_version)
        invalid_version.mkdir(parents=True)
        (invalid_version / "bootstrap-stage-version").write_bytes(version)
        run(
            [*runner, str(stage_one)],
            expected_code=9,
            cwd=invalid_version,
        )

    missing_mode = work_dir / "negative-missing-mode"
    if missing_mode.exists():
        shutil.rmtree(missing_mode)
    missing_mode.mkdir(parents=True)
    (missing_mode / "bootstrap-stage-version").write_bytes(
        b"LUNA-STAGE/1 LUNA/1\n"
    )
    run(
        [*runner, str(stage_one)],
        expected_code=1,
        cwd=missing_mode,
    )

    no_input = work_dir / "negative-no-input"
    prepare_invocation(no_input, True, [])
    run(
        [*runner, str(stage_one)],
        expected_code=2,
        cwd=no_input,
    )

    malformed = work_dir / "negative-malformed"
    prepare_invocation(malformed, True, [])
    (malformed / "bootstrap-stage-unit-0.luna").write_bytes(
        b"module negative.malformed;\nfn main( -> i32 {\n"
    )
    malformed_result = run(
        [*runner, str(stage_one)],
        expected_code=2,
        cwd=malformed,
    )
    if not malformed_result.stderr.startswith("frontend:parse:"):
        raise AssertionError(
            "self-hosted parser diagnostic lost its stable encoding: "
            f"{malformed_result.stderr!r}"
        )

    lexical = work_dir / "negative-lexical"
    prepare_invocation(lexical, True, [])
    (lexical / "bootstrap-stage-unit-0.luna").write_bytes(
        b"@module negative.lexical;\nfn main() -> i32 { return 42; }\n"
    )
    lexical_result = run(
        [*runner, str(stage_one)],
        expected_code=2,
        cwd=lexical,
    )
    if lexical_result.stderr != "frontend:lex:0:0:0\n":
        raise AssertionError(
            "self-hosted lexer diagnostic lost its stable encoding: "
            f"{lexical_result.stderr!r}"
        )

    nesting = work_dir / "negative-nesting-limit"
    prepare_invocation(nesting, True, [])
    (nesting / "bootstrap-stage-unit-0.luna").write_bytes(
        b"@module negative.nesting;\nfn main() -> i32 { return "
        + b"(" * 300
        + b"42"
        + b")" * 300
        + b"; }\n"
    )
    nesting_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        cwd=nesting,
    )
    if not nesting_result.stderr.startswith("frontend:parse:12:"):
        raise AssertionError(
            "parser nesting limit lost its stable encoding: "
            f"{nesting_result.stderr!r}"
        )

    token_length = work_dir / "negative-token-length"
    prepare_invocation(token_length, False, [])
    (token_length / "bootstrap-stage-unit-0.luna").write_bytes(
        b"module " + b"a" * 65537 + b";\n"
    )
    token_length_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        cwd=token_length,
    )
    if token_length_result.stderr != "frontend:lex:13:7:65536\n":
        raise AssertionError(
            "lexer token-length limit lost its stable encoding: "
            f"{token_length_result.stderr!r}"
        )

    token_count = work_dir / "negative-token-count"
    prepare_invocation(token_count, False, [])
    (token_count / "bootstrap-stage-unit-0.luna").write_bytes(
        b"module negative.tokens;\n" + b";" * 131072
    )
    token_count_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        cwd=token_count,
    )
    if not (
        token_count_result.stderr.startswith("frontend:lex:13:")
        and token_count_result.stderr.endswith(":131072\n")
    ):
        raise AssertionError(
            "lexer token-count limit lost its stable encoding: "
            f"{token_count_result.stderr!r}"
        )

    diagnostics = work_dir / "negative-diagnostic-count"
    prepare_invocation(diagnostics, False, [])
    (diagnostics / "bootstrap-stage-unit-0.luna").write_bytes(
        b"module negative.diagnostics;\n" + b"@" * 4097
    )
    diagnostics_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        cwd=diagnostics,
    )
    if diagnostics_result.stderr != "resource:frontend:0:4096\n":
        raise AssertionError(
            "frontend diagnostic limit lost its stable encoding: "
            f"{diagnostics_result.stderr!r}"
        )

    source_limit = work_dir / "negative-source-limit"
    prepare_invocation(source_limit, False, [])
    (source_limit / "bootstrap-stage-unit-0.luna").write_bytes(
        b"x" * (8388608 + 1)
    )
    source_limit_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        timeout=300,
        cwd=source_limit,
    )
    if source_limit_result.stderr != "resource:source:0:8388608\n":
        raise AssertionError(
            "source-size limit lost its stable encoding: "
            f"{source_limit_result.stderr!r}"
        )

    total_limit = work_dir / "negative-total-source-limit"
    prepare_invocation(total_limit, False, [])
    for index in range(5):
        (total_limit / f"bootstrap-stage-unit-{index}.luna").write_bytes(
            padded_module(f"negative.total_{index}", 8388608)
        )
    total_limit_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        timeout=300,
        cwd=total_limit,
    )
    if total_limit_result.stderr != "resource:total:4:33554432\n":
        raise AssertionError(
            "total source-size limit lost its stable encoding: "
            f"{total_limit_result.stderr!r}"
        )

    type_depth = work_dir / "negative-type-depth"
    prepare_invocation(type_depth, False, [])
    type_lines = ["module negative.type_depth;"]
    type_lines.extend(
        f"struct Type{index} {{ value: Type{index + 1}; }}"
        for index in range(256)
    )
    type_lines.append("struct Type256 { value: i32; }")
    (type_depth / "bootstrap-stage-unit-0.luna").write_text(
        "\n".join(type_lines) + "\n",
        encoding="ascii",
    )
    type_depth_result = run(
        [*runner, str(stage_one)],
        expected_code=115,
        cwd=type_depth,
    )
    if not (
        type_depth_result.stderr.startswith("semantic:51:0:")
        and type_depth_result.stderr.endswith(":256\n")
    ):
        raise AssertionError(
            "semantic type-depth limit lost its stable encoding: "
            f"{type_depth_result.stderr!r}"
        )

    semantic_diagnostics = work_dir / "negative-semantic-diagnostics"
    prepare_invocation(semantic_diagnostics, False, [])
    semantic_diagnostic_lines = [
        "module negative.semantic_diagnostics;",
        "struct Same { value: i32; }",
    ]
    semantic_diagnostic_lines.extend(
        "struct Same { value: i32; }" for _ in range(4097)
    )
    (
        semantic_diagnostics / "bootstrap-stage-unit-0.luna"
    ).write_text(
        "\n".join(semantic_diagnostic_lines) + "\n",
        encoding="ascii",
    )
    semantic_diagnostics_result = run(
        [*runner, str(stage_one)],
        expected_code=8,
        cwd=semantic_diagnostics,
    )
    if (
        semantic_diagnostics_result.stderr
        != "resource:semantic:0:4096\n"
    ):
        raise AssertionError(
            "semantic diagnostic limit lost its stable encoding: "
            f"{semantic_diagnostics_result.stderr!r}"
        )

    semantic = work_dir / "negative-semantic"
    prepare_invocation(semantic, True, [])
    (semantic / "bootstrap-stage-unit-0.luna").write_bytes(
        b"module negative.semantic;\n"
        b"fn main() -> i32 { return true; }\n"
    )
    semantic_result = run(
        [*runner, str(stage_one)],
        expected_code=108,
        cwd=semantic,
    )
    if not semantic_result.stderr.startswith("semantic:44:0:"):
        raise AssertionError(
            "self-hosted semantic diagnostic lost its stable encoding: "
            f"{semantic_result.stderr!r}"
        )

    unit_limit = work_dir / "negative-unit-limit"
    prepare_invocation(unit_limit, False, [])
    for index in range(65):
        (unit_limit / f"bootstrap-stage-unit-{index}.luna").write_bytes(
            f"module negative.limit_{index};\n".encode("ascii")
        )
    run(
        [*runner, str(stage_one)],
        expected_code=3,
        timeout=120,
        cwd=unit_limit,
    )


def run_fixed_point_probe(
    compiler: pathlib.Path,
    runner: list[str],
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    work_dir: pathlib.Path,
) -> bytes:
    work_dir.mkdir(parents=True, exist_ok=True)
    probe = work_dir / "fixed-point-probe.luna"
    probe.write_bytes(
        b"module reproducibility.probe;\n"
        b"fn fibonacci(value: i32) -> i32 {\n"
        b"    return value < 2 ? value : "
        b"fibonacci(value - 1) + fibonacci(value - 2);\n"
        b"}\n"
        b"fn main() -> i32 { return fibonacci(9) + 8; }\n"
    )
    output = work_dir / "fixed-point-probe.s"
    invoke_stage_compiler(
        compiler,
        runner,
        work_dir / "fixed-point-probe-invocation",
        True,
        [probe],
        output,
    )
    object_file = work_dir / "fixed-point-probe.o"
    executable = work_dir / "fixed-point-probe"
    assemble(assembler, output, object_file)
    verify_relocatable_object(read_elf, object_file, True)
    link(linker, executable, [object_file])
    verify_static_executable(read_elf, executable)
    run(
        [*runner, str(executable)],
        expected_code=42,
        timeout=120,
        cwd=work_dir,
    )
    return output.read_bytes()


def run_luna_one_language_probe(
    stage_zero: pathlib.Path,
    stage_one: pathlib.Path,
    stage_two: pathlib.Path,
    stage_three: pathlib.Path,
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    runner: list[str],
    source_root: pathlib.Path,
    work_dir: pathlib.Path,
) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    source = (
        source_root
        / "tests"
        / "integration"
        / "cases"
        / "luna_one_loop.luna"
    )
    if not source.is_file():
        raise AssertionError(f"Luna 1 loop probe is missing: {source}")
    stage_zero_result = run(
        [
            str(stage_zero),
            "--emit",
            "check",
            str(source),
        ],
        expected_code=1,
        timeout=120,
    )
    if "loop" not in stage_zero_result.stderr:
        raise AssertionError(
            "the Luna 0 seed unexpectedly accepted the Luna 1 loop "
            f"statement: {stage_zero_result.stderr!r}"
        )

    case = ExecutionCase(
        "luna-one-loop",
        (SourceUnit(source.name),),
        42,
        frozenset(("control-flow",)),
    )
    assemblies: list[pathlib.Path] = []
    for stage_name, compiler in (
        ("stage-one", stage_one),
        ("stage-two", stage_two),
        ("stage-three", stage_three),
    ):
        assemblies.append(
            run_self_hosted_execution_case(
                case,
                stage_name,
                compiler,
                assembler,
                linker,
                read_elf,
                runner,
                [source],
                work_dir,
            )
        )
    compare_files(
        assemblies[0],
        assemblies[1],
        "Luna 1 stage-one/stage-two loop assembly",
    )
    compare_files(
        assemblies[1],
        assemblies[2],
        "Luna 1 stage-two/stage-three loop assembly",
    )

    malformed = work_dir / "luna-one-malformed-loop.luna"
    malformed.write_bytes(
        b"module reproducibility.luna_one_malformed_loop;\n"
        b"fn main() -> i32 { loop; return 42; }\n"
    )
    diagnostics: list[str] = []
    for stage_name, compiler in (
        ("stage-one", stage_one),
        ("stage-two", stage_two),
        ("stage-three", stage_three),
    ):
        invocation = work_dir / f"{stage_name}-malformed"
        prepare_invocation(invocation, True, [malformed])
        result = run(
            [*runner, str(compiler)],
            expected_code=2,
            timeout=(
                STAGE_EMULATED_TIMEOUT_SECONDS
                if runner
                else STAGE_NATIVE_TIMEOUT_SECONDS
            ),
            cwd=invocation,
        )
        if not result.stderr.startswith("frontend:parse:"):
            raise AssertionError(
                f"{stage_name} lost malformed loop diagnostic: "
                f"{result.stderr!r}"
            )
        if (invocation / "bootstrap-stage-output.s").exists():
            raise AssertionError(
                f"{stage_name} emitted assembly for malformed loop"
            )
        diagnostics.append(result.stderr)
    if len(set(diagnostics)) != 1:
        raise AssertionError(
            "self-hosted stages disagree on malformed loop diagnostic"
        )


def materialize_convergence_units(
    case_name: str,
    units: tuple[SourceUnit, ...],
    source_root: pathlib.Path,
    generated_root: pathlib.Path,
) -> list[pathlib.Path]:
    materialized: list[pathlib.Path] = []
    case_root = source_root / "tests" / "integration" / "cases"
    output_root = generated_root / case_name
    for unit_index, unit in enumerate(units):
        if unit.content is None:
            source = case_root / unit.name
            if not source.is_file():
                raise AssertionError(
                    f"semantic convergence source is missing: {source}"
                )
        else:
            output_root.mkdir(parents=True, exist_ok=True)
            source = output_root / f"{unit_index}-{unit.name}"
            source.write_bytes(unit.content)
        materialized.append(source)
    return materialized


def run_quiet_executable(
    executable: pathlib.Path,
    runner: list[str],
    expected_code: int,
    work_dir: pathlib.Path,
) -> None:
    result = run(
        [*runner, str(executable)],
        expected_code=expected_code,
        timeout=120,
        cwd=work_dir,
    )
    if result.stdout or result.stderr:
        raise AssertionError(
            f"{executable.name} unexpectedly produced output"
        )


def run_stage_zero_execution_case(
    case: ExecutionCase,
    compiler: pathlib.Path,
    linker: pathlib.Path,
    runner: list[str],
    sources: list[pathlib.Path],
    work_dir: pathlib.Path,
    suffix: str = "",
) -> None:
    case_root = work_dir / f"stage-zero{suffix}"
    case_root.mkdir(parents=True, exist_ok=True)
    object_file = case_root / f"{case.name}.o"
    executable = case_root / case.name
    run(
        [
            str(compiler),
            "--emit",
            "obj",
            "-o",
            str(object_file),
            *(str(source) for source in sources),
        ],
        timeout=180,
    )
    link(linker, executable, [object_file])
    run_quiet_executable(
        executable,
        runner,
        case.expected_code,
        case_root,
    )


def run_self_hosted_execution_case(
    case: ExecutionCase,
    stage_name: str,
    compiler: pathlib.Path,
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    runner: list[str],
    sources: list[pathlib.Path],
    work_dir: pathlib.Path,
) -> pathlib.Path:
    case_root = work_dir / stage_name
    case_root.mkdir(parents=True, exist_ok=True)
    assembly = case_root / f"{case.name}.s"
    invoke_stage_compiler(
        compiler,
        runner,
        case_root / f"{case.name}-invocation",
        True,
        sources,
        assembly,
    )
    object_file = case_root / f"{case.name}.o"
    executable = case_root / case.name
    assemble(assembler, assembly, object_file)
    verify_relocatable_object(read_elf, object_file, True)
    link(linker, executable, [object_file])
    verify_static_executable(read_elf, executable)
    run_quiet_executable(
        executable,
        runner,
        case.expected_code,
        case_root,
    )
    return assembly


def run_stage_zero_rejection_case(
    case: RejectionCase,
    compiler: pathlib.Path,
    sources: list[pathlib.Path],
) -> None:
    run(
        [
            str(compiler),
            "--emit",
            "check",
            *(str(source) for source in sources),
        ],
        expected_code=1,
        timeout=120,
    )


def run_self_hosted_rejection_case(
    case: RejectionCase,
    stage_name: str,
    compiler: pathlib.Path,
    runner: list[str],
    sources: list[pathlib.Path],
    work_dir: pathlib.Path,
) -> str:
    invocation = work_dir / stage_name / case.name
    prepare_invocation(invocation, True, sources)
    result = run(
        [*runner, str(compiler)],
        expected_code=64 + case.diagnostic_kind,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=invocation,
    )
    expected_prefix = f"semantic:{case.diagnostic_kind}:"
    if not result.stderr.startswith(expected_prefix):
        raise AssertionError(
            f"{case.name} lost diagnostic {case.diagnostic_kind}: "
            f"{result.stderr!r}"
        )
    if (
        invocation / "bootstrap-stage-output.s"
    ).exists():
        raise AssertionError(
            f"{case.name} emitted assembly after semantic rejection"
        )
    return result.stderr


def run_semantic_convergence(
    stage_zero: pathlib.Path,
    stage_two: pathlib.Path,
    stage_three: pathlib.Path,
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    runner: list[str],
    source_root: pathlib.Path,
    work_dir: pathlib.Path,
) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    generated_root = work_dir / "sources"

    execution_cases = (
        *fixed_execution_cases(),
        *generated_execution_cases(),
    )
    for case in execution_cases:
        sources = materialize_convergence_units(
            case.name,
            case.units,
            source_root,
            generated_root,
        )
        case_root = work_dir / "execution" / case.name
        run_stage_zero_execution_case(
            case,
            stage_zero,
            linker,
            runner,
            sources,
            case_root,
        )
        stage_two_assembly = run_self_hosted_execution_case(
            case,
            "stage-two",
            stage_two,
            assembler,
            linker,
            read_elf,
            runner,
            sources,
            case_root,
        )
        stage_three_assembly = run_self_hosted_execution_case(
            case,
            "stage-three",
            stage_three,
            assembler,
            linker,
            read_elf,
            runner,
            sources,
            case_root,
        )
        compare_files(
            stage_two_assembly,
            stage_three_assembly,
            f"{case.name} semantic convergence assembly",
        )

        if case.verify_reversed_order:
            reversed_sources = list(reversed(sources))
            run_stage_zero_execution_case(
                case,
                stage_zero,
                linker,
                runner,
                reversed_sources,
                case_root,
                "-reversed",
            )
            reversed_assembly = run_self_hosted_execution_case(
                case,
                "stage-two-reversed",
                stage_two,
                assembler,
                linker,
                read_elf,
                runner,
                reversed_sources,
                case_root,
            )
            compare_files(
                stage_two_assembly,
                reversed_assembly,
                f"{case.name} source-order-independent assembly",
            )

    for case in rejection_cases():
        sources = materialize_convergence_units(
            case.name,
            case.units,
            source_root,
            generated_root,
        )
        run_stage_zero_rejection_case(case, stage_zero, sources)
        stage_two_diagnostic = run_self_hosted_rejection_case(
            case,
            "stage-two",
            stage_two,
            runner,
            sources,
            work_dir / "rejection",
        )
        stage_three_diagnostic = run_self_hosted_rejection_case(
            case,
            "stage-three",
            stage_three,
            runner,
            sources,
            work_dir / "rejection",
        )
        if stage_two_diagnostic != stage_three_diagnostic:
            raise AssertionError(
                f"{case.name} diagnostic differs between stage two "
                "and stage three"
            )


def positive_float_fraction(
    floating_format: BinaryFloatFormat,
    bits: int,
) -> fractions.Fraction:
    if bits < 0 or bits > floating_format.maximum_bits:
        raise AssertionError(
            f"{floating_format.name} bits are not finite: {bits:#x}"
        )
    fraction_mask = (1 << floating_format.fraction_bits) - 1
    fraction = bits & fraction_mask
    exponent_field = bits >> floating_format.fraction_bits
    if exponent_field == 0:
        significand = fraction
        binary_exponent = (
            1
            - floating_format.exponent_bias
            - floating_format.fraction_bits
        )
    else:
        significand = (1 << floating_format.fraction_bits) | fraction
        binary_exponent = (
            exponent_field
            - floating_format.exponent_bias
            - floating_format.fraction_bits
        )
    if binary_exponent >= 0:
        return fractions.Fraction(significand << binary_exponent)
    return fractions.Fraction(significand, 1 << -binary_exponent)


def terminating_decimal(value: fractions.Fraction) -> str:
    if value < 0:
        raise AssertionError("floating probe only formats positive values")
    denominator = value.denominator
    power_of_two = 0
    while denominator % 2 == 0:
        denominator //= 2
        power_of_two += 1
    power_of_five = 0
    while denominator % 5 == 0:
        denominator //= 5
        power_of_five += 1
    if denominator != 1:
        raise AssertionError(f"{value} has no terminating decimal form")

    decimal_places = max(power_of_two, power_of_five)
    scaled = value.numerator
    scaled *= 2 ** (decimal_places - power_of_two)
    scaled *= 5 ** (decimal_places - power_of_five)
    digits = str(scaled)
    if decimal_places == 0:
        return f"{digits}.0"
    if len(digits) <= decimal_places:
        digits = "0" * (decimal_places + 1 - len(digits)) + digits
    split = len(digits) - decimal_places
    return f"{digits[:split]}.{digits[split:]}"


def decimal_perturbation(literal: str) -> fractions.Fraction:
    decimal_places = len(literal.partition(".")[2])
    return fractions.Fraction(1, 10 ** (decimal_places + 4))


def floating_probe_cases() -> list[tuple[BinaryFloatFormat, str, int]]:
    cases: list[tuple[BinaryFloatFormat, str, int]] = []
    generator = random.Random(0x4C554E41464C4F41)
    for floating_format in BINARY_FLOAT_FORMATS:
        fraction_mask = (1 << floating_format.fraction_bits) - 1
        one_bits = floating_format.exponent_bias << (
            floating_format.fraction_bits
        )
        fixed_lower_bits = {
            0,
            1,
            2,
            fraction_mask - 1,
            fraction_mask,
            fraction_mask + 1,
            one_bits - 1,
            one_bits,
            one_bits + 1,
            floating_format.maximum_bits - 1,
        }
        while len(fixed_lower_bits) < 16:
            fixed_lower_bits.add(
                generator.randrange(0, floating_format.maximum_bits)
            )

        for lower_bits in sorted(fixed_lower_bits):
            lower = positive_float_fraction(floating_format, lower_bits)
            upper = positive_float_fraction(
                floating_format,
                lower_bits + 1,
            )
            midpoint = (lower + upper) / 2
            midpoint_literal = terminating_decimal(midpoint)
            perturbation = decimal_perturbation(midpoint_literal)
            cases.extend(
                (
                    (
                        floating_format,
                        terminating_decimal(midpoint - perturbation),
                        lower_bits,
                    ),
                    (
                        floating_format,
                        midpoint_literal,
                        (
                            lower_bits
                            if lower_bits & 1 == 0
                            else lower_bits + 1
                        ),
                    ),
                    (
                        floating_format,
                        terminating_decimal(midpoint + perturbation),
                        lower_bits + 1,
                    ),
                )
            )

        for exact_bits in (
            0,
            1,
            fraction_mask,
            fraction_mask + 1,
            one_bits,
            floating_format.maximum_bits,
        ):
            cases.append(
                (
                    floating_format,
                    terminating_decimal(
                        positive_float_fraction(
                            floating_format,
                            exact_bits,
                        )
                    ),
                    exact_bits,
                )
            )

        maximum = positive_float_fraction(
            floating_format,
            floating_format.maximum_bits,
        )
        previous = positive_float_fraction(
            floating_format,
            floating_format.maximum_bits - 1,
        )
        overflow_midpoint = maximum + (maximum - previous) / 2
        overflow_literal = terminating_decimal(overflow_midpoint)
        cases.append(
            (
                floating_format,
                terminating_decimal(
                    overflow_midpoint
                    - decimal_perturbation(overflow_literal)
                ),
                floating_format.maximum_bits,
            )
        )

    cases.extend(
        (
            (
                BINARY_FLOAT_FORMATS[0],
                "1.0000000596046447753906251",
                0x3F800001,
            ),
            (
                BINARY_FLOAT_FORMATS[1],
                "9007199254740993.0",
                0x4340000000000000,
            ),
            (BINARY_FLOAT_FORMATS[0], "1_0.0_0e-1", 0x3F800000),
            (BINARY_FLOAT_FORMATS[1], "1e-10000", 0),
            (BINARY_FLOAT_FORMATS[1], "0e999999999999999999999", 0),
        )
    )

    midpoint = terminating_decimal(
        (
            positive_float_fraction(
                BINARY_FLOAT_FORMATS[1],
                0x3FF0000000000000,
            )
            + positive_float_fraction(
                BINARY_FLOAT_FORMATS[1],
                0x3FF0000000000001,
            )
        )
        / 2
    )
    significant_count = len(midpoint.replace(".", ""))
    cases.append(
        (
            BINARY_FLOAT_FORMATS[1],
            midpoint + "0" * (1300 - significant_count),
            0x3FF0000000000000,
        )
    )
    cases.append(
        (
            BINARY_FLOAT_FORMATS[1],
            midpoint
            + "0" * (1200 - significant_count)
            + "1",
            0x3FF0000000000001,
        )
    )
    return cases


def write_floating_probe(source: pathlib.Path) -> None:
    lines = [
        "module reproducibility.floating_probe;",
        "fn f32_bits(value: f32) -> u32 {",
        "    return *((&value) as *const f32 as *const u32);",
        "}",
        "fn f64_bits(value: f64) -> u64 {",
        "    return *((&value) as *const f64 as *const u64);",
        "}",
        "fn main() -> i32 {",
    ]
    for case_index, (floating_format, literal, expected) in enumerate(
        floating_probe_cases()
    ):
        failure_code = case_index + 1
        if failure_code == 42:
            failure_code = 242
        lines.extend(
            (
                f"    let value_{case_index}: "
                f"{floating_format.name} = {literal};",
                f"    if ({floating_format.name}_bits("
                f"value_{case_index}) != {expected}) {{",
                f"        return {failure_code};",
                "    }",
            )
        )
    lines.extend(("    return 42;", "}", ""))
    source.write_text("\n".join(lines), encoding="ascii")


def compile_and_run_stage_zero_probe(
    compiler: pathlib.Path,
    linker: pathlib.Path,
    runner: list[str],
    read_elf: str,
    source: pathlib.Path,
    work_dir: pathlib.Path,
) -> None:
    work_dir.mkdir(parents=True, exist_ok=True)
    object_file = work_dir / "floating-probe.o"
    executable = work_dir / "floating-probe"
    run(
        [
            str(compiler),
            "--emit",
            "obj",
            "-o",
            str(object_file),
            str(source),
        ],
        timeout=180,
    )
    verify_relocatable_object(read_elf, object_file, True)
    link(linker, executable, [object_file])
    verify_static_executable(read_elf, executable)
    run(
        [*runner, str(executable)],
        expected_code=42,
        timeout=120,
        cwd=work_dir,
    )


def compile_and_run_self_hosted_probe(
    compiler: pathlib.Path,
    runner: list[str],
    assembler: pathlib.Path,
    linker: pathlib.Path,
    read_elf: str,
    source: pathlib.Path,
    work_dir: pathlib.Path,
) -> bytes:
    work_dir.mkdir(parents=True, exist_ok=True)
    assembly = work_dir / "floating-probe.s"
    invoke_stage_compiler(
        compiler,
        runner,
        work_dir / "invocation",
        True,
        [source],
        assembly,
    )
    object_file = work_dir / "floating-probe.o"
    executable = work_dir / "floating-probe"
    assemble(assembler, assembly, object_file)
    verify_relocatable_object(read_elf, object_file, True)
    link(linker, executable, [object_file])
    verify_static_executable(read_elf, executable)
    run(
        [*runner, str(executable)],
        expected_code=42,
        timeout=120,
        cwd=work_dir,
    )
    return assembly.read_bytes()


def run_floating_overflow_tests(
    stage_zero: pathlib.Path,
    stage_two: pathlib.Path,
    stage_three: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
) -> None:
    work_dir.mkdir(parents=True, exist_ok=True)
    for floating_format in BINARY_FLOAT_FORMATS:
        maximum = positive_float_fraction(
            floating_format,
            floating_format.maximum_bits,
        )
        previous = positive_float_fraction(
            floating_format,
            floating_format.maximum_bits - 1,
        )
        overflow_midpoint = maximum + (maximum - previous) / 2
        source = work_dir / f"{floating_format.name}-overflow.luna"
        source.write_text(
            "module reproducibility."
            f"{floating_format.name}_overflow;\n"
            f"fn value() -> {floating_format.name} {{\n"
            f"    return {terminating_decimal(overflow_midpoint)};\n"
            "}\n"
            "fn main() -> i32 { return 0; }\n",
            encoding="ascii",
        )
        stage_zero_result = run(
            [
                str(stage_zero),
                "--emit",
                "obj",
                "-o",
                str(work_dir / f"{floating_format.name}-stage-zero.o"),
                str(source),
            ],
            expected_code=1,
            timeout=120,
        )
        if "does not fit" not in stage_zero_result.stderr:
            raise AssertionError(
                f"stage zero lost {floating_format.name} overflow diagnostic"
            )
        for stage_name, compiler in (
            ("stage-two", stage_two),
            ("stage-three", stage_three),
        ):
            invocation = work_dir / f"{floating_format.name}-{stage_name}"
            prepare_invocation(invocation, True, [source])
            result = run(
                [*runner, str(compiler)],
                expected_code=114,
                timeout=(
                    STAGE_EMULATED_TIMEOUT_SECONDS
                    if runner
                    else STAGE_NATIVE_TIMEOUT_SECONDS
                ),
                cwd=invocation,
            )
            if not result.stderr.startswith("semantic:50:0:"):
                raise AssertionError(
                    f"{stage_name} lost {floating_format.name} "
                    f"overflow diagnostic: {result.stderr!r}"
                )


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
    driver_source = (
        source_root / "tools" / "bootstrap" / "stage_compiler.luna"
    )

    read_elf = require_tool("llvm-readelf")
    graph = module_graph(source_root, sysroot)
    ensure_sysroot(compiler, graph)
    runner = target_runner()

    stage_one_object = work_dir / "stage-one-compiler.o"
    stage_one = work_dir / "stage-one-compiler"
    compile_stage_one_driver(
        compiler,
        driver_source,
        stage_one_object,
        graph,
    )
    link(
        linker,
        stage_one,
        [
            stage_one_object,
            *(
                required_object(graph, key)
                for key in STAGE_LIBRARY_KEYS
            ),
        ],
    )
    verify_static_executable(read_elf, stage_one)
    run_negative_driver_tests(
        stage_one,
        runner,
        work_dir / "negative",
    )

    stage_two = build_stage(
        "stage-two",
        stage_one,
        runner,
        assembler,
        linker,
        read_elf,
        graph,
        driver_source,
        work_dir,
    )
    stage_three = build_stage(
        "stage-three",
        stage_two.compiler,
        runner,
        assembler,
        linker,
        read_elf,
        graph,
        driver_source,
        work_dir,
    )
    compare_stages(stage_two, stage_three)

    stage_two_probe = run_fixed_point_probe(
        stage_two.compiler,
        runner,
        assembler,
        linker,
        read_elf,
        work_dir / "stage-two-probe",
    )
    stage_three_probe = run_fixed_point_probe(
        stage_three.compiler,
        runner,
        assembler,
        linker,
        read_elf,
        work_dir / "stage-three-probe",
    )
    if stage_two_probe != stage_three_probe:
        raise AssertionError(
            "stage-two and stage-three compilers disagree on probe assembly"
        )

    run_luna_one_language_probe(
        compiler,
        stage_one,
        stage_two.compiler,
        stage_three.compiler,
        assembler,
        linker,
        read_elf,
        runner,
        source_root,
        work_dir / "luna-one-language",
    )

    run_semantic_convergence(
        compiler,
        stage_two.compiler,
        stage_three.compiler,
        assembler,
        linker,
        read_elf,
        runner,
        source_root,
        work_dir / "semantic-convergence",
    )

    floating_root = work_dir / "floating"
    floating_root.mkdir(parents=True, exist_ok=True)
    floating_source = floating_root / "floating-probe.luna"
    write_floating_probe(floating_source)
    compile_and_run_stage_zero_probe(
        compiler,
        linker,
        runner,
        read_elf,
        floating_source,
        floating_root / "stage-zero",
    )
    stage_two_floating = compile_and_run_self_hosted_probe(
        stage_two.compiler,
        runner,
        assembler,
        linker,
        read_elf,
        floating_source,
        floating_root / "stage-two",
    )
    stage_three_floating = compile_and_run_self_hosted_probe(
        stage_three.compiler,
        runner,
        assembler,
        linker,
        read_elf,
        floating_source,
        floating_root / "stage-three",
    )
    if stage_two_floating != stage_three_floating:
        raise AssertionError(
            "stage-two and stage-three compilers disagree on exact "
            "floating-point literals"
        )
    run_floating_overflow_tests(
        compiler,
        stage_two.compiler,
        stage_three.compiler,
        runner,
        floating_root / "overflow",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
