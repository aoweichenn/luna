# Luna

Luna is a small, strongly typed systems language derived from the procedural
core of C23. The bootstrap compiler is written in C23 and lowers Luna directly
to a typed control-flow IR and an x86-64 backend. It does not transpile through
C or C++.

The project is deliberately narrow at this stage:

- target: x86-64 Linux, System V ABI, ELF64;
- frontend: explicit types, functions, expressions, local variables and
  structured control flow;
- middle end: typed, non-SSA control-flow IR;
- backend: direct x86-64 instruction selection;
- bootstrap host: conforming C23;
- quality gate: warnings-as-errors, unit tests, negative tests, IR snapshots
  and executable cross-target tests.

The language and compiler are under active construction. Implemented syntax is
tracked separately from accepted language design so that documentation never
pretends an unfinished feature works.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For undefined-behaviour checks:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

The `asan` preset adds AddressSanitizer for native Linux and CI environments.
ASan cannot reserve its shadow address space inside some Android/PRoot AArch64
environments; that host limitation occurs before the test process reaches
`main`.

## Compile

```sh
build/debug/lunac --emit asm -o hello.s examples/hello.luna
llvm-mc --triple=x86_64-unknown-linux-gnu --filetype=obj \
  -o hello.o hello.s
ld.lld -static -e _start -o hello hello.o
qemu-x86_64-static ./hello
```

See [the language draft](docs/language.md),
[compiler architecture](docs/architecture.md), and
[implementation roadmap](docs/roadmap.md).
