# x86-64 register allocation

Luna computes a deterministic physical-location plan after verified x86-64
machine IR and verified liveness:

```text
verified machine IR
    |
    v
verified liveness
    |
    v
verified linear-scan allocation
    |
    v
verified instruction rewrite
    |
    v
register-resident assembly
```

This stage allocates locations but does not optimize or rewrite machine
instructions. The independently verified instruction-rewrite stage consumes
the result and supplies the only accepted assembly-emission path.

## Result model

Every virtual register has exactly one inclusive interval `[start, end]` and
one location. Instruction positions are assigned by function block order and
instruction order. A definition starts its interval, and its last use ends the
interval. A value with no uses has a one-position interval.

A location is either:

- one allocatable physical register of the virtual register's class; or
- one function-local spill slot.

The current correctness-first pool uses `rbx` and `r12` through `r15` for GPR
values, and `xmm8` through `xmm14` for FPR values. Fixed scratch and ABI
registers are reserved for instruction expansion. GPR and FPR ownership is
independent.

Each spilled value receives a unique, dense slot number. Slot reuse is
intentionally deferred: unique ownership is simpler to verify and prevents a
storage-lifetime optimization from affecting correctness at this stage.

## Call preservation

For every call, a value is marked `crosses_call` exactly when it appears in
both `live_before` and `live_after`. Under the x86-64 System V ABI, such a GPR
value may be assigned only to `rbx` or `r12` through `r15`. All XMM registers
are caller-saved, so an FPR value crossing a call is spilled.

This is a conservative allocation contract. It does not insert caller-save
stores, split an interval around a call or rematerialize values.

## Deterministic linear scan

Intervals are ordered by `(start, end, virtual-register ID)`. Before each
assignment, owners whose interval ends before the new inclusive start are
expired. The allocator chooses the first free allowed register from a fixed
target order. If none is free, it compares the current interval with the
active allowed interval ending furthest in the future: the longer-lived value
is spilled. There is no interval splitting.

The current MIR verifier requires every virtual-register definition and use
to remain in one block. Therefore each interval is a complete closed local
range with no CFG holes. Extending virtual values across blocks requires a
separately reviewed interval representation before relaxing this constraint.

## Independent verification

Allocation verification first requires valid machine IR and valid liveness,
then independently checks:

- exact module, function and virtual-register result cardinality;
- intervals reconstructed from definitions, uses and call live sets;
- physical-register existence, allocatability and register-class agreement;
- System V call-preservation restrictions;
- absence of overlapping intervals assigned to one physical register;
- unique, dense and in-range spill slots;
- exact used-register and used-callee-saved-register masks.

Allocation is built into a temporary result and published only after this
verification succeeds. `lunac --emit allocation` prints the checked result in
a deterministic review and snapshot format. Unit mutation tests deliberately
corrupt intervals and locations to ensure the verifier rejects them; random
mutation and coverage-guided fuzz tests exercise lowering, liveness,
allocation, instruction rewriting, printing and assembly emission together.

## Rewrite handoff

The allocation result is copied into an owned instruction-rewrite result that
also describes division and shift constraints, parallel ABI destinations,
call clobbers, spill slots and callee-saved preservation. Rewrite verification
proves that no live-through physical value intersects an instruction clobber
before the emitter consumes any location.

The complete handoff is documented in
[allocation-aware instruction rewrite](instruction-rewrite.md). It changes
neither Luna semantics nor the no-libc runtime boundary: target executables
still enter through project-owned `_start` and use the Linux x86-64 `syscall`
ABI directly.

## Cost

With `V` virtual registers, `I` instructions and `R` allocatable physical
registers, interval construction is `O(I + U + C * V)`, where `U` is the
number of machine uses and `C` is the number of calls. Sorting is
`O(V log V)`. The linear scan and independent interference check are
`O(V * R)`; `R` is a fixed x86-64 target constant. Result storage is `O(V)`.
