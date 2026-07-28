#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import io
import json
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from collections.abc import Iterable


SEED_FORMAT = "LUNA-BOOTSTRAP-SEED/1"
SEED_PROJECT = "luna"
SEED_TARGET = "x86_64-unknown-linux-gnu"
SEED_ARCHIVE_PREFIX = "luna-bootstrap-seed"
SEED_MANIFEST_PATH = "manifest.json"
SEED_MAX_ARCHIVE_BYTES = 268_435_456
SEED_MAX_FILE_BYTES = 67_108_864
SEED_MAX_PAYLOAD_BYTES = 201_326_592
SEED_MAX_MEMBERS = 128
SEED_TAR_BLOCK_BYTES = 512
SEED_TAR_RECORD_BYTES = 10_240
SEED_DIRECTORY_MODE = 0o755
SEED_REGULAR_MODE = 0o644
SEED_EXECUTABLE_MODE = 0o755
SEED_ELF_HEADER_BYTES = 64
SEED_ELF_PROGRAM_HEADER_BYTES = 56
SEED_ELF_MACHINE_X86_64 = 62
SEED_ELF_TYPE_EXECUTABLE = 2
SEED_ELF_PROGRAM_LOAD = 1
SEED_ELF_PROGRAM_DYNAMIC = 2
SEED_ELF_PROGRAM_INTERPRETER = 3
SEED_ELF_FLAG_EXECUTE = 1
SEED_NATIVE_TIMEOUT_SECONDS = 300
SEED_EMULATED_TIMEOUT_SECONDS = 1200

SEED_TOOL_NAMES = ("lunac", "luna-as", "luna-link")
SEED_LIBRARY_KEYS = (
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
SEED_INTERFACE_KEYS = ("syscall", *SEED_LIBRARY_KEYS)

SEED_MODULE_STEMS = {
    "syscall": "runtime/luna/linux/syscall",
    "runtime": "runtime/luna/runtime",
    "memory": "runtime/luna/std/memory",
    "bytes": "runtime/luna/std/bytes",
    "text": "runtime/luna/std/text",
    "path": "runtime/luna/std/path",
    "io": "runtime/luna/std/io",
    "bootstrap_lexer": "runtime/luna/bootstrap/frontend/lexer",
    "bootstrap_parser": "runtime/luna/bootstrap/frontend/parser",
    "bootstrap_type": (
        "runtime/luna/bootstrap/middleend/type/type"
    ),
    "bootstrap_ir": "runtime/luna/bootstrap/middleend/ir/ir",
    "bootstrap_sema": "runtime/luna/bootstrap/middleend/sema/sema",
    "bootstrap_x86_64_text": (
        "runtime/luna/bootstrap/backend/x86_64/text/text"
    ),
    "bootstrap_x86_64_abi": (
        "runtime/luna/bootstrap/backend/x86_64/abi/abi"
    ),
    "bootstrap_x86_64_frame": (
        "runtime/luna/bootstrap/backend/x86_64/frame/frame"
    ),
    "bootstrap_x86_64_codegen": (
        "runtime/luna/bootstrap/backend/x86_64/codegen/codegen"
    ),
    "bootstrap_x86_64_object": (
        "runtime/luna/bootstrap/backend/x86_64/object/object"
    ),
    "bootstrap_x86_64_assembler": (
        "runtime/luna/bootstrap/backend/x86_64/assembler/assembler"
    ),
    "bootstrap_x86_64_linker": (
        "runtime/luna/bootstrap/backend/x86_64/linker/linker"
    ),
}

SEED_MODULE_DEPENDENCIES = {
    "syscall": (),
    "runtime": ("syscall",),
    "memory": ("runtime",),
    "bytes": ("runtime", "memory"),
    "text": ("runtime", "bytes"),
    "path": ("runtime", "bytes", "text"),
    "io": ("runtime", "bytes", "text", "path"),
    "bootstrap_lexer": ("runtime", "bytes", "text"),
    "bootstrap_parser": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
    ),
    "bootstrap_type": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
    ),
    "bootstrap_ir": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
        "bootstrap_type",
    ),
    "bootstrap_sema": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
        "bootstrap_parser",
        "bootstrap_type",
        "bootstrap_ir",
    ),
    "bootstrap_x86_64_text": ("runtime", "bytes", "text"),
    "bootstrap_x86_64_abi": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
        "bootstrap_type",
        "bootstrap_ir",
    ),
    "bootstrap_x86_64_frame": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
        "bootstrap_type",
        "bootstrap_ir",
        "bootstrap_x86_64_abi",
    ),
    "bootstrap_x86_64_codegen": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_lexer",
        "bootstrap_parser",
        "bootstrap_type",
        "bootstrap_ir",
        "bootstrap_sema",
        "bootstrap_x86_64_text",
        "bootstrap_x86_64_abi",
        "bootstrap_x86_64_frame",
    ),
    "bootstrap_x86_64_object": ("runtime", "bytes", "text"),
    "bootstrap_x86_64_assembler": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_x86_64_object",
    ),
    "bootstrap_x86_64_linker": (
        "runtime",
        "bytes",
        "text",
        "bootstrap_x86_64_object",
        "bootstrap_x86_64_assembler",
    ),
}

