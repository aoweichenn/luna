#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import platform
import shutil
import subprocess
import sys


EXPECTED_OUTPUT = "luna syscall abi\n"


def run(
    command: list[str],
    *,
    expected_code: int = 0,
    timeout: int = 30,
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
        print(f"SKIP: required syscall ABI test tool is missing: {name}")
        raise SystemExit(77)
    return tool


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--metadata", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    source = (
        arguments.source_root
        / "tests"
        / "integration"
        / "cases"
        / "linux_syscall_abi.luna"
    )
    object_file = arguments.work_dir / "linux-syscall-abi.o"
    executable = arguments.work_dir / "linux-syscall-abi"

    interface = (
        arguments.source_root
        / "runtime"
        / "luna"
        / "linux"
        / "syscall.interface.luna"
    )
    implementation = (
        arguments.source_root
        / "runtime"
        / "luna"
        / "linux"
        / "syscall.luna"
    )
    if not arguments.metadata.is_file():
        arguments.metadata.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                str(arguments.compiler),
                "--compile-module",
                "luna.linux.syscall",
                "--emit",
                "metadata",
                "-o",
                str(arguments.metadata),
                str(interface),
                str(implementation),
            ]
        )
    generated_metadata = arguments.work_dir / "syscall-generated.lmi"
    repeated_metadata = arguments.work_dir / "syscall-repeated.lmi"
    for output in (generated_metadata, repeated_metadata):
        run(
            [
                str(arguments.compiler),
                "--compile-module",
                "luna.linux.syscall",
                "--emit",
                "metadata",
                "-o",
                str(output),
                str(interface),
                str(implementation),
            ]
        )
    expected_metadata = arguments.metadata.read_bytes()
    if (
        generated_metadata.read_bytes() != expected_metadata
        or repeated_metadata.read_bytes() != expected_metadata
    ):
        raise AssertionError("syscall module metadata is stale or nondeterministic")

    run(
        [
            str(arguments.compiler),
            "--emit",
            "obj",
            "-o",
            str(object_file),
            str(source),
            str(arguments.metadata),
        ]
    )

    symbols = run([read_elf, "--symbols", str(object_file)]).stdout
    for argument_count in range(7):
        symbol = f"luna_linux_syscall{argument_count}"
        matching_lines = [line for line in symbols.splitlines() if symbol in line]
        if len(matching_lines) != 1 or "UND" not in matching_lines[0]:
            raise AssertionError(
                f"{symbol} is not one unresolved ABI reference:\n{symbols}"
            )

    run([str(arguments.linker), "-o", str(executable), str(object_file)])
    program_headers = run([read_elf, "--program-headers", str(executable)]).stdout
    if "INTERP" in program_headers or "DYNAMIC" in program_headers:
        raise AssertionError("syscall ABI executable gained a dynamic runtime")

    result = run(
        [*target_runner(), str(executable)],
        expected_code=42,
    )
    if result.stdout != EXPECTED_OUTPUT or result.stderr:
        raise AssertionError(
            "direct syscall output mismatch\n"
            f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        )

    print("PASS direct Linux x86-64 syscall ABI arities 0 through 6")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
