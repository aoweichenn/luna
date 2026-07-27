#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import platform
import shutil
import struct
import subprocess
import sys


def run(
    command: list[str],
    *,
    expected_code: int = 0,
    timeout: int = 20,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if result.returncode != expected_code:
        rendered = " ".join(command)
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected_code}: "
            f"{rendered}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        print(f"SKIP: required integration tool is missing: {name}")
        raise SystemExit(77)
    return path


def require_target_runner() -> list[str]:
    qemu = shutil.which("qemu-x86_64-static")
    if qemu is not None:
        return [qemu]
    if platform.system() == "Linux" and platform.machine().lower() in (
        "x86_64",
        "amd64",
    ):
        return []
    print("SKIP: qemu-x86_64-static is required on this host")
    raise SystemExit(77)


def compile_and_run(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    target_runner: list[str],
    source: pathlib.Path,
    work_dir: pathlib.Path,
    expected_code: int,
    additional_objects: tuple[pathlib.Path, ...] = (),
    additional_source_units: tuple[pathlib.Path, ...] = (),
) -> None:
    stem = source.stem
    assembly = work_dir / f"{stem}.s"
    object_file = work_dir / f"{stem}.o"
    executable = work_dir / stem

    run(
        [
            str(compiler),
            "--emit",
            "asm",
            "-o",
            str(assembly),
            str(source),
            *(str(path) for path in additional_source_units),
        ]
    )
    run(
        [
            llvm_mc,
            "--triple=x86_64-unknown-linux-gnu",
            "--filetype=obj",
            "-o",
            str(object_file),
            str(assembly),
        ]
    )
    run(
        [
            linker,
            "-static",
            "-e",
            "_start",
            "-o",
            str(executable),
            str(object_file),
            *(str(path) for path in additional_objects),
        ]
    )
    run([*target_runner, str(executable)], expected_code=expected_code)


def assemble(
    llvm_mc: str,
    assembly: pathlib.Path,
    object_file: pathlib.Path,
) -> None:
    run(
        [
            llvm_mc,
            "--triple=x86_64-unknown-linux-gnu",
            "--filetype=obj",
            "-o",
            str(object_file),
            str(assembly),
        ]
    )


def metadata_fingerprint(payload: bytes, language_abi: int) -> int:
    fingerprint = 14695981039346656037
    for byte in b"LUNALMI\0" + struct.pack("<I", language_abi) + payload:
        fingerprint ^= byte
        fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
    return fingerprint


