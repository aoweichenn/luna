#!/usr/bin/env python3
"""Luna self-hosting driver.

Builds the complete Luna toolchain from pure Luna sources using a previously
built toolchain (the committed `anchor/`), then verifies the result by
rebuilding itself and comparing every artifact byte-for-byte.

    python3 tools/selfhost.py build    # anchor -> out/stage-next
    python3 tools/selfhost.py verify   # stage-next -> stage-fixed, byte compare
    python3 tools/selfhost.py test     # run tests/cases through stage-next
    python3 tools/selfhost.py audit    # read-only anchor/module/source checks
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import filecmp
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Optional emulator prefix (e.g. "qemu-x86_64-static") applied to every
# toolchain binary invocation, for cross-architecture development hosts.
DEFAULT_RUNNER = tuple(shlex.split(os.environ.get("LUNA_TOOL_RUNNER", "")))
IMPORT_PATTERN = re.compile(
    r"^[ \t]*import[ \t]+([A-Za-z0-9_.]+)(?:[ \t]+as[ \t]+[A-Za-z0-9_]+)?"
    r"(?:[ \t]*::[ \t]*\{[^}]*\})?[ \t]*;[ \t]*$",
    re.MULTILINE,
)
MODULE_PATTERN = re.compile(
    r"^[ \t]*(export[ \t]+)?module[ \t]+([A-Za-z0-9_.]+)[ \t]*;[ \t]*$",
    re.MULTILINE,
)


@dataclass(frozen=True)
class RegisteredModule:
    name: str
    interface: str
    implementation: str


def library_module(name: str, implementation_stem: str) -> RegisteredModule:
    module_path = name.replace(".", "/")
    return RegisteredModule(
        name=name,
        interface=f"library/include/{module_path}.lh",
        implementation=f"library/src/{implementation_stem}.la",
    )


def compiler_module(name: str, implementation_stem: str) -> RegisteredModule:
    module_path = name.replace(".", "/")
    return RegisteredModule(
        name=name,
        interface=f"compiler/include/{module_path}.lh",
        implementation=f"compiler/src/{implementation_stem}.la",
    )


# key -> explicit interface/implementation source record. Dependencies and
# build order are derived, so adding a module still has one configuration site.
LIBRARIES = {
    "ascii": library_module("luna.std.ascii", "std/ascii"),
    "checked": library_module("luna.std.checked", "std/checked"),
    "syscall": library_module("luna.linux.syscall", "linux/syscall"),
    "runtime": library_module("luna.runtime", "runtime"),
    "memory": library_module("luna.std.memory", "std/memory"),
    "bytes": library_module("luna.std.bytes", "std/bytes"),
    "binary": library_module("luna.std.binary", "std/binary"),
    "text": library_module("luna.std.text", "std/text"),
    "path": library_module("luna.std.path", "std/path"),
    "io": library_module("luna.std.io", "std/io"),
    "lexer": compiler_module("luna.bootstrap.frontend.lexer", "frontend/lexer"),
    "syntax": compiler_module("luna.bootstrap.frontend.syntax", "frontend/syntax"),
    "parser_state": compiler_module("luna.bootstrap.frontend.parser.state", "frontend/parser/state"),
    "parser_expression": compiler_module("luna.bootstrap.frontend.parser.expression", "frontend/parser/expression"),
    "parser_statements": compiler_module("luna.bootstrap.frontend.parser.statements", "frontend/parser/statements"),
    "parser_declarations": compiler_module(
        "luna.bootstrap.frontend.parser.declarations",
        "frontend/parser/declarations",
    ),
    "parser": compiler_module("luna.bootstrap.frontend.parser", "frontend/parser"),
    "type": compiler_module("luna.bootstrap.middleend.type", "middleend/type"),
    "ir": compiler_module("luna.bootstrap.middleend.ir", "middleend/ir"),
    "ir_verify": compiler_module("luna.bootstrap.middleend.ir.verify", "middleend/ir/verify"),
    "sem_ctx": compiler_module("luna.bootstrap.middleend.semantic.context", "middleend/semantic/context"),
    "sem_ctx_lookup": compiler_module(
        "luna.bootstrap.middleend.semantic.context.lookup",
        "middleend/semantic/context/lookup",
    ),
    "sem_ctx_builder": compiler_module(
        "luna.bootstrap.middleend.semantic.context.builder",
        "middleend/semantic/context/builder",
    ),
    "sem_attributes": compiler_module("luna.bootstrap.middleend.semantic.attributes", "middleend/semantic/attributes"),
    "sem_modules": compiler_module("luna.bootstrap.middleend.semantic.modules", "middleend/semantic/modules"),
    "sem_types": compiler_module("luna.bootstrap.middleend.semantic.types", "middleend/semantic/types"),
    "sem_types_lookup": compiler_module(
        "luna.bootstrap.middleend.semantic.types.lookup",
        "middleend/semantic/types/lookup",
    ),
    "sem_types_visibility": compiler_module(
        "luna.bootstrap.middleend.semantic.types.visibility",
        "middleend/semantic/types/visibility",
    ),
    "sem_consteval_model": compiler_module(
        "luna.bootstrap.middleend.semantic.consteval.model",
        "middleend/semantic/consteval/model",
    ),
    "sem_consteval_engine": compiler_module(
        "luna.bootstrap.middleend.semantic.consteval.engine",
        "middleend/semantic/consteval/engine",
    ),
    "sem_consteval": compiler_module("luna.bootstrap.middleend.semantic.consteval", "middleend/semantic/consteval"),
    "sem_intrinsics": compiler_module("luna.bootstrap.middleend.semantic.intrinsics", "middleend/semantic/intrinsics"),
    "sem_funcs": compiler_module("luna.bootstrap.middleend.semantic.functions", "middleend/semantic/functions"),
    "sem_funcs_ir": compiler_module(
        "luna.bootstrap.middleend.semantic.functions.ir",
        "middleend/semantic/functions/ir",
    ),
    "sem_expr_base": compiler_module("luna.bootstrap.middleend.semantic.expr.base", "middleend/semantic/expr/base"),
    "sem_expr_numeric": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.numeric",
        "middleend/semantic/expr/numeric",
    ),
    "sem_expr_strings": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.strings",
        "middleend/semantic/expr/strings",
    ),
    "sem_expr_api": compiler_module("luna.bootstrap.middleend.semantic.expr.api", "middleend/semantic/expr/api"),
    "sem_expr_initializer": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.initializer",
        "middleend/semantic/expr/initializer",
    ),
    "sem_expr_access": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.access",
        "middleend/semantic/expr/access",
    ),
    "sem_expr_operators": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.operators",
        "middleend/semantic/expr/operators",
    ),
    "sem_expr": compiler_module("luna.bootstrap.middleend.semantic.expr", "middleend/semantic/expr"),
    "sem_stmt_api": compiler_module("luna.bootstrap.middleend.semantic.stmt.api", "middleend/semantic/stmt/api"),
    "sem_stmt_labels": compiler_module(
        "luna.bootstrap.middleend.semantic.stmt.labels",
        "middleend/semantic/stmt/labels",
    ),
    "sem_stmt": compiler_module("luna.bootstrap.middleend.semantic.stmt", "middleend/semantic/stmt"),
    "sema": compiler_module("luna.bootstrap.middleend.sema", "middleend/sema"),
    "x86_64_text": compiler_module("luna.bootstrap.backend.x86_64.text", "backend/x86_64/text"),
    "x86_64_abi": compiler_module("luna.bootstrap.backend.x86_64.abi", "backend/x86_64/abi"),
    "x86_64_frame": compiler_module("luna.bootstrap.backend.x86_64.frame", "backend/x86_64/frame"),
    "x86_64_codegen_support": compiler_module(
        "luna.bootstrap.backend.x86_64.codegen.support",
        "backend/x86_64/codegen/support",
    ),
    "x86_64_codegen_instruction_value": compiler_module(
        "luna.bootstrap.backend.x86_64.codegen.instruction.value",
        "backend/x86_64/codegen/instruction/value",
    ),
    "x86_64_codegen_instruction_call": compiler_module(
        "luna.bootstrap.backend.x86_64.codegen.instruction.call",
        "backend/x86_64/codegen/instruction/call",
    ),
    "x86_64_codegen_instruction": compiler_module(
        "luna.bootstrap.backend.x86_64.codegen.instruction",
        "backend/x86_64/codegen/instruction",
    ),
    "x86_64_codegen": compiler_module("luna.bootstrap.backend.x86_64.codegen", "backend/x86_64/codegen"),
    "x86_64_object": compiler_module("luna.bootstrap.backend.x86_64.object", "backend/x86_64/object"),
    "x86_64_elf_format": compiler_module("luna.bootstrap.backend.x86_64.elf.format", "backend/x86_64/elf/format"),
    "x86_64_elf_reader": compiler_module("luna.bootstrap.backend.x86_64.elf.reader", "backend/x86_64/elf/reader"),
    "x86_64_elf_writer": compiler_module("luna.bootstrap.backend.x86_64.elf.writer", "backend/x86_64/elf/writer"),
    "x86_64_elf": compiler_module("luna.bootstrap.backend.x86_64.elf", "backend/x86_64/elf"),
    "x86_64_assembler_operands": compiler_module(
        "luna.bootstrap.backend.x86_64.assembler.operands",
        "backend/x86_64/assembler/operands",
    ),
    "x86_64_assembler_encoding": compiler_module(
        "luna.bootstrap.backend.x86_64.assembler.encoding",
        "backend/x86_64/assembler/encoding",
    ),
    "x86_64_assembler_source": compiler_module(
        "luna.bootstrap.backend.x86_64.assembler.source",
        "backend/x86_64/assembler/source",
    ),
    "x86_64_assembler": compiler_module("luna.bootstrap.backend.x86_64.assembler", "backend/x86_64/assembler"),
    "x86_64_linker": compiler_module("luna.bootstrap.backend.x86_64.linker", "backend/x86_64/linker"),
}

# tool name -> driver source. Interface and object closures are derived.
DRIVERS = {
    "lunac": "drivers/src/stage_compiler.la",
    "luna-as": "drivers/src/stage_assembler.la",
    "luna-link": "drivers/src/stage_linker.la",
}

NATIVE_TIMEOUT_SECONDS = 600
EMULATED_TIMEOUT_SECONDS = 3600


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def timeout_seconds(runner: tuple[str, ...]) -> int:
    return EMULATED_TIMEOUT_SECONDS if runner else NATIVE_TIMEOUT_SECONDS


def tool(
    directory: pathlib.Path,
    name: str,
    runner: tuple[str, ...],
) -> list[str | pathlib.Path]:
    return [*runner, directory / name]


def run(
    command: list[str | pathlib.Path],
    *,
    cwd: pathlib.Path | None = None,
    timeout: int = NATIVE_TIMEOUT_SECONDS,
) -> None:
    printable = " ".join(str(part) for part in command)
    print(f"  $ {printable}", flush=True)
    try:
        completed = subprocess.run(
            [str(part) for part in command],
            cwd=None if cwd is None else str(cwd),
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        fail(f"command timed out after {timeout}s: {printable}")
    if completed.returncode != 0:
        fail(f"command returned {completed.returncode}: {printable}")


def module_keys_by_name() -> dict[str, str]:
    return {module.name: key for key, module in LIBRARIES.items()}


def registered_module(key: str) -> RegisteredModule:
    if key not in LIBRARIES:
        fail(f"unknown library key {key}")
    return LIBRARIES[key]


def library_interface(key: str) -> pathlib.Path:
    return ROOT / registered_module(key).interface


def library_implementation(key: str) -> pathlib.Path:
    return ROOT / registered_module(key).implementation


def source_dependency_keys(paths: tuple[pathlib.Path, ...]) -> tuple[str, ...]:
    names_to_keys = module_keys_by_name()
    required: set[str] = set()
    for path in paths:
        for module_name in IMPORT_PATTERN.findall(path.read_text(encoding="utf-8")):
            dependency = names_to_keys.get(module_name)
            if dependency is None:
                fail(f"{path.relative_to(ROOT)} imports unregistered module {module_name}")
            required.add(dependency)
    return tuple(key for key in LIBRARIES if key in required)


def library_dependencies(key: str, *, implementation: bool) -> tuple[str, ...]:
    paths = [library_interface(key)]
    if implementation:
        paths.append(library_implementation(key))
    return source_dependency_keys(tuple(paths))


def library_order() -> list[str]:
    """Topologically order the implementation graph with stable tie breaks."""
    dependencies = {
        key: set(library_dependencies(key, implementation=True))
        for key in LIBRARIES
    }
    ordered: list[str] = []
    placed: set[str] = set()
    while len(ordered) != len(LIBRARIES):
        progress = False
        for key in LIBRARIES:
            if key not in placed and dependencies[key] <= placed:
                ordered.append(key)
                placed.add(key)
                progress = True
        if not progress:
            cycle = sorted(set(LIBRARIES) - placed)
            fail(f"library implementation dependency cycle: {cycle}")
    return ordered


def dependency_closure(
    direct_dependencies: tuple[str, ...],
    *,
    implementation: bool,
) -> list[str]:
    required: set[str] = set(direct_dependencies)
    pending = list(direct_dependencies)
    while pending:
        current = pending.pop()
        for dependency in library_dependencies(
            current,
            implementation=implementation,
        ):
            if dependency not in required:
                required.add(dependency)
                pending.append(dependency)
    return [key for key in library_order() if key in required]


def interface_closure(direct_dependencies: tuple[str, ...]) -> list[str]:
    return dependency_closure(direct_dependencies, implementation=False)


def implementation_closure(direct_dependencies: tuple[str, ...]) -> list[str]:
    return dependency_closure(direct_dependencies, implementation=True)


def library_units(key: str) -> list[pathlib.Path]:
    units = [library_implementation(key), library_interface(key)]
    direct_dependencies = library_dependencies(key, implementation=True)
    units.extend(
        library_interface(dependency)
        for dependency in interface_closure(direct_dependencies)
    )
    return units


def driver_dependencies(source: pathlib.Path) -> tuple[str, ...]:
    return source_dependency_keys((source,))


def driver_units(source: pathlib.Path) -> list[pathlib.Path]:
    units = [source]
    units.extend(
        library_interface(dependency)
        for dependency in interface_closure(driver_dependencies(source))
    )
    return units


def declared_module(path: pathlib.Path) -> tuple[bool, str] | None:
    matches = MODULE_PATTERN.findall(path.read_text(encoding="utf-8"))
    if len(matches) != 1:
        return None
    exported, name = matches[0]
    return bool(exported), name


def audit_sources() -> None:
    """Validate the registered module graph and non-mutating source rules."""
    errors: list[str] = []
    warnings: list[str] = []

    names_to_keys: dict[str, str] = {}
    for key, module in LIBRARIES.items():
        name = module.name
        if name in names_to_keys:
            errors.append(
                f"module {name} is registered by both {names_to_keys[name]} and {key}"
            )
        names_to_keys[name] = key

    source_paths: list[pathlib.Path] = []
    for _key, module in LIBRARIES.items():
        name = module.name
        interface = ROOT / module.interface
        implementation = ROOT / module.implementation
        source_paths.extend((interface, implementation))
        for path, should_export in (
            (interface, True),
            (implementation, False),
        ):
            if not path.is_file():
                errors.append(f"missing registered source {path.relative_to(ROOT)}")
                continue
            declaration = declared_module(path)
            if declaration is None:
                errors.append(
                    f"{path.relative_to(ROOT)} must contain exactly one module declaration"
                )
                continue
            exported, actual_name = declaration
            if actual_name != name or exported != should_export:
                expected = "export module" if should_export else "module"
                errors.append(
                    f"{path.relative_to(ROOT)} declares {actual_name!r}; expected "
                    f"{expected} {name}"
                )

        imports_by_path: dict[pathlib.Path, list[str]] = {}
        for path in (interface, implementation):
            if not path.is_file():
                continue
            imported_names = IMPORT_PATTERN.findall(
                path.read_text(encoding="utf-8")
            )
            imports_by_path[path] = imported_names
            if len(imported_names) != len(set(imported_names)):
                errors.append(f"{path.relative_to(ROOT)} imports a module more than once")
            for imported_name in imported_names:
                dependency = names_to_keys.get(imported_name)
                if dependency is None:
                    errors.append(
                        f"{path.relative_to(ROOT)} imports unregistered module "
                        f"{imported_name}"
                    )
        duplicate_pair_imports = set(imports_by_path.get(interface, [])) & set(
            imports_by_path.get(implementation, [])
        )
        if duplicate_pair_imports:
            errors.append(
                f"module {name} interface/implementation both import "
                f"{sorted(duplicate_pair_imports)}"
            )
    for _name, source_name in DRIVERS.items():
        source = ROOT / source_name
        source_paths.append(source)
        if not source.is_file():
            errors.append(f"missing driver source {source_name}")
            continue
        declaration = declared_module(source)
        if declaration is None or declaration[0]:
            errors.append(f"driver {source_name} must contain one non-export module")
        imported_names = IMPORT_PATTERN.findall(
            source.read_text(encoding="utf-8")
        )
        if len(imported_names) != len(set(imported_names)):
            errors.append(f"driver {source_name} imports a module more than once")
        for imported_name in imported_names:
            dependency = names_to_keys.get(imported_name)
            if dependency is None:
                errors.append(
                    f"driver {source_name} imports unregistered module {imported_name}"
                )

    registered_paths = {path.resolve() for path in source_paths}
    discovered_paths = {
        path.resolve()
        for area in (ROOT / "compiler", ROOT / "library", ROOT / "drivers")
        for suffix in ("*.la", "*.lh")
        for path in area.rglob(suffix)
    }
    for path in sorted(discovered_paths - registered_paths):
        errors.append(f"unregistered source {path.relative_to(ROOT)}")

    for path in source_paths:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if not text.endswith("\n"):
            errors.append(f"{path.relative_to(ROOT)} has no final newline")
        lines = text.splitlines()
        if len(lines) > 2000:
            warnings.append(
                f"{path.relative_to(ROOT)} exceeds the soft 2,000-line ceiling "
                f"({len(lines)} lines)"
            )
        for line_number, line in enumerate(lines, 1):
            if "\t" in line:
                errors.append(
                    f"{path.relative_to(ROOT)}:{line_number} contains a tab"
                )
            if line.rstrip() != line:
                errors.append(
                    f"{path.relative_to(ROOT)}:{line_number} has trailing whitespace"
                )
            if len(line) > 120:
                errors.append(
                    f"{path.relative_to(ROOT)}:{line_number} exceeds 120 columns "
                    f"({len(line)})"
                )

    for warning in warnings:
        print(f"WARN {warning}")
    if errors:
        fail("source audit failed:\n  " + "\n  ".join(errors))
    ordered = library_order()
    for name, source_name in DRIVERS.items():
        source = ROOT / source_name
        direct = driver_dependencies(source)
        interfaces = interface_closure(direct)
        objects = implementation_closure(direct)
        print(
            f"AUDIT driver {name}: {len(interfaces)} interfaces, "
            f"{len(objects)} library objects"
        )
    print(
        f"AUDIT: {len(ordered)} modules and {len(DRIVERS)} drivers are consistent"
    )


def reset(directory: pathlib.Path) -> None:
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)


def build_stage(
    tools: pathlib.Path,
    out: pathlib.Path,
    runner: tuple[str, ...],
) -> dict[str, pathlib.Path]:
    """Compile, assemble and link one complete toolchain into `out`."""
    compiler = tool(tools, "lunac", runner)
    assembler = tool(tools, "luna-as", runner)
    linker = tool(tools, "luna-link", runner)
    timeout = timeout_seconds(runner)
    for prefix in (compiler, assembler, linker):
        if not prefix[-1].is_file():
            fail(f"missing tool {prefix[-1]}")

    assembly_root = out / "assembly"
    object_root = out / "objects"
    binary_root = out / "bin"
    reset(assembly_root)
    reset(object_root)
    reset(binary_root)

    order = library_order()
    objects: dict[str, pathlib.Path] = {}
    for key in order:
        assembly = assembly_root / f"{key}.s"
        object_file = object_root / f"{key}.lo"
        run(
            [*compiler, "--library", "-o", assembly, *library_units(key)],
            timeout=timeout,
        )
        run([*assembler, "-o", object_file, assembly], timeout=timeout)
        objects[key] = object_file
        print(f"  built library {key}")

    executables: dict[str, pathlib.Path] = {}
    for name, source_name in DRIVERS.items():
        source = ROOT / source_name
        driver_object = object_root / f"{name}.lo"
        assembly = assembly_root / f"{name}.s"
        run(
            [*compiler, "--executable", "-o", assembly, *driver_units(source)],
            timeout=timeout,
        )
        run([*assembler, "-o", driver_object, assembly], timeout=timeout)
        executable = binary_root / name
        link_keys = implementation_closure(driver_dependencies(source))
        run(
            [*linker, "-o", executable, driver_object, *(objects[key] for key in link_keys)],
            timeout=timeout,
        )
        executables[name] = executable
        print(f"  linked {name} ({len(link_keys)} library objects)")

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
    """Map context::DiagnosticKind names to their enum ordinals."""
    interface = library_interface("sem_ctx")
    text = interface.read_text(encoding="utf-8")
    match = re.search(
        r"enum DiagnosticKind[^}]*\{(.*?)\}", text, re.S
    )
    if match is None:
        fail("cannot locate DiagnosticKind")
    return {
        name: ordinal
        for ordinal, name in enumerate(
            re.findall(r"[a-z_][a-z0-9_]*", match.group(1))
        )
    }


# Flags for the gcc-compiled C fixture: freestanding, no PIC, small model and
# no unwind tables keep the object inside the ELF subset the Luna linker
# reader accepts; -Wall -Werror proves the fixture is warning-clean.
GCC_FIXTURE_FLAGS = (
    "-ffreestanding",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-common",
    "-fno-asynchronous-unwind-tables",
    "-mcmodel=small",
    "-O1",
    "-Wall",
    "-Werror",
    "-c",
)


def ffi_compiler() -> tuple[str, ...] | None:
    """Return an x86-64 C compiler command for optional FFI tests.

    `LUNA_FFI_CC` may name a native or cross compiler, including command
    prefixes such as `ccache x86_64-linux-gnu-gcc`. A present compiler for a
    different target is not useful: its ET_REL objects cannot enter Luna's
    x86-64-only linker, and its hosted executables cannot consume Luna objects.
    """
    configured = tuple(shlex.split(os.environ.get("LUNA_FFI_CC", "gcc")))
    if not configured:
        print("SKIP host FFI: LUNA_FFI_CC is empty")
        return None
    executable = shutil.which(configured[0])
    if executable is None:
        print(f"SKIP host FFI: compiler not found: {configured[0]}")
        return None
    compiler = (executable, *configured[1:])
    try:
        target = subprocess.run(
            [*compiler, "-dumpmachine"],
            capture_output=True,
            check=False,
            text=True,
            timeout=NATIVE_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"SKIP host FFI: cannot query compiler target: {error}")
        return None
    triple = target.stdout.strip()
    if target.returncode != 0 or re.match(r"^(x86_64|amd64)(-|$)", triple) is None:
        description = triple or f"status {target.returncode}"
        print(f"SKIP host FFI: compiler target is not x86-64: {description}")
        return None
    return compiler


def build_gcc_fixtures(
    ffi: pathlib.Path,
    compiler: tuple[str, ...] | None,
) -> pathlib.Path | None:
    """Compile tests/ffi/fixture.c when an x86-64 C compiler is available."""
    if compiler is None:
        return None
    work = ROOT / "out" / "tests" / "ffi-gcc"
    reset(work)
    run([*compiler, *GCC_FIXTURE_FLAGS, "-o", work / "fixture.o", ffi / "fixture.c"])
    return work


def ffi_units(ffi: pathlib.Path, name: str) -> list[pathlib.Path]:
    """Source units for an FFI case: the case itself plus every tests.ffi.*
    module it imports, supplied as interface/implementation source pairs."""
    units = [ffi / name]
    source = (ffi / name).read_text(encoding="utf-8")
    for module in IMPORT_PATTERN.findall(source):
        if module.startswith("tests.ffi."):
            stem = ffi / module.removeprefix("tests.ffi.")
            units.extend(
                (stem.with_suffix(".la"), stem.with_suffix(".lh"))
            )
    return units


def case_units(cases: pathlib.Path, name: str) -> list[pathlib.Path]:
    """Source units for a tests/cases entry: the case itself plus every
    tests.modules.* module it imports, supplied as interface/implementation
    source pairs; dots after the prefix become directory separators under
    tests/modules/ (tests.modules.a.b -> tests/modules/a/b.{la,lh})."""
    units = [cases / name]
    source = (cases / name).read_text(encoding="utf-8")
    modules_root = ROOT / "tests" / "modules"
    for module in IMPORT_PATTERN.findall(source):
        if module.startswith("tests.modules."):
            stem = modules_root / module.removeprefix("tests.modules.").replace(".", "/")
            units.extend(
                (stem.with_suffix(".la"), stem.with_suffix(".lh"))
            )
    return units


def syscall_object(stage_bin: pathlib.Path) -> pathlib.Path:
    """The luna.linux.syscall object from the build step, home of the
    luna_linux_syscallN asm fn stubs (test cases and FFI shims declare them
    extern; the linker no longer injects their definitions). It defines no
    _start, so linking it into every test executable is inert for cases
    that never touch syscalls."""
    return stage_bin.parent / "objects" / "syscall.lo"


def execute_ffi_tests(
    stage_bin: pathlib.Path,
    runner: tuple[str, ...],
) -> tuple[int, list[str], int]:
    """Link tests/ffi cases against the checked-in ELF64 fixture objects.

    Each expectation line is `<case>.la <fixture>.o <exit>`, or
    `<case>.la <fixture>.o link:<status>` when luna-link itself must fail
    with the given exit status (malformed or unresolvable fixtures). Fixtures
    not checked in (fixture.o) are built from C sources with an x86-64 C
    compiler; without one their cases are skipped, not failed.

    Two more shapes exercise the luna-as --emit elf writer:
    `<case>.la elf-link <fixture>.o <exit>` (or link:<status>) assembles the
    case to a standard ELF64 ET_REL instead of LUNAOBJ1, then links with
    luna-link exactly as above — a writer -> reader -> linker round-trip;
    `<case>.la host-link <cmain>.c <exit>` compiles the case with
    `lunac --library`, assembles it with --emit elf and links the object
    against a C main with the same compiler, running the resulting hosted
    executable (skipped when no suitable compiler is present).
    """
    ffi = ROOT / "tests" / "ffi"
    expectations = ffi / "expectations.txt"
    if not expectations.is_file():
        return 0, [], 0
    compiler = ffi_compiler()
    gcc_fixtures = build_gcc_fixtures(ffi, compiler)
    timeout = timeout_seconds(runner)
    passed = 0
    failed: list[str] = []
    skipped = 0
    for line in expectations.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split()
        name = parts[0]
        if len(parts) == 4 and parts[1] in ("elf-link", "host-link"):
            mode, fixture, expected = parts[1], parts[2], parts[3]
        else:
            mode, fixture, expected = "luna-link", parts[1], parts[2]
        work = ROOT / "out" / "tests" / name.removesuffix(".la")
        reset(work)
        assembly = work / f"{name}.s"
        executable = work / name.removesuffix(".la")
        try:
            if mode == "host-link":
                cmain = ffi / fixture
                if not cmain.is_file():
                    raise AssertionError(f"missing C main {cmain}")
                if compiler is None:
                    print(f"SKIP {name}: no x86-64 C compiler is available")
                    skipped += 1
                    continue
                object_file = work / f"{name}.o"
                run(
                    [
                        *tool(stage_bin, "lunac", runner),
                        "--library",
                        "-o",
                        assembly,
                        *ffi_units(ffi, name),
                    ],
                    timeout=timeout,
                )
                run(
                    [
                        *tool(stage_bin, "luna-as", runner),
                        "--emit",
                        "elf",
                        "-o",
                        object_file,
                        assembly,
                    ],
                    timeout=timeout,
                )
                # Link flags mirror GCC_FIXTURE_FLAGS where they apply to a
                # hosted link: Luna objects use small-model non-PIC addressing
                # like the -fno-pic fixtures, so -no-pie counters the distro
                # PIE default; -Wall -Werror keeps the C main warning-clean.
                run(
                    [
                        *compiler,
                        "-Wall",
                        "-Werror",
                        "-no-pie",
                        "-o",
                        executable,
                        object_file,
                        cmain,
                    ]
                )
                completed = subprocess.run(
                    [*runner, str(executable)],
                    timeout=timeout,
                )
                if completed.returncode != int(expected):
                    raise AssertionError(
                        f"exit {completed.returncode}, expected {expected}"
                    )
                passed += 1
                print(f"PASS {name} (host-link {fixture}, {expected})")
                continue
            fixture_path = ffi / fixture
            if not fixture_path.is_file():
                if gcc_fixtures is None:
                    print(f"SKIP {name}: gcc-built fixture {fixture} unavailable")
                    skipped += 1
                    continue
                fixture_path = gcc_fixtures / fixture
            object_file = work / f"{name}.lo" if mode == "luna-link" else work / f"{name}.o"
            run(
                [
                    *tool(stage_bin, "lunac", runner),
                    "--executable",
                    "-o",
                    assembly,
                    *ffi_units(ffi, name),
                ],
                timeout=timeout,
            )
            assemble_command = [*tool(stage_bin, "luna-as", runner)]
            if mode == "elf-link":
                assemble_command += ["--emit", "elf"]
            run(
                [*assemble_command, "-o", object_file, assembly],
                timeout=timeout,
            )
            linked = subprocess.run(
                [
                    *tool(stage_bin, "luna-link", runner),
                    "-o",
                    executable,
                    object_file,
                    fixture_path,
                    syscall_object(stage_bin),
                ],
                timeout=timeout,
            )
            if expected.startswith("link:"):
                wanted = int(expected.removeprefix("link:"))
                if linked.returncode != wanted:
                    raise AssertionError(
                        f"luna-link exit {linked.returncode}, expected {wanted}"
                    )
            else:
                if linked.returncode != 0:
                    raise AssertionError(
                        f"luna-link exit {linked.returncode}, expected 0"
                    )
                completed = subprocess.run(
                    [*runner, str(executable)],
                    timeout=timeout,
                )
                if completed.returncode != int(expected):
                    raise AssertionError(
                        f"exit {completed.returncode}, expected {expected}"
                    )
            passed += 1
            print(f"PASS {name} ({mode} {fixture}, {expected})")
        except Exception as error:  # noqa: BLE001 - report and continue
            failed.append(name)
            print(f"FAIL {name}: {error}")
    return passed, failed, skipped


def execute_tests(stage_bin: pathlib.Path, runner: tuple[str, ...]) -> int:
    expectations = ROOT / "tests" / "expectations.txt"
    cases = ROOT / "tests" / "cases"
    kinds = semantic_diagnostic_kinds()
    timeout = timeout_seconds(runner)
    passed = 0
    failed: list[str] = []
    for line in expectations.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        name = parts[0]
        work = ROOT / "out" / "tests" / name.removesuffix(".la")
        reset(work)
        assembly = work / f"{name}.s"
        try:
            if len(parts) == 3 and parts[1] == "FAIL":
                kind = parts[2]
                if kind not in kinds:
                    raise AssertionError(f"unknown diagnostic kind {kind}")
                completed = subprocess.run(
                    [
                        *tool(stage_bin, "lunac", runner),
                        "--executable",
                        "-o",
                        assembly,
                        *case_units(cases, name),
                    ],
                    timeout=timeout,
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
            executable = work / name.removesuffix(".la")
            run(
                [
                    *tool(stage_bin, "lunac", runner),
                    "--executable",
                    "-o",
                    assembly,
                    *case_units(cases, name),
                ],
                timeout=timeout,
            )
            run(
                [*tool(stage_bin, "luna-as", runner), "-o", object_file, assembly],
                timeout=timeout,
            )
            run(
                [
                    *tool(stage_bin, "luna-link", runner),
                    "-o",
                    executable,
                    object_file,
                    syscall_object(stage_bin),
                ],
                timeout=timeout,
            )
            completed = subprocess.run(
                [*runner, str(executable)],
                timeout=timeout,
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
    ffi_passed, ffi_failed, ffi_skipped = execute_ffi_tests(stage_bin, runner)
    passed += ffi_passed
    failed.extend(ffi_failed)
    print(f"{passed} passed, {len(failed)} failed, {ffi_skipped} skipped")
    return 1 if failed else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_common(sub: argparse.ArgumentParser) -> None:
        sub.add_argument("--anchor", type=pathlib.Path, default=ROOT / "anchor")
        sub.add_argument(
            "--runner",
            nargs="*",
            default=list(DEFAULT_RUNNER),
            help="prefix for x86-64 tools and generated programs",
        )

    build_parser = subparsers.add_parser("build", help="anchor -> out/stage-next")
    add_common(build_parser)
    build_parser.add_argument("--out", type=pathlib.Path)

    verify_parser = subparsers.add_parser("verify", help="fixed-point gate")
    add_common(verify_parser)
    verify_parser.add_argument(
        "--out",
        type=pathlib.Path,
        help="output root containing stage-next and stage-fixed",
    )

    test_parser = subparsers.add_parser("test", help="run behavior tests")
    add_common(test_parser)
    test_parser.add_argument("--stage", type=pathlib.Path)

    audit_parser = subparsers.add_parser(
        "audit",
        help="check anchor, module graph, and source rules without writing",
    )
    audit_parser.add_argument(
        "--anchor",
        type=pathlib.Path,
        default=ROOT / "anchor",
    )

    arguments = parser.parse_args()
    default_out = ROOT / "out"

    if arguments.command == "audit":
        verify_anchor(arguments.anchor)
        audit_sources()
        return

    runner = tuple(arguments.runner)
    audit_sources()
    if arguments.command == "build":
        out = arguments.out or default_out / "stage-next"
        verify_anchor(arguments.anchor)
        build_stage(arguments.anchor, out, runner)
    elif arguments.command == "verify":
        verify_root = arguments.out or default_out
        next_out = verify_root / "stage-next"
        fixed_out = verify_root / "stage-fixed"
        verify_anchor(arguments.anchor)
        build_stage(arguments.anchor, next_out, runner)
        print("building the fixed-point stage from its own output")
        build_stage(next_out / "bin", fixed_out, runner)
        compare_stages(next_out, fixed_out)
        print("FIXED POINT: all artifacts byte-identical")
    else:
        stage = arguments.stage or default_out / "stage-next" / "bin"
        if not (stage / "lunac").is_file():
            fail(f"no built toolchain under {stage}; run 'build' first")
        raise SystemExit(execute_tests(stage, runner))


if __name__ == "__main__":
    main()
