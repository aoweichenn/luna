#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys


def run(command: list[str], *, timeout: int = 20) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require_tool(name: str) -> str:
    tool = shutil.which(name)
    if tool is None:
        print(f"SKIP: required debug-information tool is missing: {name}")
        raise SystemExit(77)
    return tool


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    dwarf_dump = require_tool("llvm-dwarfdump")
    read_elf = require_tool("llvm-readelf")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    source = (
        arguments.source_root / "tests" / "integration" / "cases"
        / "function_call.luna"
    ).resolve()
    first_object = arguments.work_dir / "debug-first.o"
    second_object = arguments.work_dir / "debug-second.o"
    first_executable = arguments.work_dir / "debug-first"
    second_executable = arguments.work_dir / "debug-second"

    for object_file in (first_object, second_object):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "obj",
                "-o",
                str(object_file),
                str(source),
            ]
        )
    if first_object.read_bytes() != second_object.read_bytes():
        raise AssertionError("object debug metadata is not deterministic")

    object_sections = run([read_elf, "--sections", str(first_object)]).stdout
    if ".luna.debug" not in object_sections:
        raise AssertionError("native object does not contain .luna.debug")

    for object_file, executable in (
        (first_object, first_executable),
        (second_object, second_executable),
    ):
        run(
            [
                str(arguments.linker),
                "-o",
                str(executable),
                str(object_file),
            ]
        )
    if first_executable.read_bytes() != second_executable.read_bytes():
        raise AssertionError("linked DWARF output is not deterministic")

    run([dwarf_dump, "--verify", str(first_executable)])
    sections = run([read_elf, "--sections", str(first_executable)]).stdout
    for section_name in (
        ".debug_abbrev",
        ".debug_info",
        ".debug_line",
        ".debug_str",
        ".debug_line_str",
    ):
        if section_name not in sections:
            raise AssertionError(f"linked executable is missing {section_name}")

    debug_info = run(
        [
            dwarf_dump,
            "--debug-info",
            "--debug-line",
            str(first_executable),
        ]
    ).stdout
    for expected in (
        "version = 0x0005",
        "DW_TAG_compile_unit",
        "DW_TAG_subprogram",
        'DW_AT_name\t("main")',
        source.name,
        "is_stmt",
    ):
        if expected not in debug_info:
            raise AssertionError(
                f"DWARF dump does not contain {expected!r}\n{debug_info}"
            )

    gdb = shutil.which("gdb")
    if gdb is not None:
        gdb_result = run(
            [
                gdb,
                "--batch",
                "-ex",
                f"file {first_executable}",
                "-ex",
                f"info line {source}:8",
                "-ex",
                "break main",
            ]
        )
        combined = gdb_result.stdout + gdb_result.stderr
        if "Line 8" not in combined or "Breakpoint 1" not in combined:
            raise AssertionError(f"GDB could not consume Luna DWARF:\n{combined}")

    print("PASS deterministic project Debug IR and verified DWARF 5")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
