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
anchor/                    fixed-point toolchain from m0; sole binary trust root
library/include/luna/      runtime and standard-library interfaces (.lh)
library/src/               runtime and standard-library implementations (.la)
compiler/include/luna/     compiler interfaces under luna/bootstrap/ (.lh)
compiler/src/              frontend, middle end and x86-64 backend (.la)
drivers/src/               freestanding lunac / luna-as / luna-link drivers
tests/                     behavior and FFI fixtures + expected results
tools/                     selfhost.py: audit / build / verify / test driver
docs/                      design record (language, semantics, seed contract)
```

Interface paths mirror their full module names; implementation paths mirror
the subsystem tree. For example, `luna.std.text` currently groups
`library/include/luna/std/text.lh` with `library/src/std/text.la`, while a
larger module may register several `.la` implementation units.
`tools/selfhost.py` records all paths explicitly, so source discovery does not
depend on interfaces and implementations occupying the same directory.

## Build

Requires only Python 3 and an x86-64 Linux host (or `qemu-x86_64-static`
via `--runner`):

```sh
python3 tools/selfhost.py audit    # read-only anchor, module-graph and style checks
python3 tools/selfhost.py verify   # anchor -> transition -> stage-next ->
                                   # stage-fixed; next/fixed are identical
```

On a non-x86-64 development host, `--runner qemu-x86_64-static` prefixes both
the toolchain binaries and generated test programs; `LUNA_TOOL_RUNNER` sets the
same default. The optional C FFI cases require an x86-64-targeting C compiler
(`LUNA_FFI_CC` may name a cross compiler) and are reported as skipped when one
is unavailable. Release validation runs all FFI cases on an x86-64 host.

`build` stops after producing `out/stage-next/bin/{lunac,luna-as,luna-link}`.
`test` compiles, links and executes every case in `tests/cases/` through the
freshly built tools, checks exact callable-identity/linking invariants, and
runs the ELF/host FFI matrix. Behavior cases use `tests/expectations.txt`;
negative values are fatal signals such as traps.

The build graph has a single module registry. Interface order, implementation
order and each driver's transitive link closure are derived from source imports,
so the assembler and linker do not carry compiler-only objects. `audit` rejects
unregistered modules, dependency cycles and duplicate imports before a build.

Compile and run a program directly:

```sh
out/stage-next/bin/lunac --executable -o hello.s examples/hello.la
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
[`docs/syntax-plan.md`](docs/syntax-plan.md), and the working plan for
the next steps — m1.1 closeout, m1.2 and beyond — lives in
[`docs/next-steps.md`](docs/next-steps.md).
