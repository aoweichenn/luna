# Luna

Luna is a small, strongly typed systems language whose compiler, assembler
and linker are all written in Luna. The repository is self-hosting in the
strict sense: the only trusted binary input is the `anchor/` toolchain, and
every new toolchain is built by the previous one.

The original bootstrap — a C23 reconstruction seed building stage 1, then
stage 2 and stage 3 converging byte-for-byte with a Luna-owned assembler and
linker — is archived on the [`m0`](https://github.com/aoweichenn/luna/tree/m0)
branch and frozen there. This branch keeps only what matters going forward:
the Luna sources, the anchor they grow from, and a minimal driver for the
self-hosting loop.

## Layout

```text
anchor/     fixed-point toolchain from m0 (the sole binary trust root)
library/    runtime, Linux syscall layer, standard library (pure Luna)
compiler/   frontend, middle end, x86-64 backend (pure Luna)
drivers/    freestanding lunac / luna-as / luna-link driver programs
tests/      executable behavior cases + expected exit codes
tools/      selfhost.py: build / verify / test driver
docs/       design record (language, semantics, seed contract)
```

## Build

Requires only Python 3 and an x86-64 Linux host (or `qemu-x86_64-static`
via `--runner`):

```sh
python3 tools/selfhost.py verify   # anchor builds the toolchain twice,
                                   # every artifact must be byte-identical
```

`build` stops after producing `out/stage-next/bin/{lunac,luna-as,luna-link}`.
`test` compiles, links and executes every case in `tests/cases/` through the
freshly built tools and checks each program's exit status against
`tests/expectations.txt` (negative values are fatal signals such as traps).

Compile and run a program directly:

```sh
out/stage-next/bin/lunac --executable -o hello.s examples/hello.luna
out/stage-next/bin/luna-as -o hello.lo hello.s
out/stage-next/bin/luna-link -o hello hello.lo
./hello
```

## The iteration discipline

Development follows one rule: **the previous compiler builds the next one**.

1. edit Luna sources under `library/`, `compiler/` or `drivers/`;
2. a feature's first implementation may use only syntax the current
   toolchain already accepts;
3. `python3 tools/selfhost.py verify` must reach a byte-identical fixed
   point;
4. once the new tools are proven, their own sources may adopt the feature;
5. promote a verified toolchain into `anchor/` with refreshed checksums.

## Status

Version 0.1.0 closed the full bootstrap (see the `m0` branch and
`docs/roadmap.md`). This branch reorganizes the project around pure-Luna
development; new language work happens here, in Luna. The accepted syntax
completion direction and its milestone order are recorded in
[`docs/syntax-plan.md`](docs/syntax-plan.md).
