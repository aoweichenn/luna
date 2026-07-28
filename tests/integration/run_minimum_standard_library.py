#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import platform
import shutil
import subprocess
import sys


EXPECTED_OUTPUT = "luna stdlib\n"
EXPECTED_FILE_CONTENT = b"stdlib file\n"


@dataclasses.dataclass(frozen=True)
class Module:
    name: str
    source_stem: pathlib.Path
    metadata: pathlib.Path
    object_file: pathlib.Path | None
    dependencies: tuple[str, ...]


def run(
    command: list[str],
    *,
    expected_code: int = 0,
    timeout: int = 30,
    cwd: pathlib.Path | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=cwd,
    )
    if result.returncode != expected_code:
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected_code}: "
            f"{' '.join(command)}\nstdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def target_runner() -> list[str]:
    if platform.machine().lower() in ("x86_64", "amd64"):
        return []
    qemu = shutil.which("qemu-x86_64-static")
    if qemu is None:
        print("SKIP: qemu-x86_64-static is required on this host")
        raise SystemExit(77)
    return [qemu]


def require_tool(name: str) -> str:
    tool = shutil.which(name)
    if tool is None:
        print(f"SKIP: required standard-library test tool is missing: {name}")
        raise SystemExit(77)
    return tool


def compile_metadata(
    compiler: pathlib.Path,
    module: Module,
    output: pathlib.Path,
    dependency_metadata: list[pathlib.Path],
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(compiler),
            "--compile-module",
            module.name,
            "--emit",
            "metadata",
            "-o",
            str(output),
            str(module.source_stem.with_suffix(".interface.luna")),
            str(module.source_stem.with_suffix(".luna")),
            *(str(path) for path in dependency_metadata),
        ]
    )


def compile_object(
    compiler: pathlib.Path,
    module: Module,
    metadata: pathlib.Path,
    output: pathlib.Path,
    dependency_metadata: list[pathlib.Path],
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(compiler),
            "--compile-module",
            module.name,
            "--emit",
            "obj",
            "-o",
            str(output),
            str(metadata),
            str(module.source_stem.with_suffix(".luna")),
            *(str(path) for path in dependency_metadata),
        ]
    )


def module_graph(
    source_root: pathlib.Path, sysroot: pathlib.Path
) -> dict[str, Module]:
    runtime_root = source_root / "runtime" / "luna"
    return {
        "syscall": Module(
            "luna.linux.syscall",
            runtime_root / "linux" / "syscall",
            sysroot / "luna" / "linux" / "syscall.lmi",
            None,
            (),
        ),
        "runtime": Module(
            "luna.runtime",
            runtime_root / "runtime",
            sysroot / "luna" / "runtime.lmi",
            sysroot / "luna" / "runtime.o",
            ("syscall",),
        ),
        "memory": Module(
            "luna.std.memory",
            runtime_root / "std" / "memory",
            sysroot / "luna" / "std" / "memory.lmi",
            sysroot / "luna" / "std" / "memory.o",
            ("runtime",),
        ),
        "bytes": Module(
            "luna.std.bytes",
            runtime_root / "std" / "bytes",
            sysroot / "luna" / "std" / "bytes.lmi",
            sysroot / "luna" / "std" / "bytes.o",
            ("runtime", "memory"),
        ),
        "text": Module(
            "luna.std.text",
            runtime_root / "std" / "text",
            sysroot / "luna" / "std" / "text.lmi",
            sysroot / "luna" / "std" / "text.o",
            ("runtime", "bytes"),
        ),
        "path": Module(
            "luna.std.path",
            runtime_root / "std" / "path",
            sysroot / "luna" / "std" / "path.lmi",
            sysroot / "luna" / "std" / "path.o",
            ("runtime", "bytes", "text"),
        ),
        "io": Module(
            "luna.std.io",
            runtime_root / "std" / "io",
            sysroot / "luna" / "std" / "io.lmi",
            sysroot / "luna" / "std" / "io.o",
            ("runtime", "bytes", "text", "path"),
        ),
        "bootstrap_lexer": Module(
            "luna.bootstrap.frontend.lexer",
            runtime_root / "bootstrap" / "frontend" / "lexer",
            sysroot / "luna" / "bootstrap" / "frontend" / "lexer.lmi",
            sysroot / "luna" / "bootstrap" / "frontend" / "lexer.o",
            ("runtime", "bytes", "text"),
        ),
        "bootstrap_parser": Module(
            "luna.bootstrap.frontend.parser",
            runtime_root / "bootstrap" / "frontend" / "parser",
            sysroot / "luna" / "bootstrap" / "frontend" / "parser.lmi",
            sysroot / "luna" / "bootstrap" / "frontend" / "parser.o",
            ("runtime", "bytes", "text", "bootstrap_lexer"),
        ),
    }


def dependency_paths(
    graph: dict[str, Module],
    module: Module,
    generated_metadata: dict[str, pathlib.Path] | None = None,
) -> list[pathlib.Path]:
    return [
        (
            generated_metadata[dependency]
            if generated_metadata is not None
            and dependency in generated_metadata
            else graph[dependency].metadata
        )
        for dependency in module.dependencies
    ]


