#!/usr/bin/env python3
"""Luna self-hosting driver.

Builds the complete Luna toolchain from pure Luna sources using a previously
built toolchain (the committed `anchor/`), then verifies the result by
rebuilding itself and comparing every artifact byte-for-byte.

    python3 tools/selfhost.py build    # anchor -> out/stage-next
    python3 tools/selfhost.py verify   # anchor -> transition -> next -> fixed
    python3 tools/selfhost.py test     # run tests/cases through stage-next
    python3 tools/selfhost.py audit    # read-only anchor/module/source checks
"""

from __future__ import annotations

import argparse
from collections import Counter
import concurrent.futures
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
MAIN_DECLARATION_PATTERN = re.compile(
    r"^(?P<indent>[ \t]*)fn[ \t]+main\(\)[ \t]*->[ \t]*i32(?P<opening>[ \t]*\{[ \t]*)$",
    re.MULTILINE,
)
MAIN_REFERENCE_PATTERN = re.compile(r"\bmain[ \t]*\(")
TOP_LEVEL_DECLARATION_PATTERN = re.compile(
    r"^(?:@[A-Za-z_][A-Za-z0-9_]*(?:\([^\n)]*\))?[ \t]+)*(?:const[ \t]+)?"
    r"(?:fn|class|struct|union|enum|type)[ \t]+([A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE,
)
TOP_LEVEL_CONSTANT_PATTERN = re.compile(
    r"^const[ \t]+(?!fn\b)([A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE,
)
LUNA_LEXEME_PATTERN = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'|"
    r"(?P<identifier>[A-Za-z_][A-Za-z0-9_]*)",
    re.DOTALL,
)
SUITE_UNSAFE_SOURCE_MARKERS = (
    "@embed(",
    "@export_name(",
    "@file(",
    "asm fn",
    "extern fn",
    "process_exit(",
)
TEST_SUITE_CASE_LIMIT = 6


@dataclass(frozen=True)
class RegisteredModule:
    name: str
    interface: str
    implementations: tuple[str, ...]
    interface_only: bool = False


def source_module(
    source_tree: str,
    name: str,
    implementation_stems: tuple[str, ...],
    *,
    interface_only: bool = False,
) -> RegisteredModule:
    module_path = name.replace(".", "/")
    return RegisteredModule(
        name=name,
        interface=f"{source_tree}/include/{module_path}.lh",
        implementations=tuple(
            f"{source_tree}/src/{implementation_stem}.la"
            for implementation_stem in implementation_stems
        ),
        interface_only=interface_only,
    )


def library_module(name: str, *implementation_stems: str) -> RegisteredModule:
    return source_module("library", name, implementation_stems)


def compiler_module(name: str, *implementation_stems: str) -> RegisteredModule:
    return source_module("compiler", name, implementation_stems)


def interface_module(source_tree: str, name: str) -> RegisteredModule:
    return source_module(source_tree, name, (), interface_only=True)


def driver_module(name: str, *implementation_stems: str) -> RegisteredModule:
    return source_module("drivers", name, implementation_stems)


