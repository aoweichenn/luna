# Implementation roadmap

Accepted language design and compiler implementation status are intentionally
separate. A checked box means the feature has executable tests.

## M0: direct-codegen vertical slice

- [x] C23 project skeleton with strict warning policy
- [x] source manager and diagnostics
- [x] lexer for the accepted punctuation, literals and keywords
- [x] parser for module units, functions and the M0 statement subset
- [x] exact checking for `i32`, `bool` and no-result functions
- [x] typed CFG IR and verifier
- [x] direct internal scalar calls
- [x] x86-64 System V stack-homed backend
- [x] standalone Linux `_start`
- [x] GoogleTest unit, negative, IR snapshot and executable QEMU tests
- [x] deterministic differential random tests and libFuzzer frontend target

M0 supports enough language to compile arithmetic functions, local variables,
`if`, `while`, calls and integer-returning `main`.

## M1: accepted scalar and memory language

- [x] complete `i64` vertical slice, including IR, x86-64 ABI and traps
- [x] explicit `as` conversions between implemented integer types
- [x] complete `u32` and `u64` vertical slices with unsigned code generation
- [x] type-directed integer IR shared by signed and unsigned widths
- [x] all fixed-width integer types
- [x] target-sized `isize` and `usize`
- [x] `f32` and `f64`
- [x] explicit conversions among every numeric scalar type
- [x] explicit raw-pointer conversions
- [x] complete scalar operators and `do`/`for`/`switch`
- [x] raw pointers, fixed arrays and string literals
- [x] external C declarations
- [x] explicit target model and target data-layout tests

## M2: aggregate and module completion

- [x] structures, unions and scoped enums
- [x] type-only target layout queries
- [x] aggregate initialization
- [x] module interface and implementation matching
- [x] multiple implementation units sharing one module-private scope
- [x] production semantic functions, type layout and consteval execution split
  across same-module implementation units after anchor promotion
- [x] import visibility and dependency validation
- [x] separate compiled module metadata

## M3: production x86-64 backend

- [x] target machine IR
- [x] liveness analysis
- [x] verified linear-scan register allocation result
- [x] scalar stack arguments and verified aggregate ABI classification
- [x] aggregate by-value IR and ABI lowering
- [x] allocation-aware instruction rewrite with fixed-register constraints
- [x] instruction-level differential tests
- [x] native ELF64 relocatable-object writer
- [x] minimal project-owned ELF64 static linker
- [x] versioned Debug IR and final-address DWARF 5 information

## M4: self-hosting

- [x] project-owned x86-64 Linux system-call ABI layer using direct `syscall`
- [x] freestanding runtime with no libc dependency
- [x] standard-library minimum built only on the freestanding runtime
- [x] Luna implementation of lexer and parser
- [x] Luna implementation of type checking and IR
- [x] Luna implementation of correctness-first x86-64 backend
- [x] stage 1, stage 2 and stage 3 reproducibility comparison

## M5: correctness convergence

- [x] exact no-libc decimal-to-binary32/binary64 conversion in the
  self-hosted semantic checker
- [x] direct contextual binary32 rounding without a binary64 double-rounding
  path
- [x] exact subnormal, midpoint, ties-to-even, underflow and overflow handling
- [x] deterministic random and boundary differential execution across stage
  0, stage 2 and stage 3
- [x] close stage-0/self-host semantic differences with exhaustive public
  diagnostic coverage, deterministic combination programs and source-order
  reversal
- [x] harden malformed-input and resource-limit behavior across the complete
  self-hosted pipeline

## M6: self-hosted language authority

- [x] freeze Luna 0 as the C23 reconstruction seed language
- [x] version the self-hosted stage protocol and requested language
- [x] make the Luna compiler the sole implementation authority for Luna 1
- [x] add the Luna 1 unconditional `loop` statement without a C23
  implementation
- [x] prove stage-0 rejection, stage-1 acceptance, stage-1/stage-2/stage-3
  agreement, control-flow correctness and executable behavior

## M7: complete Luna-owned toolchain

- [x] replace the bootstrap closed-dialect assembler with a Luna
  implementation
- [x] replace the bootstrap static ELF64 linker with a Luna implementation
- [x] build stage 2 and stage 3 solely with the preceding Luna compiler,
  assembler and linker, then compare all three tools byte-for-byte
