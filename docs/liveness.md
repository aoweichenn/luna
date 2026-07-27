# x86-64 liveness analysis

Luna computes virtual-register liveness immediately after verified x86-64
machine IR lowering:

```text
verified x86-64 machine IR
    |
    v
verified liveness
    |
    v
verified register allocation
    |
    v
verified instruction rewrite
    |
    v
register-resident assembly
```

This stage is analysis only. It does not allocate physical registers, rewrite
instructions or optimize code. Its exact instruction sets are consumed by
both allocation and rewrite verification.

## Result model

Every live set is a dense bit vector indexed by
`LunaX8664MachineVirtualRegister`. Its declared value count and storage shape
are part of the checked representation. Unused high bits in the final word
must be zero.

A function result owns:

- the exact virtual-register count;
- the number of complete reverse-order fixed-point sweeps;
- one result for every machine block.

A block result owns `use`, `definition`, `live_in` and `live_out` sets, plus
one result for every instruction. Each instruction records both
`live_before` and `live_after`. Module, function, block and instruction result
ordering exactly matches the verified machine IR; no pointer identity is used
as a hidden index.

`luna_x86_64_liveness_init` and `luna_x86_64_liveness_destroy` define result
ownership. Analysis builds a temporary result and publishes it only after
successful verification, so allocation failure cannot expose a partial
analysis.

## Data-flow equations

For each block `B`, `use[B]` contains values used before a definition in that
block and `def[B]` contains every value defined there. The solver iterates in
reverse block order until no set changes:

```text
live_out[B] = union(live_in[S]) for every successor S
live_in[B]  = use[B] union (live_out[B] - def[B])
```

Instruction sets are then reconstructed backwards from `live_out`:

```text
live_after[I]  = current
current        = (current - def[I]) union uses[I]
live_before[I] = current
```

Duplicate operands and duplicate branch destinations are naturally
idempotent. Empty detached blocks have no successors and produce empty sets.
Declarations have no blocks, no values and zero solver iterations.

The current verified machine-IR contract keeps virtual-register definitions
and uses inside one block. Consequently valid programs currently have empty
block `use`, `live_in` and `live_out` sets, while their instruction-level
sets remain the complete local live ranges needed by the next allocator.
The solver already implements the general CFG equations so that a future,
separately reviewed extension of the MIR value contract does not require a
different analysis interface.

## Independent verification

Liveness verification first requires valid machine IR, then independently
checks:

- module, function, block and instruction result cardinality;
- bit-vector width, ownership metadata and zero padding;
- exact block `use` and `definition` sets recomputed from def/use;
- the successor-union and block transfer equations;
- every backwards instruction transfer;
- agreement between the first instruction's `live_before` and block
  `live_in`;
- non-zero fixed-point iteration metadata for definitions and zero metadata
  for declarations.

The assembly boundary runs analysis before emitting any instruction. A
failure therefore stops compilation rather than silently using stale backend
facts. `lunac --emit liveness` exposes the verified result in a deterministic
text form used by snapshots, review and fuzzing.

## Cost and consumer

With `V` virtual registers, sets use `ceil(V / 64)` words. Instruction result
storage is `O(I * V / 64)`, block storage is `O(B * V / 64)`, and every solver
sweep is `O((B + E) * V / 64)`. This dense representation is simple,
deterministic and efficient for the bootstrap compiler's current function
sizes.

Correctness-first linear-scan register allocation consumes these verified sets
and machine register classes. The allocator reconstructs exact intervals and
marks a value as crossing a call from the intersection of that call's
`live_before` and `live_after` sets. Its independently checked contract is
documented in [x86-64 register allocation](register-allocation.md).

The allocation-aware rewrite verifier also uses each instruction's
`live_before`/`live_after` intersection to prove that no assigned physical
register survives across a matching clobber. Liveness itself performs no
optimization and introduces no target libc dependency.
