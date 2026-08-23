#!/usr/bin/env python3
"""Luna self-hosting driver.

Builds the complete Luna toolchain from pure Luna sources using a previously
built toolchain (the committed `anchor/`), then verifies the result by
rebuilding itself and comparing every artifact byte-for-byte.

    python3 tools/selfhost.py build    # anchor -> out/stage-next
    python3 tools/selfhost.py verify   # stage-next -> stage-fixed, byte compare
    python3 tools/selfhost.py test     # run tests/cases through stage-next
"""

from __future__ import annotations

import argparse
import filecmp
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Optional emulator prefix (e.g. "qemu-x86_64-static") applied to every
# toolchain binary invocation, for cross-architecture development hosts.
TOOL_RUNNER = tuple(os.environ.get("LUNA_TOOL_RUNNER", "").split())
IMPORT_PATTERN = re.compile(
    r"^[ \t]*import[ \t]+([A-Za-z0-9_.]+)[ \t]*;[ \t]*$",
    re.MULTILINE,
)

# key -> (module name, source stem, direct dependencies)
LIBRARIES = {
    "syscall": ("luna.linux.syscall", "library/linux/syscall", ()),
    "runtime": ("luna.runtime", "library/runtime", ("syscall",)),
    "memory": ("luna.std.memory", "library/std/memory", ("runtime",)),
    "bytes": ("luna.std.bytes", "library/std/bytes", ("runtime", "memory")),
    "text": ("luna.std.text", "library/std/text", ("runtime", "bytes")),
    "path": ("luna.std.path", "library/std/path", ("runtime", "bytes", "text")),
    "io": (
        "luna.std.io",
        "library/std/io",
        ("runtime", "bytes", "text", "path"),
    ),
    "lexer": (
        "luna.bootstrap.frontend.lexer",
        "compiler/frontend/lexer",
        ("runtime", "bytes", "text"),
    ),
    "parser": (
        "luna.bootstrap.frontend.parser",
        "compiler/frontend/parser",
        ("runtime", "bytes", "text", "lexer"),
    ),
    "type": (
        "luna.bootstrap.middleend.type",
        "compiler/middleend/type",
        ("runtime", "bytes", "text", "lexer"),
    ),
    "ir": (
        "luna.bootstrap.middleend.ir",
        "compiler/middleend/ir",
        ("runtime", "bytes", "text", "lexer", "type"),
    ),
        "sem_ctx": (
        "luna.bootstrap.middleend.semantic.context",
        "compiler/middleend/semantic/context",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir"),
    ),
    "sem_attributes": (
        "luna.bootstrap.middleend.semantic.attributes",
        "compiler/middleend/semantic/attributes",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx"),
    ),
    "sem_modules": (
        "luna.bootstrap.middleend.semantic.modules",
        "compiler/middleend/semantic/modules",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx"),
    ),
    "sem_types": (
        "luna.bootstrap.middleend.semantic.types",
        "compiler/middleend/semantic/types",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx", "sem_attributes"),
    ),
    "sem_funcs": (
        "luna.bootstrap.middleend.semantic.functions",
        "compiler/middleend/semantic/functions",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx", "sem_attributes", "sem_types"),
    ),
    "sem_expr": (
        "luna.bootstrap.middleend.semantic.expr",
        "compiler/middleend/semantic/expr",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx", "sem_types"),
    ),
    "sem_stmt": (
        "luna.bootstrap.middleend.semantic.stmt",
        "compiler/middleend/semantic/stmt",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx", "sem_attributes", "sem_types", "sem_funcs",
         "sem_expr"),
    ),
    "sema": (
        "luna.bootstrap.middleend.sema",
        "compiler/middleend/sema",
        ("runtime", "bytes", "text", "lexer", "parser", "type", "ir",
         "sem_ctx", "sem_attributes", "sem_modules", "sem_types",
         "sem_funcs", "sem_expr", "sem_stmt"),
    ),
    "x86_64_text": (
        "luna.bootstrap.backend.x86_64.text",
        "compiler/backend/x86_64/text",
        ("runtime", "bytes", "text"),
    ),
    "x86_64_abi": (
        "luna.bootstrap.backend.x86_64.abi",
        "compiler/backend/x86_64/abi",
        ("runtime", "bytes", "text", "lexer", "type", "ir"),
    ),
    "x86_64_frame": (
        "luna.bootstrap.backend.x86_64.frame",
        "compiler/backend/x86_64/frame",
        ("runtime", "bytes", "text", "lexer", "type", "ir", "x86_64_abi"),
    ),
    "x86_64_codegen": (
        "luna.bootstrap.backend.x86_64.codegen",
        "compiler/backend/x86_64/codegen",
        (
            "runtime",
            "bytes",
            "text",
            "lexer",
            "parser",
            "type",
            "ir",
            "sem_ctx",
            "x86_64_text",
            "x86_64_abi",
            "x86_64_frame",
        ),
    ),
    "x86_64_object": (
        "luna.bootstrap.backend.x86_64.object",
        "compiler/backend/x86_64/object",
        ("runtime", "bytes", "text"),
    ),
    "x86_64_assembler": (
        "luna.bootstrap.backend.x86_64.assembler",
        "compiler/backend/x86_64/assembler",
        ("runtime", "bytes", "text", "x86_64_object"),
    ),
    "x86_64_linker": (
        "luna.bootstrap.backend.x86_64.linker",
        "compiler/backend/x86_64/linker",
        ("runtime", "bytes", "text", "x86_64_object", "x86_64_assembler"),
    ),
}