def compile_separate_module_graph(
    compiler: pathlib.Path,
    llvm_mc: str,
    linker: str,
    target_runner: list[str],
    case_dir: pathlib.Path,
    work_dir: pathlib.Path,
) -> None:
    core_interface = case_dir / "metadata_core_interface.luna"
    core_implementation = case_dir / "metadata_core_implementation.luna"
    math_interface = case_dir / "metadata_math_interface.luna"
    math_implementation = case_dir / "metadata_math_implementation.luna"
    application = case_dir / "metadata_app.luna"

    core_metadata = work_dir / "metadata_core.lmi"
    core_metadata_reordered = work_dir / "metadata_core_reordered.lmi"
    core_assembly = work_dir / "metadata_core.s"
    core_object = work_dir / "metadata_core.o"
    math_metadata = work_dir / "metadata_math.lmi"
    math_assembly = work_dir / "metadata_math.s"
    math_ir = work_dir / "metadata_math.lir"
    math_mir = work_dir / "metadata_math.mir"
    math_abi = work_dir / "metadata_math.abi"
    math_liveness = work_dir / "metadata_math.live"
    math_allocation = work_dir / "metadata_math.alloc"
    math_object = work_dir / "metadata_math.o"
    application_assembly = work_dir / "metadata_app.s"
    application_object = work_dir / "metadata_app.o"
    executable = work_dir / "metadata_app"

    for output, sources in (
        (core_metadata, (core_interface, core_implementation)),
        (core_metadata_reordered, (core_implementation, core_interface)),
    ):
        run(
            [
                str(compiler),
                "--compile-module",
                "tests.metadata.core",
                "--emit",
                "metadata",
                "-o",
                str(output),
                *(str(source) for source in sources),
            ]
        )
    if core_metadata.read_bytes() != core_metadata_reordered.read_bytes():
        raise AssertionError("module metadata depends on source input order")

    metadata_bytes = core_metadata.read_bytes()
    if metadata_bytes[:8] != b"LUNALMI\0":
        raise AssertionError("module metadata magic is invalid")
    major, minor, language_abi = struct.unpack_from("<HHI", metadata_bytes, 8)
    if (major, minor, language_abi) != (1, 0, 1):
        raise AssertionError("module metadata version header is invalid")

    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.core",
            "--emit",
            "asm",
            "-o",
            str(core_assembly),
            str(core_metadata),
            str(core_implementation),
        ]
    )
    core_text = core_assembly.read_text(encoding="utf-8")
    if "_start:" in core_text or ".globl _L" not in core_text:
        raise AssertionError(
            "separately compiled library has an invalid symbol boundary"
        )
    if "_H" not in core_text:
        raise AssertionError(
            "separately compiled export lacks its metadata identity"
        )
    assemble(llvm_mc, core_assembly, core_object)

    source_root_codegen = run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.core",
            "--emit",
            "asm",
            "-o",
            str(work_dir / "metadata_core_from_source.s"),
            str(core_interface),
            str(core_implementation),
        ],
        expected_code=1,
    )
    if "requires its compiled .lmi interface" not in (
        source_root_codegen.stderr
    ):
        raise AssertionError(
            "separate source-interface code generation was not rejected"
        )

    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "metadata",
            "-o",
            str(math_metadata),
            str(math_interface),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "abi",
            "-o",
            str(math_abi),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "liveness",
            "-o",
            str(math_liveness),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "allocation",
            "-o",
            str(math_allocation),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "asm",
            "-o",
            str(math_assembly),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "ir",
            "-o",
            str(math_ir),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "mir",
            "-o",
            str(math_mir),
            str(math_metadata),
            str(math_implementation),
            str(core_metadata),
        ]
    )
    math_ir_text = math_ir.read_text(encoding="utf-8")
    if (
        "import fn @tests.metadata.core::add_bias" not in math_ir_text
        or "export fn @calculate" not in math_ir_text
        or math_ir_text.count("[metadata 0x") != 2
    ):
        raise AssertionError("separate module IR linkage is missing")
    math_mir_text = math_mir.read_text(encoding="utf-8")
    if (
        "declare @f0 tests.metadata.core::add_bias" not in math_mir_text
        or "define @f1 tests.metadata.math::calculate" not in math_mir_text
        or "module-kind library" not in math_mir_text
        or math_mir_text.count("metadata=0x") != 2
    ):
        raise AssertionError("separate module machine IR linkage is missing")
    math_abi_text = math_abi.read_text(encoding="utf-8")
    if (
        "declare @f0 tests.metadata.core::add_bias parameters=7 "
        "gp=6 sse=0 stack-bytes=8 call-frame=16"
        not in math_abi_text
        or "p6 type=i32 class=integer location=stack[0]"
        not in math_abi_text
        or "p0 type=aggregate[4,4] pieces=[integer:%rdi@0+4]"
        not in math_abi_text
        or "return type=aggregate[4,4] pieces=[integer:%rax@0+4]"
        not in math_abi_text
        or "define @f1 tests.metadata.math::calculate"
        not in math_abi_text
    ):
        raise AssertionError("separate module ABI linkage is missing")
    math_liveness_text = math_liveness.read_text(encoding="utf-8")
    if (
        "declare @f0 tests.metadata.core::add_bias values=0 iterations=0"
        not in math_liveness_text
        or "define @f1 tests.metadata.math::calculate"
        not in math_liveness_text
    ):
        raise AssertionError("separate module liveness linkage is missing")
    math_allocation_text = math_allocation.read_text(encoding="utf-8")
    if (
        "declare @f0 tests.metadata.core::add_bias values=0 instructions=0"
        not in math_allocation_text
        or "define @f1 tests.metadata.math::calculate"
        not in math_allocation_text
    ):
        raise AssertionError(
            "separate module register allocation linkage is missing"
        )
    math_text = math_assembly.read_text(encoding="utf-8")
    if (
        "_start:" in math_text
        or ".extern _L" not in math_text
        or math_text.count("_H") < 3
    ):
        raise AssertionError(
            "separately compiled dependent module has invalid assembly"
        )
    assemble(llvm_mc, math_assembly, math_object)

    run(
        [
            str(compiler),
            "--emit",
            "asm",
            "-o",
            str(application_assembly),
            str(application),
            str(math_metadata),
            str(core_metadata),
        ]
    )
    application_text = application_assembly.read_text(encoding="utf-8")
    if "_start:" not in application_text or ".extern _L" not in application_text:
        raise AssertionError("metadata-backed executable has invalid assembly")
    assemble(llvm_mc, application_assembly, application_object)
    run(
        [
            linker,
            "-static",
            "-e",
            "_start",
            "-o",
            str(executable),
            str(application_object),
            str(math_object),
            str(core_object),
        ]
    )
    run([*target_runner, str(executable)], expected_code=42)

    missing_dependency = run(
        [
            str(compiler),
            "--emit",
            "check",
            str(application),
            str(math_metadata),
        ],
        expected_code=1,
    )
    if "was not supplied to this compilation" not in missing_dependency.stderr:
        raise AssertionError("missing transitive metadata diagnostic is absent")

    source_dependency = run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.math",
            "--emit",
            "check",
            str(math_interface),
            str(math_implementation),
            str(core_interface),
            str(core_implementation),
        ],
        expected_code=1,
    )
    if "requires dependency 'tests.metadata.core' as compiled metadata" not in (
        source_dependency.stderr
    ):
        raise AssertionError("source dependency rejection is missing")

    corrupted_bytes = bytearray(core_metadata.read_bytes())
    corrupted_bytes[-1] ^= 1
    corrupted_metadata = work_dir / "metadata_core_corrupted.lmi"
    corrupted_metadata.write_bytes(corrupted_bytes)
    corrupted_result = run(
        [
            str(compiler),
            "--emit",
            "check",
            str(application),
            str(math_metadata),
            str(corrupted_metadata),
        ],
        expected_code=1,
    )
    if "payload checksum mismatch" not in corrupted_result.stderr:
        raise AssertionError("corrupt metadata checksum diagnostic is missing")

    version_bytes = bytearray(core_metadata.read_bytes())
    struct.pack_into("<H", version_bytes, 8, 2)
    version_metadata = work_dir / "metadata_core_version.lmi"
    version_metadata.write_bytes(version_bytes)
    version_result = run(
        [
            str(compiler),
            "--emit",
            "check",
            str(application),
            str(math_metadata),
            str(version_metadata),
        ],
        expected_code=1,
    )
    if "unsupported module metadata format" not in version_result.stderr:
        raise AssertionError("metadata version diagnostic is missing")

    target_bytes = bytearray(core_metadata.read_bytes())
    encoded_target = b"x86_64-unknown-linux-gnu"
    target_offset = target_bytes.find(encoded_target, 32)
    if target_offset < 0:
        raise AssertionError("module metadata target triple is missing")
    target_bytes[target_offset : target_offset + len(encoded_target)] = (
        b"x86_64-unknown-linux-mus"
    )
    payload = bytes(target_bytes[32:])
    struct.pack_into(
        "<Q",
        target_bytes,
        24,
        metadata_fingerprint(payload, language_abi),
    )
    target_metadata = work_dir / "metadata_core_target.lmi"
    target_metadata.write_bytes(target_bytes)
    target_result = run(
        [
            str(compiler),
            "--emit",
            "check",
            str(application),
            str(math_metadata),
            str(target_metadata),
        ],
        expected_code=1,
    )
    if "compilation target is 'x86_64-unknown-linux-gnu'" not in (
        target_result.stderr
    ):
        raise AssertionError("metadata target mismatch diagnostic is missing")

    changed_interface = work_dir / "metadata_core_changed_interface.luna"
    changed_interface.write_text(
        core_interface.read_text(encoding="utf-8").replace(
            "    value: i32;\n",
            "    value: i32;\n    generation: i32;\n",
        ),
        encoding="utf-8",
    )
    changed_metadata = work_dir / "metadata_core_changed.lmi"
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.core",
            "--emit",
            "metadata",
            "-o",
            str(changed_metadata),
            str(changed_interface),
            str(core_implementation),
        ]
    )
    stale_result = run(
        [
            str(compiler),
            "--emit",
            "check",
            str(application),
            str(math_metadata),
            str(changed_metadata),
        ],
        expected_code=1,
    )
    if "was built against different metadata" not in stale_result.stderr:
        raise AssertionError("stale dependency metadata diagnostic is missing")

    changed_assembly = work_dir / "metadata_core_changed.s"
    changed_object = work_dir / "metadata_core_changed.o"
    run(
        [
            str(compiler),
            "--compile-module",
            "tests.metadata.core",
            "--emit",
            "asm",
            "-o",
            str(changed_assembly),
            str(changed_metadata),
            str(core_implementation),
        ]
    )
    assemble(llvm_mc, changed_assembly, changed_object)
    incompatible_link = run(
        [
            linker,
            "-static",
            "-e",
            "_start",
            "-o",
            str(work_dir / "metadata_app_incompatible"),
            str(application_object),
            str(math_object),
            str(changed_object),
        ],
        expected_code=1,
    )
    if "undefined symbol" not in incompatible_link.stderr:
        raise AssertionError(
            "linker did not report the incompatible module object"
        )


