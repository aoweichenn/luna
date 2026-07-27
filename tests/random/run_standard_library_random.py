#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import sys


INTEGRATION_DIRECTORY = (
    pathlib.Path(__file__).resolve().parent.parent / "integration"
)
sys.path.insert(0, str(INTEGRATION_DIRECTORY))

from run_minimum_standard_library import (  # noqa: E402
    ensure_sysroot,
    module_graph,
    run,
    target_runner,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    parser.add_argument("--sysroot", type=pathlib.Path, required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    graph = module_graph(arguments.source_root, arguments.sysroot)
    ensure_sysroot(arguments.compiler, graph)

    application_object = arguments.work_dir / "stdlib-random.o"
    run(
        [
            str(arguments.compiler),
            "--emit",
            "obj",
            "-o",
            str(application_object),
            str(
                arguments.source_root
                / "tests"
                / "integration"
                / "cases"
                / "minimum_standard_library_random.luna"
            ),
            str(graph["runtime"].metadata),
            str(graph["memory"].metadata),
            str(graph["bytes"].metadata),
        ]
    )

    runtime_object = graph["runtime"].object_file
    memory_object = graph["memory"].object_file
    bytes_object = graph["bytes"].object_file
    if (
        runtime_object is None
        or memory_object is None
        or bytes_object is None
    ):
        raise AssertionError("required standard-library object is unavailable")

    application = arguments.work_dir / "stdlib-random"
    run(
        [
            str(arguments.linker),
            "-o",
            str(application),
            str(application_object),
            str(runtime_object),
            str(memory_object),
            str(bytes_object),
        ]
    )
    result = run(
        [*target_runner(), str(application)],
        expected_code=42,
        cwd=arguments.work_dir,
    )
    if result.stdout or result.stderr:
        raise AssertionError("random standard-library test produced output")

    print("PASS deterministic standard-library buffer properties")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
