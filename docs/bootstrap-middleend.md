# Luna bootstrap middle end

M4 now contains a separately compiled Luna implementation of name resolution,
strong type checking, target layout and verified Typed IR construction. It
consumes the indexed syntax-tree contract from the Luna bootstrap parser and
does not call the hosted C23 semantic analyzer.

## Module boundary

The implementation is split into three independently compiled modules:

| module | responsibility |
| --- | --- |
| `luna.bootstrap.middleend.type` | canonical built-ins, pointer/array interning, named-type identity, fields and complete x86-64 layouts |
| `luna.bootstrap.middleend.ir` | non-SSA functions, parameters, slots, values, blocks, instructions, call arguments, globals and an independent verifier |
| `luna.bootstrap.middleend.sema` | source-module graphs, visibility, declarations, type checking and syntax-to-IR lowering |

`luna_sysroot` writes deterministic metadata and objects at:

```text
sysroot/luna/bootstrap/middleend/type.lmi
sysroot/luna/bootstrap/middleend/type.o
sysroot/luna/bootstrap/middleend/ir.lmi
sysroot/luna/bootstrap/middleend/ir.o
sysroot/luna/bootstrap/middleend/sema.lmi
sysroot/luna/bootstrap/middleend/sema.o
```

All three modules use the freestanding runtime and owned byte buffers. They
contain no external declaration, libc call, host callback or direct system
call. File loading and compiled-metadata decoding remain compiler-driver
responsibilities; the semantic input is deliberately a list of borrowed,
already parsed source units.

## Stable indexed representation

Every growable table owns bytes and exposes `usize` IDs. A type, field,
function, parameter, slot, value, block, instruction, call argument or global
is addressed by an index, never by a pointer into reallocatable storage.
`bootstrap_type_no_id` and `bootstrap_ir_no_id` are the only absent-ID values.

The type table starts with a fixed, verified built-in prefix. Pointer types are
interned by pointee and read-only qualification; arrays are interned by exact
element type and positive element count. Structures, unions and enums retain
distinct named identities. Layout state distinguishes declared, resolving and
complete types, which permits pointer recursion while detecting every
by-value cycle.

The bootstrap target is fixed to the accepted
`x86_64-unknown-linux-gnu` data layout: eight-byte pointers, maximum
eight-byte natural alignment, one-byte `bool`, and exact fixed-width scalar
layouts. Structure padding, union maximum size and nested-array
multiplication are checked for overflow before a layout becomes complete.
Every complete object is capped at the signed 32-bit displacement limit,
matching the hosted compiler and correctness-first x86-64 backend.

## Modules and declarations

Semantic input order has no meaning. The checker first pairs interface and
implementation units by complete dotted module name, then resolves direct
imports and validates:

- one interface and one implementation at most;
- known, non-self, non-repeated imports;
- an acyclic direct-import graph;
- direct, non-transitive export visibility;
- unambiguous imported names and no local collision with an imported export;
- exact interface/implementation function signatures;
- one body for every ordinary interface function;
- no implementation-side `export`;
- no private named type in an exported field or function signature;
- one executable `main` with the exact `fn main() -> i32` signature;
- reachability of every supplied executable module.

Declaration collection precedes resolution. Forward references therefore do
not depend on source order. Field, enum-member, function, parameter and local
duplicates carry both the primary and related source span in structured
diagnostics. Enums require an exact integer underlying type; explicit and
implicit values are range-checked in that width. IR functions are emitted in
canonical complete-module-name and function-name order, so reversing source
unit order preserves generated assembly.

## Strong typing

The Luna middle end implements the Luna 0 rules without C promotions or
truthiness:

- conditions are exactly `bool`;
- scalar binary operands have one exact type;
- integer and floating literals take their contextual type and otherwise use
  `i32` and `f64`;
- a cast target supplies context to an otherwise untyped literal expression,
  including the full unsigned 64-bit range;
- unary minus accepts the exact minimum value of every signed integer width;
- `null` requires a pointer context;
- enums convert only to or from their exact underlying type;
- pointer casts cannot remove read-only qualification;
- pointer/integer casts use only `usize`;
- arrays do not decay;
- aggregate assignment, arguments, results and conditional expressions use
  exact type identity;
- lvalue mutability follows bindings, pointer qualification and member/index
  selection;
- `sizeof`, `alignof` and `offsetof` are checked type-only constants.

Named aggregate initialization allocates exact-layout temporary storage,
zeroes the whole object, rejects unknown or repeated fields, limits a union to
one selected field, and evaluates field expressions in source order only after
the complete field set is valid. Scalar and enum `{}` initializers produce
their exact typed zero value. Returned arrays remain address-backed immutable
temporaries and support checked element reads. String escapes are decoded into
read-only IR globals with an explicit trailing zero byte.

### Exact decimal floating-point literals

The self-hosted semantic checker converts decimal literals directly to their
contextual IEEE-754 binary32 or binary64 type. It does not use libc, a host
floating-point parser, the host rounding mode, or an intermediate binary64
value for binary32 literals.

Conversion uses fixed-capacity unsigned multi-precision integers. The parser
normalizes the decimal coefficient and exponent, retains 1200 significant
digits plus a nonzero-tail sticky bit, and constructs the exact power-of-five
rational. That bound conservatively exceeds the exact decimal length of every
finite binary64 value and every adjacent-value midpoint, so discarded digits
can only decide which side of an otherwise exact boundary the source occupies.
The 128 64-bit limbs also cover the worst reachable coefficient and
power-of-five products.