SEED_DRIVER_SOURCES = {
    "lunac": "tools/bootstrap/stage_compiler.luna",
    "luna-as": "tools/bootstrap/stage_assembler.luna",
    "luna-link": "tools/bootstrap/stage_linker.luna",
}

SEED_DRIVER_DEPENDENCIES = {
    "lunac": (
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
    ),
    "luna-as": (
        "runtime",
        "bytes",
        "text",
        "path",
        "io",
        "bootstrap_x86_64_object",
        "bootstrap_x86_64_assembler",
    ),
    "luna-link": (
        "runtime",
        "bytes",
        "text",
        "path",
        "io",
        "bootstrap_x86_64_text",
        "bootstrap_x86_64_object",
        "bootstrap_x86_64_linker",
    ),
}

SEED_VERSION_PATTERN = re.compile(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?"
)
SEED_TARGET_PATTERN = re.compile(r"[0-9A-Za-z_]+(?:-[0-9A-Za-z_]+){2,3}")
SEED_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


class SeedError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class SeedPayload:
    path: str
    data: bytes
    mode: int
    kind: str


@dataclasses.dataclass(frozen=True)
class VerifiedSeed:
    archive: pathlib.Path
    archive_bytes: bytes
    archive_sha256: str
    root: str
    version: str
    target: str
    manifest_bytes: bytes
    payloads: tuple[SeedPayload, ...]

    def payload(self, path: str) -> SeedPayload:
        for payload in self.payloads:
            if payload.path == path:
                return payload
        raise SeedError(f"manifest does not contain required payload: {path}")

    def extract(self, destination: pathlib.Path) -> pathlib.Path:
        if destination.exists():
            raise SeedError(f"extraction destination already exists: {destination}")
        destination.mkdir(parents=True)
        root = destination / self.root
        try:
            root.mkdir()
            manifest = root / SEED_MANIFEST_PATH
            manifest.write_bytes(self.manifest_bytes)
            manifest.chmod(SEED_REGULAR_MODE)
            for payload in self.payloads:
                output = root / pathlib.PurePosixPath(payload.path)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(payload.data)
                output.chmod(payload.mode)
        except BaseException:
            shutil.rmtree(destination)
            raise
        return root


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_version(version: str) -> None:
    if SEED_VERSION_PATTERN.fullmatch(version) is None:
        raise SeedError(f"invalid project version: {version!r}")


def validate_target(target: str) -> None:
    if (
        SEED_TARGET_PATTERN.fullmatch(target) is None
        or target != SEED_TARGET
    ):
        raise SeedError(f"invalid seed target: {target!r}")


def archive_root(version: str, target: str) -> str:
    validate_version(version)
    validate_target(target)
    return f"{SEED_ARCHIVE_PREFIX}-{version}-{target}"


def archive_name(version: str, target: str) -> str:
    return f"{archive_root(version, target)}.tar"


def validate_payload_path(path: str) -> None:
    pure_path = pathlib.PurePosixPath(path)
    try:
        path.encode("ascii")
    except UnicodeEncodeError as error:
        raise SeedError(f"non-ASCII seed payload path: {path!r}") from error
    if (
        not path
        or path.startswith("/")
        or "\\" in path
        or pure_path.as_posix() != path
        or any(part in ("", ".", "..") for part in pure_path.parts)
    ):
        raise SeedError(f"unsafe seed payload path: {path!r}")


def inventory_digest(payloads: Iterable[SeedPayload], kind: str) -> str:
    digest = hashlib.sha256()
    for payload in sorted(payloads, key=lambda item: item.path):
        if payload.kind != kind:
            continue
        digest.update(payload.path.encode("ascii"))
        digest.update(b"\0")
        digest.update(str(len(payload.data)).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_bytes(payload.data).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def manifest_object(
    version: str,
    target: str,
    payloads: tuple[SeedPayload, ...],
) -> dict[str, object]:
    files = [
        {
            "kind": payload.kind,
            "mode": payload.mode,
            "path": payload.path,
            "sha256": sha256_bytes(payload.data),
            "size": len(payload.data),
        }
        for payload in sorted(payloads, key=lambda item: item.path)
    ]
    return {
        "archive_root": archive_root(version, target),
        "files": files,
        "format": SEED_FORMAT,
        "project": SEED_PROJECT,
        "source_tree_sha256": inventory_digest(payloads, "source"),
        "target": target,
        "toolchain_sha256": inventory_digest(payloads, "tool"),
        "version": version,
    }


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        )
        + "\n"
    ).encode("ascii")


def canonical_tar_info(
    name: str,
    mode: int,
    *,
    directory: bool,
) -> tarfile.TarInfo:
    information = tarfile.TarInfo(name)
    information.uid = 0
    information.gid = 0
    information.uname = ""
    information.gname = ""
    information.mtime = 0
    information.mode = mode
    information.type = tarfile.DIRTYPE if directory else tarfile.REGTYPE
    information.size = 0
    return information


