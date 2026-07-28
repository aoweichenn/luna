#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import fractions
import io
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tarfile

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
    "bootstrap_x86_64_object",
    "bootstrap_x86_64_assembler",
    "bootstrap_x86_64_linker",
)

STAGE_INTERFACE_KEYS = ("syscall", *STAGE_LIBRARY_KEYS)

STAGE_COMPILER_DRIVER_METADATA_KEYS = (
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

STAGE_ASSEMBLER_DRIVER_METADATA_KEYS = (
    "runtime",
    "bytes",
    "text",
    "path",
    "io",
    "bootstrap_x86_64_object",
    "bootstrap_x86_64_assembler",
)

STAGE_LINKER_DRIVER_METADATA_KEYS = (
    "runtime",
    "bytes",
    "text",
    "path",
    "io",
    "bootstrap_x86_64_text",
    "bootstrap_x86_64_object",
    "bootstrap_x86_64_linker",
)

TOOLCHAIN_UNIT_DRIVER_METADATA_KEYS = (
    "runtime",
    "bytes",
    "text",
    "bootstrap_x86_64_object",
    "bootstrap_x86_64_assembler",
    "bootstrap_x86_64_linker",
)

STAGE_DRIVERS = {
    "stage_compiler": STAGE_COMPILER_DRIVER_METADATA_KEYS,
    "stage_assembler": STAGE_ASSEMBLER_DRIVER_METADATA_KEYS,
    "stage_linker": STAGE_LINKER_DRIVER_METADATA_KEYS,
}

STAGE_NATIVE_TIMEOUT_SECONDS = 300
STAGE_EMULATED_TIMEOUT_SECONDS = 1200
BOOTSTRAP_SEED_TARGET = "x86_64-unknown-linux-gnu"
BOOTSTRAP_SEED_TAR_RECORD_BYTES = 10240


@dataclasses.dataclass(frozen=True)
class StageArtifacts:
    root: pathlib.Path
    compiler: pathlib.Path
    assembler: pathlib.Path
    linker: pathlib.Path
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
    metadata_keys: tuple[str, ...],
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
                for key in metadata_keys
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


def read_u64_le(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(data):
        raise AssertionError("truncated little-endian u64")
    return int.from_bytes(data[offset : offset + 8], "little")


def verify_luna_object(
    object_file: pathlib.Path,
    defines_entry: bool,
) -> None:
    data = object_file.read_bytes()
    if len(data) < 112 or data[:8] != b"LUNAOBJ1":
        raise AssertionError(f"{object_file} is not a Luna object")
    if int.from_bytes(data[8:12], "little") != 1:
        raise AssertionError(f"{object_file} has an unknown object version")
    if int.from_bytes(data[12:16], "little") != 112:
        raise AssertionError(f"{object_file} has an invalid object header")
    (
        text_size,
        rodata_size,
        data_size,
        _bss_size,
        name_size,
        symbol_count,
        relocation_count,
        text_alignment,
        rodata_alignment,
        data_alignment,
        bss_alignment,
        reserved,
    ) = tuple(read_u64_le(data, 16 + index * 8) for index in range(12))
    expected_size = (
        112
        + text_size
        + rodata_size
        + data_size
        + name_size
        + symbol_count * 56
        + relocation_count * 40
    )
    if expected_size != len(data) or reserved != 0:
        raise AssertionError(f"{object_file} has an invalid object extent")
    for alignment in (
        text_alignment,
        rodata_alignment,
        data_alignment,
        bss_alignment,
    ):
        if (
            alignment == 0
            or alignment > 4096
            or alignment & (alignment - 1)
        ):
            raise AssertionError(
                f"{object_file} has an invalid section alignment"
            )
    names_offset = 112 + text_size + rodata_size + data_size
    symbols_offset = names_offset + name_size
    has_entry = False
    for symbol_index in range(symbol_count):
        record = symbols_offset + symbol_index * 56
        name_offset = read_u64_le(data, record)
        symbol_name_size = read_u64_le(data, record + 8)
        if (
            name_offset > name_size
            or symbol_name_size > name_size - name_offset
        ):
            raise AssertionError(f"{object_file} has an invalid symbol name")
        name = data[
            names_offset + name_offset :
            names_offset + name_offset + symbol_name_size
        ]
        flags = read_u64_le(data, record + 40)
        if name == b"_start" and flags & 1:
            has_entry = True
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
    metadata_keys: tuple[str, ...],
) -> list[pathlib.Path]:
    return [
        driver_source,
        *(
            graph[key].source_stem.with_suffix(".interface.luna")
            for key in metadata_keys
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


def assemble_with_stage(
    assembler: pathlib.Path,
    runner: list[str],
    assembly: pathlib.Path,
    output: pathlib.Path,
) -> None:
    invocation_root = output.parent / f".{output.name}-assemble"
    if invocation_root.exists():
        shutil.rmtree(invocation_root)
    invocation_root.mkdir(parents=True)
    (invocation_root / "bootstrap-assembly-input.s").write_bytes(
        assembly.read_bytes()
    )
    run(
        [*runner, str(assembler)],
        expected_code=42,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=invocation_root,
    )
    generated = invocation_root / "bootstrap-object-output.lo"
    if not generated.is_file() or not generated.read_bytes():
        raise AssertionError(f"{assembler} produced no object for {assembly}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(generated.read_bytes())


def link_with_stage(
    linker: pathlib.Path,
    runner: list[str],
    output: pathlib.Path,
    objects: list[pathlib.Path],
) -> None:
    invocation_root = output.parent / f".{output.name}-link"
    if invocation_root.exists():
        shutil.rmtree(invocation_root)
    invocation_root.mkdir(parents=True)
    for index, object_file in enumerate(objects):
        (invocation_root / f"bootstrap-link-input-{index}.lo").write_bytes(
            object_file.read_bytes()
        )
    run(
        [*runner, str(linker)],
        expected_code=42,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=invocation_root,
    )
    generated = invocation_root / "bootstrap-link-output"
    if not generated.is_file() or not generated.read_bytes():
        raise AssertionError(f"{linker} produced no executable for {output}")
    if generated.stat().st_mode & 0o111 == 0:
        raise AssertionError(f"{linker} did not mark its output executable")
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(generated, output)


def build_stage(
    stage_name: str,
    producing_compiler: pathlib.Path,
    producing_assembler: pathlib.Path,
    producing_linker: pathlib.Path,
    runner: list[str],
    read_elf: str,
    graph: dict[str, Module],
    driver_sources_by_name: dict[str, pathlib.Path],
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
    binary_root = stage_root / "bin"
    binary_root.mkdir(parents=True)

    assemblies: dict[str, pathlib.Path] = {}
    objects: dict[str, pathlib.Path] = {}
    for key in STAGE_LIBRARY_KEYS:
        assembly = assembly_root / f"{key}.s"
        object_file = object_root / f"{key}.lo"
        invoke_stage_compiler(
            producing_compiler,
            runner,
            invocation_root / key,
            False,
            module_sources(graph, key),
            assembly,
        )
        assemble_with_stage(
            producing_assembler,
            runner,
            assembly,
            object_file,
        )
        verify_luna_object(object_file, False)
        assemblies[key] = assembly
        objects[key] = object_file

    executables: dict[str, pathlib.Path] = {}
    executable_names = {
        "stage_compiler": "lunac",
        "stage_assembler": "luna-as",
        "stage_linker": "luna-link",
    }
    for driver_name, metadata_keys in STAGE_DRIVERS.items():
        driver_assembly = assembly_root / f"{driver_name}.s"
        driver_object = object_root / f"{driver_name}.lo"
        invoke_stage_compiler(
            producing_compiler,
            runner,
            invocation_root / driver_name,
            True,
            driver_sources(
                driver_sources_by_name[driver_name],
                graph,
                metadata_keys,
            ),
            driver_assembly,
        )
        assemble_with_stage(
            producing_assembler,
            runner,
            driver_assembly,
            driver_object,
        )
        verify_luna_object(driver_object, True)
        assemblies[driver_name] = driver_assembly
        objects[driver_name] = driver_object
        executable = binary_root / executable_names[driver_name]
        link_with_stage(
            producing_linker,
            runner,
            executable,
            [
                driver_object,
                *(objects[key] for key in STAGE_LIBRARY_KEYS),
            ],
        )
        verify_static_executable(read_elf, executable)
        executables[driver_name] = executable
    return StageArtifacts(
        root=stage_root,
        compiler=executables["stage_compiler"],
        assembler=executables["stage_assembler"],
        linker=executables["stage_linker"],
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
    for key in (*STAGE_LIBRARY_KEYS, *STAGE_DRIVERS):
        compare_files(
            stage_two.assemblies[key],
            stage_three.assemblies[key],
            f"{key} assembly",
        )
        compare_files(
            stage_two.objects[key],
            stage_three.objects[key],
            f"{key} Luna object",
        )
    for description, left, right in (
        ("stage compiler executable", stage_two.compiler, stage_three.compiler),
        (
            "stage assembler executable",
            stage_two.assembler,
            stage_three.assembler,
        ),
        ("stage linker executable", stage_two.linker, stage_three.linker),
    ):
        compare_files(left, right, description)


def project_version(source_root: pathlib.Path) -> str:
    version_bytes = (source_root / "VERSION").read_bytes()
    if (
        not version_bytes.endswith(b"\n")
        or version_bytes.count(b"\n") != 1
    ):
        raise AssertionError("VERSION is not one newline-terminated value")
    try:
        version = version_bytes[:-1].decode("ascii")
    except UnicodeDecodeError as error:
        raise AssertionError("VERSION is not ASCII") from error
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is None:
        raise AssertionError(f"VERSION is not semantic: {version!r}")
    return version


def require_seed_rejection(
    result: subprocess.CompletedProcess[str],
    description: str,
) -> None:
    if not result.stderr.startswith("bootstrap seed:"):
        raise AssertionError(
            f"{description} lost the stable seed rejection diagnostic"
        )


def mutate_seed_member(
    archive: pathlib.Path,
    output: pathlib.Path,
    member_suffix: str,
) -> None:
    data = bytearray(archive.read_bytes())
    with tarfile.open(archive, mode="r:") as seed_tar:
        matching = [
            member
            for member in seed_tar.getmembers()
            if member.name.endswith(member_suffix)
        ]
    if len(matching) != 1 or matching[0].size == 0:
        raise AssertionError(
            f"cannot locate one seed member ending in {member_suffix}"
        )
    member = matching[0]
    mutation_offset = member.offset_data + min(64, member.size - 1)
    data[mutation_offset] ^= 1
    output.write_bytes(data)


def run_bootstrap_seed_distribution_tests(
    stage: StageArtifacts,
    source_root: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    first_root = work_dir
    second_root = work_dir / "second"
    mutation_root = work_dir / "mutations"
    first_root.mkdir(parents=True)
    second_root.mkdir(parents=True)
    mutation_root.mkdir(parents=True)

    version = project_version(source_root)
    archive_name = (
        f"luna-bootstrap-seed-{version}-{BOOTSTRAP_SEED_TARGET}.tar"
    )
    seed_tool = source_root / "tools" / "release" / "bootstrap_seed.py"
    first_archive = first_root / archive_name
    second_archive = second_root / archive_name
    first_checksum = first_archive.with_name(
        f"{first_archive.name}.sha256"
    )
    second_checksum = second_archive.with_name(
        f"{second_archive.name}.sha256"
    )
    expected_checksum = (
        source_root
        / "release"
        / "seeds"
        / f"{archive_name}.sha256"
    )
    for output in (first_archive, second_archive):
        run(
            [
                sys.executable,
                str(seed_tool),
                "create",
                "--source-root",
                str(source_root),
                "--tool-dir",
                str(stage.root / "bin"),
                "--output",
                str(output),
                "--target",
                BOOTSTRAP_SEED_TARGET,
            ],
            timeout=120,
        )
    compare_files(
        first_archive,
        second_archive,
        "independent bootstrap seed archives",
    )
    compare_files(
        first_checksum,
        second_checksum,
        "independent bootstrap seed checksums",
    )
    if not expected_checksum.is_file():
        raise AssertionError(
            f"versioned seed checksum is not tracked: {expected_checksum}"
        )
    compare_files(
        first_checksum,
        expected_checksum,
        "tracked bootstrap seed checksum",
    )
    run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(first_archive),
            "--checksum-file",
            str(first_checksum),
            "--expected-version",
            version,
            "--expected-target",
            BOOTSTRAP_SEED_TARGET,
        ],
        timeout=120,
    )
    extracted_root = work_dir / "verified-extraction"
    run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(first_archive),
            "--checksum-file",
            str(first_checksum),
            "--extract-dir",
            str(extracted_root),
        ],
        timeout=120,
    )
    extracted_seed = (
        extracted_root
        / f"luna-bootstrap-seed-{version}-{BOOTSTRAP_SEED_TARGET}"
    )
    for required in (
        extracted_seed / "manifest.json",
        extracted_seed / "bin" / "lunac",
        extracted_seed / "bin" / "luna-as",
        extracted_seed / "bin" / "luna-link",
    ):
        if not required.is_file():
            raise AssertionError(f"safe seed extraction omitted {required}")
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(first_archive),
            "--extract-dir",
            str(extracted_root),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "existing extraction destination")

    rebuild_root = work_dir / "offline-rebuild"
    run(
        [
            sys.executable,
            str(seed_tool),
            "rebuild",
            str(first_archive),
            "--checksum-file",
            str(first_checksum),
            "--expected-version",
            version,
            "--expected-target",
            BOOTSTRAP_SEED_TARGET,
            "--work-dir",
            str(rebuild_root),
        ],
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
    )
    for tool_name, original in (
        ("lunac", stage.compiler),
        ("luna-as", stage.assembler),
        ("luna-link", stage.linker),
    ):
        compare_files(
            original,
            rebuild_root / "rebuild" / "bin" / tool_name,
            f"offline rebuilt {tool_name}",
        )

    corrupted_payload = mutation_root / archive_name
    mutate_seed_member(
        first_archive,
        corrupted_payload,
        "/bin/lunac",
    )
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(corrupted_payload),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "payload mutation")

    trailing_archive = mutation_root / f"trailing-{archive_name}"
    trailing_archive.write_bytes(first_archive.read_bytes() + b"x")
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(trailing_archive),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "trailing archive bytes")

    archive_bytes = first_archive.read_bytes()
    truncated_archive = mutation_root / f"truncated-{archive_name}"
    truncated_archive.write_bytes(
        archive_bytes[:-BOOTSTRAP_SEED_TAR_RECORD_BYTES]
    )
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(truncated_archive),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "truncated archive")

    bad_checksum = mutation_root / f"{archive_name}.sha256"
    bad_checksum.write_text(
        f"{'0' * 64}  {archive_name}\n",
        encoding="ascii",
    )
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(first_archive),
            "--checksum-file",
            str(bad_checksum),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "checksum mutation")

    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(first_archive),
            "--expected-version",
            "999.0.0",
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "version mismatch")

    traversal_archive = mutation_root / "path-traversal.tar"
    with tarfile.open(traversal_archive, mode="w") as malicious:
        information = tarfile.TarInfo("../escaped")
        information.size = 1
        malicious.addfile(information, io.BytesIO(b"x"))
    result = run(
        [
            sys.executable,
            str(seed_tool),
            "verify",
            str(traversal_archive),
        ],
        expected_code=1,
        timeout=120,
    )
    require_seed_rejection(result, "path traversal")
    if (work_dir / "escaped").exists():
        raise AssertionError("seed verifier extracted a traversal member")