- [x] reject malformed assembly and bootstrap objects with deterministic
  direct-module, negative and mutation tests
- [x] add normal argument-driven self-hosted compiler, assembler and linker
  commands with typed `argc`/`argv`, conventional success status and atomic
  outputs while retaining the versioned fixed protocol
- [x] define a canonical versioned seed containing the fixed-point tools and
  complete Luna reconstruction source graph
- [x] verify the seed manifest, archive representation, static ELF contract,
  external checksum, mutation rejection and byte-identical offline rebuild

M7 closes the planned compiler and bootstrap implementation. Freezing a
release tag and publishing its already-versioned seed are release operations,
not an additional compiler milestone.

## m1: pure-Luna language completion (sub-milestones)

Accepted direction lives in `docs/syntax-plan.md`. A checked box means the
feature has executable tests plus a green `verify` fixed point.

### m1.1 foundation trio

- [x] transparent type aliases (`type Name = Target;`) with chain
      resolution, cycle rejection and export exposure checks
- [x] pointer arithmetic: `p + n`, `n + p`, `p += n`, `p -= n`,
      element-distance `p - q` and same-type ordering comparisons;
      expression-form counts require explicit `as usize`, assignment
      forms accept bare integer literals
- [x] function pointers as first-class scalars: `fn(T...) -> R` types,
      bare-name and `&name` function values, indirect calls with null
      trap, equality against null/same shape, `as` between shapes and
      `usize`, storage in aggregates/arrays/parameters/returns
- [x] enabling backend paths: register-indirect `call`/`jmp` encodings
      (`FF /2`, `FF /4`) with AT&T `*operand` syntax

### m1.2 qualifiers, attributes and static assertions

- [x] `volatile` object and pointee qualification
- [x] `@noreturn` unreachable-successor integration
- [x] `@inline` recorded metadata
- [x] shared attribute grammar and mounting-point validation framework
- [x] compile-time `assert(const bool)` on the m1.5 interpreter seed

### built-ins package (between m1.2 and m1.3)

- [x] intrinsic expressions plus bit operations (`@clz`, `@ctz`,
      `@popcount`, `@rotate_left/right`, `@byte_swap`) as branch-free
      arithmetic expansions
- [x] float helpers (`@sqrt`, `@floor`, `@ceil`, `@trunc`, `@round`,
      `@min`, `@max`, `@abs`) with the six new SSE2 encodings
- [x] overflow intrinsics (`@add_overflow`/`@sub_overflow`/
      `@mul_overflow`), out-parameter plus `bool` shape

### m1.3 kernel UAPI layout package

- [x] anonymous struct/union members
- [x] bitfields (`@bits`) following the x86-64 psABI
- [x] `@align(N)` alignment control with over-aligned SysV rules
- [x] `@packed` structures (no padding; field addresses rejected)
- [x] flexible trailing array member (`[?]T` header types)

### m1.4 character literals, strings, numeric literals and inference

- [x] character literals, string width prefixes (`u16"`, `u32"`), adjacent
      literal concatenation and `\u{...}` escapes; no built-in `char` type
- [x] binary (`0b`) and hexadecimal floating literals
- [x] positional and indexed array initializer lists
- [x] nested `offsetof` designators
- [x] initializer type inference for `let`/`var`

### m1.5 constant functions

- [x] `const fn` scalar interpreter feeding array lengths, enum
      discriminants, assertions and typed constants

### m1.6 labels and goto

- [x] labeled `break`/`continue`
- [x] validated `goto` (`defer` shelved, see syntax-plan decision 2)

### m1.7 variadic functions

- [x] variadic extern calls with the `%al` protocol
- [x] `va_list` with register save areas and typed `va_arg`

  No kernel consumer exists (all syscalls are fixed-arity); this milestone
  started by explicit decision to enable real C-library FFI.

### m1.8 naked assembly functions

- [x] `asm fn` with string-literal bodies, no prologue/epilogue
- [x] `luna.linux.syscall` migrated from linker injection to `asm fn`

### m1.9 source position intrinsics

- [x] `@file()`/`@line()` source position builtins

### m1.10 embedded binary data

- [x] `@embed("path")` file-to-array-constant with determinism rules

### m1.11 module qualification

