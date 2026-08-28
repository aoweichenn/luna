"""Parallel, content-addressed stage construction for Luna self-hosting."""

from __future__ import annotations

import argparse
import concurrent.futures
from dataclasses import dataclass
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import subprocess
import threading
import time

CACHE_FORMAT_VERSION = "luna-build-cache-v1"
TARGET_NAME_PATTERN = re.compile(r"[^A-Za-z0-9_.-]+")
PRINT_LOCK = threading.Lock()


class BuildFailure(RuntimeError):
    """A checked tool invocation or build invariant failed."""


def add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--jobs",
        type=int,
        default=4,
        help="parallel library workers (default: 4)",
    )
    parser.add_argument(
        "--cache",
        type=pathlib.Path,
        help="artifact cache root (default: <output-root>/cache)",
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="execute every compile/assemble/link command while refreshing cache",
    )


@dataclass(frozen=True)
class LibraryTarget:
    name: str
    units: tuple[pathlib.Path, ...]


@dataclass(frozen=True)
class DriverTarget:
    name: str
    units: tuple[pathlib.Path, ...]
    link_objects: tuple[str, ...]


@dataclass(frozen=True)
class StagePlan:
    libraries: tuple[LibraryTarget, ...]
    interface_only: tuple[str, ...]
    drivers: tuple[DriverTarget, ...]


@dataclass(frozen=True)
class TargetResult:
    name: str
    object_file: pathlib.Path
    compile_hit: bool
    assemble_hit: bool
    elapsed_seconds: float


@dataclass(frozen=True)
class DriverResult:
    name: str
    executable: pathlib.Path
    compile_hit: bool
    assemble_hit: bool
    link_hit: bool
    elapsed_seconds: float


@dataclass(frozen=True)
class StageResult:
    assembly_root: pathlib.Path
    object_root: pathlib.Path
    binary_root: pathlib.Path


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fingerprint(parts: tuple[str, ...]) -> str:
    digest = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "little"))
        digest.update(encoded)
    return digest.hexdigest()


def safe_target_name(name: str) -> str:
    return TARGET_NAME_PATTERN.sub("_", name)


