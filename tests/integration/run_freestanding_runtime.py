#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import platform
import shutil
import subprocess
import sys


EXPECTED_OUTPUT = "luna runtime\n"
EXPECTED_FILE_CONTENT = b"runtime file\n"


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
        print(f"SKIP: required freestanding runtime test tool is missing: {name}")
        raise SystemExit(77)
    return tool


def compile_syscall_metadata(
    compiler: pathlib.Path,
    output: pathlib.Path,
    source_root: pathlib.Path,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(compiler),
            "--compile-module",
            "luna.linux.syscall",
            "--emit",
            "metadata",
            "-o",
            str(output),
            str(
                source_root
                / "runtime"
                / "luna"
                / "linux"
                / "syscall.interface.luna"
            ),
            str(source_root / "runtime" / "luna" / "linux" / "syscall.luna"),
        ]
    )


def compile_runtime_metadata(
    compiler: pathlib.Path,
    output: pathlib.Path,
    source_root: pathlib.Path,
    syscall_metadata: pathlib.Path,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(compiler),
            "--compile-module",
            "luna.runtime",
            "--emit",
            "metadata",
            "-o",
            str(output),
            str(source_root / "runtime" / "luna" / "runtime.interface.luna"),
            str(source_root / "runtime" / "luna" / "runtime.luna"),
            str(syscall_metadata),
        ]
    )


