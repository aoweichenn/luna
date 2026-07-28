# Luna

Luna is a small, strongly typed systems language derived from the procedural
core of C23. Stage 0 is written in C23 and lowers Luna directly to a typed
control-flow IR, an x86-64 machine IR, native x86-64 instructions, ELF64
relocatable objects and static ELF64 executables. The self-hosting compiler,
closed-dialect assembler and static linker are all written in Luna and reach
a stage-2/stage-3 byte-for-byte fixed point together. No stage transpiles
through C or C++.

Luna 0 is frozen as the reconstruction seed language. Luna 1 is owned by the
self-hosted compiler and can add features without duplicating them in the C23
seed; its first such feature is the unconditional `loop` statement. The
versioned bootstrap contract is documented in
[`docs/bootstrap-language-versions.md`](docs/bootstrap-language-versions.md).

The project is deliberately narrow at this stage:

- target: x86-64 Linux, System V ABI, ELF64;
- frontend: explicit `bool`, `i8`, `i16`, `i32`, `i64`, `isize`, `u8`,
  `u16`, `u32`, `u64`, `usize`, `f32` and `f64` types, explicit numeric
  and raw-pointer conversions with checked floating-to-integer bounds, raw
  pointers, local fixed arrays, immutable string literals, functions,
  exact-layout structures and unions, scoped enums, typed external C function
  declarations, context-directed named aggregate initialization, exact
  whole-object copies, aggregate parameters, results and temporaries, matched
  module interface/implementation pairs,
  direct imports with explicit export visibility and validated dependency
  graphs, and deterministic compiled `.lmi` module metadata,
  type-only `sizeof`, `alignof` and `offsetof` queries, expressions, the
  short-circuit conditional operator, local variables, `if`, `while`, `do`,
  `for` and non-fallthrough `switch` control flow;
- middle end: typed, non-SSA control-flow IR with explicit object layouts,
  static data, member addresses, typed memory operations and sized,
  overlap-safe object copies;
- target model: explicit `x86_64-unknown-linux-gnu` data layout, with
  target-sized `isize` and `usize`;
- backend: verified, pre-allocation x86-64 machine IR with fixed target widths,
  explicit virtual-register definitions/uses, verified block and instruction
  liveness, verified deterministic linear-scan register allocation, verified
  allocation-aware instruction rewriting, and deterministic textual output,
  followed by direct, unoptimized register-resident x86-64 instruction
  selection, including
  IEEE-754 scalar SSE floating-point operations and checked numeric
  conversions, exact-width indirect memory access, checked fixed-array
  indexing, read-only static data, inline overlap-safe object copies and direct
  System V scalar and aggregate calls to unmangled external C symbols,
  including register rollback, stack copies, multi-register results and
  hidden result pointers;
- object writer: deterministic, self-verified ELF64 relocatable objects with
  native x86-64 encoding, symbols and explicit PC-relative relocations;
- static linker: deterministic, self-verified x86-64 ELF64 executables with
  project-owned section layout, symbol resolution and relocation application;
- self-hosted toolchain: a strict Luna-owned bootstrap object format, a
  Luna-owned closed-dialect x86-64 assembler and a Luna-owned static ELF64
  linker; after the C23 reconstruction seed creates stage 1, no hosted
  assembler or linker participates in stage 2 or stage 3;
- runtime boundary: canonical zero-to-six-argument Linux x86-64 system-call
  wrappers generated and verified by the project assembler, with raw kernel
  error results, plus a Luna-implemented typed process/file/virtual-memory
  runtime and a Luna-implemented minimum memory/byte-buffer/UTF-8/path/I/O
  standard library, plus separately compiled Luna lexer/parser/type/IR/sema
  modules with structured diagnostics, stable indexed storage and
  independently verified Typed IR, with no target libc;
- bootstrap host: conforming C23 with IEC 60559 binary32 and binary64 for
  stage 0; freestanding Luna with direct Linux system calls for later stages,
  including exact no-libc decimal-to-binary32/binary64 conversion with direct
  contextual rounding;