def build_archive_bytes(
    version: str,
    target: str,
    payloads: tuple[SeedPayload, ...],
) -> bytes:
    root = archive_root(version, target)
    manifest = canonical_json(manifest_object(version, target, payloads))
    directories = {root}
    for relative_path in (
        SEED_MANIFEST_PATH,
        *(payload.path for payload in payloads),
    ):
        parent = pathlib.PurePosixPath(root, relative_path).parent
        while parent.as_posix() != ".":
            directories.add(parent.as_posix())
            if parent.as_posix() == root:
                break
            parent = parent.parent

    output = io.BytesIO()
    with tarfile.open(
        fileobj=output,
        mode="w",
        format=tarfile.USTAR_FORMAT,
    ) as archive:
        for directory in sorted(
            directories,
            key=lambda path: (path.count("/"), path),
        ):
            archive.addfile(
                canonical_tar_info(directory, SEED_DIRECTORY_MODE, directory=True)
            )
        manifest_info = canonical_tar_info(
            f"{root}/{SEED_MANIFEST_PATH}",
            SEED_REGULAR_MODE,
            directory=False,
        )
        manifest_info.size = len(manifest)
        archive.addfile(manifest_info, io.BytesIO(manifest))
        for payload in sorted(payloads, key=lambda item: item.path):
            information = canonical_tar_info(
                f"{root}/{payload.path}",
                payload.mode,
                directory=False,
            )
            information.size = len(payload.data)
            archive.addfile(information, io.BytesIO(payload.data))
    return output.getvalue()