def compile_runtime_object(
    compiler: pathlib.Path,
    output: pathlib.Path,
    source_root: pathlib.Path,
    runtime_metadata: pathlib.Path,
    syscall_metadata: pathlib.Path,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(compiler),
            "--compile-module",
            "luna.runtime",
            "--emit",
            "obj",
            "-o",
            str(output),
            str(runtime_metadata),
            str(source_root / "runtime" / "luna" / "runtime.luna"),
            str(syscall_metadata),
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--syscall-metadata", type=pathlib.Path, required=True)
    parser.add_argument("--runtime-metadata", type=pathlib.Path, required=True)
    parser.add_argument("--runtime-object", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    if not arguments.syscall_metadata.is_file():
        compile_syscall_metadata(
            arguments.compiler,
            arguments.syscall_metadata,
            arguments.source_root,
        )
    if not arguments.runtime_metadata.is_file():
        compile_runtime_metadata(
            arguments.compiler,
            arguments.runtime_metadata,
            arguments.source_root,
            arguments.syscall_metadata,
        )
    if not arguments.runtime_object.is_file():
        compile_runtime_object(
            arguments.compiler,
            arguments.runtime_object,
            arguments.source_root,
            arguments.runtime_metadata,
            arguments.syscall_metadata,
        )

    generated_metadata = arguments.work_dir / "runtime-generated.lmi"
    repeated_metadata = arguments.work_dir / "runtime-repeated.lmi"
    for output in (generated_metadata, repeated_metadata):
        compile_runtime_metadata(
            arguments.compiler,
            output,
            arguments.source_root,
            arguments.syscall_metadata,
        )
    expected_metadata = arguments.runtime_metadata.read_bytes()
    if (
        generated_metadata.read_bytes() != expected_metadata
        or repeated_metadata.read_bytes() != expected_metadata
    ):
        raise AssertionError("runtime metadata is stale or nondeterministic")

    generated_object = arguments.work_dir / "runtime-generated.o"
    repeated_object = arguments.work_dir / "runtime-repeated.o"
    for output in (generated_object, repeated_object):
        compile_runtime_object(
            arguments.compiler,
            output,
            arguments.source_root,
            generated_metadata,
            arguments.syscall_metadata,
        )
    expected_object = arguments.runtime_object.read_bytes()
    if (
        generated_object.read_bytes() != expected_object
        or repeated_object.read_bytes() != expected_object
    ):
        raise AssertionError("runtime object is stale or nondeterministic")

    runtime_symbols = run(
        [read_elf, "--symbols", str(arguments.runtime_object)]
    ).stdout
    if "_start" in runtime_symbols:
        raise AssertionError("separately compiled runtime object defines _start")
    for argument_count in range(7):
        symbol = f"luna_linux_syscall{argument_count}"
        matching_lines = [
            line for line in runtime_symbols.splitlines() if symbol in line
        ]
        if len(matching_lines) != 1 or "UND" not in matching_lines[0]:
            raise AssertionError(
                f"runtime does not contain one raw {symbol} dependency:\n"
                f"{runtime_symbols}"
            )

    cases = arguments.source_root / "tests" / "integration" / "cases"
    application_object = arguments.work_dir / "freestanding-runtime.o"
    exit_object = arguments.work_dir / "freestanding-runtime-exit.o"
    for source, output in (
        (cases / "freestanding_runtime.luna", application_object),
        (cases / "freestanding_runtime_exit.luna", exit_object),
    ):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "obj",
                "-o",
                str(output),
                str(source),
                str(arguments.runtime_metadata),
            ]
        )

    application_symbols = run(
        [read_elf, "--symbols", str(application_object)]
    ).stdout
    if "luna_linux_syscall" in application_symbols:
        raise AssertionError(
            "runtime application bypasses the typed runtime module"
        )
    if " UND _L6c756e612e72756e74696d65_H" not in application_symbols:
        raise AssertionError("runtime application has no typed runtime import")

    type_error = run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(cases / "freestanding_runtime_type_error.luna"),
            str(arguments.runtime_metadata),
        ],
        expected_code=1,
    )
    if (
        "aggregate initialization requires braces or an lvalue of the exact "
        "same aggregate type"
        not in type_error.stderr
    ):
        raise AssertionError(
            "runtime resource type mismatch was not diagnosed precisely:\n"
            f"{type_error.stderr}"
        )

    application = arguments.work_dir / "freestanding-runtime"
    exit_application = arguments.work_dir / "freestanding-runtime-exit"
    missing_runtime = run(
        [
            str(arguments.linker),
            "-o",
            str(arguments.work_dir / "missing-runtime"),
            str(application_object),
        ],
        expected_code=1,
    )
    if "undefined symbol" not in missing_runtime.stderr:
        raise AssertionError(
            "link without the runtime object lacked an undefined-symbol error"
        )

    run(
        [
            str(arguments.linker),
            "-o",
            str(application),
            str(application_object),
            str(arguments.runtime_object),
        ]
    )
    run(
        [
            str(arguments.linker),
            "-o",
            str(exit_application),
            str(exit_object),
            str(arguments.runtime_object),
        ]
    )

    for executable in (application, exit_application):
        program_headers = run(
            [read_elf, "--program-headers", str(executable)]
        ).stdout
        if "INTERP" in program_headers or "DYNAMIC" in program_headers:
            raise AssertionError("freestanding runtime gained a dynamic loader")
        symbols = run([read_elf, "--symbols", str(executable)]).stdout
        if " UND " in symbols:
            raise AssertionError(
                f"freestanding runtime executable has unresolved symbols:\n"
                f"{symbols}"
            )

    created_file = arguments.work_dir / "runtime-created.bin"
    created_file.unlink(missing_ok=True)
    result = run(
        [*target_runner(), str(application)],
        expected_code=42,
        cwd=arguments.work_dir,
    )
    if result.stdout != EXPECTED_OUTPUT or result.stderr:
        raise AssertionError(
            "freestanding runtime output mismatch\n"
            f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        )
    if created_file.read_bytes() != EXPECTED_FILE_CONTENT:
        raise AssertionError("freestanding runtime file content mismatch")

    exit_result = run(
        [*target_runner(), str(exit_application)],
        expected_code=37,
        cwd=arguments.work_dir,
    )
    if exit_result.stdout or exit_result.stderr:
        raise AssertionError("runtime_process_exit unexpectedly produced output")

    print("PASS freestanding Luna runtime without libc")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
