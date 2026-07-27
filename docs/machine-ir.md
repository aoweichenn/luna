# x86-64 machine IR

Luna's x86-64 machine IR is the verified target boundary between the
target-neutral typed IR and x86-64 instruction emission:

```text
typed Luna IR
    |
    v
x86-64 machine lowering
    |
    v
verified pre-allocation machine IR
    |
    v
verified liveness, allocation and instruction rewrite
    |
    +--> x86-64 assembly review output
    |
    +--> native x86-64 encoding and ELF64 object emission
```

It is deliberately small. It introduces the target facts needed by the
backend without adding an optimizer, SSA construction, physical registers or
an instruction-encoding model.

## Target model

A machine module is valid only for `x86_64-unknown-linux-gnu`. It owns:

- executable or library module kind;
- immutable target description;
- read-only or writable global byte sequences with explicit alignment;
- functions and declarations;
- an entry function only for executables.

Its vectors and global bytes are independent copies. The immutable target
description, source spans and function-name views remain borrowed from the
active compilation and must outlive the machine module.

The machine type set is `void`, `bool`, fixed signed and unsigned integers,
`f32`, `f64` and opaque pointers. Target-sized source types no longer exist:
`isize` lowers to `i64` and `usize` lowers to `u64`.

Scalar machine types have an explicit register class:

| Type | Class |
| --- | --- |
| `bool`, integers, pointers | general-purpose (`gpr`) |
| `f32`, `f64` | floating-point (`fpr`) |
| `void` | none |

Register classes are allocation constraints, not physical-register
assignments.

## Functions and storage

Each function retains its module name, source name, linkage, optional compiled
module fingerprint, parameter types and return type. Every parameter and
result also owns a parallel aggregate descriptor. A scalar descriptor is
empty; an aggregate descriptor records exact size, alignment and flattened
scalar leaves when register classification needs them. Values larger than two
eightbytes are unconditionally memory-class and do not duplicate their leaf
graph. Linkage is one of:

- internal definition;
- module export definition;
- compiled-module import declaration;
- external C declaration.

Definitions own basic blocks, stack slots, virtual-register types and a
flattened call-argument table. Declarations own only a signature and identity.

A stack slot records its fixed machine type when scalar, byte size, alignment
and whether it represents a scalar or an exact-layout memory object. Aggregate
objects remain in memory. Their by-value IR carrier is an opaque pointer
virtual register naming verified exact-layout storage; aggregate calls also
name an explicit result slot. The ABI boundary, rather than ordinary pointer
semantics, expands those carriers into register pieces, stack copies or a
hidden result pointer.

## Instructions

Machine instructions are target-specific pseudos. Their categories are:

- fixed-width integer, floating, boolean and null constants;
- slot and indirect loads and stores;
- slot, global, member and indexed addresses;
- null and bounds checks;
- zeroing and overlap-safe memory copies;
- integer, floating and pointer conversions;
- integer and floating arithmetic and comparisons;
- direct calls;
- jumps, conditional branches and returns.

Integer opcode semantics use the machine type to distinguish width and
signedness. Memory pseudos record the exact access type or object size.
Calls remain ABI pseudos at this stage; the current emitter performs System V
argument and result placement while expanding them.

Every instruction exposes a uniform def/use interface. A value-producing
instruction defines exactly one virtual register. Operand virtual registers
are returned in evaluation order, and every explicit call argument is a use.
This interface is the input contract for the liveness stage.

## Verification

Machine verification is mandatory after lowering and before assembly emission.
It rejects:

- an unsupported or inconsistent target and malformed module storage;
- an executable without exactly one valid internal entry definition;
- invalid linkage, metadata identity or declaration/definition ownership;
- invalid scalar parameter and result types;
- invalid stack sizes, alignments, scalar layouts or virtual-register types;
- missing, duplicate or mistyped virtual-register definitions;
- invalid or mistyped uses, including every call argument;
- aggregate arguments that do not address exact-layout snapshot slots and
  aggregate returns that do not address exact-layout return snapshots;
- aggregate calls whose result slots do not match the callee descriptor;
- opcode, immediate, memory-width, slot, global, callee or block mismatches;
- malformed terminators, branch targets or cached predecessor counts.

Non-empty blocks always terminate. Empty detached blocks are accepted because
typed lowering may create unreachable merge blocks; they may neither be the
entry block nor have predecessors.

The verifier is independent of the typed-IR verifier. Lowering first requires
valid typed IR, then builds an owned machine module, and finally validates the
new representation on its own terms.

## Text form

`lunac --emit mir` writes deterministic, human-readable machine IR:

```text
target-machine x86_64
target-triple "x86_64-unknown-linux-gnu"
module-kind executable

define @f0 example::main linkage=internal () -> i32 {
  vreg %v0 type=i32 class=gpr

  bb0 predecessors=0:
    %v0 = const.integer type=i32 imm=0x000000000000002a
    return type=void uses=[%v0]
}
```

Function, global, stack-slot, virtual-register and block IDs are local numeric
identities. The format also prints linkage, metadata fingerprints, register
classes, definitions, ordered uses and instruction-specific operands. It is
intended for review, tests and backend debugging; it is not a versioned
serialization format.

## Current boundary

The current assembly boundary analyzes verified machine IR, allocates physical
registers, builds fixed-register instruction rewrites and independently
verifies every result before emission. The emitter consumes register and spill
locations, preserves used callee-saved registers and expands ABI moves only
from the checked rewrite. It performs no optimization. Generated executables
remain freestanding: `_start` exits through the Linux x86-64 `syscall`
instruction and no target libc is linked.

The analysis representation and invariants are documented in
[x86-64 liveness analysis](liveness.md); the physical-location result is
documented in [x86-64 register allocation](register-allocation.md), and scalar
register/stack placement plus aggregate lowering are documented in the
[x86-64 System V ABI analysis](abi.md). Fixed-register constraints, parallel
call moves, spill materialization and callee-save preservation are documented
in [allocation-aware instruction rewrite](instruction-rewrite.md).