- [x] `qual::name` qualified access in expression, type and interface
  positions, composing with enum member access
- [x] `import a.b.c as t;` alias imports binding only the qualifier
- [x] source extensions shortened: `foo.la` / `foo.lh`

### m1.12 selective imports

- [x] `import a.b.c::{x, y};` flat-binds only the listed exports, still
  binding the `c::` qualifier
- [x] nonempty unique-name lists, optional trailing comma and precise empty,
  duplicate-name and alias/selective diagnostics

### m1.13 de-prefixed exports

- [x] every library/compiler module migrated off manual name prefixes
  (`std_text_view` → `text::view`, `BootstrapSemanticContext` →
  `context::Context`, `bootstrap_x86_64_text_append_c_string` →
  `asm_text::append_c_string`); consumers use qualified or alias imports,
  landed one module family per gated slice

### m1.14 qualifier-only imports

- [x] plain `import a.b.c;` binds only the `c::` qualifier; flat binding
  requires an explicit selective import `::{x, y}`
- [x] conflicting plain or aliased qualifiers are rejected at the import site,
  even when the qualifier is never used

### m1.15 C calls Luna

- [x] `elf::save` ELF64 `ET_REL` writer (one section per region,
  locals-first symtab, RELA addends, `.note.GNU-stack`)
- [x] `luna-as --emit elf`; round-trip through the project's own
  reader/linker plus an end-to-end gcc host link of a C main against
  Luna `@export_name` definitions

## Current M2: callable infrastructure

This is distinct from the archived Luna 0 bootstrap milestone named `M2`
above. The accepted design direction is tracked in
[`m2-callable-infrastructure.md`](m2-callable-infrastructure.md).

- [x] callable-infrastructure design draft
- [x] M2.0 canonical signatures, deterministic ordering and symbol mangling
- [x] M2.1 bindings, overload sets and declaration/definition matching
- [x] M2.2 exact overload calls and function-pointer selection
- [x] M2.3 default parameters
- [x] M2.4 reusable value-category foundation

## Current M3: object-oriented programming

M3 consumes the completed M2 callable foundation. Its design is tracked in
[`m3-oop-design.md`](m3-oop-design.md).

- [x] class-system design draft
- [x] M3.0a shared callable identities and owner-scoped bindings
- [x] M3.0b class metadata model and semantic-store lifecycle
- [x] M3.0c nominal empty classes and canonical type identity
- [x] M3.0 class metadata, access, overloaded constructors and direct methods
- [x] M3.1 single inheritance and explicit override contracts
- [x] M3.2 read-only global function-address relocations
- [x] M3.3 virtual/abstract dispatch
- [x] M3.4 restricted operators, bound methods, RTTI and friendship
- [x] M3.5 optional opaque classes
- [x] M3.6 class-value composition and ordered member initialization

## Current M4: native generics

M4 follows the completed M3 object model. Its accepted design is tracked in
[`m4-generics-design.md`](m4-generics-design.md). This phase is distinct from
the archived Luna 0 self-hosting milestone named `M4` above.

- [x] native-generics design draft
- [x] M4.0 syntax, generic patterns, substitution and canonical instance
      identity
- [x] M4.1 generic free functions, exact inference and monomorphization
- [x] M4.2 generic structures, unions and transparent aliases
- [x] M4.3 generic classes, methods and concrete M3 interaction
- [x] M4.4 cross-module standard-library proving ground
- [x] M4 release: source commit followed by anchor promotion

## Current toolchain packaging

- [x] one `luna` executable with compile, assemble and link commands
- [x] independent command modules behind a table-driven root dispatcher
- [x] legacy three-tool anchor accepted only at the transition boundary
- [x] unified-tool fixed point and full test suite
- [ ] unified-tool anchor promotion

## Explicitly deferred

Optimization beyond local x86-64 improvements, general metaprogramming beyond
the bounded M4 generic model,
parallel compilation, incremental compilation, package management, language
server work and non-x86-64 backends are outside the bootstrap path.
Using libc to implement the target runtime or standard library is also outside
the accepted design; optional explicit FFI remains supported. Debug
presentation for local variables and types, optimized location lists, macro
information and unwind tables are separate post-bootstrap contracts. The
feature-level dispositions, including every deliberate rejection, are
accounted for in `syntax-plan.md`'s C23 disposition tables.