- quality gate: warnings-as-errors, GoogleTest unit tests, negative tests,
  typed-IR, machine-IR, ABI, liveness, register-allocation and
  instruction-rewrite snapshots, full-opcode machine-IR-to-x86-64
  differential execution, differential random programs, libFuzzer,
  executable cross-target tests, malformed-object mutation tests and complete
  compiler/assembler/linker stage-2/stage-3 artifact comparison.

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

## Build the self-hosted toolchain

The correctness-gated target reconstructs stage 1 from the C23 seed, uses it
to build stage 2, uses stage 2 to build stage 3, compares every stage-2 and
stage-3 compiler/assembler/linker artifact byte-for-byte, and runs the
Luna-owned toolchain tests:

```sh
cmake --build --preset debug --target luna_selfhost_toolchain
```

The resulting no-libc, statically linked x86-64 tools are:

```text
build/debug/selfhost-bootstrap/stage-three/bin/lunac
build/debug/selfhost-bootstrap/stage-three/bin/luna-as
build/debug/selfhost-bootstrap/stage-three/bin/luna-link
```

Their deliberately narrow command-line pipeline is:

```sh
SELFHOST=build/debug/selfhost-bootstrap/stage-three/bin

"$SELFHOST/lunac" --executable -o hello.s examples/hello.luna
"$SELFHOST/luna-as" -o hello.lo hello.s
"$SELFHOST/luna-link" -o hello hello.lo
./hello
```

Use `lunac --library` for source units that do not define `main`. This
self-hosted compiler currently emits the project-owned assembly dialect; the
separate assembler emits `LUNAOBJ1`, and the linker emits a static Linux
ELF64 executable. The C23 seed compiler's broader ELF `.o`, `.lmi`, Debug IR
and inspection commands remain reconstruction and development facilities.

## Build the versioned bootstrap seed

The final distribution target packages the fixed-point tools together with
the complete pure-Luna reconstruction source graph, writes a canonical
SHA-256 file, verifies the bounded archive and rebuilds all three tools from
the package without a hosted compiler, assembler or linker:

```sh
cmake --preset release
cmake --build --preset release --target luna_bootstrap_seed
```

The release artifact is written below
`build/release/selfhost-bootstrap/dist/`. Its checksum must match the
versioned identity under `release/seeds/`. The archive format, offline
verification command, byte-identical rebuild requirement and trust boundary
are documented in
[`docs/bootstrap-seed.md`](docs/bootstrap-seed.md).

## Compile

```sh
build/debug/lunac --target x86_64-unknown-linux-gnu \
  --emit obj -o hello.o examples/hello.luna
build/debug/lunalink -o hello hello.o
qemu-x86_64-static ./hello
```

`--emit asm` remains available for backend review. Native object emission does
not invoke LLVM MC or another external assembler. `lunalink` performs the
static link itself and does not invoke LLD, GNU ld or a host compiler.

The native object contains versioned project Debug IR, and `lunalink` emits
final-address DWARF 5 without invoking a host linker:

```sh
llvm-dwarfdump --verify hello
gdb hello
```

The current source-level contract covers files, functions, lines, columns,
breakpoints and statement stepping. The exact format and deliberate bootstrap
limits are documented in
[`docs/debug-information.md`](docs/debug-information.md).

The verified target machine IR can be inspected without producing assembly:

```sh
build/debug/lunac --emit mir -o hello.mir examples/hello.luna
```

This is the x86-64 pre-register-allocation boundary, not a portable interchange
format. Its `isize` and `usize` values have already become `i64` and `u64`.

Verified System V parameter, result and stack-frame locations can be
inspected independently:

```sh
build/debug/lunac --emit abi -o hello.abi examples/hello.luna
```

This output exposes scalar locations, aggregate eightbyte pieces, whole-value
stack rollback and hidden-result-pointer contracts.

