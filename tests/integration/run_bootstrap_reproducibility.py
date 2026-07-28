#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import shutil

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


def run_negative_driver_tests(
    stage_one: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
) -> None:
    missing_mode = work_dir / "negative-missing-mode"
    if missing_mode.exists():
        shutil.rmtree(missing_mode)
    missing_mode.mkdir(parents=True)
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
    run(
        [*runner, str(stage_one)],
        expected_code=2,
        cwd=malformed,
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
