# x86-64 System V ABI analysis

Luna has one owned ABI-analysis layer between verified x86-64 machine IR and
assembly emission. It is a correctness contract, not an optimization pass.
The bootstrap target is exactly `x86_64-unknown-linux-gnu`.

```text
verified x86-64 machine IR
              |
              v
 verified System V ABI locations
              |
              v
    reference assembly emitter
```

The implementation follows the x86-64 System V processor supplement's
register classes, scalar argument registers, stack alignment and aggregate
eightbyte rules. `lunac --emit abi` prints the analyzed result after its
independent verifier succeeds. The normative upstream source is the
[x86-64 psABI project](https://gitlab.com/x86-psABIs/x86-64-ABI).

## Scalar parameters

Every implemented machine parameter belongs to exactly one class:

| Machine type | ABI class |
| --- | --- |
| `bool`, integer, pointer | `INTEGER` |
| `f32`, `f64` | `SSE` |

The two register banks are assigned independently while parameters are scanned
in source order:

- `INTEGER`: `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`;
- `SSE`: `%xmm0` through `%xmm7`.

Exhausting one bank does not prevent a later parameter from using the other.
Each parameter that cannot use its class's register bank receives one
eight-byte stack slot. Stack offsets are dense and increase in source
parameter order: `stack[0]`, `stack[8]`, and so on. The caller reserves the
complete stack-argument area, rounded up to 16 bytes, before moving any
arguments and restores `%rsp` after the call.

The reference emitter keeps `%rsp` 16-byte aligned immediately before `call`.
After the return address and old frame pointer are stored, the callee reads
`stack[0]` at `16(%rbp)`, `stack[8]` at `24(%rbp)`, and so on. It then copies
all incoming parameters into the same stack-home representation used for
register parameters. This path is shared by internal calls, compiled-module
calls and non-variadic external C calls.

Scalar results keep the existing System V locations: general-purpose results
use `%rax`, and `f32`/`f64` results use `%xmm0`. Narrow signed arguments at an
external C boundary receive the required extension before placement.

## Aggregate classification

The ABI layer also owns a target-independent input description for one
flattened Luna aggregate layout. A layout records its complete byte size and
alignment plus scalar leaf components. Each leaf has:

- a byte offset;
- a naturally aligned integer scalar size of 1, 2, 4 or 8 bytes, or an SSE
  scalar size of 4 or 8 bytes;
- either `INTEGER` or `SSE` class.

Overlapping leaves are permitted so unions can be classified. Classification
is deterministic:

1. an aggregate larger than two eightbytes is `MEMORY`;
2. an unaligned leaf makes the complete aggregate `MEMORY`;
3. otherwise each touched eightbyte starts as `NO_CLASS`;
4. equal classes remain unchanged, `NO_CLASS` yields to the other class,
   `MEMORY` dominates, and `INTEGER` dominates `SSE`.

The aggregate-classification verifier recomputes the result from the layout
and rejects stale, corrupted or malformed data. Unit tests cover structures,
mixed integer/SSE layouts, overlapping union leaves, unaligned fields,
oversized aggregates and verifier corruption.

This classifier is intentionally ahead of value lowering. Typed IR and
machine IR still represent aggregate objects as memory, so structures, unions
and arrays cannot yet be passed or returned by value. Connecting classified
aggregate values to function signatures, register rollback, stack copying and
multi-register returns is a separate roadmap stage. The compiler does not
claim that support merely because the classification algorithm exists.

## Ownership and verification

A module ABI result owns one function result for every machine function,
including imports and external declarations. Each function result records:

- used general-purpose and vector-register counts;
- exact unrounded stack-argument bytes;
- the aligned caller frame size;
- one class and physical location for every parameter.

Analysis accepts only an empty initialized destination, first verifies machine
IR, builds into temporary owned storage, independently verifies the complete
result, and publishes it only on success. The assembly boundary analyzes the
ABI again and never consumes unchecked locations.

The textual form is deliberately small:

```text
define @f0 app::consume parameters=7 gp=6 sse=0 stack-bytes=8 call-frame=16
  p0 type=i32 class=integer location=%rdi
  ...
  p6 type=i32 class=integer location=stack[0]
```

It is a deterministic review and test format, not a versioned object format.
ABI analysis allocates no target runtime and introduces no libc dependency.