def run_command_line_tool_tests(
    stage: StageArtifacts,
    runner: list[str],
    read_elf: str,
    work_dir: pathlib.Path,
) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)

    for tool, expected_name in (
        (stage.compiler, "lunac"),
        (stage.assembler, "luna-as"),
        (stage.linker, "luna-link"),
    ):
        help_result = run(
            [*runner, str(tool), "--help"],
            expected_code=0,
            timeout=120,
        )
        if "usage:" not in help_result.stdout:
            raise AssertionError(f"{expected_name} lost its help text")
        version_result = run(
            [*runner, str(tool), "--version"],
            expected_code=0,
            timeout=120,
        )
        if expected_name not in version_result.stdout:
            raise AssertionError(f"{expected_name} lost its version identity")

    source = work_dir / "command-line-entry.luna"
    source.write_text(
        "module cli.command_line_entry;\n"
        "fn main(argc: usize, argv: **const u8) -> i32 {\n"
        "    if (argc != 3) { return 1; }\n"
        "    if (argv[0][0] == 0) { return 2; }\n"
        "    if (argv[1][0] != 120 || argv[1][1] != 0) { return 3; }\n"
        "    if (argv[2][0] != 121 || argv[2][1] != 122 ||\n"
        "        argv[2][2] != 0) { return 4; }\n"
        "    return 42;\n"
        "}\n",
        encoding="ascii",
    )
    assembly = work_dir / "command-line-entry.s"
    object_file = work_dir / "command-line-entry.lo"
    executable = work_dir / "command-line-entry"
    run(
        [
            *runner,
            str(stage.compiler),
            "--executable",
            "-o",
            str(assembly),
            str(source),
        ],
        expected_code=0,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    run(
        [
            *runner,
            str(stage.assembler),
            "-o",
            str(object_file),
            str(assembly),
        ],
        expected_code=0,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    verify_luna_object(object_file, True)
    run(
        [
            *runner,
            str(stage.linker),
            "-o",
            str(executable),
            str(object_file),
        ],
        expected_code=0,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    verify_static_executable(read_elf, executable)
    run(
        [*runner, str(executable), "x", "yz"],
        expected_code=42,
        timeout=120,
    )

    library_source = work_dir / "library.luna"
    library_source.write_text(
        "module cli.library;\n"
        "fn answer() -> i32 { return 42; }\n",
        encoding="ascii",
    )
    library_assembly = work_dir / "library.s"
    library_object = work_dir / "library.lo"
    run(
        [
            *runner,
            str(stage.compiler),
            "--library",
            "-o",
            str(library_assembly),
            str(library_source),
        ],
        expected_code=0,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    run(
        [
            *runner,
            str(stage.assembler),
            "-o",
            str(library_object),
            str(library_assembly),
        ],
        expected_code=0,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    verify_luna_object(library_object, False)

    preserved = b"preserved-output\n"
    rejected_source = work_dir / "rejected.luna"
    rejected_source.write_bytes(
        b"module cli.rejected;\n"
        b"fn main(argc: isize, argv: **const u8) -> i32 {\n"
        b"    return argc as i32;\n"
        b"}\n"
    )
    rejected_assembly = work_dir / "rejected.s"
    rejected_assembly.write_bytes(preserved)
    run(
        [
            *runner,
            str(stage.compiler),
            "-o",
            str(rejected_assembly),
            str(rejected_source),
        ],
        expected_code=87,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    if rejected_assembly.read_bytes() != preserved:
        raise AssertionError("lunac clobbered output after a semantic error")

    malformed_assembly = work_dir / "malformed.s"
    malformed_assembly.write_bytes(b"not-an-instruction\n")
    rejected_object = work_dir / "rejected.lo"
    rejected_object.write_bytes(preserved)
    run(
        [
            *runner,
            str(stage.assembler),
            "-o",
            str(rejected_object),
            str(malformed_assembly),
        ],
        expected_code=2,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    if rejected_object.read_bytes() != preserved:
        raise AssertionError("luna-as clobbered output after an assembly error")

    malformed_object = work_dir / "malformed.lo"
    malformed_object.write_bytes(b"not-a-luna-object\n")
    rejected_executable = work_dir / "rejected-executable"
    rejected_executable.write_bytes(preserved)
    run(
        [
            *runner,
            str(stage.linker),
            "-o",
            str(rejected_executable),
            str(malformed_object),
        ],
        expected_code=2,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    if rejected_executable.read_bytes() != preserved:
        raise AssertionError("luna-link clobbered output after an object error")

    same_path = work_dir / "same-path.s"
    same_path.write_bytes(b"    .text\n")
    run(
        [
            *runner,
            str(stage.assembler),
            "-o",
            str(same_path),
            str(same_path),
        ],
        expected_code=125,
        timeout=STAGE_NATIVE_TIMEOUT_SECONDS,
    )
    if same_path.read_bytes() != b"    .text\n":
        raise AssertionError("luna-as accepted an input/output alias")

    temporary_files = list(work_dir.glob("*.luna-tmp-*"))
    if temporary_files:
        raise AssertionError(
            f"command-line tools leaked temporary outputs: {temporary_files}"
        )


def build_and_run_toolchain_unit_driver(
    stage: StageArtifacts,
    runner: list[str],
    read_elf: str,
    graph: dict[str, Module],
    source: pathlib.Path,
    work_dir: pathlib.Path,
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    assembly = work_dir / "bootstrap-toolchain-unit.s"
    object_file = work_dir / "bootstrap-toolchain-unit.lo"
    executable = work_dir / "bootstrap-toolchain-unit"
    invoke_stage_compiler(
        stage.compiler,
        runner,
        work_dir / "compile",
        True,
        driver_sources(
            source,
            graph,
            TOOLCHAIN_UNIT_DRIVER_METADATA_KEYS,
        ),
        assembly,
    )
    assemble_with_stage(
        stage.assembler,
        runner,
        assembly,
        object_file,
    )
    verify_luna_object(object_file, True)
    link_with_stage(
        stage.linker,
        runner,
        executable,
        [
            object_file,
            *(stage.objects[key] for key in STAGE_LIBRARY_KEYS),
        ],
    )
    verify_static_executable(read_elf, executable)
    run(
        [*runner, str(executable)],
        expected_code=42,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
    )
    return assembly, object_file, executable


def invoke_toolchain_assembler_case(
    assembler: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
    source: bytes,
    expected_code: int,
) -> pathlib.Path:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    (work_dir / "bootstrap-assembly-input.s").write_bytes(source)
    result = run(
        [*runner, str(assembler)],
        expected_code=expected_code,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=work_dir,
    )
    output = work_dir / "bootstrap-object-output.lo"
    if expected_code == 42:
        if not output.is_file() or not output.read_bytes():
            raise AssertionError("pure Luna assembler produced no object")
    elif output.exists():
        raise AssertionError(
            "pure Luna assembler retained output after rejected input"
        )
    if expected_code == 2 and not result.stderr.startswith("assembler:"):
        raise AssertionError(
            "pure Luna assembler lost its stable diagnostic: "
            f"{result.stderr!r}"
        )
    return output


def invoke_toolchain_linker_case(
    linker: pathlib.Path,
    runner: list[str],
    work_dir: pathlib.Path,
    objects: list[bytes],
    expected_code: int,
) -> pathlib.Path:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    for index, contents in enumerate(objects):
        (work_dir / f"bootstrap-link-input-{index}.lo").write_bytes(
            contents
        )
    run(
        [*runner, str(linker)],
        expected_code=expected_code,
        timeout=(
            STAGE_EMULATED_TIMEOUT_SECONDS
            if runner
            else STAGE_NATIVE_TIMEOUT_SECONDS
        ),
        cwd=work_dir,
    )
    output = work_dir / "bootstrap-link-output"
    if expected_code == 42:
        if not output.is_file() or not output.read_bytes():
            raise AssertionError("pure Luna linker produced no executable")
    elif output.exists():
        raise AssertionError(
            "pure Luna linker retained output after rejected input"
        )
    return output


def mutate_object_header(
    original: bytes,
    mutation: str,
    value: int,
) -> bytes:
    data = bytearray(original)
    if mutation == "magic":
        data[value % 8] ^= 0x80
    elif mutation == "version":
        data[8:12] = (value | 2).to_bytes(4, "little")
    elif mutation == "alignment":
        field = 7 + value % 4
        data[16 + field * 8 : 24 + field * 8] = (3).to_bytes(
            8, "little"
        )
    elif mutation == "reserved":
        data[104:112] = (value | 1).to_bytes(8, "little")
    elif mutation == "append":
        data.append(value & 0xFF)
    elif mutation == "truncate":
        del data[-(1 + value % min(32, len(data) - 1)) :]
    else:
        raise AssertionError(f"unknown object mutation {mutation}")
    return bytes(data)


def run_toolchain_negative_and_random_tests(
    stage: StageArtifacts,
    runner: list[str],
    work_dir: pathlib.Path,
) -> None:
    invalid_assembly_cases = (
        (
            "unknown-instruction",
            b"    .text\n    impossible %rax\n",
            2,
        ),
        (
            "duplicate-symbol",
            b"    .text\nsame:\nsame:\n    ret\n",
            2,
        ),
        (
            "unresolved-numeric-label",
            b"    .text\n    jmp 1f\n",
            2,
        ),
        (
            "positive-i32-overflow",
            b"    .text\n    movq 0xffffffff(%rax), %rbx\n",
            2,
        ),
        (
            "negative-alignment",
            b"    .text\n    .p2align -1\n",
            2,
        ),
        ("invalid-utf8", b"    .text\n\xff\n", 1),
    )
    for name, source, expected_code in invalid_assembly_cases:
        invoke_toolchain_assembler_case(
            stage.assembler,
            runner,
            work_dir / "assembler-negative" / name,
            source,
            expected_code,
        )

    generator = random.Random(0x4C554E41)
    for index in range(24):
        token = "".join(
            chr(ord("a") + generator.randrange(26))
            for _ in range(1 + generator.randrange(31))
        )
        invoke_toolchain_assembler_case(
            stage.assembler,
            runner,
            work_dir / "assembler-random-rejection" / str(index),
            f"    .text\n    invalid_{token} %rax\n".encode("ascii"),
            2,
        )
    for index in range(16):
        immediate = generator.randrange(-(1 << 31), 1 << 31)
        object_file = invoke_toolchain_assembler_case(
            stage.assembler,
            runner,
            work_dir / "assembler-random-acceptance" / str(index),
            (
                "    .text\n"
                "    .globl _start\n"
                "    .type _start, @function\n"
                "_start:\n"
                f"    movl ${immediate}, %edi\n"
                "    movl $60, %eax\n"
                "    syscall\n"
                "    .size _start, .-_start\n"
            ).encode("ascii"),
            42,
        )
        verify_luna_object(object_file, True)

    original = stage.objects["stage_compiler"].read_bytes()
    mutations = (
        "magic",
        "version",
        "alignment",
        "reserved",
        "append",
        "truncate",
    )
    for index in range(48):
        mutation = mutations[generator.randrange(len(mutations))]
        value = generator.randrange(1, 1 << 16)
        malformed = mutate_object_header(original, mutation, value)
        invoke_toolchain_linker_case(
            stage.linker,
            runner,
            work_dir / "object-mutation" / str(index),
            [malformed],
            2,
        )

    invoke_toolchain_linker_case(
        stage.linker,
        runner,
        work_dir / "linker-no-input",
        [],
        1,
    )
    unresolved_object = invoke_toolchain_assembler_case(
        stage.assembler,
        runner,
        work_dir / "linker-unresolved-assembly",
        (
            b"    .text\n    .globl _start\n"
            b"_start:\n    call missing_symbol\n"
        ),
        42,
    ).read_bytes()
    invoke_toolchain_linker_case(
        stage.linker,
        runner,
        work_dir / "linker-unresolved",
        [unresolved_object],
        3,
    )
    provider_object = invoke_toolchain_assembler_case(
        stage.assembler,
        runner,
        work_dir / "linker-provider-assembly",
        (
            b"    .text\n    .globl missing_symbol\n"
            b"    .type missing_symbol, @function\n"
            b"missing_symbol:\n    ret\n"
            b"    .size missing_symbol, .-missing_symbol\n"
        ),
        42,
    ).read_bytes()
    overflow = bytearray(unresolved_object)
    relocation_count = read_u64_le(overflow, 64)
    if relocation_count != 1:
        raise AssertionError("overflow probe has unexpected relocations")
    relocation_offset = (
        112
        + read_u64_le(overflow, 16)
        + read_u64_le(overflow, 24)
        + read_u64_le(overflow, 32)
        + read_u64_le(overflow, 48)
        + read_u64_le(overflow, 56) * 56
    )
    overflow[relocation_offset + 32 : relocation_offset + 40] = (
        (1 << 63) - 1
    ).to_bytes(8, "little")
    invoke_toolchain_linker_case(
        stage.linker,
        runner,
        work_dir / "linker-relocation-overflow",
        [bytes(overflow), provider_object],
        3,
    )
    non_text_entry = invoke_toolchain_assembler_case(
        stage.assembler,
        runner,
        work_dir / "linker-non-text-entry-assembly",
        (
            b"    .section .rodata\n    .globl _start\n"
            b"    .type _start, @object\n"
            b"_start:\n    .byte 0\n"
            b"    .size _start, .-_start\n"
        ),
        42,
    ).read_bytes()
    invoke_toolchain_linker_case(
        stage.linker,
        runner,
        work_dir / "linker-non-text-entry",
        [non_text_entry],
        3,
    )
    invoke_toolchain_linker_case(
        stage.linker,
        runner,
        work_dir / "linker-input-limit",
        [original] * 65,
        1,
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
    object_file = work_dir / "fixed-point-probe.lo"
    executable = work_dir / "fixed-point-probe"
    assemble_with_stage(assembler, runner, output, object_file)
    verify_luna_object(object_file, True)
    link_with_stage(linker, runner, executable, [object_file])
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
    object_file = case_root / f"{case.name}.lo"
    executable = case_root / case.name
    assemble_with_stage(assembler, runner, assembly, object_file)
    verify_luna_object(object_file, True)
    link_with_stage(linker, runner, executable, [object_file])
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
    host_linker: pathlib.Path,
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
            host_linker,
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
                host_linker,
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
    object_file = work_dir / "floating-probe.lo"
    executable = work_dir / "floating-probe"
    assemble_with_stage(assembler, runner, assembly, object_file)
    verify_luna_object(object_file, True)
    link_with_stage(linker, runner, executable, [object_file])
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
    parser.add_argument("--sysroot", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    compiler = arguments.compiler.resolve()
    host_linker = arguments.linker.resolve()
    sysroot = arguments.sysroot.resolve()
    source_root = arguments.source_root.resolve()
    work_dir = arguments.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    driver_sources_by_name = {
        driver_name: (
            source_root
            / "tools"
            / "bootstrap"
            / f"{driver_name}.luna"
        )
        for driver_name in STAGE_DRIVERS
    }

    read_elf = require_tool("llvm-readelf")
    graph = module_graph(source_root, sysroot)
    ensure_sysroot(compiler, graph)
    runner = target_runner()

    stage_one_root = work_dir / "stage-one"
    if stage_one_root.exists():
        shutil.rmtree(stage_one_root)
    stage_one_root.mkdir(parents=True)
    stage_one_binary_root = stage_one_root / "bin"
    stage_one_binary_root.mkdir(parents=True)
    stage_one_executables: dict[str, pathlib.Path] = {}
    executable_names = {
        "stage_compiler": "lunac",
        "stage_assembler": "luna-as",
        "stage_linker": "luna-link",
    }
    for driver_name, metadata_keys in STAGE_DRIVERS.items():
        driver_object = stage_one_root / f"{driver_name}.o"
        executable = stage_one_binary_root / executable_names[driver_name]
        compile_stage_one_driver(
            compiler,
            driver_sources_by_name[driver_name],
            driver_object,
            graph,
            metadata_keys,
        )
        link(
            host_linker,
            executable,
            [
                driver_object,
                *(
                    required_object(graph, key)
                    for key in STAGE_LIBRARY_KEYS
                ),
            ],
        )
        verify_static_executable(read_elf, executable)
        stage_one_executables[driver_name] = executable
    stage_one = StageArtifacts(
        root=stage_one_root,
        compiler=stage_one_executables["stage_compiler"],
        assembler=stage_one_executables["stage_assembler"],
        linker=stage_one_executables["stage_linker"],
        assemblies={},
        objects={},
    )
    run_negative_driver_tests(
        stage_one.compiler,
        runner,
        work_dir / "negative",
    )

    stage_two = build_stage(
        "stage-two",
        stage_one.compiler,
        stage_one.assembler,
        stage_one.linker,
        runner,
        read_elf,
        graph,
        driver_sources_by_name,
        work_dir,
    )
    stage_three = build_stage(
        "stage-three",
        stage_two.compiler,
        stage_two.assembler,
        stage_two.linker,
        runner,
        read_elf,
        graph,
        driver_sources_by_name,
        work_dir,
    )
    compare_stages(stage_two, stage_three)
    run_command_line_tool_tests(
        stage_three,
        runner,
        read_elf,
        work_dir / "command-line-tools",
    )
    run_bootstrap_seed_distribution_tests(
        stage_three,
        source_root,
        runner,
        work_dir / "dist",
    )

    toolchain_unit_source = (
        source_root
        / "tests"
        / "integration"
        / "cases"
        / "bootstrap_toolchain_unit_driver.luna"
    )
    stage_two_toolchain_unit = build_and_run_toolchain_unit_driver(
        stage_two,
        runner,
        read_elf,
        graph,
        toolchain_unit_source,
        work_dir / "toolchain-unit" / "stage-two",
    )
    stage_three_toolchain_unit = build_and_run_toolchain_unit_driver(
        stage_three,
        runner,
        read_elf,
        graph,
        toolchain_unit_source,
        work_dir / "toolchain-unit" / "stage-three",
    )
    for index, description in enumerate(
        (
            "toolchain unit assembly",
            "toolchain unit Luna object",
            "toolchain unit executable",
        )
    ):
        compare_files(
            stage_two_toolchain_unit[index],
            stage_three_toolchain_unit[index],
            description,
        )
    run_toolchain_negative_and_random_tests(
        stage_three,
        runner,
        work_dir / "toolchain-negative-and-random",
    )

    stage_two_probe = run_fixed_point_probe(
        stage_two.compiler,
        runner,
        stage_two.assembler,
        stage_two.linker,
        read_elf,
        work_dir / "stage-two-probe",
    )
    stage_three_probe = run_fixed_point_probe(
        stage_three.compiler,
        runner,
        stage_three.assembler,
        stage_three.linker,
        read_elf,
        work_dir / "stage-three-probe",
    )
    if stage_two_probe != stage_three_probe:
        raise AssertionError(
            "stage-two and stage-three compilers disagree on probe assembly"
        )

    run_luna_one_language_probe(
        compiler,
        stage_one.compiler,
        stage_two.compiler,
        stage_three.compiler,
        stage_three.assembler,
        stage_three.linker,
        read_elf,
        runner,
        source_root,
        work_dir / "luna-one-language",
    )

    run_semantic_convergence(
        compiler,
        stage_two.compiler,
        stage_three.compiler,
        host_linker,
        stage_three.assembler,
        stage_three.linker,
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
        host_linker,
        runner,
        read_elf,
        floating_source,
        floating_root / "stage-zero",
    )
    stage_two_floating = compile_and_run_self_hosted_probe(
        stage_two.compiler,
        runner,
        stage_two.assembler,
        stage_two.linker,
        read_elf,
        floating_source,
        floating_root / "stage-two",
    )
    stage_three_floating = compile_and_run_self_hosted_probe(
        stage_three.compiler,
        runner,
        stage_three.assembler,
        stage_three.linker,
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
