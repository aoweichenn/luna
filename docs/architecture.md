# Compiler architecture

## Non-negotiable boundaries

The bootstrap compiler is C23. Luna source is never translated to C. The
initial target is exactly `x86_64-unknown-linux-gnu`: x86-64 instructions,
System V calling convention and ELF64 objects or executables.

The compiler owns the language semantics. LLVM tools are currently used only
to encode emitted x86-64 assembly and link ELF64 test executables. They are not
used as the compiler IR, optimizer or instruction selector.

## Pipeline

```text
source units
    |
    v
lexer -> parser -> syntax tree
                    |
                    v
             name and type checking
                    |
                    v
             typed Luna IR (CFG)
                    |
                    v
          x86-64 instruction selection
                    |
                    v
              assembly emission
                    |
                    v
       LLVM MC -> ELF64 object -> LLD
```

Assembly is an output encoding of the x86-64 backend, not an intermediate
source language. A native ELF64 relocatable-object writer is planned after the
instruction set and relocation model have stabilized.

## Frontend

Source locations are byte spans into immutable source files. Tokens and syntax
nodes retain spans so every parser, type and IR error can point to the original
text. The parser uses an arena and never owns isolated syntax nodes.

Compilation is split into global and local phases:

1. parse every source unit;
2. validate module units and imports;
3. collect exported and module-private declarations;
4. match interface declarations with implementation definitions;
5. type-check function bodies;
6. lower checked bodies to IR.

This order removes source-order dependencies and ordinary forward declarations.

## Luna IR

The bootstrap IR is deliberately non-SSA. It is a typed control-flow graph
with virtual values and explicit local slots:

- `const`
- `load` and `store`
- width-explicit `i32` and `i64` integer arithmetic
- comparisons
- direct calls
- unconditional and conditional branches
- return

Every reachable basic block has exactly one terminator. Detached empty merge
blocks are permitted, while non-empty detached blocks must also terminate.
Virtual values are defined once, remain local to one basic block and must be
defined before use. IR verification runs before the backend in every build
mode. Mutable variables use slots, avoiding phi nodes until optimization work
demonstrates that SSA is worth its compiler cost.

The verifier independently checks exact operand and result types, call
signatures and flattened argument ownership, terminator placement, cached
predecessor counts and graph reachability. Backend emission never receives
unchecked compiler-generated IR.

The IR is target-neutral. Target-specific registers, calling convention,
instruction encodings and relocations must not appear in it.

## x86-64 backend

The first backend is correctness-first:

- System V 32-bit and 64-bit integer argument and result registers;
- explicit stack frames;
- virtual values assigned stack homes;
- deterministic labels and symbol mangling;
- a Linux `_start` shim that exits through syscall 60;
- no dependency on a target C runtime.

This backend is intentionally not the performance endpoint. Planned stages are:

1. correct stack-homed code;
2. x86-64 machine IR;
3. liveness;
4. linear-scan register allocation;
5. peephole and local instruction selection improvements;
6. ELF64 relocatable-object emission.

The simple backend remains available as a reference backend for differential
testing after optimization is introduced.

## Error handling

Invalid user input must produce a diagnostic and a non-zero exit code, never a
crash or assertion. Internal invariants are checked by the IR verifier.
Allocation and I/O failures are propagated explicitly.

## Testing

The quality gate contains:

- GoogleTest unit tests for utilities, source handling, lexing, parsing,
  semantic lowering, IR invariants and x86-64 emission;
- parser, type and module-error negative tests;
- textual IR snapshots;
- x86-64 assembly validation through LLVM MC;
- static ELF64 linking through LLD;
- execution under `qemu-x86_64-static`;
- deterministic generated-program differential tests;
- deterministic mutation tests and a coverage-guided libFuzzer target;
- UBSan runs for the host compiler and ASan runs on compatible native hosts;
- warnings treated as errors.

Generated x86-64 programs are freestanding in the first milestone, which makes
cross-target execution deterministic and independent of a target sysroot.