LIBRARY_ORDER = (
    "syscall",
    "runtime",
    "memory",
    "bytes",
    "text",
    "path",
    "io",
    "lexer",
    "parser",
    "type",
    "ir",
    "sem_ctx",
    "sem_attributes",
    "sem_modules",
    "sem_types",
    "sem_funcs",
    "sem_expr",
    "sem_stmt",
    "sema",
    "x86_64_text",
    "x86_64_abi",
    "x86_64_frame",
    "x86_64_codegen",
    "x86_64_object",
    "x86_64_assembler",
    "x86_64_linker",
)

# tool name -> (driver source, interface keys)
DRIVERS = {
    "lunac": (
        "drivers/stage_compiler.luna",
        (
            "runtime",
            "bytes",
            "text",
            "path",
            "io",
            "lexer",
            "parser",
            "type",
            "ir",
            "sem_ctx",
            "sem_attributes",
            "sem_modules",
            "sem_types",
            "sem_funcs",
            "sem_expr",
            "sem_stmt",
            "sema",
            "x86_64_text",
            "x86_64_abi",
            "x86_64_frame",
            "x86_64_codegen",
        ),
    ),
    "luna-as": (
        "drivers/stage_assembler.luna",
        (
            "runtime",
            "bytes",
            "text",
            "path",
            "io",
            "x86_64_object",
            "x86_64_assembler",
        ),
    ),
    "luna-link": (
        "drivers/stage_linker.luna",
        (
            "runtime",
            "bytes",
            "text",
            "path",
            "io",
            "x86_64_text",
            "x86_64_object",
            "x86_64_linker",
        ),
    ),
}

TIMEOUT_SECONDS = 600 if not TOOL_RUNNER else 3600


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def tool(directory: pathlib.Path, name: str) -> list[str | pathlib.Path]:
    return [*TOOL_RUNNER, directory / name]


def run(command: list[str], *, cwd: pathlib.Path | None = None) -> None:
    printable = " ".join(str(part) for part in command)
    print(f"  $ {printable}")
    try:
        completed = subprocess.run(
            [str(part) for part in command],
            cwd=None if cwd is None else str(cwd),
            timeout=TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        fail(f"command timed out after {TIMEOUT_SECONDS}s: {printable}")
    if completed.returncode != 0:
        fail(f"command returned {completed.returncode}: {printable}")


def import_closure(key: str) -> list[str]:
    """Transitively resolve interface imports starting from direct deps."""
    names_to_keys = {module[0]: name for name, module in LIBRARIES.items()}
    required: set[str] = set(LIBRARIES[key][2])
    pending = sorted(required)
    while pending:
        current = pending.pop()
        stem = ROOT / LIBRARIES[current][1]
        text = stem.with_suffix(".interface.luna").read_text(encoding="utf-8")
        for name in IMPORT_PATTERN.findall(text):
            dependency = names_to_keys.get(name)
            if dependency is None:
                fail(f"{stem}.interface.luna imports unknown module {name}")
            if dependency not in required:
                required.add(dependency)
                pending.append(dependency)
    return [key for key in LIBRARY_ORDER if key in required]


def library_units(key: str) -> list[pathlib.Path]:
    module = LIBRARIES[key]
    stem = ROOT / module[1]
    units = [stem.with_suffix(".luna"), stem.with_suffix(".interface.luna")]
    units.extend(
        (ROOT / LIBRARIES[dependency][1]).with_suffix(".interface.luna")
        for dependency in import_closure(key)
    )
    return units


def reset(directory: pathlib.Path) -> None:
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)