def atomic_copy(source: pathlib.Path, destination: pathlib.Path, mode: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(
        f".{destination.name}.{os.getpid()}.{threading.get_ident()}.tmp"
    )
    try:
        shutil.copyfile(source, temporary)
        os.chmod(temporary, mode)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


class ArtifactCache:
    """Bounded latest-entry cache with content-verified artifact manifests."""

    def __init__(self, root: pathlib.Path) -> None:
        self._root = root

    def restore(
        self,
        scope: str,
        action: str,
        target: str,
        input_fingerprint: str,
        destination: pathlib.Path,
    ) -> bool:
        artifact, manifest = self._paths(scope, action, target)
        if not artifact.is_file() or not manifest.is_file():
            return False
        try:
            record = json.loads(manifest.read_text(encoding="utf-8"))
            expected = {
                "version": CACHE_FORMAT_VERSION,
                "input": input_fingerprint,
            }
            if any(record.get(key) != value for key, value in expected.items()):
                return False
            if artifact.stat().st_size != record.get("size"):
                return False
            if file_sha256(artifact) != record.get("sha256"):
                return False
            mode = int(record["mode"])
            if mode < 0 or mode > 0o7777:
                return False
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            return False
        atomic_copy(artifact, destination, mode)
        return True

    def store(
        self,
        scope: str,
        action: str,
        target: str,
        input_fingerprint: str,
        source: pathlib.Path,
    ) -> None:
        artifact, manifest = self._paths(scope, action, target)
        mode = stat.S_IMODE(source.stat().st_mode)
        atomic_copy(source, artifact, mode)
        record = {
            "version": CACHE_FORMAT_VERSION,
            "input": input_fingerprint,
            "sha256": file_sha256(artifact),
            "size": artifact.stat().st_size,
            "mode": mode,
        }
        manifest.parent.mkdir(parents=True, exist_ok=True)
        temporary = manifest.with_name(
            f".{manifest.name}.{os.getpid()}.{threading.get_ident()}.tmp"
        )
        try:
            temporary.write_text(
                json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            os.replace(temporary, manifest)
        finally:
            temporary.unlink(missing_ok=True)

    def prune_scope(
        self,
        scope: str,
        targets_by_action: dict[str, set[str]],
    ) -> None:
        scope_root = self._root / safe_target_name(scope)
        for action, targets in targets_by_action.items():
            action_root = scope_root / action
            if not action_root.is_dir():
                continue
            expected = {safe_target_name(target) for target in targets}
            for entry in action_root.iterdir():
                if entry.name not in expected:
                    if entry.is_dir():
                        shutil.rmtree(entry)
                    else:
                        entry.unlink()

    def _paths(self, scope: str, action: str, target: str) -> tuple[pathlib.Path, pathlib.Path]:
        entry = self._root / safe_target_name(scope) / action / safe_target_name(target)
        return entry / "artifact", entry / "manifest.json"


class Fingerprinter:
    """Memoize tool, runner and source content hashes for one stage build."""

    def __init__(
        self,
        source_root: pathlib.Path,
        toolchain: pathlib.Path,
        runner: tuple[str, ...],
    ) -> None:
        self._source_root = source_root.resolve()
        self._tool_digest = file_sha256(toolchain / "luna")
        self._runner_digest = self._digest_runner(runner)
        self._source_digests: dict[pathlib.Path, str] = {}
        self._lock = threading.Lock()

    def compile(self, mode: str, units: tuple[pathlib.Path, ...]) -> str:
        parts = [CACHE_FORMAT_VERSION, "compile", self._tool_digest, self._runner_digest, mode]
        for unit in units:
            parts.extend((self._logical_path(unit), self._source_digest(unit)))
        return fingerprint(tuple(parts))

    def assemble(self, assembly: pathlib.Path) -> str:
        return fingerprint(
            (
                CACHE_FORMAT_VERSION,
                "assemble",
                self._tool_digest,
                self._runner_digest,
                file_sha256(assembly),
            )
        )

    def link(self, objects: tuple[pathlib.Path, ...]) -> str:
        parts = [CACHE_FORMAT_VERSION, "link", self._tool_digest, self._runner_digest]
        for object_file in objects:
            parts.extend((object_file.name, file_sha256(object_file)))
        return fingerprint(tuple(parts))

    def _source_digest(self, path: pathlib.Path) -> str:
        resolved = path.resolve()
        with self._lock:
            existing = self._source_digests.get(resolved)
        if existing is not None:
            return existing
        digest = file_sha256(resolved)
        with self._lock:
            return self._source_digests.setdefault(resolved, digest)

    def _logical_path(self, path: pathlib.Path) -> str:
        resolved = path.resolve()
        try:
            return resolved.relative_to(self._source_root).as_posix()
        except ValueError:
            return resolved.as_posix()

    @staticmethod
    def _digest_runner(runner: tuple[str, ...]) -> str:
        if not runner:
            return fingerprint(("native",))
        executable = shutil.which(runner[0])
        parts = ["runner", *runner]
        if executable is not None and pathlib.Path(executable).is_file():
            parts.extend((str(pathlib.Path(executable).resolve()), file_sha256(pathlib.Path(executable))))
        return fingerprint(tuple(parts))


class StageBuilder:
    """Build one complete toolchain stage with parallel targets and safe caching."""

    def __init__(
        self,
        source_root: pathlib.Path,
        tools: pathlib.Path,
        out: pathlib.Path,
        runner: tuple[str, ...],
        jobs: int,
        cache: ArtifactCache,
        cache_scope: str,
        fresh: bool,
        timeout: int,
    ) -> None:
        self._tools = tools
        self._out = out
        self._runner = runner
        self._jobs = jobs
        self._cache = cache
        self._cache_scope = cache_scope
        self._fresh = fresh
        self._timeout = timeout
        self._fingerprinter = Fingerprinter(source_root, tools, runner)
        self._assembly_root = out / "assembly"
        self._object_root = out / "objects"
        self._binary_root = out / "bin"

    def build(self, plan: StagePlan) -> StageResult:
        started = time.monotonic()
        target_names = {
            target.name for target in (*plan.libraries, *plan.drivers)
        }
        self._cache.prune_scope(
            self._cache_scope,
            {
                "compile": target_names,
                "assemble": target_names,
                "link": {target.name for target in plan.drivers},
            },
        )
        self._reset_outputs()
        for name in plan.interface_only:
            self._print(f"  registered interface-only module {name}")
        library_results = self._build_libraries(plan.libraries)
        objects = {result.name: result.object_file for result in library_results}
        driver_results = self._build_drivers(plan.drivers, objects)
        elapsed = time.monotonic() - started
        compile_hits = sum(result.compile_hit for result in (*library_results, *driver_results))
        assemble_hits = sum(result.assemble_hit for result in (*library_results, *driver_results))
        link_hits = sum(result.link_hit for result in driver_results)
        compile_total = len(library_results) + len(driver_results)
        self._print(
            f"STAGE {self._cache_scope}: jobs={self._jobs}, "
            f"compile cache {compile_hits}/{compile_total}, "
            f"assemble cache {assemble_hits}/{compile_total}, "
            f"link cache {link_hits}/{len(driver_results)}, wall={elapsed:.2f}s"
        )
        return StageResult(self._assembly_root, self._object_root, self._binary_root)

    def _build_libraries(self, targets: tuple[LibraryTarget, ...]) -> tuple[TargetResult, ...]:
        results: dict[str, TargetResult] = {}
        workers = min(self._jobs, len(targets))
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = {executor.submit(self._build_library, target): target for target in targets}
            try:
                for future in concurrent.futures.as_completed(futures):
                    result = future.result()
                    results[result.name] = result
                    self._print_target(result, "library")
            except BaseException:
                for future in futures:
                    future.cancel()
                raise
        return tuple(results[target.name] for target in targets)

    def _build_library(self, target: LibraryTarget) -> TargetResult:
        started = time.monotonic()
        assembly = self._assembly_root / f"{target.name}.s"
        object_file = self._object_root / f"{target.name}.lo"
        compile_hit, assemble_hit = self._compile_assemble(
            target.name,
            "library",
            target.units,
            assembly,
            object_file,
        )
        return TargetResult(
            target.name,
            object_file,
            compile_hit,
            assemble_hit,
            time.monotonic() - started,
        )

    def _build_drivers(
        self,
        targets: tuple[DriverTarget, ...],
        objects: dict[str, pathlib.Path],
    ) -> tuple[DriverResult, ...]:
        results: list[DriverResult] = []
        for target in targets:
            started = time.monotonic()
            assembly = self._assembly_root / f"{target.name}.s"
            object_file = self._object_root / f"{target.name}.lo"
            executable = self._binary_root / target.name
            compile_hit, assemble_hit = self._compile_assemble(
                target.name,
                "executable",
                target.units,
                assembly,
                object_file,
            )
            link_inputs = (object_file, *(objects[name] for name in target.link_objects))
            link_fingerprint = self._fingerprinter.link(link_inputs)
            link_hit = self._restore("link", target.name, link_fingerprint, executable)
            if not link_hit:
                self._run(
                    [*self._tool("link"), "-o", executable, *link_inputs]
                )
                self._cache.store(
                    self._cache_scope,
                    "link",
                    target.name,
                    link_fingerprint,
                    executable,
                )
            result = DriverResult(
                target.name,
                executable,
                compile_hit,
                assemble_hit,
                link_hit,
                time.monotonic() - started,
            )
            results.append(result)
            self._print(
                f"  linked {target.name} ({len(target.link_objects)} library objects) "
                f"[{self._cache_label(result.link_hit)}] {result.elapsed_seconds:.2f}s"
            )
        return tuple(results)

    def _compile_assemble(
        self,
        name: str,
        mode: str,
        units: tuple[pathlib.Path, ...],
        assembly: pathlib.Path,
        object_file: pathlib.Path,
    ) -> tuple[bool, bool]:
        compile_fingerprint = self._fingerprinter.compile(mode, units)
        compile_hit = self._restore("compile", name, compile_fingerprint, assembly)
        if not compile_hit:
            self._run(
                [*self._tool("compile"), f"--{mode}", "-o", assembly, *units]
            )
            self._cache.store(
                self._cache_scope,
                "compile",
                name,
                compile_fingerprint,
                assembly,
            )
        assemble_fingerprint = self._fingerprinter.assemble(assembly)
        assemble_hit = self._restore("assemble", name, assemble_fingerprint, object_file)
        if not assemble_hit:
            self._run([*self._tool("assemble"), "-o", object_file, assembly])
            self._cache.store(
                self._cache_scope,
                "assemble",
                name,
                assemble_fingerprint,
                object_file,
            )
        return compile_hit, assemble_hit

    def _restore(
        self,
        action: str,
        name: str,
        input_fingerprint: str,
        destination: pathlib.Path,
    ) -> bool:
        if self._fresh:
            return False
        return self._cache.restore(
            self._cache_scope,
            action,
            name,
            input_fingerprint,
            destination,
        )

    def _run(self, command: list[str | pathlib.Path]) -> None:
        printable = " ".join(str(part) for part in command)
        self._print(f"  $ {printable}")
        try:
            completed = subprocess.run(
                [str(part) for part in command],
                timeout=self._timeout,
            )
        except subprocess.TimeoutExpired as error:
            raise BuildFailure(
                f"command timed out after {self._timeout}s: {printable}"
            ) from error
        if completed.returncode != 0:
            raise BuildFailure(
                f"command returned {completed.returncode}: {printable}"
            )

    def _tool(self, command: str) -> list[str | pathlib.Path]:
        return [*self._runner, self._tools / "luna", command]

    def _reset_outputs(self) -> None:
        for directory in (self._assembly_root, self._object_root, self._binary_root):
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir(parents=True)

    def _print_target(self, result: TargetResult, role: str) -> None:
        self._print(
            f"  built {role} {result.name} "
            f"[compile {self._cache_label(result.compile_hit)}, "
            f"assemble {self._cache_label(result.assemble_hit)}] "
            f"{result.elapsed_seconds:.2f}s"
        )

    @staticmethod
    def _cache_label(hit: bool) -> str:
        return "hit" if hit else "miss"

    @staticmethod
    def _print(message: str) -> None:
        with PRINT_LOCK:
            print(message, flush=True)