def ensure_sysroot(
    compiler: pathlib.Path,
    graph: dict[str, Module],
) -> None:
    for key in (
        "syscall",
        "runtime",
        "memory",
        "bytes",
        "text",
        "path",
        "io",
        "bootstrap_lexer",
        "bootstrap_parser",
    ):
        module = graph[key]
        dependencies = dependency_paths(graph, module)
        if not module.metadata.is_file():
            compile_metadata(compiler, module, module.metadata, dependencies)
        if module.object_file is not None and not module.object_file.is_file():
            compile_object(
                compiler,
                module,
                module.metadata,
                module.object_file,
                dependencies,
            )


def reproduce_standard_library(
    compiler: pathlib.Path,
    graph: dict[str, Module],
    output_root: pathlib.Path,
) -> None:
    generated_metadata: dict[str, pathlib.Path] = {}
    for key in ("memory", "bytes", "text", "path", "io"):
        module = graph[key]
        metadata = output_root / f"{key}.lmi"
        object_file = output_root / f"{key}.o"
        dependencies = dependency_paths(graph, module, generated_metadata)
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--sysroot", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    graph = module_graph(arguments.source_root, arguments.sysroot)
    ensure_sysroot(arguments.compiler, graph)

    reproduce_standard_library(
        arguments.compiler, graph, arguments.work_dir / "reproduction-one"
    )
    reproduce_standard_library(
        arguments.compiler, graph, arguments.work_dir / "reproduction-two"
    )

    standard_objects: list[pathlib.Path] = []
    for key in ("memory", "bytes", "text", "path", "io"):
        module = graph[key]
        if module.object_file is None:
            raise AssertionError(f"{module.name} has no separately linked object")
        standard_objects.append(module.object_file)
        symbols = run(
            [read_elf, "--wide", "--symbols", str(module.object_file)]
        ).stdout
        if "_start" in symbols:
            raise AssertionError(f"{module.name} defines the process entry point")
        if "luna_linux_syscall" in symbols:
            raise AssertionError(
                f"{module.name} bypasses the typed freestanding runtime"
            )

    cases = arguments.source_root / "tests" / "integration" / "cases"
    application_object = arguments.work_dir / "minimum-standard-library.o"
    public_metadata = [
        graph[key].metadata
        for key in ("runtime", "memory", "bytes", "text", "path", "io")
    ]
    run(
        [
            str(arguments.compiler),
            "--emit",
            "obj",
            "-o",
            str(application_object),
            str(cases / "minimum_standard_library.luna"),
            *(str(path) for path in public_metadata),
        ]
    )
    application_symbols = run(
        [read_elf, "--wide", "--symbols", str(application_object)]
    ).stdout
    if "luna_linux_syscall" in application_symbols:
        raise AssertionError("standard-library application bypasses its modules")

    type_error = run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(cases / "minimum_standard_library_type_error.luna"),
            str(graph["runtime"].metadata),
            str(graph["bytes"].metadata),
        ],
        expected_code=1,
    )
    if (
        "aggregate initialization requires braces or an lvalue of the exact "
        "same aggregate type"
        not in type_error.stderr
    ):
        raise AssertionError(
            "byte-buffer type mismatch was not diagnosed precisely:\n"
            f"{type_error.stderr}"
        )

    runtime_object = graph["runtime"].object_file
    if runtime_object is None:
        raise AssertionError("runtime object is unavailable")
    all_objects = [application_object, runtime_object, *standard_objects]
    for omitted in standard_objects:
        missing = run(
            [
                str(arguments.linker),
                "-o",
                str(arguments.work_dir / f"missing-{omitted.stem}"),
                *(str(path) for path in all_objects if path != omitted),
            ],
            expected_code=1,
        )
        if "undefined symbol" not in missing.stderr:
            raise AssertionError(
                f"link without {omitted.name} lacked an undefined-symbol error"
            )

    application = arguments.work_dir / "minimum-standard-library"
    run(
        [
            str(arguments.linker),
            "-o",
            str(application),
            *(str(path) for path in all_objects),
        ]
    )
    program_headers = run(
        [read_elf, "--program-headers", str(application)]
    ).stdout
    if "INTERP" in program_headers or "DYNAMIC" in program_headers:
        raise AssertionError("standard-library executable gained a dynamic loader")
    executable_symbols = run(
        [read_elf, "--wide", "--symbols", str(application)]
    ).stdout
    if " UND " in executable_symbols:
        raise AssertionError(
            "standard-library executable has unresolved symbols:\n"
            f"{executable_symbols}"
        )

    created_file = arguments.work_dir / "stdlib-created.bin"
    invalid_file = arguments.work_dir / "stdlib-invalid.bin"
    created_file.unlink(missing_ok=True)
    invalid_file.unlink(missing_ok=True)
    result = run(
        [*target_runner(), str(application)],
        expected_code=42,
        cwd=arguments.work_dir,
    )
    if result.stdout != EXPECTED_OUTPUT or result.stderr:
        raise AssertionError(
            "standard-library output mismatch\n"
            f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        )
    if created_file.read_bytes() != EXPECTED_FILE_CONTENT:
        raise AssertionError("standard-library file content mismatch")
    if invalid_file.exists():
        raise AssertionError("invalid write mutated the filesystem")

    print("PASS minimum Luna standard library without libc or raw syscalls")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
