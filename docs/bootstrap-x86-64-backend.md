# Luna bootstrap x86-64 backend

The M4 bootstrap backend is a separately compiled Luna implementation whose
semantic input is a verified `BootstrapTypedIr` and its verified type table.
It borrows the immutable semantic-input source views only to recover module,
function and external symbol spellings already selected by the IR. It does not
translate Luna to C, call libc or perform optimization. Its first correctness
boundary is x86-64 System V assembly in the same closed dialect accepted by
the project-owned assembler.

## Modules and artifacts

| Module | Responsibility |
| --- | --- |
| `luna.bootstrap.backend.x86_64.abi` | recursive System V classification, parameter/return pieces, register-bank rollback and stack-call areas |
| `luna.bootstrap.backend.x86_64.frame` | deterministic homes for slots, values, aggregate call results and hidden return pointers |
| `luna.bootstrap.backend.x86_64.text` | checked, owned assembly-byte construction |
| `luna.bootstrap.backend.x86_64.codegen` | complete Typed IR instruction selection, function/call lowering, symbols, globals and entry-point emission |

`luna_sysroot` builds deterministic `.lmi` and `.o` files for all four modules
under:

```text
sysroot/luna/bootstrap/backend/x86_64/
```

The backend result owns its assembly, ABI analysis and frame plan. A successful
result must pass `bootstrap_x86_64_backend_is_success`; release is explicit and
validates every owned buffer.

## Correctness-first storage

Every scalar IR value has an eight-byte stack home. Every source slot has a
stable exact-layout home, and every aggregate call result has separate
exact-layout storage plus an address-valued IR home. Frame sizes and call
areas are aligned to 16 bytes and capped at the signed x86-64 displacement
limit.

This is intentionally not register allocation. Short-lived `%rax`, `%rcx`,
`%rdx`, `%r10`, `%r11` and XMM scratch registers implement one instruction at
a time; the observable value is immediately written back to its home. The
policy makes instruction behavior local, deterministic and easy to audit.

Both ABI and frame plans are owned indexed records. Their public validators
recompute the complete plan from Typed IR and compare the records byte for
byte. Code generation never accepts a merely well-shaped but stale or damaged
plan.

Assembly construction is capped at 8388608 bytes. Both bulk append and
single-byte push check the remaining budget before allocation or copying and
preserve the existing text on failure. Exceeding the cap reports
`RuntimeError.out_of_memory`; the stage driver classifies that result as a
resource-limit failure.

## System V boundary

The ABI implementation handles:

- independent six-register integer and eight-register SSE banks;
- recursive structure, union and fixed-array classification into at most two
  eightbytes;
- full rollback of an aggregate parameter to the stack when either register
  bank is exhausted;
- mixed INTEGER/SSE aggregate parameters and returns;
- hidden-return-pointer calls for memory-class results;
- exact stack offsets and 16-byte call-site alignment;
- scalar, narrow integer, pointer, enum and floating-point C interoperability.

Fixed arrays are valid Luna values but are rejected in external C parameter
and return positions because C has no matching by-value array ABI. External
names `_start` and the `_L` namespace are also rejected before IR construction.

Aggregate arguments are snapshotted immediately after each argument is
evaluated. A later argument therefore cannot mutate the already evaluated
representation observed by the callee.

## Instruction semantics

The emitter covers every verified bootstrap opcode:

- integer, Boolean, null and exact-bit floating constants;
- slot, member, global and scaled-pointer addresses;
- exact-width scalar loads/stores, aggregate zeroing and overlap-safe copy;
- null and unsigned bounds traps;
- wrapping integer arithmetic, masked shifts and hardware division traps;
- IEEE binary32/binary64 arithmetic and ordered/unordered comparisons;
- integer/float conversions, including the complete `u64` range and explicit
  out-of-range or NaN traps;
- calls, jumps, branches, scalar/aggregate returns and `_start`.

No optimization, liveness analysis or physical register allocation occurs in
this stage. Those concerns cannot change language behavior while the
self-hosted path is being proven.

Scaled-pointer address instructions consume the element-size scale already
verified in Typed IR. The backend does not infer a second scale from the
pointer expression's surface type; this is required for indexing an array
lvalue, whose expression type is the complete array rather than one element.

## Assembly and linking

Luna symbols use `_L<hex-module>_<hex-function>`; external C symbols remain
unmangled. Globals are module-local labels. Executables define a freestanding
`_start`, initialize MXCSR deterministically, call `main` and exit through
Linux syscall 60. Generated programs contain no dynamic loader or libc
dependency.

The integration gate sends every generated assembly file through both:

1. the project-owned closed assembler and native ELF64 object verifier; and
2. LLVM MC as an independent syntax/encoding oracle.

The object produced by the project assembler is linked by `lunalink` and
executed. LLVM is test-only and is not part of the compiler or target runtime.

## Verification

The stage is guarded by:

1. GoogleTest compilation of every real backend interface and implementation
   through verified stage-0 IR, assembly and object emission;
2. deliberate ABI and frame-plan corruption followed by recomputation-based
   rejection and restored-plan acceptance;
3. actual execution of scalar, control-flow, pointer, fixed-array, string,
   aggregate, narrow integer, binary32/binary64 and conversion programs;
4. register, stack, mixed aggregate and hidden-return System V calls,
   including independently assembled external ABI functions;
5. a fixed-seed random differential corpus whose expected exit statuses are
   calculated independently of the backend;
6. project-assembler, LLVM-MC, ELF verification, static linking and
   no-output execution checks for every corpus member;
7. a target-side exact-boundary append followed by a one-byte-over rejection
   for the assembly text owner;
8. two byte-for-byte reproductions of every backend `.lmi` and `.o`, plus
   unresolved-symbol and dynamic-loader exclusion checks.

This completes the Luna x86-64 backend item in M4. Its output now closes the
[stage 1/stage 2/stage 3 reproducibility comparison](bootstrap-reproducibility.md).