def build_stage(
    tools: pathlib.Path,
    out: pathlib.Path,
    runner: list[str],
) -> dict[str, pathlib.Path]:
    """Compile, assemble and link one complete toolchain into `out`."""
    compiler = tool(tools, "lunac")
    assembler = tool(tools, "luna-as")
    linker = tool(tools, "luna-link")
    for prefix in (compiler, assembler, linker):
        if not prefix[-1].is_file():
            fail(f"missing tool {prefix[-1]}")

    assembly_root = out / "assembly"
    object_root = out / "objects"
    binary_root = out / "bin"
    reset(assembly_root)
    reset(object_root)
    reset(binary_root)

    objects: dict[str, pathlib.Path] = {}
    for key in LIBRARY_ORDER:
        assembly = assembly_root / f"{key}.s"
        object_file = object_root / f"{key}.lo"
        run([*compiler, "--library", "-o", assembly, *library_units(key)])
        run([*assembler, "-o", object_file, assembly])
        objects[key] = object_file
        print(f"  built library {key}")

    executables: dict[str, pathlib.Path] = {}
    for name, (source, interface_keys) in DRIVERS.items():
        driver_object = object_root / f"{name}.lo"
        assembly = assembly_root / f"{name}.s"
        units = [ROOT / source]
        units.extend(
            (ROOT / LIBRARIES[key][1]).with_suffix(".interface.luna")
            for key in interface_keys
        )
        run([*compiler, "--executable", "-o", assembly, *units])
        run([*assembler, "-o", driver_object, assembly])
        executable = binary_root / name
        run([*linker, "-o", executable, driver_object, *(objects[k] for k in LIBRARY_ORDER)])
        executables[name] = executable
        print(f"  linked {name}")

    return {"assemblies": assembly_root, "objects": object_root, "binaries": binary_root}


ARTIFACT_DIRS = ("assembly", "objects", "bin")


def compare_stages(left: pathlib.Path, right: pathlib.Path) -> None:
    for directory in ARTIFACT_DIRS:
        left_files = sorted((left / directory).iterdir())
        right_files = sorted((right / directory).iterdir())
        left_names = [path.name for path in left_files]
        right_names = [path.name for path in right_files]
        if left_names != right_names:
            fail(f"{directory} artifact sets differ between stages")
        for left_file, right_file in zip(left_files, right_files):
            if not filecmp.cmp(left_file, right_file, shallow=False):
                fail(f"stage artifact differs: {directory}/{left_file.name}")
            print(f"  identical {directory}/{left_file.name}")


def verify_anchor(anchor: pathlib.Path) -> None:
    sums = anchor / "SHA256SUMS"
    if not sums.is_file():
        fail(f"missing {sums}")
    run(["sha256sum", "--check", "--strict", sums], cwd=anchor)


SEMANTIC_DIAGNOSTIC_BASE = 64


def semantic_diagnostic_kinds() -> dict[str, int]:
    """Map BootstrapSemanticDiagnosticKind names to their enum ordinals."""
    interface = (
        ROOT
        / "compiler"
        / "middleend"
        / "semantic"
        / "context.interface.luna"
    )
    text = interface.read_text(encoding="utf-8")
    match = re.search(
        r"enum BootstrapSemanticDiagnosticKind[^}]*\{(.*?)\}", text, re.S
    )
    if match is None:
        fail("cannot locate BootstrapSemanticDiagnosticKind")
    return {
        name: ordinal
        for ordinal, name in enumerate(
            re.findall(r"[a-z_][a-z0-9_]*", match.group(1))
        )
    }