Verified block and instruction live sets can be inspected independently:

```sh
build/debug/lunac --emit liveness -o hello.live examples/hello.luna
```

The independently verified physical-location plan can also be inspected:

```sh
build/debug/lunac --emit allocation -o hello.alloc examples/hello.luna
```

The allocation-aware rewrite, including fixed registers, call moves and
clobbers, can be inspected separately:

```sh
build/debug/lunac --emit rewrite -o hello.rewrite examples/hello.luna
```

Assembly emission consumes only a verified rewrite. Register values remain
resident in their assigned physical locations, spills use dense frame slots
and used callee-saved GPRs are preserved explicitly. This is a
correctness-first storage rewrite and performs no optimization.

An executable build may pass every transitive module source unit in one
invocation. Source order has no semantic effect:

```sh
build/debug/lunac --emit obj -o app.o \
  app.luna math.interface.luna math.luna \
  core.interface.luna core.luna
```

The interface begins with `export module`, contains complete type definitions
and bodyless function declarations, and is matched exactly against definitions
in the implementation unit. An `import` exposes only declarations marked
`export` by the imported interface. The compiler rejects missing or duplicate
units, unknown or repeated imports, cycles, ambiguous names, private access,
multiple executable roots and supplied modules unreachable from the root.

Modules can instead be compiled independently. First emit and consume their
versioned `.lmi` interface metadata, then emit and link each module object:

```sh
build/debug/lunac --compile-module app.core --emit metadata \
  -o core.lmi core.interface.luna core.luna
build/debug/lunac --compile-module app.core --emit obj \
  -o core.o core.lmi core.luna

build/debug/lunac --compile-module app.math --emit metadata \
  -o math.lmi math.interface.luna math.luna core.lmi
build/debug/lunac --compile-module app.math --emit obj \
  -o math.o math.lmi math.luna core.lmi

build/debug/lunac --emit obj -o app.o app.luna math.lmi core.lmi
build/debug/lunalink -o app app.o math.o core.o
```

`--compile-module` emits no `_start`. Metadata emission validates the selected
module's source interface and implementation; typed IR, machine IR, assembly
and object emission then require that module's generated `.lmi` plus its
implementation. Every dependency must also be a `.lmi`. Exported Luna
definitions and metadata imports receive global symbols bound to the exact
interface fingerprint, so a stale or mismatched module object is rejected by
the final static link.

The `.lmi` format is deterministic and little-endian. Its fixed header records
the format version, language ABI version, payload size and content fingerprint;
the protected payload begins with the target triple. Each direct import records
the exact dependency fingerprint used to build it. The compiler rejects
truncation, corruption, unsupported versions, target mismatches and stale or
mixed dependency metadata before semantic lowering. The fingerprint is an
accidental-corruption and build consistency check, not a signature for
untrusted distribution.

`--target` defaults to `x86_64-unknown-linux-gnu`, currently the only
supported target. On an x86-64 Linux host, the integration and differential
test runners execute generated static binaries natively when
`qemu-x86_64-static` is unavailable.

An external definition is declared without a body:

```luna
extern fn c_value(input: i32) -> i32;
```

Compile the C23 implementation to an x86-64 object and include that object in
the final `lunalink` command. Luna emits the exact symbol name and does not
implicitly link libc. The current external ABI accepts non-variadic
scalar, pointer, structure and union signatures. It uses the six integer and
eight SSE System V argument registers independently, stack slots and aggregate
memory copies when needed. Fixed arrays remain internal-only by value because
C function types do not have by-value array parameters or results.

## Runtime boundary

Luna target programs are freestanding. Generated executables enter at
project-owned `_start` code and do not link libc. `lunalink` supplies the
project-owned `luna_linux_syscall0` through `luna_linux_syscall6` wrappers,
which convert System V calls to the Linux kernel register ABI and execute
`syscall` directly. The corresponding strongly typed module metadata is
generated at `build/<preset>/sysroot/luna/linux/syscall.lmi`.