# key -> explicit interface and implementation source records. Dependencies
# and build order are derived, so adding or splitting a module still has one
# configuration site.
LIBRARIES = {
    "ascii": library_module("luna.std.ascii", "std/ascii"),
    "checked": library_module("luna.std.checked", "std/checked"),
    "utility": interface_module("library", "luna.std.utility"),
    "syscall": library_module("luna.linux.syscall", "linux/syscall"),
    "runtime": library_module("luna.runtime", "runtime"),
    "span": interface_module("library", "luna.std.span"),
    "memory": library_module("luna.std.memory", "std/memory"),
    "buffer": library_module("luna.std.buffer", "std/buffer"),
    "charconv": library_module("luna.std.charconv", "std/charconv"),
    "string": library_module("luna.std.string", "std/string"),
    "vector": interface_module("library", "luna.std.vector"),
    "pool": library_module("luna.internal.pool", "internal/pool"),
    "deque": interface_module("library", "luna.std.deque"),
    "list": interface_module("library", "luna.std.list"),
    "tree": interface_module("library", "luna.internal.tree"),
    "map": interface_module("library", "luna.std.map"),
    "queue": interface_module("library", "luna.std.queue"),
    "bytes": library_module("luna.std.bytes", "std/bytes"),
    "binary": library_module("luna.std.binary", "std/binary"),
    "string_view": library_module("luna.std.string_view", "std/string_view"),
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
    "sem_callable": compiler_module("luna.bootstrap.middleend.semantic.callable", "middleend/semantic/callable"),
    "sem_value": compiler_module("luna.bootstrap.middleend.semantic.value", "middleend/semantic/value"),
    "sem_class_model": compiler_module(
        "luna.bootstrap.middleend.semantic.classes.model",
        "middleend/semantic/classes/model",
    ),
    "sem_generic_model": compiler_module(
        "luna.bootstrap.middleend.semantic.generics.model",
        "middleend/semantic/generics/model",
    ),
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
    "sem_generics": compiler_module("luna.bootstrap.middleend.semantic.generics", "middleend/semantic/generics"),
    "sem_types": compiler_module(
        "luna.bootstrap.middleend.semantic.types",
        "middleend/semantic/types",
        "middleend/semantic/types/layout",
    ),
    "sem_types_lookup": compiler_module(
        "luna.bootstrap.middleend.semantic.types.lookup",
        "middleend/semantic/types/lookup",
    ),
    "sem_types_visibility": compiler_module(
        "luna.bootstrap.middleend.semantic.types.visibility",
        "middleend/semantic/types/visibility",
    ),
    "sem_classes": compiler_module("luna.bootstrap.middleend.semantic.classes", "middleend/semantic/classes"),
    "sem_consteval_model": interface_module("compiler", "luna.bootstrap.middleend.semantic.consteval.model"),
    "sem_consteval_engine": compiler_module(
        "luna.bootstrap.middleend.semantic.consteval.engine",
        "middleend/semantic/consteval/engine",
        "middleend/semantic/consteval/engine/execute",
    ),
    "sem_consteval": compiler_module("luna.bootstrap.middleend.semantic.consteval", "middleend/semantic/consteval"),
    "sem_intrinsics": compiler_module("luna.bootstrap.middleend.semantic.intrinsics", "middleend/semantic/intrinsics"),
    "sem_funcs": compiler_module(
        "luna.bootstrap.middleend.semantic.functions",
        "middleend/semantic/functions",
        "middleend/semantic/functions/const",
        "middleend/semantic/functions/signature",
        "middleend/semantic/functions/overloads",
        "middleend/semantic/functions/bindings",
        "middleend/semantic/functions/defaults",
        "middleend/semantic/functions/generics",
        "middleend/semantic/functions/methods",
        "middleend/semantic/functions/special",
    ),
    "sem_funcs_ir": compiler_module(
        "luna.bootstrap.middleend.semantic.functions.ir",
        "middleend/semantic/functions/ir",
    ),
    "sem_lifetime": compiler_module(
        "luna.bootstrap.middleend.semantic.lifetime",
        "middleend/semantic/lifetime",
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
    "sem_expr_probe": compiler_module(
        "luna.bootstrap.middleend.semantic.expr.probe",
        "middleend/semantic/expr/probe",
        "middleend/semantic/expr/probe/operators",
        "middleend/semantic/expr/probe/call",
    ),
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
    "sem_stmt_api": interface_module("compiler", "luna.bootstrap.middleend.semantic.stmt.api"),
    "sem_stmt_labels": compiler_module(
        "luna.bootstrap.middleend.semantic.stmt.labels",
        "middleend/semantic/stmt/labels",
    ),
    "sem_stmt": compiler_module("luna.bootstrap.middleend.semantic.stmt", "middleend/semantic/stmt"),
    "sema": compiler_module("luna.bootstrap.middleend.sema", "middleend/sema"),
    "x86_64_codegen": compiler_module(
        "luna.compiler.x86.codegen",
        "backend/x86_64/codegen/abi",
        "backend/x86_64/codegen/frame",
        "backend/x86_64/codegen/support",
        "backend/x86_64/codegen/value",
        "backend/x86_64/codegen/call",
        "backend/x86_64/codegen/instruction",
        "backend/x86_64/codegen/facade",
    ),
    "x86_64_object": compiler_module("luna.compiler.x86.object", "backend/x86_64/object/object"),
    "x86_64_elf": compiler_module(
        "luna.compiler.x86.elf",
        "backend/x86_64/elf/format",
        "backend/x86_64/elf/reader",
        "backend/x86_64/elf/writer",
        "backend/x86_64/elf/facade",
    ),
    "x86_64_assembler": compiler_module(
        "luna.compiler.x86.assembler",
        "backend/x86_64/assembler/operands",
        "backend/x86_64/assembler/symbols",
        "backend/x86_64/assembler/encoding",
        "backend/x86_64/assembler/source",
        "backend/x86_64/assembler/facade",
    ),
    "x86_64_linker": compiler_module("luna.compiler.x86.linker", "backend/x86_64/linker/linker"),
    "tool_cli": driver_module("luna.tools.cli", "cli"),
    "tool_compile": driver_module("luna.tools.compile", "stage_compiler"),
    "tool_assemble": driver_module("luna.tools.assemble", "stage_assembler"),
    "tool_link": driver_module("luna.tools.link", "stage_linker"),
}

# tool name -> driver source. Interface and object closures are derived.
DRIVERS = {
    "luna": "drivers/src/luna.la",
}

NATIVE_TIMEOUT_SECONDS = 600
EMULATED_TIMEOUT_SECONDS = 3600


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def timeout_seconds(runner: tuple[str, ...]) -> int:
    return EMULATED_TIMEOUT_SECONDS if runner else NATIVE_TIMEOUT_SECONDS


def tool(
    directory: pathlib.Path,
    command: str,
    runner: tuple[str, ...],
) -> list[str | pathlib.Path]:
    return [*runner, directory / "luna", command]


def toolchain_is_available(directory: pathlib.Path) -> bool:
    return (directory / "luna").is_file()


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


def library_implementations(key: str) -> tuple[pathlib.Path, ...]:
    return tuple(ROOT / path for path in registered_module(key).implementations)


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
        paths.extend(library_implementations(key))
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
    return [
        key
        for key in dependency_closure(direct_dependencies, implementation=True)
        if not registered_module(key).interface_only
    ]


def library_units(key: str) -> list[pathlib.Path]:
    units = [*library_implementations(key), library_interface(key)]
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


def audit_migrated_layout(errors: list[str]) -> None:
    backend_root = ROOT / "compiler" / "src" / "backend" / "x86_64"
    directories = (backend_root, *sorted(path for path in backend_root.rglob("*") if path.is_dir()))
    for directory in directories:
        entries = tuple(directory.iterdir())
        has_sources = any(entry.is_file() and entry.suffix in (".la", ".lh") for entry in entries)
        has_directories = any(entry.is_dir() for entry in entries)
        if has_sources and has_directories:
            errors.append(
                f"{directory.relative_to(ROOT)} mixes source files and directories"
            )

    interface_root = ROOT / "compiler" / "include" / "luna" / "compiler"
    for interface in sorted(interface_root.rglob("*.lh")):
        line_count = len(interface.read_text(encoding="utf-8").splitlines())
        if line_count > 250:
            errors.append(
                f"{interface.relative_to(ROOT)} exceeds the 250-line interface ceiling "
                f"({line_count} lines)"
            )


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
        implementations = tuple(ROOT / path for path in module.implementations)
        if module.interface_only and implementations:
            errors.append(f"interface-only module {name} registers an implementation")
        if not module.interface_only and not implementations:
            errors.append(f"module {name} has no registered implementation")
        source_paths.extend((interface, *implementations))
        units = [(interface, True)]
        units.extend((implementation, False) for implementation in implementations)
        for path, should_export in units:
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

        first_import_path: dict[str, pathlib.Path] = {}
        for path in (interface, *implementations):
            if not path.is_file():
                continue
            imported_names = IMPORT_PATTERN.findall(
                path.read_text(encoding="utf-8")
            )
            if len(imported_names) != len(set(imported_names)):
                errors.append(f"{path.relative_to(ROOT)} imports a module more than once")
            for imported_name in imported_names:
                previous_path = first_import_path.get(imported_name)
                if previous_path is not None:
                    errors.append(
                        f"module {name} imports {imported_name} in both "
                        f"{previous_path.relative_to(ROOT)} and {path.relative_to(ROOT)}"
                    )
                else:
                    first_import_path[imported_name] = path
                dependency = names_to_keys.get(imported_name)
                if dependency is None:
                    errors.append(
                        f"{path.relative_to(ROOT)} imports unregistered module "
                        f"{imported_name}"
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

    audit_migrated_layout(errors)

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
    if not toolchain_is_available(tools):
        fail(f"missing toolchain under {tools}")
    compiler = tool(tools, "compile", runner)
    assembler = tool(tools, "assemble", runner)
    linker = tool(tools, "link", runner)
    timeout = timeout_seconds(runner)

    assembly_root = out / "assembly"
    object_root = out / "objects"
    binary_root = out / "bin"
    reset(assembly_root)
    reset(object_root)
    reset(binary_root)

    order = library_order()
    objects: dict[str, pathlib.Path] = {}
    for key in order:
        if registered_module(key).interface_only:
            print(f"  registered interface-only module {key}")
            continue
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


def expectation_units(
    cases: pathlib.Path,
    name: str,
    specification: list[str],
) -> list[pathlib.Path]:
    """Resolve an optional ordered source set from an expectation line.

    The default preserves the import-derived test convention. `UNITS` makes
    every source explicit so module diagnostics can exercise input order,
    duplicate units and unreachable modules without teaching the harness Luna
    module semantics.
    """
    if not specification:
        return case_units(cases, name)
    if specification[0] != "UNITS" or len(specification) == 1:
        raise AssertionError(f"invalid unit specification for {name}")
    units: list[pathlib.Path] = []
    for source_name in specification[1:]:
        source = (ROOT / source_name).resolve()
        if not source.is_relative_to(ROOT):
            raise AssertionError(f"unit outside repository for {name}: {source_name}")
        if source.suffix not in (".la", ".lh") or not source.is_file():
            raise AssertionError(f"invalid unit for {name}: {source_name}")
        units.append(source)
    return units


@dataclass(frozen=True)
class TestExpectation:
    index: int
    name: str
    expected_exit: int | None
    diagnostic_kind: str | None
    unit_specification: tuple[str, ...]

    @classmethod
    def parse(cls, index: int, line: str) -> TestExpectation:
        match tuple(line.split()):
            case (name, "FAIL", diagnostic_kind, *unit_specification):
                return cls(
                    index=index,
                    name=name,
                    expected_exit=None,
                    diagnostic_kind=diagnostic_kind,
                    unit_specification=tuple(unit_specification),
                )
            case (name, expected_exit, *unit_specification):
                return cls(
                    index=index,
                    name=name,
                    expected_exit=int(expected_exit),
                    diagnostic_kind=None,
                    unit_specification=tuple(unit_specification),
                )
            case _:
                raise AssertionError(f"invalid expectation: {line}")


@dataclass(frozen=True)
class TestSuiteCase:
    expectation: TestExpectation
    entry_name: str
    body: str


@dataclass(frozen=True)
class TestSuitePlan:
    identifier: int
    cases: tuple[TestSuiteCase, ...]

    @property
    def first_expectation_index(self) -> int:
        return self.cases[0].expectation.index


@dataclass(frozen=True)
class ExpectationTaskResult:
    order: int
    passed: int
    failed: tuple[str, ...]
    messages: tuple[str, ...]


class TestSuitePlanner:
    def __init__(self, cases: pathlib.Path) -> None:
        self._cases = cases

    def plan(
        self,
        expectations: tuple[TestExpectation, ...],
    ) -> tuple[tuple[TestSuitePlan, ...], tuple[TestExpectation, ...]]:
        name_counts = Counter(expectation.name for expectation in expectations)
        candidates: list[TestSuiteCase] = []
        isolated: list[TestExpectation] = []
        for expectation in expectations:
            candidate = self._make_case(expectation, name_counts[expectation.name])
            if candidate is None:
                isolated.append(expectation)
            else:
                candidates.append(candidate)

        suites = tuple(
            TestSuitePlan(identifier, tuple(candidates[offset : offset + TEST_SUITE_CASE_LIMIT]))
            for identifier, offset in enumerate(range(0, len(candidates), TEST_SUITE_CASE_LIMIT))
        )
        isolated.sort(key=lambda expectation: expectation.index)
        return suites, tuple(isolated)

    def _make_case(self, expectation: TestExpectation, name_count: int) -> TestSuiteCase | None:
        source_path = self._cases / expectation.name
        source = source_path.read_text(encoding="utf-8")
        module_matches = MODULE_PATTERN.findall(source)
        declaration_matches = MAIN_DECLARATION_PATTERN.findall(source)
        has_expected_exit = expectation.expected_exit is not None
        expected_exit_is_safe = has_expected_exit and expectation.expected_exit >= 0
        expectation_is_unique = name_count == 1
        uses_default_units = not expectation.unit_specification
        has_single_module = len(module_matches) == 1
        has_single_entry = len(declaration_matches) == 1
        entry_is_unreferenced = len(MAIN_REFERENCE_PATTERN.findall(source)) == 1
        source_is_portable = not any(marker in source for marker in SUITE_UNSAFE_SOURCE_MARKERS)
        has_no_case_interface = not source_path.with_suffix(".lh").exists()
        has_no_imports = IMPORT_PATTERN.search(source) is None
        is_eligible = all(
            (
                expected_exit_is_safe,
                expectation_is_unique,
                uses_default_units,
                has_single_module,
                has_single_entry,
                entry_is_unreferenced,
                source_is_portable,
                has_no_case_interface,
                has_no_imports,
            )
        )
        if not is_eligible:
            return None

        entry_name = f"run_case_{expectation.index:04d}"
        body, replacement_count = MAIN_DECLARATION_PATTERN.subn(
            rf"\g<indent>fn {entry_name}() -> i32\g<opening>",
            source,
        )
        if replacement_count != 1:
            raise AssertionError(f"cannot rewrite entry for {expectation.name}")
        body = MODULE_PATTERN.sub("", body, count=1).strip()
        declarations = set(TOP_LEVEL_DECLARATION_PATTERN.findall(body))
        declarations.update(TOP_LEVEL_CONSTANT_PATTERN.findall(body))
        declarations.discard(entry_name)
        replacements = {
            declaration: f"case_{expectation.index:04d}_{declaration}"
            for declaration in declarations
        }
        return TestSuiteCase(
            expectation=expectation,
            entry_name=entry_name,
            body=self._replace_identifiers(body, replacements),
        )

    @staticmethod
    def _replace_identifiers(source: str, replacements: dict[str, str]) -> str:
        return LUNA_LEXEME_PATTERN.sub(
            lambda match: replacements.get(
                match.group("identifier"),
                match.group(0),
            )
            if match.group("identifier") is not None
            else match.group(0),
            source,
        )


def test_command(
    command: list[str | pathlib.Path],
    *,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    printable = " ".join(str(part) for part in command)
    print(f"  $ {printable}", flush=True)
    return subprocess.run(
        [str(part) for part in command],
        timeout=timeout,
        capture_output=True,
        text=True,
    )


def require_test_success(
    command: list[str | pathlib.Path],
    *,
    timeout: int,
) -> None:
    completed = test_command(command, timeout=timeout)
    if completed.returncode != 0:
        raise AssertionError(
            f"command returned {completed.returncode}: {completed.stderr.strip()}"
        )


def require_two_unit_order_identity(
    compiler: list[str | pathlib.Path],
    work: pathlib.Path,
    stem: str,
    interface: pathlib.Path,
    first_unit: pathlib.Path,
    second_unit: pathlib.Path,
    *,
    timeout: int,
) -> None:
    outputs = (work / f"{stem}-first.s", work / f"{stem}-second.s")
    orders = ((first_unit, second_unit), (second_unit, first_unit))
    for output, units in zip(outputs, orders, strict=True):
        require_test_success(
            [*compiler, "--library", "-o", output, *units, interface],
            timeout=timeout,
        )
    if outputs[0].read_bytes() != outputs[1].read_bytes():
        raise AssertionError("assembly differs after implementation-unit permutation")


def execute_tool_cli_tests(
    stage_bin: pathlib.Path,
    runner: tuple[str, ...],
) -> tuple[int, list[str]]:
    executable = stage_bin / "luna"
    root_usage = "usage: luna <compile|assemble|link> [options]\n"
    cases = (
        (
            "tool-root-help",
            ("--help",),
            0,
            "usage: luna <command> [options]\n"
            "commands:\n"
            "  compile   lower Luna source to assembly\n"
            "  assemble  encode assembly into an object\n"
            "  link      link objects into an executable\n",
            "",
        ),
        ("tool-root-version", ("--version",), 0, "luna (self-hosted Luna 1) 0.1.0\n", ""),
        ("tool-root-missing-command", (), 125, "", root_usage),
        ("tool-root-unknown-command", ("unknown",), 125, "", root_usage),
        (
            "tool-compile-help",
            ("compile", "--help"),
            0,
            "usage: luna compile [--executable|--library] -o <output.s> <source-unit>...\n",
            "",
        ),
        (
            "tool-assemble-help",
            ("assemble", "--help"),
            0,
            "usage: luna assemble [--emit elf|lunaobj] -o <output> <input.s>\n",
            "",
        ),
        (
            "tool-link-help",
            ("link", "--help"),
            0,
            "usage: luna link -o <executable> <input.lo>...\n",
            "",
        ),
    )
    timeout = timeout_seconds(runner)
    passed = 0
    failed: list[str] = []
    for name, arguments, expected_status, expected_stdout, expected_stderr in cases:
        try:
            completed = test_command(
                [*runner, executable, *arguments],
                timeout=timeout,
            )
            actual = (completed.returncode, completed.stdout, completed.stderr)
            expected = (expected_status, expected_stdout, expected_stderr)
            if actual != expected:
                raise AssertionError(f"result {actual!r}, expected {expected!r}")
            passed += 1
            print(f"PASS {name}")
        except Exception as error:  # noqa: BLE001 - report and continue
            failed.append(name)
            print(f"FAIL {name}: {error}")
    return passed, failed


def execute_callable_identity_tests(
    stage_bin: pathlib.Path,
    runner: tuple[str, ...],
) -> tuple[int, list[str]]:
    source_root = ROOT / "tests" / "callable_identity"
    work = ROOT / "out" / "tests" / "callable-identity"
    reset(work)
    timeout = timeout_seconds(runner)
    compiler = tool(stage_bin, "compile", runner)
    assembler = tool(stage_bin, "assemble", runner)
    linker = tool(stage_bin, "link", runner)
    passed = 0
    failed: list[str] = []

    name = "callable-signature-symbols"
    try:
        assembly = work / "signatures.s"
        require_test_success(
            [
                *compiler,
                "--library",
                "-o",
                assembly,
                source_root / "signatures.la",
            ],
            timeout=timeout,
        )
        assembly_text = assembly.read_text(encoding="utf-8")
        module_hex = "ci.s".encode().hex()
        expectation_count = 0
        for line in (source_root / "expectations.txt").read_text(
            encoding="utf-8"
        ).splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            function_name, signature_hex = line.split()
            symbol = (
                f"_L{module_hex}_{function_name.encode().hex()}__{signature_hex}:"
            )
            if symbol not in assembly_text:
                raise AssertionError(f"missing exact symbol {symbol[:-1]}")
            expectation_count += 1
        passed += 1
        print(f"PASS {name} ({expectation_count} exact symbols)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    name = "callable-source-order"
    try:
        require_two_unit_order_identity(
            compiler,
            work,
            "order",
            source_root / "order.lh",
            source_root / "order" / "first.la",
            source_root / "order" / "second.la",
            timeout=timeout,
        )
        passed += 1
        print(f"PASS {name} (byte-identical assembly)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    name = "method-source-order"
    try:
        method_order = source_root / "method_order"
        require_two_unit_order_identity(
            compiler,
            work,
            "methods",
            source_root / "method_order.lh",
            method_order / "alpha.la",
            method_order / "zed.la",
            timeout=timeout,
        )
        passed += 1
        print(f"PASS {name} (byte-identical assembly)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    name = "method-hierarchy-source-order"
    try:
        inheritance_order = source_root / "inheritance_order"
        require_two_unit_order_identity(
            compiler,
            work,
            "inheritance",
            source_root / "inheritance_order.lh",
            inheritance_order / "base.la",
            inheritance_order / "derived.la",
            timeout=timeout,
        )
        passed += 1
        print(f"PASS {name} (byte-identical assembly)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    name = "class-dispatch-shape"
    try:
        assembly = work / "class-dispatch.s"
        require_test_success(
            [
                *compiler,
                "--executable",
                "-o",
                assembly,
                ROOT / "tests" / "cases" / "class_devirtualization.la",
            ],
            timeout=timeout,
        )
        assembly_text = assembly.read_text(encoding="utf-8")
        indirect_call_count = assembly_text.count("    call *%r11\n")
        vtable_slot_count = assembly_text.count("    .quad ")
        if indirect_call_count != 1:
            raise AssertionError(
                f"expected one virtual call, found {indirect_call_count}"
            )
        if vtable_slot_count != 3:
            raise AssertionError(
                f"expected three vtable slots, found {vtable_slot_count}"
            )
        passed += 1
        print(f"PASS {name} (1 indirect call, 3 vtable slots)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    name = "callable-link-identity"
    try:
        mismatch = source_root / "mismatch"
        sources = {
            "caller": ("--executable", mismatch / "caller.la", mismatch / "api_i32.lh"),
            "matching": ("--library", mismatch / "callee_i32.la", mismatch / "api_i32.lh"),
            "different": ("--library", mismatch / "callee_u32.la", mismatch / "api_u32.lh"),
        }
        objects: dict[str, pathlib.Path] = {}
        for source_name, units in sources.items():
            assembly = work / f"{source_name}.s"
            object_file = work / f"{source_name}.lo"
            require_test_success(
                [*compiler, units[0], "-o", assembly, *units[1:]],
                timeout=timeout,
            )
            require_test_success(
                [*assembler, "-o", object_file, assembly],
                timeout=timeout,
            )
            objects[source_name] = object_file
        matching_executable = work / "matching"
        require_test_success(
            [
                *linker,
                "-o",
                matching_executable,
                objects["caller"],
                objects["matching"],
                syscall_object(stage_bin),
            ],
            timeout=timeout,
        )
        completed = test_command(
            [*runner, matching_executable],
            timeout=timeout,
        )
        if completed.returncode != 42:
            raise AssertionError(
                f"matching executable returned {completed.returncode}, expected 42"
            )
        different_executable = work / "different"
        completed = test_command(
            [
                *linker,
                "-o",
                different_executable,
                objects["caller"],
                objects["different"],
                syscall_object(stage_bin),
            ],
            timeout=timeout,
        )
        if completed.returncode != 3:
            raise AssertionError(
                f"mismatched link returned {completed.returncode}, expected 3"
            )
        passed += 1
        print(f"PASS {name} (matching 42, mismatch link:3)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    return passed, failed


def syscall_object(stage_bin: pathlib.Path) -> pathlib.Path:
    """The luna.linux.syscall object from the build step, home of the
    luna_linux_syscallN asm fn stubs (test cases and FFI shims declare them
    extern; the linker no longer injects their definitions). It defines no
    _start, so linking it into every test executable is inert for cases
    that never touch syscalls."""
    return stage_bin.parent / "objects" / "syscall.lo"


def execute_relocation_data_tests(
    stage_bin: pathlib.Path,
    runner: tuple[str, ...],
) -> tuple[int, list[str]]:
    source_root = ROOT / "tests" / "relocation_data"
    work = ROOT / "out" / "tests" / "relocation-data"
    reset(work)
    timeout = timeout_seconds(runner)
    assembler = tool(stage_bin, "assemble", runner)
    linker = tool(stage_bin, "link", runner)
    passed = 0
    failed: list[str] = []

    probes = {
        "readonly-ir-function-address": "ir_codegen.la",
        "readonly-object-relocation-roundtrip": "object_roundtrip.la",
    }
    for name, source_name in probes.items():
        try:
            source = source_root / source_name
            assembly = work / f"{name}.s"
            object_file = work / f"{name}.lo"
            executable = work / name
            require_test_success(
                [
                    *tool(stage_bin, "compile", runner),
                    "--executable",
                    "-o",
                    assembly,
                    *driver_units(source),
                ],
                timeout=timeout,
            )
            require_test_success(
                [*assembler, "-o", object_file, assembly],
                timeout=timeout,
            )
            link_keys = implementation_closure(driver_dependencies(source))
            library_objects = [
                stage_bin.parent / "objects" / f"{key}.lo" for key in link_keys
            ]
            require_test_success(
                [*linker, "-o", executable, object_file, *library_objects],
                timeout=timeout,
            )
            completed = test_command([*runner, executable], timeout=timeout)
            if completed.returncode != 42:
                raise AssertionError(
                    f"relocation probe returned {completed.returncode}, expected 42"
                )
            passed += 1
            print(f"PASS {name} (exit 42)")
        except Exception as error:  # noqa: BLE001 - report and continue
            failed.append(name)
            print(f"FAIL {name}: {error}")

    name = "readonly-function-address-link"
    try:
        objects = (work / "dispatch-first.lo", work / "dispatch-second.lo")
        for output in objects:
            require_test_success(
                [*assembler, "-o", output, source_root / "dispatch.s"],
                timeout=timeout,
            )
        if objects[0].read_bytes() != objects[1].read_bytes():
            raise AssertionError("absolute relocation object is not deterministic")

        elf_object = work / "dispatch.o"
        require_test_success(
            [
                *assembler,
                "--emit",
                "elf",
                "-o",
                elf_object,
                source_root / "dispatch.s",
            ],
            timeout=timeout,
        )
        executable = work / "dispatch"
        require_test_success(
            [*linker, "-o", executable, elf_object],
            timeout=timeout,
        )
        completed = test_command([*runner, executable], timeout=timeout)
        if completed.returncode != 42:
            raise AssertionError(
                f"dispatch executable returned {completed.returncode}, expected 42"
            )
        passed += 1
        print(f"PASS {name} (deterministic LUNAOBJ1, ELF round-trip, exit 42)")
    except Exception as error:  # noqa: BLE001 - report and continue
        failed.append(name)
        print(f"FAIL {name}: {error}")

    malformed = {
        "readonly-function-address-expression": ("fail_expression.s", 3),
        "readonly-function-address-section": ("fail_section.s", 2),
        "readonly-function-address-unknown": ("fail_unknown.s", 2),
    }
    for name, (source_name, expected_line) in malformed.items():
        try:
            completed = test_command(
                [
                    *assembler,
                    "-o",
                    work / f"{name}.lo",
                    source_root / source_name,
                ],
                timeout=timeout,
            )
            if completed.returncode != 2:
                raise AssertionError(
                    f"luna assemble returned {completed.returncode}, expected 2"
                )
            expected_error = f"assembler:{expected_line}\n"
            if completed.stderr != expected_error:
                raise AssertionError(
                    f"stderr {completed.stderr!r}, expected {expected_error!r}"
                )
            passed += 1
            print(f"PASS {name} (rejected at line {expected_line})")
        except Exception as error:  # noqa: BLE001 - report and continue
            failed.append(name)
            print(f"FAIL {name}: {error}")

    return passed, failed


def execute_ffi_tests(
    stage_bin: pathlib.Path,
    runner: tuple[str, ...],
) -> tuple[int, list[str], int]:
    """Link tests/ffi cases against the checked-in ELF64 fixture objects.

    Each expectation line is `<case>.la <fixture>.o <exit>`, or
    `<case>.la <fixture>.o link:<status>` when `luna link` itself must fail
    with the given exit status (malformed or unresolvable fixtures). Fixtures
    not checked in (fixture.o) are built from C sources with an x86-64 C
    compiler; without one their cases are skipped, not failed.

    Two more shapes exercise the `luna assemble --emit elf` writer:
    `<case>.la elf-link <fixture>.o <exit>` (or link:<status>) assembles the
    case to a standard ELF64 ET_REL instead of LUNAOBJ1, then links with
    `luna link` exactly as above — a writer -> reader -> linker round-trip;
    `<case>.la host-link <cmain>.c <exit>` compiles the case with
    `luna compile --library`, assembles it with `--emit elf` and links the object
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
            mode, fixture, expected = "link", parts[1], parts[2]
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
                        *tool(stage_bin, "compile", runner),
                        "--library",
                        "-o",
                        assembly,
                        *ffi_units(ffi, name),
                    ],
                    timeout=timeout,
                )
                run(
                    [
                        *tool(stage_bin, "assemble", runner),
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
            object_file = work / f"{name}.lo" if mode == "link" else work / f"{name}.o"
            run(
                [
                    *tool(stage_bin, "compile", runner),
                    "--executable",
                    "-o",
                    assembly,
                    *ffi_units(ffi, name),
                ],
                timeout=timeout,
            )
            assemble_command = [*tool(stage_bin, "assemble", runner)]
            if mode == "elf-link":
                assemble_command += ["--emit", "elf"]
            run(
                [*assemble_command, "-o", object_file, assembly],
                timeout=timeout,
            )
            linked = subprocess.run(
                [
                    *tool(stage_bin, "link", runner),
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
                        f"luna link exit {linked.returncode}, expected {wanted}"
                    )
            else:
                if linked.returncode != 0:
                    raise AssertionError(
                        f"luna link exit {linked.returncode}, expected 0"
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


class ExpectationExecutor:
    def __init__(
        self,
        stage_bin: pathlib.Path,
        runner: tuple[str, ...],
        cases: pathlib.Path,
        kinds: dict[str, int],
        timeout: int,
    ) -> None:
        self._stage_bin = stage_bin
        self._runner = runner
        self._cases = cases
        self._kinds = kinds
        self._timeout = timeout

    def execute_case(self, expectation: TestExpectation) -> ExpectationTaskResult:
        name = expectation.name
        work = ROOT / "out" / "tests" / f"{expectation.index:04d}-{name.removesuffix('.la')}"
        reset(work)
        assembly = work / f"{name}.s"
        try:
            if expectation.diagnostic_kind is not None:
                self._compile_failure(expectation, assembly)
                message = f"PASS {name} (FAIL {expectation.diagnostic_kind})"
            else:
                self._compile_and_run_case(expectation, work, assembly)
                message = f"PASS {name} ({expectation.expected_exit})"
            return ExpectationTaskResult(expectation.index, 1, (), (message,))
        except Exception as error:  # noqa: BLE001 - return one deterministic failure record
            return ExpectationTaskResult(
                expectation.index,
                0,
                (name,),
                (f"FAIL {name}: {error}",),
            )

    def execute_suite(self, suite: TestSuitePlan) -> ExpectationTaskResult:
        try:
            self._compile_and_run_suite(suite)
            message = f"PASS suite-{suite.identifier:02d} ({len(suite.cases)} expectations)"
            return ExpectationTaskResult(
                suite.first_expectation_index,
                len(suite.cases),
                (),
                (message,),
            )
        except Exception as suite_error:  # noqa: BLE001 - isolate only the failed suite
            messages = (f"FALLBACK suite-{suite.identifier:02d}: {suite_error}",)
            results = tuple(self.execute_case(case.expectation) for case in suite.cases)
            return ExpectationTaskResult(
                suite.first_expectation_index,
                sum(result.passed for result in results),
                tuple(name for result in results for name in result.failed),
                messages + tuple(message for result in results for message in result.messages),
            )

    def _compile_failure(self, expectation: TestExpectation, assembly: pathlib.Path) -> None:
        kind = expectation.diagnostic_kind
        if kind not in self._kinds:
            raise AssertionError(f"unknown diagnostic kind {kind}")
        command = [
            *tool(self._stage_bin, "compile", self._runner),
            "--executable",
            "-o",
            assembly,
            *expectation_units(
                self._cases,
                expectation.name,
                list(expectation.unit_specification),
            ),
        ]
        completed = self._execute(command)
        expected_kind = self._kinds[kind]
        expected_status = SEMANTIC_DIAGNOSTIC_BASE + expected_kind
        if completed.returncode != expected_status:
            raise AssertionError(
                f"compile exit {completed.returncode}, expected {expected_status} ({kind}); "
                f"stderr={completed.stderr.strip()!r}"
            )
        if not completed.stderr.startswith(f"semantic:{expected_kind}:"):
            raise AssertionError(
                f"stderr {completed.stderr.strip()!r}, expected leading semantic:{expected_kind}:"
            )

    def _compile_and_run_case(
        self,
        expectation: TestExpectation,
        work: pathlib.Path,
        assembly: pathlib.Path,
    ) -> None:
        object_file = work / f"{expectation.name}.lo"
        executable = work / expectation.name.removesuffix(".la")
        units = expectation_units(
            self._cases,
            expectation.name,
            list(expectation.unit_specification),
        )
        self._compile_assemble_link(assembly, object_file, executable, units)
        completed = self._execute([*self._runner, executable])
        if completed.returncode != expectation.expected_exit:
            raise AssertionError(
                f"exit {completed.returncode}, expected {expectation.expected_exit}"
            )

    def _compile_and_run_suite(self, suite: TestSuitePlan) -> None:
        work = ROOT / "out" / "tests" / "suites" / f"suite-{suite.identifier:02d}"
        reset(work)
        units = self._materialize_suite(suite, work)
        assembly = work / "suite.s"
        object_file = work / "suite.lo"
        executable = work / "suite"
        self._compile_assemble_link(assembly, object_file, executable, units)
        completed = self._execute([*self._runner, executable])
        if completed.returncode == 0:
            return
        failed_case = self._suite_failure_name(suite, completed.returncode)
        raise AssertionError(f"{failed_case} returned suite status {completed.returncode}")

    def _compile_assemble_link(
        self,
        assembly: pathlib.Path,
        object_file: pathlib.Path,
        executable: pathlib.Path,
        units: list[pathlib.Path],
    ) -> None:
        commands = (
            [
                *tool(self._stage_bin, "compile", self._runner),
                "--executable",
                "-o",
                assembly,
                *units,
            ],
            [*tool(self._stage_bin, "assemble", self._runner), "-o", object_file, assembly],
            [
                *tool(self._stage_bin, "link", self._runner),
                "-o",
                executable,
                object_file,
                syscall_object(self._stage_bin),
            ],
        )
        for command in commands:
            completed = self._execute(command)
            if completed.returncode != 0:
                printable = " ".join(str(part) for part in command)
                raise AssertionError(
                    f"command returned {completed.returncode}: {printable}; "
                    f"stderr={completed.stderr.strip()!r}"
                )

    def _materialize_suite(
        self,
        suite: TestSuitePlan,
        work: pathlib.Path,
    ) -> list[pathlib.Path]:
        sources = work / "sources"
        sources.mkdir()
        assertions: list[str] = []
        for ordinal, case in enumerate(suite.cases, start=1):
            assertions.append(
                f"    suite.EXPECT_EXIT({case.entry_name}(), "
                f"{case.expectation.expected_exit}, {ordinal});"
            )

        driver = sources / "suite.la"
        bodies = tuple(case.body for case in suite.cases)
        driver.write_text(
            "\n".join(
                (
                    f"module tests.suites.suite_{suite.identifier:02d};",
                    "",
                    "import tests.modules.framework::{TestSuite};",
                    "",
                    *bodies,
                    "",
                    "fn main() -> i32 {",
                    "    var suite: TestSuite = TestSuite();",
                    *assertions,
                    "    return suite.result();",
                    "}",
                    "",
                )
            ),
            encoding="utf-8",
        )
        framework = ROOT / "tests" / "modules" / "framework"
        return [
            driver,
            framework.with_suffix(".la"),
            framework.with_suffix(".lh"),
        ]

    def _execute(
        self,
        command: list[str | pathlib.Path],
    ) -> subprocess.CompletedProcess[str]:
        printable = " ".join(str(part) for part in command)
        try:
            return subprocess.run(
                [str(part) for part in command],
                timeout=self._timeout,
                capture_output=True,
                text=True,
            )
        except subprocess.TimeoutExpired as error:
            raise AssertionError(
                f"command timed out after {self._timeout}s: {printable}"
            ) from error

    @staticmethod
    def _suite_failure_name(suite: TestSuitePlan, status: int) -> str:
        if status < 1 or status > len(suite.cases):
            return "suite executable"
        return suite.cases[status - 1].expectation.name


def execute_tests(stage_bin: pathlib.Path, runner: tuple[str, ...], jobs: int) -> int:
    expectations = ROOT / "tests" / "expectations.txt"
    cases = ROOT / "tests" / "cases"
    kinds = semantic_diagnostic_kinds()
    timeout = timeout_seconds(runner)
    passed = 0
    failed: list[str] = []
    cli_passed, cli_failed = execute_tool_cli_tests(stage_bin, runner)
    passed += cli_passed
    failed.extend(cli_failed)
    expectation_lines = tuple(
        line
        for line in expectations.read_text(encoding="utf-8").splitlines()
        if line.strip()
    )
    parsed_expectations = tuple(
        TestExpectation.parse(index, line)
        for index, line in enumerate(expectation_lines)
    )
    suites, isolated = TestSuitePlanner(cases).plan(parsed_expectations)
    executor_service = ExpectationExecutor(stage_bin, runner, cases, kinds, timeout)
    task_count = len(suites) + len(isolated)
    worker_count = min(jobs, task_count)
    suite_case_count = sum(len(suite.cases) for suite in suites)
    print(
        f"running {len(suites)} suites ({suite_case_count} expectations) and "
        f"{len(isolated)} isolated expectations with {worker_count} jobs"
    )
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as executor:
        futures = [executor.submit(executor_service.execute_suite, suite) for suite in suites]
        futures.extend(
            executor.submit(executor_service.execute_case, expectation)
            for expectation in isolated
        )
        results = sorted(
            (future.result() for future in futures),
            key=lambda result: result.order,
        )
        for result in results:
            for message in result.messages:
                print(message)
            passed += result.passed
            failed.extend(result.failed)
    identity_passed, identity_failed = execute_callable_identity_tests(
        stage_bin,
        runner,
    )
    passed += identity_passed
    failed.extend(identity_failed)
    relocation_passed, relocation_failed = execute_relocation_data_tests(
        stage_bin,
        runner,
    )
    passed += relocation_passed
    failed.extend(relocation_failed)
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
        help="output root containing stage-transition, stage-next and stage-fixed",
    )

    test_parser = subparsers.add_parser("test", help="run behavior tests")
    add_common(test_parser)
    test_parser.add_argument("--stage", type=pathlib.Path)
    test_parser.add_argument(
        "--jobs",
        type=int,
        default=4,
        help="parallel expectation workers (default: 4)",
    )

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
        transition_out = verify_root / "stage-transition"
        next_out = verify_root / "stage-next"
        fixed_out = verify_root / "stage-fixed"
        verify_anchor(arguments.anchor)
        build_stage(arguments.anchor, transition_out, runner)
        print("building stage-next with the transition tools")
        build_stage(transition_out / "bin", next_out, runner)
        print("confirming the fixed point with stage-next")
        build_stage(next_out / "bin", fixed_out, runner)
        compare_stages(next_out, fixed_out)
        print("FIXED POINT: all artifacts byte-identical")
    else:
        if arguments.jobs <= 0:
            fail("--jobs must be positive")
        stage = arguments.stage or default_out / "stage-next" / "bin"
        if not toolchain_is_available(stage):
            fail(f"no built toolchain under {stage}; run 'build' first")
        raise SystemExit(execute_tests(stage, runner, arguments.jobs))


if __name__ == "__main__":
    main()