The converter binary-searches the finite positive encoding space, compares
the decimal rational against candidate dyadics exactly, and then compares
against the exact midpoint to implement round-to-nearest, ties-to-even.
Underflow may round to zero, including the zero/minimum-subnormal tie.
The maximum-finite/infinity midpoint and larger values produce
`floating_overflow`. A binary32 result is bit-cast and widened exactly only
after binary32 rounding has completed.

## Typed control-flow IR

The self-hosting IR is deliberately non-SSA. Mutable bindings and merge
results use explicit typed slots. Values have one definition and one owning
function/block. Blocks form an explicit CFG with cached predecessor counts
and exactly one terminator for every non-empty reachable block.

The instruction set records:

- typed integer, floating, boolean, null and global constants;
- local, member and indexed addresses;
- scalar loads/stores, whole-object zeroing and sized copies;
- null and array-bounds checks;
- typed unary, binary, comparison and conversion operations;
- direct typed calls with separately owned argument records;
- jumps, branches and typed returns.

`if`, `while`, `do`, `for`, Luna 1 `loop`, non-fallthrough `switch`, `&&`,
`||` and the conditional operator are all lowered to ordinary blocks and
branches. `loop` has no condition block; normal completion and `continue`
return directly to its body entry, while `break` targets its exit. There is
no hidden structured-control opcode and no optimization pass. Missing
return analysis traverses the completed CFG from the function entry instead
of treating a detached block's local predecessor count as global
reachability.

`bootstrap_ir_is_valid` is independent of semantic construction. It checks
all buffers and ranges, type IDs, function ownership, slot/parameter
ownership, value definitions, instruction lists, target blocks, terminator
placement, predecessor counts, call arguments, globals and entry identity.
A successful semantic result requires both a complete type table and a
verified IR.

## Ownership and failure

`BootstrapSemanticInput` owns only its unit-record buffer. Source text, tokens
and syntax trees are borrowed and must outlive `bootstrap_semantic_check`.
The semantic result owns its type table, IR and diagnostic buffer. Each owner
has one release function, and release functions validate storage before
freeing it.

Language errors are structured `BootstrapSemanticDiagnostic` records with a
kind, unit, primary span, optional related unit/span and numeric detail.
Allocation or invariant failures use `RuntimeError` and never masquerade as a
language diagnostic.

Recursive semantic work has explicit stack contracts. Import traversal and
reachability are limited to 256 module edges, by-value type-layout resolution
to 256 types and lexical lowering to 256 active scopes. Semantic input accepts
at most 2048 interface/implementation units and retains at most 4096
diagnostics. Exceeding a contract produces `resource_limit` or a deterministic
resource failure before work continues. A failed scope entry returns the
absent-ID sentinel, so callers cannot accidentally leave a scope that was
never entered or corrupt the active-scope count.

## Source-module compilation

Unit zero identifies the root module for a semantic invocation. In library
mode, the root implementation must satisfy its interface, while reachable
imported modules may be supplied as interfaces without implementation bodies.
In executable mode, reachability starts at the module containing the selected
entry point. Any supplied module outside that closure is rejected.

Record field type IDs are resolved before any field records for that owner are
appended. Recursive dependency layout can therefore add its own fields without
interleaving them into the owner's contiguous field range. This invariant is
checked by the type-table verifier and by a library-mode regression containing
an imported aggregate followed by another root field.

## Verification

The stage is guarded by:

1. GoogleTest compilation of every real Luna middle-end interface and
   implementation through verified stage-0 IR, assembly and object emission.
2. Target x86-64 executions covering the complete scalar, aggregate, pointer,
   layout-query and structured-control surface, including aggregate
   temporaries and external declarations.
3. Source-order-independent executable graphs and separate library-module
   compilation with interface-only dependencies.
4. Deliberate in-memory corruption of built-in layouts, call arguments and
   CFG predecessor counts, proving that the independent verifiers reject
   damage and accept the restored representation.
5. Exact negative diagnostics for recursive types, unknown types, duplicate
   locals, immutable writes, mismatches, invalid casts, invalid control flow,
   missing returns and by-value type-depth exhaustion.
6. Stage-0/stage-2/stage-3 convergence for 23 fixed execution programs,
   32 fixed-seed generated programs, reversed multi-module source order and
   one rejection for every public semantic diagnostic kind from 1 through 50.
7. Fixed-seed valid and invalid random semantic programs, also registered as
   the semantic fuzz gate.
8. Two byte-for-byte reproductions of every middle-end `.lmi` and `.o`, ELF
   symbol checks, unresolved-symbol checks and dynamic-loader exclusion.
9. Exact-rational decimal probes at normal, subnormal, binade, underflow and
   overflow boundaries, including deterministic random adjacent-value
   midpoints, ties-to-even, values on both sides, long discarded tails and
   stage-0/stage-2/stage-3 execution agreement.

This completes the Luna type-checking and Typed IR item in M4. The verified
`BootstrapTypedIr` is consumed by the
[Luna bootstrap x86-64 backend](bootstrap-x86-64-backend.md) and the complete
source-module graph participates in the
[stage reproducibility gate](bootstrap-reproducibility.md).