def compile_external_support(
    clang: str,
    source_root: pathlib.Path,
    work_dir: pathlib.Path,
) -> pathlib.Path:
    source = (
        source_root
        / "tests"
        / "integration"
        / "support"
        / "external_functions.c"
    )
    object_file = work_dir / "external_functions.o"
    run(
        [
            clang,
            "--target=x86_64-unknown-linux-gnu",
            "-std=c23",
            "-O2",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-pic",
            "-fno-pie",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-Wconversion",
            "-Wsign-conversion",
            "-Wstrict-prototypes",
            "-Wmissing-prototypes",
            "-c",
            str(source),
            "-o",
            str(object_file),
        ]
    )
    return object_file


def convert_integer(
    value: int,
    source_width: int,
    source_signed: bool,
    target_width: int,
    target_signed: bool,
) -> int:
    source_modulus = 2**source_width
    source_bits = value % source_modulus
    if target_width > source_width and source_signed:
        source_value = (
            source_bits - source_modulus
            if source_bits >= 2 ** (source_width - 1)
            else source_bits
        )
        target_bits = source_value % (2**target_width)
    else:
        target_bits = source_bits % (2**target_width)

    if target_signed and target_bits >= 2 ** (target_width - 1):
        return target_bits - 2**target_width
    return target_bits


def generate_integer_conversion_matrix(work_dir: pathlib.Path) -> pathlib.Path:
    integer_types = (
        ("i8", 8, True, -85),
        ("i16", 16, True, -21846),
        ("i32", 32, True, -1431655766),
        ("i64", 64, True, -6148914691236517206),
        ("isize", 64, True, -6148914691236517206),
        ("u8", 8, False, 171),
        ("u16", 16, False, 43690),
        ("u32", 32, False, 2863311530),
        ("u64", 64, False, 12297829382473034410),
        ("usize", 64, False, 12297829382473034410),
    )
    lines = ["module test.all_integer_conversions;", ""]
    for source_name, _, _, _ in integer_types:
        for target_name, _, _, _ in integer_types:
            lines.extend(
                (
                    f"fn convert_{source_name}_to_{target_name}("
                    f"value: {source_name}) -> {target_name} {{",
                    f"    return value as {target_name};",
                    "}",
                    "",
                )
            )

    lines.append("fn main() -> i32 {")
    for (
        source_name,
        source_width,
        source_signed,
        source_value,
    ) in integer_types:
        for target_name, target_width, target_signed, _ in integer_types:
            expected = convert_integer(
                source_value,
                source_width,
                source_signed,
                target_width,
                target_signed,
            )
            lines.extend(
                (
                    f"    if (convert_{source_name}_to_{target_name}("
                    f"{source_value}) != {expected}) {{",
                    "        return 1;",
                    "    }",
                )
            )
    lines.extend(("    return 42;", "}", ""))

    source = work_dir / "all_integer_conversions.luna"
    source.write_text("\n".join(lines), encoding="utf-8")
    return source


