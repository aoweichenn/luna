# Luna

Luna is a small, strongly typed systems language derived from the procedural
core of C23. The bootstrap compiler is written in C23 and lowers Luna directly
to a typed control-flow IR and an x86-64 backend. It does not transpile through
C or C++.

The project is deliberately narrow at this stage:

- target: x86-64 Linux, System V ABI, ELF64;
- frontend: explicit `bool`, `i8`, `i16`, `i32`, `i64`, `isize`, `u8`,
  `u16`, `u32`, `u64`, `usize`, `f32` and `f64` types, explicit numeric
  and raw-pointer conversions with checked floating-to-integer bounds, raw
  pointers, local fixed arrays, immutable string literals, functions,
  exact-layout structures and unions, scoped enums, typed external C function
  declarations, context-directed named aggregate initialization, exact
  whole-object copies, matched module interface/implementation pairs,
  type-only `sizeof`, `alignof` and `offsetof` queries, expressions, the
  short-circuit conditional operator, local variables, `if`, `while`, `do`,
  `for` and non-fallthrough `switch` control flow;
- middle end: typed, non-SSA control-flow IR with explicit object layouts,
  static data, member addresses, typed memory operations and sized,
  overlap-safe object copies;
- target model: explicit `x86_64-unknown-linux-gnu` data layout, with
  target-sized `isize` and `usize`;
- backend: direct, unoptimized x86-64 instruction selection, including
  IEEE-754 scalar SSE floating-point operations and checked numeric
  conversions, exact-width indirect memory access, checked fixed-array
  indexing, read-only static data, inline overlap-safe object copies and direct
  System V calls to unmangled external C symbols;
- bootstrap host: conforming C23 with IEC 60559 binary32 and binary64;
- quality gate: warnings-as-errors, GoogleTest unit tests, negative tests, IR
  snapshots, differential random programs, libFuzzer and executable
  cross-target tests.

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

GoogleTest 1.14 or newer and Python 3.10 or newer are required when
`BUILD_TESTING` is enabled. Test groups can be run independently:

```sh
ctest --preset debug -L unit
ctest --preset debug -L integration
ctest --preset debug -L random
```

The coverage-guided fuzz gate uses Clang:

```sh
cmake --preset fuzz
cmake --build --preset fuzz
ctest --preset fuzz -L fuzz
```

Native Linux CI additionally runs the same target with the `fuzz-asan` preset.

## Compile

```sh
build/debug/lunac --target x86_64-unknown-linux-gnu \
  --emit asm -o hello.s examples/hello.luna
llvm-mc --triple=x86_64-unknown-linux-gnu --filetype=obj \
  -o hello.o hello.s
ld.lld -static -e _start -o hello hello.o
qemu-x86_64-static ./hello
```

For a module with a separate interface, pass both source units in either
order:

```sh
build/debug/lunac --emit asm -o app.s app.luna app.interface.luna
```

The interface begins with `export module`, contains complete type definitions
and bodyless function declarations, and is matched exactly against definitions
in the implementation unit. Cross-module imports remain disabled until the
next M2 stage.

`--target` defaults to `x86_64-unknown-linux-gnu`, currently the only
supported target. On an x86-64 Linux host, the integration and differential
test runners execute generated static binaries natively when
`qemu-x86_64-static` is unavailable.

An external definition is declared without a body:

```luna
extern fn c_value(input: i32) -> i32;
```

Compile the C23 implementation to an x86-64 object and include that object in
the final `ld.lld` command. Luna emits the exact symbol name and does not
implicitly link libc. The current external ABI accepts non-variadic
scalar/pointer signatures that fit in the six integer and eight SSE System V
argument registers; stack and aggregate arguments remain deferred.

See [the language draft](docs/language.md),
[compiler architecture](docs/architecture.md),
[bootstrap execution semantics](docs/execution-semantics.md), and the
[implementation roadmap](docs/roadmap.md).
