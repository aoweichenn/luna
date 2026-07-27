# x86-64 allocation-aware instruction rewrite

Luna consumes verified ABI, liveness and register-allocation results through a
separate rewrite boundary:

```text
verified machine IR
    |
    +--> verified System V ABI
    +--> verified liveness
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

This stage changes physical storage, not language semantics. It performs no
optimization, interval splitting, rematerialization or peephole rewriting.

## Conservative physical-register contract

The current emitter expands machine pseudos through a small fixed scratch and
ABI register set. Those registers are reserved before linear scan:

- GPR: `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8` through `r11`;
- FPR: `xmm0` through `xmm7` and `xmm15`.

Linear scan therefore allocates GPR values only to `rbx` and `r12` through
`r15`, and FPR values only to `xmm8` through `xmm14`. This deliberately
trades register capacity for a simple correctness proof:

- division can use `rax:rdx`;
- variable shifts can use `rcx`;
- memory pseudos can use `rdi`, `rsi`, `rcx`, `r10` and `r11`;
- scalar and aggregate ABI moves can target argument and result registers
  without overwriting an allocated source;
- fixed scratch expansion cannot silently destroy a live allocated value.

System V defines every XMM register as caller-saved. An FPR value live on both
sides of a call is consequently spilled even when it would otherwise use
`xmm8` through `xmm14`. Allocated GPRs are callee-saved and are preserved in
each function frame.

The reservation is a bootstrap policy, not an ABI rule. A later allocator may
recover more registers only after it can split intervals or preserve
live-through values around exact per-instruction clobbers.

## Result model

Every function rewrite owns:

- one exact physical or spill location for every virtual register;
- the dense spill-slot count;
- exact used-register and used-callee-saved-register masks;
- one rewritten record for every machine instruction in flattened position
  order.

Every instruction record owns:

- its original opcode and stable instruction position;
- copied result and ordered use locations;
- fixed input and output register masks;
- a conservative current-emitter clobber mask;
- the destination mask and count of its parallel ABI input moves.

Call records derive their input destinations from the independently verified
System V ABI result. Aggregate pieces contribute their actual GPR or SSE
destinations, a hidden result contributes `rdi`, and scalar or register
aggregate results record their ABI output registers. A call clobbers every
System V caller-saved GPR and XMM register.

## Assembly consumption

The assembly emitter accepts the rewrite only after full verification.
Register locations are used directly by scalar operations. A spilled value is
materialized in its unique eight-byte frame slot; values assigned to
registers no longer receive redundant virtual-register stack homes.

The frame retains exact source object slots, dense spill storage and an
optional hidden-result pointer. Used callee-saved GPRs receive disjoint save
slots after that local data. The complete frame is rounded to 16 bytes, and
the epilogue restores every saved register before `leave`.

Call arguments are expanded after stack arguments have been copied. Because
the allocation pool is disjoint from ABI destinations, the recorded parallel
input set can currently be emitted as deterministic sequential moves without
a cycle-breaking temporary. This invariant is checked rather than assumed.

## Independent verification

Rewrite verification first requires valid machine IR, ABI, liveness and
allocation results. It then independently rejects:

- malformed module, function, value, instruction or use vectors;
- stale instruction positions, opcodes, definitions or ordered uses;
- a copied location that differs from the allocation;
- any allocated value occupying a reserved fixed register;
- stale spill counts, register masks or callee-saved summaries;
- fixed-register, clobber or parallel-move masks that do not match the opcode
  and callee ABI;
- a physical value that is live before and after an instruction whose
  clobber mask contains that register.

The result is built in temporary owned storage and published only after this
verification succeeds. `lunac --emit rewrite` prints the complete checked
plan deterministically. Unit mutation tests corrupt locations, constraint
masks and summaries; integration snapshots, random mutation tests and
coverage-guided fuzzing exercise the public pipeline.

## Runtime boundary

Instruction rewriting introduces no target runtime dependency. Executables
still enter through project-owned `_start` and exit with the Linux x86-64
`syscall` instruction. No target libc is linked.