def atomic_write(path: pathlib.Path, data: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def read_version(source_root: pathlib.Path) -> str:
    version_file = source_root / "VERSION"
    try:
        version_bytes = version_file.read_bytes()
    except OSError as error:
        raise SeedError(f"cannot read {version_file}: {error}") from error
    try:
        version = version_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise SeedError("VERSION must contain ASCII") from error
    if not version.endswith("\n") or version.count("\n") != 1:
        raise SeedError("VERSION must contain one newline-terminated version")
    version = version[:-1]
    validate_version(version)
    return version


def verify_elf_executable(name: str, data: bytes) -> None:
    if len(data) < SEED_ELF_HEADER_BYTES:
        raise SeedError(f"{name} has a truncated ELF header")
    if (
        data[:7] != b"\x7fELF\x02\x01\x01"
        or data[7:16] != b"\0" * 9
    ):
        raise SeedError(f"{name} is not little-endian ELF64")
    (
        executable_type,
        machine,
        elf_version,
    ) = struct.unpack_from("<HHI", data, 16)
    (
        entry,
        program_offset,
        section_offset,
    ) = struct.unpack_from("<QQQ", data, 24)
    elf_flags = struct.unpack_from("<I", data, 48)[0]
    (
        header_size,
        program_entry_size,
        program_count,
        section_entry_size,
        section_count,
        section_names,
    ) = struct.unpack_from("<HHHHHH", data, 52)
    if (
        executable_type != SEED_ELF_TYPE_EXECUTABLE
        or machine != SEED_ELF_MACHINE_X86_64
        or elf_version != 1
        or entry == 0
        or elf_flags != 0
        or header_size != SEED_ELF_HEADER_BYTES
        or program_entry_size != SEED_ELF_PROGRAM_HEADER_BYTES
        or program_count == 0
        or program_count > 16
        or section_offset != 0
        or section_entry_size != 0
        or section_count != 0
        or section_names != 0
    ):
        raise SeedError(f"{name} violates the canonical static ELF contract")
    program_bytes = program_count * program_entry_size
    if (
        program_offset != header_size
        or program_offset > len(data)
        or program_bytes > len(data) - program_offset
    ):
        raise SeedError(f"{name} has invalid ELF program headers")
    entry_is_executable = False
    for index in range(program_count):
        offset = program_offset + index * program_entry_size
        program_type, program_flags = struct.unpack_from("<II", data, offset)
        (
            file_offset,
            virtual_address,
            _physical_address,
            file_size,
            memory_size,
            alignment,
        ) = struct.unpack_from("<QQQQQQ", data, offset + 8)
        if program_type in (
            SEED_ELF_PROGRAM_DYNAMIC,
            SEED_ELF_PROGRAM_INTERPRETER,
        ):
            raise SeedError(f"{name} depends on a dynamic loader")
        if (
            file_size > memory_size
            or file_offset > len(data)
            or file_size > len(data) - file_offset
            or alignment == 0
            or alignment & (alignment - 1) != 0
        ):
            raise SeedError(f"{name} has an invalid ELF load extent")
        if (
            program_type == SEED_ELF_PROGRAM_LOAD
            and program_flags & SEED_ELF_FLAG_EXECUTE
            and entry >= virtual_address
            and entry - virtual_address < memory_size
        ):
            entry_is_executable = True
    if not entry_is_executable:
        raise SeedError(f"{name} entry is outside executable load memory")


def source_payloads(source_root: pathlib.Path) -> list[SeedPayload]:
    runtime_root = source_root / "runtime" / "luna"
    runtime_paths = sorted(runtime_root.rglob("*.luna"))
    if not runtime_paths:
        raise SeedError(f"no Luna runtime sources found below {runtime_root}")
    source_paths = [
        *runtime_paths,
        *(
            source_root / pathlib.PurePosixPath(path)
            for path in SEED_DRIVER_SOURCES.values()
        ),
    ]
    payloads: list[SeedPayload] = []
    seen: set[str] = set()
    for source in source_paths:
        if not source.is_file() or source.is_symlink():
            raise SeedError(f"missing bootstrap source: {source}")
        relative = source.relative_to(source_root).as_posix()
        validate_payload_path(relative)
        if relative in seen:
            continue
        seen.add(relative)
        data = source.read_bytes()
        if not data or len(data) > SEED_MAX_FILE_BYTES:
            raise SeedError(f"invalid bootstrap source size: {relative}")
        payloads.append(
            SeedPayload(relative, data, SEED_REGULAR_MODE, "source")
        )
    return payloads


def auxiliary_payloads(source_root: pathlib.Path) -> list[SeedPayload]:
    auxiliary = (
        ("VERSION", "VERSION", SEED_REGULAR_MODE),
        ("LICENSE", "LICENSE", SEED_REGULAR_MODE),
        (
            "tools/release/SEED-README.md",
            "README.md",
            SEED_REGULAR_MODE,
        ),
        (
            "tools/release/bootstrap_seed.py",
            "bootstrap_seed.py",
            SEED_EXECUTABLE_MODE,
        ),
    )
    payloads: list[SeedPayload] = []
    for source_name, archive_name, mode in auxiliary:
        source = source_root / source_name
        if not source.is_file() or source.is_symlink():
            raise SeedError(f"missing seed support file: {source}")
        data = source.read_bytes()
        if not data or len(data) > SEED_MAX_FILE_BYTES:
            raise SeedError(f"invalid seed support file size: {source_name}")
        payloads.append(SeedPayload(archive_name, data, mode, "auxiliary"))
    return payloads


def tool_payloads(tool_directory: pathlib.Path) -> list[SeedPayload]:
    payloads: list[SeedPayload] = []
    for tool_name in SEED_TOOL_NAMES:
        source = tool_directory / tool_name
        if (
            not source.is_file()
            or source.is_symlink()
            or source.stat().st_mode & 0o111 == 0
        ):
            raise SeedError(f"missing fixed-point tool: {source}")
        data = source.read_bytes()
        if not data or len(data) > SEED_MAX_FILE_BYTES:
            raise SeedError(f"invalid fixed-point tool size: {tool_name}")
        verify_elf_executable(tool_name, data)
        payloads.append(
            SeedPayload(
                f"bin/{tool_name}",
                data,
                SEED_EXECUTABLE_MODE,
                "tool",
            )
        )
    return payloads


def create_seed(
    source_root: pathlib.Path,
    tool_directory: pathlib.Path,
    output: pathlib.Path,
    target: str,
) -> tuple[str, pathlib.Path]:
    source_root = source_root.resolve()
    tool_directory = tool_directory.resolve()
    output = output.resolve()
    version = read_version(source_root)
    validate_target(target)
    expected_name = archive_name(version, target)
    if output.name != expected_name:
        raise SeedError(
            f"seed archive must be named {expected_name}, got {output.name}"
        )
    payloads = tuple(
        sorted(
            (
                *source_payloads(source_root),
                *auxiliary_payloads(source_root),
                *tool_payloads(tool_directory),
            ),
            key=lambda payload: payload.path,
        )
    )
    if len(payloads) > SEED_MAX_MEMBERS:
        raise SeedError("seed payload count exceeds the format limit")
    packaged_bytes = build_archive_bytes(version, target, payloads)
    if len(packaged_bytes) > SEED_MAX_ARCHIVE_BYTES:
        raise SeedError("seed archive exceeds the format size limit")
    digest = sha256_bytes(packaged_bytes)
    checksum = f"{digest}  {output.name}\n".encode("ascii")
    atomic_write(output, packaged_bytes, SEED_REGULAR_MODE)
    checksum_path = output.with_name(f"{output.name}.sha256")
    atomic_write(checksum_path, checksum, SEED_REGULAR_MODE)
    return digest, checksum_path


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise SeedError(f"duplicate manifest key: {key}")
        result[key] = value
    return result


def parse_manifest(data: bytes) -> dict[str, object]:
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        raise SeedError("manifest is not ASCII") from error
    try:
        manifest = json.loads(text, object_pairs_hook=reject_duplicate_keys)
    except (json.JSONDecodeError, SeedError) as error:
        raise SeedError(f"invalid seed manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise SeedError("seed manifest must be an object")
    return manifest


def validate_manifest_scalar(
    manifest: dict[str, object],
    name: str,
) -> str:
    value = manifest.get(name)
    if not isinstance(value, str):
        raise SeedError(f"manifest field {name!r} must be a string")
    return value


def parse_payloads(
    manifest: dict[str, object],
    archived_files: dict[str, tuple[tarfile.TarInfo, bytes]],
    root: str,
) -> tuple[SeedPayload, ...]:
    raw_files = manifest.get("files")
    if not isinstance(raw_files, list):
        raise SeedError("manifest files must be an array")
    payloads: list[SeedPayload] = []
    previous_path = ""
    for raw_entry in raw_files:
        if not isinstance(raw_entry, dict) or set(raw_entry) != {
            "kind",
            "mode",
            "path",
            "sha256",
            "size",
        }:
            raise SeedError("manifest contains an invalid file entry")
        path = raw_entry["path"]
        kind = raw_entry["kind"]
        mode = raw_entry["mode"]
        size = raw_entry["size"]
        digest = raw_entry["sha256"]
        if not isinstance(path, str):
            raise SeedError("manifest file path must be a string")
        validate_payload_path(path)
        if path <= previous_path:
            raise SeedError("manifest file paths must be unique and sorted")
        previous_path = path
        if kind not in ("auxiliary", "source", "tool"):
            raise SeedError(f"manifest contains invalid file kind: {kind!r}")
        if type(mode) is not int or mode not in (
            SEED_REGULAR_MODE,
            SEED_EXECUTABLE_MODE,
        ):
            raise SeedError(f"manifest contains invalid mode for {path}")
        if type(size) is not int or size <= 0 or size > SEED_MAX_FILE_BYTES:
            raise SeedError(f"manifest contains invalid size for {path}")
        expected_mode = (
            SEED_EXECUTABLE_MODE
            if kind == "tool" or path == "bootstrap_seed.py"
            else SEED_REGULAR_MODE
        )
        if mode != expected_mode:
            raise SeedError(f"manifest contains non-canonical mode for {path}")
        if not isinstance(digest, str) or SEED_SHA256_PATTERN.fullmatch(
            digest
        ) is None:
            raise SeedError(f"manifest contains invalid digest for {path}")
        archive_path = f"{root}/{path}"
        archived = archived_files.get(archive_path)
        if archived is None:
            raise SeedError(f"archive is missing manifest payload: {path}")
        information, data = archived
        if (
            information.mode != mode
            or len(data) != size
            or sha256_bytes(data) != digest
        ):
            raise SeedError(f"archive payload does not match manifest: {path}")
        payloads.append(SeedPayload(path, data, mode, kind))
    if sum(len(payload.data) for payload in payloads) > SEED_MAX_PAYLOAD_BYTES:
        raise SeedError("seed payload exceeds the aggregate size limit")
    return tuple(payloads)


def validate_payload_roles(
    payloads: tuple[SeedPayload, ...],
    version: str,
) -> None:
    tools = {f"bin/{name}" for name in SEED_TOOL_NAMES}
    auxiliary = {
        "VERSION",
        "LICENSE",
        "README.md",
        "bootstrap_seed.py",
    }
    drivers = set(SEED_DRIVER_SOURCES.values())
    for payload in payloads:
        if payload.path in tools:
            expected_kind = "tool"
        elif payload.path in auxiliary:
            expected_kind = "auxiliary"
        elif (
            payload.path in drivers
            or (
                payload.path.startswith("runtime/luna/")
                and payload.path.endswith(".luna")
            )
        ):
            expected_kind = "source"
        else:
            raise SeedError(f"seed contains an unsupported payload: {payload.path}")
        if payload.kind != expected_kind:
            raise SeedError(
                f"seed payload {payload.path} has role {payload.kind}, "
                f"expected {expected_kind}"
            )
    version_payload = next(
        (
            payload
            for payload in payloads
            if payload.path == "VERSION"
        ),
        None,
    )
    if (
        version_payload is None
        or version_payload.data != f"{version}\n".encode("ascii")
    ):
        raise SeedError("seed VERSION does not match the manifest version")


def validate_tar_member(information: tarfile.TarInfo) -> None:
    path = pathlib.PurePosixPath(information.name)
    if (
        information.name.startswith("/")
        or "\\" in information.name
        or path.as_posix() != information.name
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        raise SeedError(f"unsafe archive member path: {information.name!r}")
    if not (information.isdir() or information.isfile()):
        raise SeedError(f"unsupported archive member type: {information.name}")
    if (
        information.uid != 0
        or information.gid != 0
        or information.uname != ""
        or information.gname != ""
        or information.mtime != 0
        or (
            information.isdir()
            and information.mode != SEED_DIRECTORY_MODE
        )
        or (
            information.isfile()
            and information.mode
            not in (SEED_REGULAR_MODE, SEED_EXECUTABLE_MODE)
        )
        or (information.isdir() and information.size != 0)
    ):
        raise SeedError(f"non-canonical archive metadata: {information.name}")


def read_checksum(
    checksum_file: pathlib.Path,
    archive_name: str,
) -> str:
    try:
        checksum = checksum_file.read_bytes().decode("ascii")
    except (OSError, UnicodeDecodeError) as error:
        raise SeedError(f"cannot read checksum file: {error}") from error
    match = re.fullmatch(
        rf"([0-9a-f]{{64}})  {re.escape(archive_name)}\n",
        checksum,
    )
    if match is None:
        raise SeedError("checksum file is not in canonical SHA-256 format")
    return match.group(1)


def verify_seed(
    archive: pathlib.Path,
    *,
    checksum_file: pathlib.Path | None,
    expected_version: str | None,
    expected_target: str | None,
) -> VerifiedSeed:
    archive = archive.resolve()
    try:
        archive_bytes = archive.read_bytes()
    except OSError as error:
        raise SeedError(f"cannot read seed archive: {error}") from error
    if (
        not archive_bytes
        or len(archive_bytes) > SEED_MAX_ARCHIVE_BYTES
        or len(archive_bytes) % SEED_TAR_RECORD_BYTES != 0
    ):
        raise SeedError("seed archive has an invalid bounded tar extent")
    archive_digest = sha256_bytes(archive_bytes)
    if checksum_file is not None:
        expected_digest = read_checksum(
            checksum_file.resolve(),
            archive.name,
        )
        if archive_digest != expected_digest:
            raise SeedError("seed archive does not match its SHA-256 file")

    members: list[tarfile.TarInfo]
    archived_files: dict[str, tuple[tarfile.TarInfo, bytes]] = {}
    try:
        with tarfile.open(
            fileobj=io.BytesIO(archive_bytes),
            mode="r:",
        ) as input_archive:
            members = input_archive.getmembers()
            if not members or len(members) > SEED_MAX_MEMBERS:
                raise SeedError("seed archive has an invalid member count")
            names: set[str] = set()
            for information in members:
                validate_tar_member(information)
                if information.name in names:
                    raise SeedError(
                        f"duplicate archive member: {information.name}"
                    )
                names.add(information.name)
                if information.isfile():
                    if (
                        information.size <= 0
                        or information.size > SEED_MAX_FILE_BYTES
                    ):
                        raise SeedError(
                            f"invalid archive member size: {information.name}"
                        )
                    extracted = input_archive.extractfile(information)
                    if extracted is None:
                        raise SeedError(
                            f"cannot read archive member: {information.name}"
                        )
                    data = extracted.read(SEED_MAX_FILE_BYTES + 1)
                    if len(data) != information.size:
                        raise SeedError(
                            f"truncated archive member: {information.name}"
                        )
                    archived_files[information.name] = (information, data)
    except (tarfile.TarError, OSError) as error:
        raise SeedError(f"invalid seed tar archive: {error}") from error

    roots = {pathlib.PurePosixPath(member.name).parts[0] for member in members}
    if len(roots) != 1:
        raise SeedError("seed archive must have exactly one root directory")
    root = next(iter(roots))
    manifest_record = archived_files.get(f"{root}/{SEED_MANIFEST_PATH}")
    if manifest_record is None:
        raise SeedError("seed archive has no root manifest")
    manifest = parse_manifest(manifest_record[1])
    if set(manifest) != {
        "archive_root",
        "files",
        "format",
        "project",
        "source_tree_sha256",
        "target",
        "toolchain_sha256",
        "version",
    }:
        raise SeedError("seed manifest schema is not canonical")
    version = validate_manifest_scalar(manifest, "version")
    target = validate_manifest_scalar(manifest, "target")
    validate_version(version)
    validate_target(target)
    if (
        validate_manifest_scalar(manifest, "format") != SEED_FORMAT
        or validate_manifest_scalar(manifest, "project") != SEED_PROJECT
        or validate_manifest_scalar(manifest, "archive_root") != root
        or root != archive_root(version, target)
    ):
        raise SeedError("seed identity fields are inconsistent")
    expected_archive_name = archive_name(version, target)
    if archive.name != expected_archive_name:
        raise SeedError(
            f"seed archive must be named {expected_archive_name}, "
            f"got {archive.name}"
        )
    if expected_version is not None and version != expected_version:
        raise SeedError(
            f"seed version {version!r} does not match {expected_version!r}"
        )
    if expected_target is not None and target != expected_target:
        raise SeedError(
            f"seed target {target!r} does not match {expected_target!r}"
        )

    payloads = parse_payloads(manifest, archived_files, root)
    validate_payload_roles(payloads, version)
    expected_files = {
        f"{root}/{SEED_MANIFEST_PATH}",
        *(f"{root}/{payload.path}" for payload in payloads),
    }
    if set(archived_files) != expected_files:
        raise SeedError("archive contains a file absent from the manifest")
    rebuilt_manifest = manifest_object(version, target, payloads)
    if manifest != rebuilt_manifest:
        raise SeedError("seed manifest fingerprints are inconsistent")
    if manifest_record[1] != canonical_json(rebuilt_manifest):
        raise SeedError("seed manifest JSON is not canonical")
    if archive_bytes != build_archive_bytes(version, target, payloads):
        raise SeedError("seed tar representation is not canonical")

    payload_paths = {payload.path for payload in payloads}
    required_paths = {
        "VERSION",
        "LICENSE",
        "README.md",
        "bootstrap_seed.py",
        *(f"bin/{name}" for name in SEED_TOOL_NAMES),
        *SEED_DRIVER_SOURCES.values(),
        *(
            f"{stem}.interface.luna"
            for stem in SEED_MODULE_STEMS.values()
        ),
        *(
            f"{SEED_MODULE_STEMS[key]}.luna"
            for key in SEED_LIBRARY_KEYS
        ),
    }
    missing = sorted(required_paths - payload_paths)
    if missing:
        raise SeedError(f"seed omits required reconstruction files: {missing}")
    for tool_name in SEED_TOOL_NAMES:
        verify_elf_executable(
            tool_name,
            next(
                payload.data
                for payload in payloads
                if payload.path == f"bin/{tool_name}"
            ),
        )
    return VerifiedSeed(
        archive=archive,
        archive_bytes=archive_bytes,
        archive_sha256=archive_digest,
        root=root,
        version=version,
        target=target,
        manifest_bytes=canonical_json(rebuilt_manifest),
        payloads=payloads,
    )


def interface_dependencies(
    source_root: pathlib.Path,
    key: str,
    keys_by_name: dict[str, str],
) -> tuple[str, ...]:
    interface = (
        source_root / SEED_MODULE_STEMS[key]
    ).with_suffix(".interface.luna")
    try:
        source = interface.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise SeedError(f"cannot read interface {interface}: {error}") from error
    names = re.findall(
        r"^[ \t]*import[ \t]+([A-Za-z0-9_.]+)[ \t]*;[ \t]*$",
        source,
        flags=re.MULTILINE,
    )
    dependencies: list[str] = []
    for name in names:
        dependency = keys_by_name.get(name)
        if dependency is None:
            raise SeedError(f"{interface} imports unknown module {name}")
        dependencies.append(dependency)
    return tuple(dependencies)


def dependency_closure(
    source_root: pathlib.Path,
    key: str,
) -> tuple[str, ...]:
    keys_by_name: dict[str, str] = {}
    for candidate, stem_name in SEED_MODULE_STEMS.items():
        interface = (source_root / stem_name).with_suffix(".interface.luna")
        try:
            source = interface.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise SeedError(
                f"cannot read interface {interface}: {error}"
            ) from error
        match = re.search(
            r"^[ \t]*export[ \t]+module[ \t]+"
            r"([A-Za-z0-9_.]+)[ \t]*;[ \t]*$",
            source,
            flags=re.MULTILINE,
        )
        if match is None or match.group(1) in keys_by_name:
            raise SeedError(f"invalid exported module in {interface}")
        keys_by_name[match.group(1)] = candidate

    required: set[str] = set(SEED_MODULE_DEPENDENCIES[key])
    pending = list(required)
    while pending:
        current = pending.pop()
        for dependency in interface_dependencies(
            source_root,
            current,
            keys_by_name,
        ):
            if dependency not in required:
                required.add(dependency)
                pending.append(dependency)
    return tuple(
        candidate
        for candidate in SEED_INTERFACE_KEYS
        if candidate in required
    )


def module_sources(source_root: pathlib.Path, key: str) -> list[pathlib.Path]:
    stem = source_root / SEED_MODULE_STEMS[key]
    return [
        stem.with_suffix(".luna"),
        stem.with_suffix(".interface.luna"),
        *(
            (
                source_root / SEED_MODULE_STEMS[dependency]
            ).with_suffix(".interface.luna")
            for dependency in dependency_closure(source_root, key)
        ),
    ]


def driver_sources(
    source_root: pathlib.Path,
    tool_name: str,
) -> list[pathlib.Path]:
    return [
        source_root / SEED_DRIVER_SOURCES[tool_name],
        *(
            (
                source_root / SEED_MODULE_STEMS[dependency]
            ).with_suffix(".interface.luna")
            for dependency in SEED_DRIVER_DEPENDENCIES[tool_name]
        ),
    ]


def target_runner() -> list[str]:
    if platform.machine().lower() in ("x86_64", "amd64"):
        return []
    emulator = shutil.which("qemu-x86_64-static")
    if emulator is None:
        raise SeedError(
            "qemu-x86_64-static is required to rebuild this seed on "
            f"{platform.machine()}"
        )
    return [emulator]


def run(
    command: list[str],
    *,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SeedError(f"cannot execute seed command: {error}") from error
    if result.returncode != 0:
        raise SeedError(
            f"seed command returned {result.returncode}: "
            f"{' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def rebuild_seed(
    verified: VerifiedSeed,
    work_directory: pathlib.Path,
) -> None:
    work_directory = work_directory.resolve()
    if work_directory.exists():
        if any(work_directory.iterdir()):
            raise SeedError(
                f"rebuild work directory is not empty: {work_directory}"
            )
    else:
        work_directory.mkdir(parents=True)
    extraction = work_directory / "seed"
    source_root = verified.extract(extraction)
    build_root = work_directory / "rebuild"
    assembly_root = build_root / "assembly"
    object_root = build_root / "objects"
    binary_root = build_root / "bin"
    assembly_root.mkdir(parents=True)
    object_root.mkdir(parents=True)
    binary_root.mkdir(parents=True)

    runner = target_runner()
    timeout = (
        SEED_EMULATED_TIMEOUT_SECONDS
        if runner
        else SEED_NATIVE_TIMEOUT_SECONDS
    )
    seed_binary_root = source_root / "bin"
    compiler = seed_binary_root / "lunac"
    assembler = seed_binary_root / "luna-as"
    linker = seed_binary_root / "luna-link"
    objects: dict[str, pathlib.Path] = {}

    for key in SEED_LIBRARY_KEYS:
        assembly = assembly_root / f"{key}.s"
        object_file = object_root / f"{key}.lo"
        run(
            [
                *runner,
                str(compiler),
                "--library",
                "-o",
                str(assembly),
                *(str(path) for path in module_sources(source_root, key)),
            ],
            timeout=timeout,
        )
        run(
            [
                *runner,
                str(assembler),
                "-o",
                str(object_file),
                str(assembly),
            ],
            timeout=timeout,
        )
        objects[key] = object_file

    for tool_name in SEED_TOOL_NAMES:
        assembly = assembly_root / f"{tool_name}.s"
        object_file = object_root / f"{tool_name}.lo"
        executable = binary_root / tool_name
        run(
            [
                *runner,
                str(compiler),
                "--executable",
                "-o",
                str(assembly),
                *(
                    str(path)
                    for path in driver_sources(source_root, tool_name)
                ),
            ],
            timeout=timeout,
        )
        run(
            [
                *runner,
                str(assembler),
                "-o",
                str(object_file),
                str(assembly),
            ],
            timeout=timeout,
        )
        run(
            [
                *runner,
                str(linker),
                "-o",
                str(executable),
                str(object_file),
                *(str(objects[key]) for key in SEED_LIBRARY_KEYS),
            ],
            timeout=timeout,
        )
        rebuilt = executable.read_bytes()
        expected = verified.payload(f"bin/{tool_name}").data
        if rebuilt != expected:
            mismatch = min(len(rebuilt), len(expected))
            for index, (left, right) in enumerate(zip(rebuilt, expected)):
                if left != right:
                    mismatch = index
                    break
            raise SeedError(
                f"rebuilt {tool_name} is not byte-identical: "
                f"{len(rebuilt)} != {len(expected)}, mismatch {mismatch}"
            )
        verify_elf_executable(tool_name, rebuilt)
        version_result = run(
            [*runner, str(executable), "--version"],
            timeout=timeout,
        )
        if verified.version not in version_result.stdout:
            raise SeedError(
                f"rebuilt {tool_name} does not report {verified.version}"
            )


def create_command(arguments: argparse.Namespace) -> int:
    digest, checksum = create_seed(
        arguments.source_root,
        arguments.tool_dir,
        arguments.output,
        arguments.target,
    )
    print(f"created {arguments.output}: {digest}")
    print(f"checksum {checksum}")
    return 0


def verify_command(arguments: argparse.Namespace) -> int:
    verified = verify_seed(
        arguments.archive,
        checksum_file=arguments.checksum_file,
        expected_version=arguments.expected_version,
        expected_target=arguments.expected_target,
    )
    if arguments.extract_dir is not None:
        verified.extract(arguments.extract_dir.resolve())
    print(
        f"verified {verified.version} {verified.target}: "
        f"{verified.archive_sha256}"
    )
    return 0


def rebuild_command(arguments: argparse.Namespace) -> int:
    verified = verify_seed(
        arguments.archive,
        checksum_file=arguments.checksum_file,
        expected_version=arguments.expected_version,
        expected_target=arguments.expected_target,
    )
    rebuild_seed(verified, arguments.work_dir)
    print(
        f"rebuilt {verified.version} {verified.target}: "
        f"{verified.archive_sha256}"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create, verify and rebuild a Luna bootstrap seed"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create")
    create.add_argument(
        "--source-root",
        type=pathlib.Path,
        required=True,
    )
    create.add_argument(
        "--tool-dir",
        type=pathlib.Path,
        required=True,
    )
    create.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
    )
    create.add_argument("--target", default=SEED_TARGET)
    create.set_defaults(handler=create_command)

    verify = subparsers.add_parser("verify")
    verify.add_argument("archive", type=pathlib.Path)
    verify.add_argument("--checksum-file", type=pathlib.Path)
    verify.add_argument("--expected-version")
    verify.add_argument("--expected-target")
    verify.add_argument("--extract-dir", type=pathlib.Path)
    verify.set_defaults(handler=verify_command)

    rebuild = subparsers.add_parser("rebuild")
    rebuild.add_argument("archive", type=pathlib.Path)
    rebuild.add_argument("--checksum-file", type=pathlib.Path)
    rebuild.add_argument("--expected-version")
    rebuild.add_argument("--expected-target")
    rebuild.add_argument(
        "--work-dir",
        type=pathlib.Path,
        required=True,
    )
    rebuild.set_defaults(handler=rebuild_command)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        return arguments.handler(arguments)
    except SeedError as error:
        print(f"bootstrap seed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