The wrappers preserve raw negative Linux error results. The Luna-implemented
`luna.runtime` module builds typed process, file and virtual-memory services on
that boundary. Its `RuntimeError`, `RuntimeFile`, `RuntimeMemory` and result
types make raw descriptors, addresses, byte counts and errors explicit without
introducing `errno`, hidden retries or target libc. Build its deterministic
metadata and object together with the syscall metadata:

```sh
cmake --build --preset debug --target luna_sysroot
```

Compile applications with `sysroot/luna/runtime.lmi` and link
`sysroot/luna/runtime.o`; `lunalink` continues to inject the verified raw
wrapper object. Optional user-supplied `extern fn` objects remain an explicit
FFI choice and are not part of the Luna runtime.

The same sysroot target builds the minimum standard-library modules under
`sysroot/luna/std/`. They provide owned allocations, growable byte buffers,
validated UTF-8 text, NUL-terminated paths and complete file I/O. Every module
is written in Luna, imports only the typed runtime or another standard module,
and is emitted as deterministic `.lmi` and `.o` artifacts. Ownership is an
explicit API invariant until the language has a move or affine type system.

It also builds the M4 frontend modules under
`sysroot/luna/bootstrap/frontend/` and middle-end modules under
`sysroot/luna/bootstrap/middleend/`, plus the correctness-first backend under
`sysroot/luna/bootstrap/backend/x86_64/`. The Luna lexer owns token and
structured diagnostic buffers; the recursive-descent parser owns an
index-linked concrete syntax tree with verified parent/child relationships
and bounded nesting.
The Luna semantic modules consume borrowed parsed units, resolve complete
source-module graphs and target layouts, and emit independently verified,
non-SSA Typed IR. The Luna backend recomputes System V ABI and stack-frame
plans, then emits the project-owned x86-64 assembly dialect without
optimization or C translation. The Luna object, assembler and linker modules
consume that dialect and write a static ELF64 executable directly. They all
run freestanding on the x86-64 target.

The stage-0 compiler remains a hosted C23 development tool. The Luna stage
compiler, assembler and linker are freestanding. Stage 1 is reconstructed by
the C23 seed; stage 1 builds all stage-2 modules and all three stage-2 tools,
and stage 2 does the same for stage 3. Stage 2 and stage 3 assemblies, Luna
objects and tool executables must be byte-identical. Host-library use never
becomes a dependency of generated target programs.

See [the language draft](docs/language.md),
[compiler architecture](docs/architecture.md),
[x86-64 machine IR](docs/machine-ir.md),
[x86-64 System V ABI analysis](docs/abi.md),
[x86-64 liveness analysis](docs/liveness.md),
[x86-64 register allocation](docs/register-allocation.md),
[allocation-aware instruction rewrite](docs/instruction-rewrite.md),
[instruction-level differential testing](docs/instruction-differential-testing.md),
[native ELF64 objects](docs/elf-object.md),
[project-owned static ELF64 linking](docs/elf-linker.md),
[Linux x86-64 system-call ABI](docs/linux-syscall-abi.md),
[freestanding runtime](docs/freestanding-runtime.md),
[minimum standard library](docs/minimum-standard-library.md),
[Luna bootstrap frontend](docs/bootstrap-frontend.md),
[Luna bootstrap middle end](docs/bootstrap-middleend.md),
[Luna bootstrap x86-64 backend](docs/bootstrap-x86-64-backend.md),
[complete Luna-owned bootstrap toolchain](docs/bootstrap-toolchain.md),
[versioned bootstrap seed](docs/bootstrap-seed.md),
[bootstrap reproducibility](docs/bootstrap-reproducibility.md),
[compiled module metadata format](docs/module-metadata.md),
[bootstrap execution semantics](docs/execution-semantics.md), and the
[implementation roadmap](docs/roadmap.md).
