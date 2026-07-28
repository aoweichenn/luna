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

## Explicitly deferred

Optimization beyond local x86-64 improvements, general metaprogramming,
parallel compilation, incremental compilation, package management, language
server work and non-x86-64 backends are outside the bootstrap path.
Using libc to implement the target runtime or standard library is also outside
the accepted design; optional explicit FFI remains supported. Debug
presentation for local variables and types, optimized location lists, macro
information and unwind tables are separate post-bootstrap contracts.
