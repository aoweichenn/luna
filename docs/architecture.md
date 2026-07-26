# Compiler architecture

## Non-negotiable boundaries

The bootstrap compiler is C23. Luna source is never translated to C. The
initial target is exactly `x86_64-unknown-linux-gnu`: x86-64 instructions,
System V calling convention and ELF64 objects or executables.

The compiler owns the language semantics. LLVM tools are currently used only
to encode emitted x86-64 assembly and link ELF64 test executables. They are not
used as the compiler IR, optimizer or instruction selector.

External C functions are represented directly throughout the pipeline; Luna
never generates a C translation unit. The final ELF link may combine a
Luna-generated object with caller-supplied C23 objects or libraries.

Target selection produces an immutable target description before semantic
lowering begins. It records the architecture, operating system, ABI, byte
order, scalar sizes and ABI alignments. The target description is carried by
the typed IR module so target-sized language types never depend on host C
properties.

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

The syntax tree represents `if`, all three loop forms and non-fallthrough
switch arms directly. Semantic lowering uses one ordered control-frame stack:
`break` selects its innermost loop or switch frame, while `continue` searches
for the innermost loop frame. This preserves nesting semantics without adding
target-specific control constructs.

Parsed type references preserve pointer qualification and recursive
fixed-array shape. Semantic lowering interns those references into canonical
types, so exact pointer and array equality is independent of syntax-node
identity. Canonical semantic type IDs are distinct from the closed syntax
type-kind enum; dynamic composite identities are never stored in that enum.
Composite values are kept out of the scalar ABI until aggregate ABI
classification is implemented.

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
- type-directed integer arithmetic shared by fixed-width and target-sized
  integer types
- binary32 and binary64 constants, arithmetic and ordered comparisons
- explicit integer conversions whose extension or truncation follows source
  and target type metadata
- exact floating widening, rounded floating narrowing and type-directed
  signed/unsigned integer-to-floating and checked floating-to-integer
  conversions
- comparisons
- direct calls to internal definitions and typed external C declarations
- unconditional and conditional branches
- return

Conditional expressions use a typed temporary slot at their merge. Switch
lowering stores the controlling value once and emits an ordered chain of typed
equality branches. `do` and `for` are expressed entirely with ordinary basic
blocks. No conditional, loop or switch opcode is hidden from the verifier or
backend.

IR pointers are intentionally opaque address values, while every indirect
load, store and element-address instruction carries the scalar access type or
element size it needs. This follows the same separation as modern opaque
pointer IRs: source-level pointee compatibility is established by semantic
checking, and the verifier independently checks address operands, access
types, bounds checks, slot layouts and global-data references.

Local slots record byte size and ABI alignment instead of assuming one
eight-byte home. Fixed arrays therefore occupy their exact target layout while
scalar virtual values remain stack-homed. Immutable string bytes live in a
module global-data table and are referenced through target-neutral global
address instructions.

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

Function linkage is explicit IR metadata. An internal function owns parameter
slots, values and a CFG body. An external C function owns only its typed
signature and must have no slots, values, call-argument storage or blocks.
External functions may be callees but may never be the module entry point.
Textual IR prints them as bodyless `extern fn` declarations.

The IR instruction set is target-neutral. Each module is parameterized by an
explicit target data layout so `isize` and `usize` retain their exact IR types
while width-dependent verification and conversion printing remain
deterministic. Textual IR records the target triple. Target-specific
registers, calling convention, instruction encodings and relocations do not
appear in IR instructions.

## x86-64 backend

The first backend is correctness-first:

- System V's first six integer argument-register assignments and integer result
  register for every fixed-width and target-sized integer type;
- System V's first eight SSE argument-register assignments and SSE result
  register for `f32` and `f64`, classified independently from integer
  registers so mixed signatures can use both banks;
- explicit stack frames;
- virtual values assigned stack homes;
- canonical zero-extended raw bits in the low 32 bits for 8-bit and 16-bit
  arguments, results and stack homes, with explicit sign extension at signed
  comparisons, division, right shifts and widening conversions;
- canonical `bool` values after both direct and indirect memory loads,
  including raw-pointer aliasing;
- deterministic labels and symbol mangling;
- exact, unmangled ELF names for external C functions, with `.extern`
  declarations and unresolved relocations left for the final linker;
- C ABI sign extension for external `i8` and `i16` arguments and explicit
  canonicalization of external `_Bool` results at the language boundary;
- scalar `movss`/`movsd` arithmetic and ordered `ucomiss`/`ucomisd`
  comparisons, with an explicitly initialized IEEE floating-point
  environment;
- scalar SSE format and integer conversions, including software sequences for
  the unsigned 64-bit range and explicit traps before invalid
  floating-to-integer operations;
- exact-width indirect scalar loads and stores, checked fixed-array indexing,
  raw-pointer scaled addressing and deterministic read-only data emission;
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
- executable matrices and boundary traps for every numeric scalar conversion
  family;
- executable conditional matrices for every scalar type and switch-boundary
  matrices for every integer type;
- exact-width memory matrices, null and bounds traps, read-only qualification
  negatives and typed-memory IR mutation checks;
- real C23-to-Luna static linking tests covering every scalar type, pointers,
  no-result calls, narrow signed promotion and independently classified
  integer/SSE register banks;
- structured-control negative cases, IR snapshots and randomized differential
  programs;
- deterministic mutation tests and a coverage-guided libFuzzer target;
- UBSan runs for the host compiler and ASan runs on compatible native hosts;
- warnings treated as errors.

Generated x86-64 programs are freestanding in the first milestone, which makes
cross-target execution deterministic and independent of a target sysroot.