def round_f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def format_float_literal(value: float, type_name: str) -> str:
    precision = 9 if type_name == "f32" else 17
    text = format(value, f".{precision}g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text


def generate_scalar_conversion_matrix(work_dir: pathlib.Path) -> pathlib.Path:
    integer_types = (
        ("i8", -85),
        ("i16", -21846),
        ("i32", -1431655766),
        ("i64", -6148914691236517206),
        ("isize", -6148914691236517206),
        ("u8", 171),
        ("u16", 43690),
        ("u32", 2863311530),
        ("u64", 12297829382473034410),
        ("usize", 12297829382473034410),
    )
    float_types = ("f32", "f64")
    lines = ["module test.all_scalar_conversions;", ""]

    for source_name, _ in integer_types:
        for target_name in float_types:
            lines.extend(
                (
                    f"fn convert_{source_name}_to_{target_name}("
                    f"value: {source_name}) -> {target_name} {{",
                    f"    return value as {target_name};",
                    "}",
                    "",
                )
            )
    for source_name in float_types:
        for target_name, _ in integer_types:
            lines.extend(
                (
                    f"fn convert_{source_name}_to_{target_name}("
                    f"value: {source_name}) -> {target_name} {{",
                    f"    return value as {target_name};",
                    "}",
                    "",
                )
            )
    lines.extend(
        (
            "fn convert_f32_to_f64(value: f32) -> f64 {",
            "    return value as f64;",
            "}",
            "",
            "fn convert_f64_to_f32(value: f64) -> f32 {",
            "    return value as f32;",
            "}",
            "",
            "fn main() -> i32 {",
        )
    )

    for source_name, source_value in integer_types:
        for target_name in float_types:
            expected = float(source_value)
            if target_name == "f32":
                expected = round_f32(expected)
            expected_literal = format_float_literal(expected, target_name)
            lines.extend(
                (
                    f"    if (convert_{source_name}_to_{target_name}("
                    f"{source_value}) != {expected_literal}) {{",
                    "        return 1;",
                    "    }",
                )
            )
    for source_name in float_types:
        for target_name, _ in integer_types:
            source_value = -42.75 if target_name.startswith("i") else 42.75
            expected = -42 if source_value < 0 else 42
            lines.extend(
                (
                    f"    if (convert_{source_name}_to_{target_name}("
                    f"{source_value}) != {expected}) {{",
                    "        return 1;",
                    "    }",
                )
            )
    lines.extend(
        (
            "    if (convert_f32_to_f64(1.5) != 1.5) { return 1; }",
            "    if (convert_f64_to_f32(1.5) != 1.5) { return 1; }",
            "    return 42;",
            "}",
            "",
        )
    )

    source = work_dir / "all_scalar_conversions.luna"
    source.write_text("\n".join(lines), encoding="utf-8")
    return source


def generate_conditional_matrix(work_dir: pathlib.Path) -> pathlib.Path:
    scalar_types = (
        ("bool", "true", "false"),
        ("i8", "-85", "42"),
        ("i16", "-21846", "12345"),
        ("i32", "-1431655766", "1431655765"),
        ("i64", "-6148914691236517206", "6148914691236517205"),
        ("isize", "-4096", "8192"),
        ("u8", "171", "85"),
        ("u16", "43690", "21845"),
        ("u32", "2863311530", "1431655765"),
        ("u64", "12297829382473034410", "6148914691236517205"),
        ("usize", "8192", "4096"),
        ("f32", "1.25", "-2.5"),
        ("f64", "9007199254740992.0", "-0.125"),
    )
    lines = ["module test.all_conditional_types;", ""]
    for type_name, _, _ in scalar_types:
        lines.extend(
            (
                f"fn select_{type_name}(condition: bool, left: {type_name}, "
                f"right: {type_name}) -> {type_name} {{",
                "    return condition ? left : right;",
                "}",
                "",
            )
        )

    lines.append("fn main() -> i32 {")
    for type_name, left_value, right_value in scalar_types:
        lines.extend(
            (
                f"    if (select_{type_name}(true, {left_value}, "
                f"{right_value}) != {left_value}) {{ return 1; }}",
                f"    if (select_{type_name}(false, {left_value}, "
                f"{right_value}) != {right_value}) {{ return 1; }}",
            )
        )
    lines.extend(("    return 42;", "}", ""))

    source = work_dir / "all_conditional_types.luna"
    source.write_text("\n".join(lines), encoding="utf-8")
    return source


def generate_switch_matrix(work_dir: pathlib.Path) -> pathlib.Path:
    integer_types = (
        ("i8", "-128", "127", "42"),
        ("i16", "-32768", "32767", "42"),
        ("i32", "-2147483648", "2147483647", "42"),
        (
            "i64",
            "-9223372036854775808",
            "9223372036854775807",
            "42",
        ),
        (
            "isize",
            "-9223372036854775808",
            "9223372036854775807",
            "42",
        ),
        ("u8", "0", "255", "42"),
        ("u16", "0", "65535", "42"),
        ("u32", "0", "4294967295", "42"),
        ("u64", "0", "18446744073709551615", "42"),
        ("usize", "0", "18446744073709551615", "42"),
    )
    lines = ["module test.all_switch_integer_types;", ""]
    for type_name, minimum, maximum, other in integer_types:
        maximum_label = (
            "-1" if type_name.startswith("u") else maximum
        )
        lines.extend(
            (
                f"fn classify_{type_name}(value: {type_name}) -> i32 {{",
                "    switch (value) {",
                f"        case {minimum} {{ return 1; }}",
                f"        case {maximum_label} {{ return 2; }}",
                "        default { return 3; }",
                "    }",
                "}",
                "",
            )
        )

    lines.append("fn main() -> i32 {")
    for type_name, minimum, maximum, other in integer_types:
        lines.extend(
            (
                f"    if (classify_{type_name}({minimum}) != 1) "
                "{ return 1; }",
                f"    if (classify_{type_name}({maximum}) != 2) "
                "{ return 1; }",
                f"    if (classify_{type_name}({other}) != 3) "
                "{ return 1; }",
            )
        )
    lines.extend(("    return 42;", "}", ""))

    source = work_dir / "all_switch_integer_types.luna"
    source.write_text("\n".join(lines), encoding="utf-8")
    return source


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    llvm_mc = require_tool("llvm-mc")
    linker = require_tool("ld.lld")
    clang = require_tool("clang")
    target_runner = require_target_runner()

    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    case_dir = arguments.source_root / "tests" / "integration" / "cases"
    external_support = compile_external_support(
        clang, arguments.source_root, arguments.work_dir
    )

    help_result = run([str(arguments.compiler), "--help"])
    if "usage: lunac" not in help_result.stdout:
        raise AssertionError("--help did not print the compiler usage")
    if "x86_64-unknown-linux-gnu" not in help_result.stdout:
        raise AssertionError("--help did not list the supported target")
    if "transitive module source" not in help_result.stdout:
        raise AssertionError("--help did not describe module source inputs")
    if "--compile-module" not in help_result.stdout or ".lmi" not in (
        help_result.stdout
    ):
        raise AssertionError("--help did not describe separate compilation")
    if "--emit mir" not in help_result.stdout:
        raise AssertionError("--help did not describe machine IR emission")
    if "--emit abi" not in help_result.stdout:
        raise AssertionError("--help did not describe ABI emission")
    if "--emit liveness" not in help_result.stdout:
        raise AssertionError("--help did not describe liveness emission")
    if "--emit allocation" not in help_result.stdout:
        raise AssertionError(
            "--help did not describe register allocation emission"
        )
    version_result = run([str(arguments.compiler), "--version"])
    if "lunac 0.1.0-dev" not in version_result.stdout:
        raise AssertionError("--version did not print the compiler version")
    run([str(arguments.compiler)], expected_code=2)
    run(
        [str(arguments.compiler), "--emit", "invalid", "input.luna"],
        expected_code=2,
    )
    run([str(arguments.compiler), "--target"], expected_code=2)
    run([str(arguments.compiler), "--compile-module"], expected_code=2)
    metadata_without_module = run(
        [
            str(arguments.compiler),
            "--emit",
            "metadata",
            str(case_dir / "module_pair_interface.luna"),
            str(case_dir / "module_pair_implementation.luna"),
        ],
        expected_code=1,
    )
    if "metadata emission requires --compile-module" not in (
        metadata_without_module.stderr
    ):
        raise AssertionError("metadata mode diagnostic is missing")
    unsupported_target = run(
        [
            str(arguments.compiler),
            "--target",
            "aarch64-unknown-linux-gnu",
            str(case_dir / "return_42.luna"),
        ],
        expected_code=2,
    )
    if "unsupported target" not in unsupported_target.stderr:
        raise AssertionError("unsupported target diagnostic is missing")
    run(
        [
            str(arguments.compiler),
            "--target",
            "x86_64-unknown-linux-gnu",
            "--emit",
            "check",
            str(case_dir / "return_42.luna"),
        ]
    )
    run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(case_dir / "does_not_exist.luna"),
        ],
        expected_code=1,
    )
    duplicate_implementation = run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(case_dir / "return_42.luna"),
            str(case_dir / "return_42.luna"),
        ],
        expected_code=1,
    )
    if (
        "more than one implementation unit"
        not in duplicate_implementation.stderr
    ):
        raise AssertionError("duplicate implementation diagnostic is missing")
    duplicate_interface = run(
        [
            str(arguments.compiler),
            "--emit",
            "check",
            str(case_dir / "module_pair_interface.luna"),
            str(case_dir / "module_pair_interface.luna"),
        ],
        expected_code=1,
    )
    if "more than one interface unit" not in duplicate_interface.stderr:
        raise AssertionError("duplicate interface diagnostic is missing")
    print("PASS compiler command-line contract")

    executable_cases = {
        "return_42.luna": 42,
        "arithmetic.luna": 42,
        "function_call.luna": 42,
        "if_else.luna": 17,
        "while_loop.luna": 45,
        "short_circuit.luna": 42,
        "nested_call.luna": 42,
        "recursive_factorial.luna": 120,
        "break_continue.luna": 25,
        "six_arguments.luna": 21,
        "stack_arguments.luna": 42,
        "too_many_float_arguments.luna": 42,
        "signed_arithmetic.luna": 36,
        "defined_i32_semantics.luna": 42,
        "defined_i64_semantics.luna": 42,
        "division_by_zero.luna": -8,
        "division_overflow.luna": -8,
        "void_call.luna": 42,
        "bool_call.luna": 42,
        "i64_operations.luna": 42,
        "i64_boundaries.luna": 42,
        "i64_six_arguments.luna": 42,
        "i64_recursive_factorial.luna": 42,
        "i64_division_by_zero.luna": -8,
        "i64_division_overflow.luna": -8,
        "mixed_width_arguments.luna": 42,
        "fixed_width_arguments.luna": 42,
        "integer_conversions.luna": 42,
        "conversion_round_trip.luna": 255,
        "unsigned_operations.luna": 42,
        "unsigned_conversions.luna": 42,
        "remaining_integer_conversions.luna": 42,
        "narrow_integer_operations.luna": 42,
        "i8_division_by_zero.luna": -8,
        "i16_division_by_zero.luna": -8,
        "u8_division_by_zero.luna": -8,
        "u16_division_by_zero.luna": -8,
        "i8_division_overflow.luna": -8,
        "i16_remainder_overflow.luna": -8,
        "u32_division_by_zero.luna": -8,
        "u64_division_by_zero.luna": -8,
        "pointer_sized_integer_operations.luna": 42,
        "isize_division_by_zero.luna": -8,
        "isize_division_overflow.luna": -8,
        "isize_remainder_overflow.luna": -8,
        "usize_division_by_zero.luna": -8,
        "floating_operations.luna": 42,
        "floating_literals.luna": 42,
        "scalar_conversions.luna": 42,
        "structured_control_flow.luna": 42,
        "memory_operations.luna": 42,
        "memory_control_flow.luna": 42,
        "memory_scalar_matrix.luna": 42,
        "zero_initializer_scalars.luna": 42,
        "aggregate_types.luna": 42,
        "aggregate_initialization.luna": 42,
        "aggregate_assignment.luna": 42,
        "aggregate_by_value.luna": 42,
        "aggregate_parameter.luna": 0,
        "aggregate_return.luna": 0,
        "array_parameter.luna": 0,
        "array_return.luna": 0,
        "array_whole_assignment.luna": 42,
        "memory_copy_overlap.luna": 42,
        "null_dereference.luna": -4,
        "null_pointer_index.luna": -4,
        "null_pointer_write.luna": -4,
        "array_out_of_bounds.luna": -4,
        "array_out_of_bounds_write.luna": -4,
        "float_to_integer_nan_trap.luna": -4,
        "float_to_integer_infinity_trap.luna": -4,
        "float_to_integer_signed_range_trap.luna": -4,
        "float_to_integer_unsigned_range_trap.luna": -4,
        "float_to_i64_upper_trap.luna": -4,
        "float_to_u64_upper_trap.luna": -4,
        "float_to_i8_lower_trap.luna": -4,
    }

    for case_name, expected_code in executable_cases.items():
        compile_and_run(
            arguments.compiler,
            llvm_mc,
            linker,
            target_runner,
            case_dir / case_name,
            arguments.work_dir,
            expected_code,
        )
        print(f"PASS executable: {case_name}")

    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        case_dir / "external_c_abi.luna",
        arguments.work_dir,
        42,
        (external_support,),
    )
    print("PASS executable: external_c_abi.luna with a real C23 object")

    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        case_dir / "aggregate_c_abi.luna",
        arguments.work_dir,
        42,
        (external_support,),
    )
    print("PASS executable: aggregate_c_abi.luna with C23 layout assertions")

    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        case_dir / "module_pair_implementation.luna",
        arguments.work_dir,
        42,
        additional_objects=(external_support,),
        additional_source_units=(case_dir / "module_pair_interface.luna",),
    )
    print("PASS executable: matched module interface and implementation")

    module_import_sources = (
        case_dir / "module_import_math_implementation.luna",
        case_dir / "module_import_core_interface.luna",
        case_dir / "module_import_math_interface.luna",
        case_dir / "module_import_core_implementation.luna",
    )
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        case_dir / "module_import_app.luna",
        arguments.work_dir,
        42,
        additional_source_units=module_import_sources,
    )
    print("PASS executable: transitive module imports and exported types")

    compile_separate_module_graph(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        case_dir,
        arguments.work_dir,
    )
    print("PASS separate compilation: versioned .lmi dependency graph")

    conversion_matrix = generate_integer_conversion_matrix(arguments.work_dir)
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        conversion_matrix,
        arguments.work_dir,
        42,
    )
    print("PASS executable: all 100 integer conversion pairs")

    scalar_conversion_matrix = generate_scalar_conversion_matrix(
        arguments.work_dir
    )
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        scalar_conversion_matrix,
        arguments.work_dir,
        42,
    )
    print("PASS executable: all 42 remaining scalar conversion pairs")

    conditional_matrix = generate_conditional_matrix(arguments.work_dir)
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        conditional_matrix,
        arguments.work_dir,
        42,
    )
    print("PASS executable: conditional operator for all scalar types")

    switch_matrix = generate_switch_matrix(arguments.work_dir)
    compile_and_run(
        arguments.compiler,
        llvm_mc,
        linker,
        target_runner,
        switch_matrix,
        arguments.work_dir,
        42,
    )
    print("PASS executable: switch boundaries for all integer types")

    negative_cases = {
        "type_error.luna": "expected bool, found i32",
        "immutable_assignment.luna": "cannot assign to immutable local",
        "missing_return.luna": "not every path",
        "parse_error.luna": "expected ';'",
        "unknown_function.luna": "unknown function 'missing'",
        "wrong_arity.luna": "expects 1 arguments, found 2",
        "duplicate_local.luna": "duplicate local variable 'answer'",
        "break_outside_loop.luna": (
            "break is only valid inside a loop or switch"
        ),
        "integer_overflow.luna": "integer literal does not fit in i32",
        "module_interface_pending.luna": (
            "requires a matching implementation unit"
        ),
        "import_pending.luna": "was not supplied to this compilation",
        "duplicate_function.luna": "duplicate function 'main'",
        "unreachable_type_error.luna": "expected bool, found i32",
        "void_local.luna": "local variables cannot have type void",
        "wrong_return_type.luna": "expected i32, found bool",
        "continue_outside_loop.luna": "continue is only valid inside a loop",
        "duplicate_parameter.luna": "duplicate parameter 'value'",
        "i64_positive_overflow.luna": "integer literal does not fit in i64",
        "i64_mixed_types.luna": "expected i64, found i32",
        "u32_positive_overflow.luna": "integer literal does not fit in u32",
        "i8_positive_overflow.luna": "integer literal does not fit in i8",
        "i16_positive_overflow.luna": "integer literal does not fit in i16",
        "u8_positive_overflow.luna": "integer literal does not fit in u8",
        "u16_positive_overflow.luna": "integer literal does not fit in u16",
        "narrow_mixed_types.luna": "expected i8, found u8",
        "signed_unsigned_mixed.luna": "expected u64, found i64",
        "invalid_bool_conversion.luna": (
            "explicit conversion requires numeric source and target types"
        ),
        "invalid_void_conversion.luna": (
            "explicit conversion requires numeric source and target types"
        ),
        "isize_positive_overflow.luna": (
            "integer literal does not fit in isize"
        ),
        "pointer_sized_mixed_types.luna": "expected isize, found i64",
        "f32_literal_overflow.luna": (
            "floating-point literal does not fit in f32"
        ),
        "f64_literal_overflow.luna": (
            "floating-point literal does not fit in f64"
        ),
        "float_mixed_types.luna": "expected f32, found f64",
        "float_integer_mixed.luna": "expected f64, found i32",
        "invalid_float_operator.luna": (
            "is not defined for floating-point operands"
        ),
        "invalid_float_conversion.luna": (
            "explicit conversion requires numeric source and target types"
        ),
        "malformed_float_literal.luna": "invalid floating-point literal",
        "conditional_condition_error.luna": "expected bool, found i32",
        "conditional_type_error.luna": "expected i32, found bool",
        "conditional_numeric_type_error.luna": "expected i32, found i64",
        "do_condition_error.luna": "expected bool, found i32",
        "for_condition_error.luna": "expected bool, found i32",
        "for_scope_error.luna": "unknown local variable 'index'",
        "switch_type_error.luna": (
            "switch expression requires an integer type"
        ),
        "switch_duplicate_case.luna": "duplicate switch case value",
        "switch_duplicate_default.luna": (
            "switch has more than one default arm"
        ),
        "switch_label_overflow.luna": (
            "switch case label does not fit in i8"
        ),
        "continue_in_switch_without_loop.luna": (
            "continue is only valid inside a loop"
        ),
        "invalid_switch_label.luna": (
            "expected integer literal as switch case label"
        ),
        "readonly_pointer_write.luna": (
            "cannot assign through an immutable lvalue"
        ),
        "readonly_pointer_cast.luna": (
            "pointer conversion cannot remove read-only qualification"
        ),
        "nested_readonly_pointer_cast.luna": (
            "pointer conversion cannot remove read-only qualification"
        ),
        "readonly_pointer_void_laundering.luna": (
            "pointer conversion cannot remove read-only qualification"
        ),
        "readonly_pointer_array_laundering.luna": (
            "pointer conversion cannot remove read-only qualification"
        ),
        "pointer_type_mismatch.luna": (
            "expected *u32, found *i32"
        ),
        "invalid_pointer_integer_cast.luna": (
            "or a pointer and usize"
        ),
        "invalid_array_index_type.luna": (
            "expected usize, found i32"
        ),
        "zero_length_array.luna": (
            "fixed-array length must be positive"
        ),
        "void_element_array.luna": (
            "fixed-array element type cannot be void"
        ),
        "array_scalar_use.luna": (
            "fixed arrays are not scalar values"
        ),
        "void_pointer_dereference.luna": (
            "cannot dereference a pointer to void"
        ),
        "invalid_string_escape.luna": (
            "invalid escape in string literal"
        ),
        "external_body.luna": (
            "external function declaration must end with ';'"
        ),
        "plain_function_declaration.luna": (
            "implementation function 'pending' must have a body"
        ),
        "external_array_parameter.luna": (
            "fixed arrays have no by-value external C parameter ABI"
        ),
        "external_array_return.luna": (
            "fixed arrays have no by-value external C return ABI"
        ),
        "external_void_parameter.luna": (
            "parameter 'value' has an invalid type"
        ),
        "external_reserved_start.luna": (
            "external function name '_start' is reserved"
        ),
        "external_reserved_mangled.luna": (
            "external function name '_Lreserved' is reserved"
        ),
        "external_main.luna": (
            "bootstrap entry point 'main' must be defined in Luna"
        ),
        "external_duplicate_definition.luna": (
            "duplicate function 'c_value'"
        ),
        "duplicate_type_declaration.luna": (
            "duplicate type declaration 'Item'"
        ),
        "duplicate_aggregate_field.luna": "duplicate field 'value'",
        "unknown_named_type.luna": "unknown type 'Missing'",
        "recursive_aggregate.luna": "contains itself by value",
        "aggregate_layout_overflow.luna": (
            "field 'bytes' has no valid target layout"
        ),
        "invalid_enum_underlying.luna": (
            "enum underlying type must be a built-in integer type"
        ),
        "enum_value_overflow.luna": (
            "enum member value does not fit in u8"
        ),
        "enum_implicit_integer.luna": "expected Kind, found i32",
        "enum_switch_label_type.luna": (
            "enum switch case label must be a member"
        ),
        "unknown_aggregate_field.luna": "has no field named 'missing'",
        "readonly_aggregate_field.luna": (
            "cannot assign through an immutable lvalue"
        ),
        "aggregate_initializer.luna": (
            "aggregate initialization requires braces"
        ),
        "aggregate_initializer_duplicate.luna": (
            "duplicate initializer field 'value'"
        ),
        "aggregate_initializer_unknown_field.luna": (
            "type 'Item' has no field named 'missing'"
        ),
        "union_initializer_multiple_fields.luna": (
            "union initializer may name at most one field"
        ),
        "aggregate_initializer_field_type.luna": (
            "expected i32, found bool"
        ),
        "aggregate_copy_type_mismatch.luna": (
            "expected Right, found Left"
        ),
        "aggregate_compound_assignment.luna": (
            "compound assignment requires a scalar numeric type"
        ),
        "array_named_initializer.luna": (
            "named aggregate initializer requires a struct or union context"
        ),
        "array_initializer_scalar.luna": (
            "fixed-array initialization requires '{}'"
        ),
        "immutable_aggregate_assignment.luna": (
            "cannot assign to immutable local 'left'"
        ),
        "enum_type_mismatch.luna": "expected Left, found Right",
        "invalid_sizeof_type.luna": (
            "layout query requires a type with a valid target layout"
        ),
        "invalid_offsetof_type.luna": (
            "offsetof requires a struct or union type"
        ),
        "unknown_offsetof_field.luna": (
            "offsetof names an unknown field 'missing'"
        ),
        "enum_arithmetic.luna": (
            "arithmetic and bitwise operators require numeric operands"
        ),
        "enum_conversion_underlying.luna": (
            "enum conversion requires the enum's exact underlying integer type"
        ),
        "enum_duplicate_switch_value.luna": (
            "duplicate switch case value"
        ),
    }

    for case_name, expected_diagnostic in negative_cases.items():
        result = run(
            [
                str(arguments.compiler),
                "--emit",
                "check",
                str(case_dir / case_name),
            ],
            expected_code=1,
        )
        if expected_diagnostic not in result.stderr:
            raise AssertionError(
                f"{case_name}: expected diagnostic "
                f"{expected_diagnostic!r}\nstderr:\n{result.stderr}"
            )
        print(f"PASS negative: {case_name}")

    module_pair_negative_cases = (
        (
            "module_pair_name_mismatch_interface.luna",
            "module_pair_name_mismatch_implementation.luna",
            "requires a matching implementation unit",
        ),
        (
            "module_pair_missing_interface.luna",
            "module_pair_missing_implementation.luna",
            "has no implementation definition",
        ),
        (
            "module_pair_signature_interface.luna",
            "module_pair_signature_implementation.luna",
            "does not match the interface declaration",
        ),
        (
            "module_pair_interface_body_interface.luna",
            "module_pair_interface_body_implementation.luna",
            "must be a declaration without a body",
        ),
        (
            "module_pair_export_interface.luna",
            "module_pair_export_implementation.luna",
            "'export' is only allowed",
        ),
        (
            "module_pair_duplicate_type_interface.luna",
            "module_pair_duplicate_type_implementation.luna",
            "duplicate type declaration 'Item'",
        ),
        (
            "module_pair_external_interface.luna",
            "module_pair_external_implementation.luna",
            "is external in the interface",
        ),
        (
            "module_pair_private_type_interface.luna",
            "module_pair_private_type_implementation.luna",
            "unknown type 'Hidden'",
        ),
    )
    for interface_name, implementation_name, expected_diagnostic in (
        module_pair_negative_cases
    ):
        result = run(
            [
                str(arguments.compiler),
                "--emit",
                "check",
                str(case_dir / implementation_name),
                str(case_dir / interface_name),
            ],
            expected_code=1,
        )
        if expected_diagnostic not in result.stderr:
            raise AssertionError(
                f"{interface_name} + {implementation_name}: expected "
                f"{expected_diagnostic!r}\nstderr:\n{result.stderr}"
            )
        print(f"PASS module-pair negative: {interface_name}")

    module_import_negative_cases = (
        (
            (
                "module_import_cycle_left_implementation.luna",
                "module_import_cycle_right_interface.luna",
                "module_import_cycle_left_interface.luna",
                "module_import_cycle_right_implementation.luna",
            ),
            "import cycle detected",
        ),
        (
            (
                "module_import_private_app.luna",
                "module_import_private_interface.luna",
                "module_import_private_implementation.luna",
            ),
            "unknown function 'hidden'",
        ),
        (
            (
                "module_import_duplicate_app_implementation.luna",
                "module_import_core_implementation.luna",
                "module_import_duplicate_app_interface.luna",
                "module_import_core_interface.luna",
            ),
            "imports module 'tests.imports.core' more than once",
        ),
        (
            (
                "module_import_unreachable_library.luna",
                "module_import_unreachable_app.luna",
            ),
            "is not reachable from executable root",
        ),
    )
    for source_names, expected_diagnostic in module_import_negative_cases:
        result = run(
            [
                str(arguments.compiler),
                "--emit",
                "check",
                *(str(case_dir / name) for name in source_names),
            ],
            expected_code=1,
        )
        if expected_diagnostic not in result.stderr:
            raise AssertionError(
                f"{source_names}: expected {expected_diagnostic!r}\n"
                f"stderr:\n{result.stderr}"
            )
        print(f"PASS module-import negative: {source_names[0]}")

    for snapshot_name in (
        "function_call",
        "i64_six_arguments",
        "conversion_round_trip",
        "unsigned_conversions",
        "narrow_ir",
        "pointer_sized_integer_ir",
        "floating_ir",
        "scalar_conversion_ir",
        "structured_control_flow_ir",
        "memory_ir",
        "external_ir",
        "aggregate_ir",
        "aggregate_assignment",
    ):
        ir_output = arguments.work_dir / f"{snapshot_name}.lir"
        run(
            [
                str(arguments.compiler),
                "--emit",
                "ir",
                "-o",
                str(ir_output),
                str(case_dir / f"{snapshot_name}.luna"),
            ]
        )
        expected_ir = (
            arguments.source_root
            / "tests"
            / "integration"
            / "golden"
            / f"{snapshot_name}.lir"
        ).read_text(encoding="utf-8")
        actual_ir = ir_output.read_text(encoding="utf-8")
        if actual_ir.rstrip("\n") != expected_ir.rstrip("\n"):
            raise AssertionError(
                f"IR snapshot mismatch for {snapshot_name}.luna\n"
                f"expected:\n{expected_ir}\nactual:\n{actual_ir}"
            )
        print(f"PASS IR snapshot: {snapshot_name}.luna")

    module_pair_ir = arguments.work_dir / "module_pair.lir"
    run(
        [
            str(arguments.compiler),
            "--emit",
            "ir",
            "-o",
            str(module_pair_ir),
            str(case_dir / "module_pair_interface.luna"),
            str(case_dir / "module_pair_implementation.luna"),
        ]
    )
    expected_module_pair_ir = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "module_pair.lir"
    ).read_text(encoding="utf-8")
    actual_module_pair_ir = module_pair_ir.read_text(encoding="utf-8")
    if actual_module_pair_ir.rstrip("\n") != expected_module_pair_ir.rstrip(
        "\n"
    ):
        raise AssertionError(
            "IR snapshot mismatch for paired module\n"
            f"expected:\n{expected_module_pair_ir}\n"
            f"actual:\n{actual_module_pair_ir}"
        )
    print("PASS IR snapshot: matched module interface and implementation")

    module_import_ir = arguments.work_dir / "module_import.lir"
    run(
        [
            str(arguments.compiler),
            "--emit",
            "ir",
            "-o",
            str(module_import_ir),
            str(case_dir / "module_import_app.luna"),
            *(str(path) for path in module_import_sources),
        ]
    )
    expected_module_import_ir = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "module_import.lir"
    ).read_text(encoding="utf-8")
    actual_module_import_ir = module_import_ir.read_text(encoding="utf-8")
    if actual_module_import_ir.rstrip(
        "\n"
    ) != expected_module_import_ir.rstrip("\n"):
        raise AssertionError(
            "IR snapshot mismatch for imported module graph\n"
            f"expected:\n{expected_module_import_ir}\n"
            f"actual:\n{actual_module_import_ir}"
        )
    print("PASS IR snapshot: imported module graph")

    machine_ir_first = arguments.work_dir / "function_call_first.mir"
    machine_ir_second = arguments.work_dir / "function_call_second.mir"
    for output in (machine_ir_first, machine_ir_second):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "mir",
                "-o",
                str(output),
                str(case_dir / "function_call.luna"),
            ]
        )
    expected_machine_ir = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "function_call.mir"
    ).read_text(encoding="utf-8")
    actual_machine_ir = machine_ir_first.read_text(encoding="utf-8")
    if actual_machine_ir.rstrip("\n") != expected_machine_ir.rstrip("\n"):
        raise AssertionError(
            "machine IR snapshot mismatch for function_call.luna\n"
            f"expected:\n{expected_machine_ir}\nactual:\n{actual_machine_ir}"
        )
    if machine_ir_first.read_bytes() != machine_ir_second.read_bytes():
        raise AssertionError("machine IR output is not deterministic")
    print("PASS machine IR snapshot and determinism: function_call.luna")

    abi_first = arguments.work_dir / "stack_arguments_first.abi"
    abi_second = arguments.work_dir / "stack_arguments_second.abi"
    for output in (abi_first, abi_second):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "abi",
                "-o",
                str(output),
                str(case_dir / "stack_arguments.luna"),
            ]
        )
    expected_abi = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "stack_arguments.abi"
    ).read_text(encoding="utf-8")
    actual_abi = abi_first.read_text(encoding="utf-8")
    if actual_abi.rstrip("\n") != expected_abi.rstrip("\n"):
        raise AssertionError(
            "ABI snapshot mismatch for stack_arguments.luna\n"
            f"expected:\n{expected_abi}\nactual:\n{actual_abi}"
        )
    if abi_first.read_bytes() != abi_second.read_bytes():
        raise AssertionError("ABI output is not deterministic")
    print("PASS ABI snapshot and determinism: stack_arguments.luna")

    liveness_first = arguments.work_dir / "function_call_first.live"
    liveness_second = arguments.work_dir / "function_call_second.live"
    for output in (liveness_first, liveness_second):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "liveness",
                "-o",
                str(output),
                str(case_dir / "function_call.luna"),
            ]
        )
    expected_liveness = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "function_call.live"
    ).read_text(encoding="utf-8")
    actual_liveness = liveness_first.read_text(encoding="utf-8")
    if actual_liveness.rstrip("\n") != expected_liveness.rstrip("\n"):
        raise AssertionError(
            "liveness snapshot mismatch for function_call.luna\n"
            f"expected:\n{expected_liveness}\nactual:\n{actual_liveness}"
        )
    if liveness_first.read_bytes() != liveness_second.read_bytes():
        raise AssertionError("liveness output is not deterministic")
    print("PASS liveness snapshot and determinism: function_call.luna")

    allocation_first = arguments.work_dir / "function_call_first.alloc"
    allocation_second = arguments.work_dir / "function_call_second.alloc"
    for output in (allocation_first, allocation_second):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "allocation",
                "-o",
                str(output),
                str(case_dir / "function_call.luna"),
            ]
        )
    expected_allocation = (
        arguments.source_root
        / "tests"
        / "integration"
        / "golden"
        / "function_call.alloc"
    ).read_text(encoding="utf-8")
    actual_allocation = allocation_first.read_text(encoding="utf-8")
    if actual_allocation.rstrip("\n") != expected_allocation.rstrip("\n"):
        raise AssertionError(
            "register allocation snapshot mismatch for function_call.luna\n"
            f"expected:\n{expected_allocation}\nactual:\n{actual_allocation}"
        )
    if allocation_first.read_bytes() != allocation_second.read_bytes():
        raise AssertionError("register allocation output is not deterministic")
    print(
        "PASS register allocation snapshot and determinism: "
        "function_call.luna"
    )

    for deterministic_name in (
        "recursive_factorial",
        "i64_operations",
        "unsigned_operations",
        "narrow_integer_operations",
        "pointer_sized_integer_operations",
        "floating_operations",
        "scalar_conversions",
        "structured_control_flow",
        "memory_operations",
        "external_c_abi",
        "aggregate_initialization",
        "memory_copy_overlap",
    ):
        deterministic_first = (
            arguments.work_dir / f"{deterministic_name}_first.s"
        )
        deterministic_second = (
            arguments.work_dir / f"{deterministic_name}_second.s"
        )
        for output in (deterministic_first, deterministic_second):
            run(
                [
                    str(arguments.compiler),
                    "--emit",
                    "asm",
                    "-o",
                    str(output),
                    str(case_dir / f"{deterministic_name}.luna"),
                ]
            )
        if deterministic_first.read_bytes() != deterministic_second.read_bytes():
            raise AssertionError(
                f"assembly output is not deterministic: {deterministic_name}"
            )
        print(f"PASS deterministic assembly: {deterministic_name}.luna")

    module_pair_first = arguments.work_dir / "module_pair_first.s"
    module_pair_second = arguments.work_dir / "module_pair_second.s"
    for output, source_units in (
        (
            module_pair_first,
            (
                case_dir / "module_pair_interface.luna",
                case_dir / "module_pair_implementation.luna",
            ),
        ),
        (
            module_pair_second,
            (
                case_dir / "module_pair_implementation.luna",
                case_dir / "module_pair_interface.luna",
            ),
        ),
    ):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "asm",
                "-o",
                str(output),
                *(str(path) for path in source_units),
            ]
        )
    if module_pair_first.read_bytes() != module_pair_second.read_bytes():
        raise AssertionError(
            "paired-module assembly depends on source-unit argument order"
        )
    print("PASS deterministic assembly: paired module in either CLI order")

    module_import_first = arguments.work_dir / "module_import_first.s"
    module_import_second = arguments.work_dir / "module_import_second.s"
    module_import_units = (
        case_dir / "module_import_app.luna",
        *module_import_sources,
    )
    for output, source_units in (
        (module_import_first, module_import_units),
        (module_import_second, tuple(reversed(module_import_units))),
    ):
        run(
            [
                str(arguments.compiler),
                "--emit",
                "asm",
                "-o",
                str(output),
                *(str(path) for path in source_units),
            ]
        )
    if module_import_first.read_bytes() != module_import_second.read_bytes():
        raise AssertionError(
            "imported-module assembly depends on source-unit argument order"
        )
    print("PASS deterministic assembly: imported module graph in any CLI order")

    print("all integration tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
