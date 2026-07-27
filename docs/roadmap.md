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
- [ ] allocation-aware instruction rewrite with fixed-register constraints
- [ ] instruction-level differential tests
- [ ] native ELF64 relocatable-object writer
- [ ] debug information design

## M4: self-hosting

- [ ] project-owned x86-64 Linux system-call ABI layer using direct `syscall`
- [ ] freestanding runtime with no libc dependency
- [ ] standard-library minimum built only on the project system-call layer
- [ ] Luna implementation of lexer and parser
- [ ] Luna implementation of type checking and IR
- [ ] Luna implementation of x86-64 backend
- [ ] stage 1, stage 2 and stage 3 reproducibility comparison

## Explicitly deferred

Optimization beyond local x86-64 improvements, general metaprogramming,
parallel compilation, incremental compilation, package management, language
server work and non-x86-64 backends are outside the bootstrap path.
Using libc to implement the target runtime or standard library is also outside
the accepted design; optional explicit FFI remains supported.