def execute_tests(stage_bin: pathlib.Path, runner: list[str]) -> int:
    expectations = ROOT / "tests" / "expectations.txt"
    cases = ROOT / "tests" / "cases"
    kinds = semantic_diagnostic_kinds()
    passed = 0
    failed: list[str] = []
    for line in expectations.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        name = parts[0]
        work = ROOT / "out" / "tests" / name.removesuffix(".luna")
        reset(work)
        assembly = work / f"{name}.s"
        try:
            if len(parts) == 3 and parts[1] == "FAIL":
                kind = parts[2]
                if kind not in kinds:
                    raise AssertionError(f"unknown diagnostic kind {kind}")
                completed = subprocess.run(
                    [
                        *tool(stage_bin, "lunac"),
                        "--executable",
                        "-o",
                        assembly,
                        cases / name,
                    ],
                    timeout=TIMEOUT_SECONDS,
                    capture_output=True,
                    text=True,
                )
                expected_status = SEMANTIC_DIAGNOSTIC_BASE + kinds[kind]
                if completed.returncode != expected_status:
                    raise AssertionError(
                        f"exit {completed.returncode}, expected {expected_status} ({kind})"
                    )
                if not completed.stderr.startswith(f"semantic:{kinds[kind]}:"):
                    raise AssertionError(
                        f"stderr {completed.stderr.strip()!r}, expected leading semantic:{kinds[kind]}:"
                    )
                passed += 1
                print(f"PASS {name} (FAIL {kind})")
                continue
            expected = int(parts[1])
            object_file = work / f"{name}.lo"
            executable = work / name.removesuffix(".luna")
            run(
                [
                    *tool(stage_bin, "lunac"),
                    "--executable",
                    "-o",
                    assembly,
                    cases / name,
                ]
            )
            run([*tool(stage_bin, "luna-as"), "-o", object_file, assembly])
            run([*tool(stage_bin, "luna-link"), "-o", executable, object_file])
            completed = subprocess.run(
                [*runner, str(executable)],
                timeout=TIMEOUT_SECONDS,
            )
            if completed.returncode != expected:
                raise AssertionError(
                    f"exit {completed.returncode}, expected {expected}"
                )
            passed += 1
            print(f"PASS {name} ({expected})")
        except Exception as error:  # noqa: BLE001 - report and continue
            failed.append(name)
            print(f"FAIL {name}: {error}")
    print(f"{passed} passed, {len(failed)} failed")
    return 1 if failed else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_common(sub: argparse.ArgumentParser) -> None:
        sub.add_argument("--anchor", type=pathlib.Path, default=ROOT / "anchor")
        sub.add_argument("--runner", nargs="*", default=[])

    build_parser = subparsers.add_parser("build", help="anchor -> out/stage-next")
    add_common(build_parser)
    build_parser.add_argument("--out", type=pathlib.Path)

    verify_parser = subparsers.add_parser("verify", help="fixed-point gate")
    add_common(verify_parser)
    verify_parser.add_argument("--out", type=pathlib.Path)

    test_parser = subparsers.add_parser("test", help="run behavior tests")
    add_common(test_parser)
    test_parser.add_argument("--stage", type=pathlib.Path)

    arguments = parser.parse_args()
    default_out = ROOT / "out"

    if arguments.command == "build":
        out = arguments.out or default_out / "stage-next"
        verify_anchor(arguments.anchor)
        build_stage(arguments.anchor, out, arguments.runner)
    elif arguments.command == "verify":
        next_out = default_out / "stage-next"
        fixed_out = default_out / "stage-fixed"
        verify_anchor(arguments.anchor)
        artifacts = build_stage(arguments.anchor, next_out, arguments.runner)
        print("building the fixed-point stage from its own output")
        build_stage(next_out / "bin", fixed_out, arguments.runner)
        compare_stages(next_out, fixed_out)
        print("FIXED POINT: all artifacts byte-identical")
    else:
        stage = arguments.stage or default_out / "stage-next" / "bin"
        if not (stage / "lunac").is_file():
            fail(f"no built toolchain under {stage}; run 'build' first")
        raise SystemExit(execute_tests(stage, arguments.runner))


if __name__ == "__main__":
    main()
