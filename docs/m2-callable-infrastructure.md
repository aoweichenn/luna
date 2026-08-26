# M2 callable infrastructure design

## Status and purpose

M2 is the callable and call-resolution foundation for the pure-Luna compiler.
It precedes the M3 class/OOP phase. The older uppercase M2 in `roadmap.md`
belongs to the archived Luna 0 bootstrap milestones and is unrelated to this
phase.

M2 stabilizes the common machinery required by:

- overloaded free functions;
- default parameters;
- function-pointer selection;
- future class methods and overloaded constructors;
- future operators and non-owning bound methods;
- deterministic interface/implementation matching and symbol identity.

M2 adds no class syntax, inheritance, virtual dispatch, ownership, lifetime,
implicit conversion, generics or exception semantics.

## Why this phase comes before classes

The current compiler largely assumes:

```text
name -> one SymbolId
module export symbol -> module name + function name
```

Classes would immediately require method sets, constructor sets, operator sets
and signature-distinct symbols. Adding each feature independently would create
several incompatible call resolvers. M2 instead establishes one callable model
that M3 can reuse unchanged.

## Non-negotiable rules

1. No implicit numeric, pointer, user-defined or inheritance conversion enters
   overload ranking.
2. Return type alone never distinguishes overloads.
3. Candidate selection is exact and deterministic; zero or multiple survivors
   are errors.
4. Parameter names remain outside callable identity.
5. Default values remain outside function types and ABI symbol identity.
6. Every exported Luna callable has a collision-free signature-based symbol.
7. Source-unit order cannot affect overload candidate order, IR order or
   emitted bytes.
8. Arguments are evaluated exactly once and left-to-right after one candidate
   has been selected.

## Canonical type encoding

M2 first defines a target-independent canonical encoding for every type that
can appear in a callable signature.

```text
CanonicalType :=
    builtin tag
  | named type identity
  | pointer qualifiers + CanonicalType
  | fixed array count + CanonicalType
  | function parameter list + result CanonicalType
```

Required properties:

- independent of transient semantic `TypeId` values;
- independent of source-unit order;
- recursive with the existing type-depth limit;
- unambiguous without relying on a fixed-width hash;
- identical for transparent aliases and their resolved target type;
- sensitive to pointer `const`/`volatile`, array length, variadic state and
  calling convention;
- named structure/union/enum identity uses canonical module name plus declared
  type name, not physical layout or source path.

M2.0 freezes canonical signature format version 1. Multi-byte integers are
little-endian. A free-function signature is:

```text
u8  version                 = 1
u8  calling convention      = 0 (System V)
u8  callable kind           = 0 (free function)
u8  owner kind              = 0 (no owner)
u8  receiver kind           = 0 (no receiver)
u8  flags                   = variadic:1 | noreturn:2 | external:4
u32 fixed parameter count
CanonicalType parameters[parameter count]
CanonicalType result
```

Owner and receiver discriminants are present in version 1 even though M2 emits
only their zero forms. M3 can add payloads for nonzero discriminants without
changing the position or meaning of existing fields.

Canonical type tags are independent of semantic `TypeId` and `type_info::Kind`
ordinals:

| tag | type | payload |
| ---: | --- | --- |
| 1 | `void` | none |
| 2 | `bool` | none |
| 3..7 | `i8`, `i16`, `i32`, `i64`, `isize` | none |
| 8..12 | `u8`, `u16`, `u32`, `u64`, `usize` | none |
| 13..14 | `f32`, `f64` | none |
| 15 | pointer | `u8` qualifier bits, then pointee type |
| 16 | fixed array | `u64` element count, then element type |
| 17..19 | structure, union, enum | canonical module and declared name |
| 20 | function pointer | convention, `u32` parameter count, parameter types, result type |
| 21 | `va_list` | none |

Pointer qualifier bits are `const:1 | volatile:2`. A named identity is `u32`
module-component count, then every component as `u32` byte length plus bytes,
followed by the declared name in the same length-prefixed form. Transparent
aliases emit only their resolved target encoding. Tag zero is reserved as an
invalid sentinel and is never emitted.

The corresponding conceptual grammar is:

```text
V                         void
B                         bool
I8 I16 I32 I64 IS         signed integers
U8 U16 U32 U64 US         unsigned integers
F32 F64                   floating types
N <module> <name>         named type
P <qualifier bits> <type> pointer
A <count> <type>          fixed array
C <parameter list> R <t>  function pointer
```

Names and variable-length lists use explicit byte lengths. The ELF spelling may
hex-encode these canonical bytes, preserving exact identity without collision.

## Callable signatures and keys

The shared semantic representation is:

```text
CallableSignature {
    calling_convention
    callable_kind
    owner_type
    receiver_kind
    first_parameter / parameter_count
    variadic
    return_type
    semantic_flags
}
```

M2 uses `callable_kind = free_function`; fields for static/instance methods,
constructors and operators are reserved for M3 rather than retrofitted later.

Three related identities must remain distinct:

### Overload key

```text
name
callable kind
owner type
receiver kind
parameter types
variadic state
calling convention
```

The result type is excluded from overload selection. Two declarations that
share an overload key but disagree on result type are a signature mismatch,
not two overloads.

### Function type

```text
calling convention
parameter types
variadic state
result type
```

Names, owner, parameter names, attributes and defaults are excluded.

### Link/ABI identity

```text
canonical module
owner type when present
source name
callable kind
receiver kind
parameter types
variadic state
result type
calling convention
ABI-affecting semantic flags
```

The result type participates here so incompatible caller/callee objects do not
silently resolve to the same symbol.

## Signature-based symbol mangling

Ordinary Luna exports move from:

```text
_L <module hex> _ <name hex>
```

to this exact version-1 spelling:

```text
_L <hex(canonical dotted module)> _ <hex(source name)> __ <hex(version-1 signature)>
```

The signature itself carries owner and receiver identity. Full canonical bytes
are used rather than a short hash. If a hash is ever introduced for
symbol-length control, the compiler must retain and compare full canonical
signatures to detect collisions deterministically.

Exceptions:

- `extern fn` uses the exact external C symbol;
- `@export_name` uses its exact validated symbol;
- two declarations may not emit the same verbatim external name;
- C symbols do not form Luna overload sets at link time.

## Deterministic callable order

Every callable is sorted by:

```text
canonical module name
canonical owner type (empty in M2)
source name
callable kind
receiver kind
canonical parameter types
variadic state
canonical result type
calling convention
```

This order drives:

- semantic overload candidate slices;
- IR function construction and body lowering;
- diagnostics that list candidates;
- future method and vtable construction;
- reproducible assembly under source-unit permutation.

## Bindings and overload sets

The name layer becomes explicit:

```text
Binding {
    kind
    first_candidate
    candidate_count
}

BindingKind {
    type
    constant
    callable_set
}
```

One top-level name still cannot denote both a type/constant and a callable set.
An overload set owns one or more callable candidates sorted by canonical
signature.

```luna
fn convert(value: i32) -> i64;
fn convert(value: f32) -> f64;
fn convert(value: *const u8) -> usize;
```

Exact duplicate overload keys are rejected. Parameter names do not distinguish
candidates. A result-only difference reports `signature_mismatch`.

The first implementation may use sorted buffers and bounded linear scans. M2
does not require a general hash table; the representation, not average lookup
complexity, is the compatibility boundary.

### M2.1 storage model

M2.1 keeps declaration identity and name binding as separate layers:

```text
Symbol                         one exact declaration/source identity
Function                       one canonical overload candidate
Binding                        one module-local callable name
callable_candidates[]          function IDs in canonical global order
Binding.first_candidate/count  one contiguous slice of that order
```

Every `Function` retains its own `Symbol`, so overload diagnostics and debug
locations point at the exact declaration rather than at an arbitrary
representative of the set. Each function also records its owning binding. The
binding records one representative spelling only for lookup; it does not own
the declarations or collapse their visibility flags.

The candidate array is built once after canonical signatures are available.
It is sorted by module, name and canonical signature, then partitioned into
binding slices. IR construction and function-body lowering consume this same
array; no downstream pass builds another function order. A boundary validator
checks that slices are contiguous, cover every function exactly once, contain
only one module/name identity and agree with every function's `binding_id`.

Visibility is evaluated per candidate:

- an implementation unit sees every candidate in its module-local binding;
- an interface unit sees the candidates declared in that interface;
- an importing module sees only exported candidates;
- private implementation-only overloads therefore extend local resolution
  without leaking into the imported overload set.

The emitting lowerer preserves its established path when exactly one visible
candidate exists. A multi-candidate binding is never reduced to its first
element: M2.2 probes its arguments or expected function type and requires one
exact survivor before any argument IR is emitted.

Declaration collection uses a small classification state rather than mixing
diagnostics with mutation:

```text
add_candidate
attach_definition
duplicate_overload
contract_mismatch
non_callable_conflict
```

The overload key compares parameter types/order, variadic state and the
extern/calling-convention boundary. Result type and `@noreturn` are checked as
the separate declaration contract. Parameter names, `@inline` and `asm fn`
body form do not affect either identity. One bounded scan returns both the
first same-name function and an exact overload-key match; no hash table or
second declaration scan is introduced at this stage.

## Interface and implementation matching

An interface overload set and all implementation units of the same module are
merged by overload key:

```luna
// interface
export fn convert(value: i32) -> i64;
export fn convert(value: f32) -> f64;
```

```luna
// one or more implementations, any source order
fn convert(value: f32) -> f64 { ... }
fn convert(value: i32) -> i64 { ... }
```

Rules:

- every non-extern interface candidate has exactly one body across all supplied
  implementation units;
- a matching overload key with a different result or ABI flag is a mismatch;
- a second body for the same overload key is a duplicate definition;
- implementation-only private overloads may extend the set with distinct keys;
- source and implementation order do not affect candidate identity or output;
- `asm fn` implementation form does not create another overload key;
- `@inline` does not affect identity;
- `@noreturn` is a caller-visible semantic contract and must be validated
  consistently even though it does not affect parameter selection.

## Exact overload resolution

For a call `name(arguments...)`, resolution is:

1. resolve one lexical/module binding;
2. obtain its callable candidates;
3. filter by callable kind and calling context;
4. filter by accepted arity after default parameters;
5. run side-effect-free argument compatibility against candidate parameter
   types;
6. require exactly one candidate;
7. lower every explicit argument once, left-to-right, using the selected
   parameter types;
8. materialize omitted constant defaults in declaration order;
9. emit one direct call.

There is no ranking after filtering. Multiple survivors report
`ambiguous_call`; the related location is the first visible declaration in
canonical candidate order, independent of source-unit order.

## Side-effect-free argument probing

The current compiler lowers many expressions directly into IR while type
checking. Overload resolution cannot speculatively lower an argument once per
candidate: that would duplicate diagnostics, allocations and side effects.

M2 therefore introduces a non-emitting compatibility path:

```text
probe_expression(node, expected_type) ->
    incompatible
    compatible
    ambiguous
    invalid
```

The probe:

- emits no IR and mutates no local/control-flow state;
- produces no user diagnostic during candidate filtering;
- may recursively resolve nested calls using the same deterministic rules;
- validates contextual literals and aggregate initializers against one expected
  type;
- records enough selected-call information to avoid resolving the same nested
  call differently during final lowering;
- lowers the selected expression exactly once after the outer candidate is
  known.

M2.2 implements this as the `luna.bootstrap.middleend.semantic.expr.probe`
type-only service. Its facade contains result construction, literals, names,
initializers and access expressions; same-module `probe/operators.la` and
`probe/call.la` implementation units own operator compatibility and callable
selection. These are implementation boundaries behind one interface, not
additional dependency-graph modules.

`CandidateResolution` is a small state accumulator. Definite incompatibility
eliminates one candidate, nested ambiguity remains ambiguity, and invalid
expression state is retained only while no exact candidate can be selected.
The resolver visits the already canonical candidate slice once and performs no
ranking or source-order tie break.

Nested successful calls are memoized in a lazily allocated table with one
function ID per syntax node. The table is allocated only when a multi-candidate
call is actually probed. Final lowering validates the cached binding and uses
the same selected function, then lowers each explicit argument once from left
to right. This avoids both speculative IR rollback and a sparse linear cache
whose repeated lookups would become quadratic.

Compile-time source I/O is deliberately outside candidate probing:
`@embed(...)` must first be materialized in an explicitly typed binding before
that binding is passed to an overload set. Malformed type syntax inside a cast
or `va_arg` retains the ordinary type resolver's diagnostic; ordinary
compatibility misses themselves emit no diagnostic.

## Contextual argument rules

### Numeric literals

If a literal is compatible with multiple exact parameter types, the call is
ambiguous:

```luna
fn set(value: i32);
fn set(value: i64);

set(1);        // ambiguous_call
set(1 as i32); // exact
```

Luna does not add a C++-style preferred integral type solely to rank overloads.

### Null pointers

```luna
fn use(value: *A);
fn use(value: *B);

use(null);       // ambiguous_call
use(null as *A); // exact
```

### Aggregate initializers

An untyped `{}` compatible with multiple candidate aggregates is ambiguous. A
typed binding/cast or otherwise unique candidate is required.

### Existing expressions

An expression with an already known exact type matches only the identical
parameter type. M2 adds no promotions or qualifier conversions to ordinary
arguments.

## Function values and pointers

An overload set is not a run-time value. A complete target function type may
select one candidate:

```luna
fn transform(value: i32) -> i64;
fn transform(value: f32) -> f64;

let callback: fn(i32) -> i64 = transform;
let explicit: fn(i32) -> i64 = &transform;
```

Without an expected function type:

```luna
let callback = transform; // ambiguous_function_value
```

The target function type compares full parameter/result shape. Default
parameters do not participate. Luna's current function-pointer syntax is
fixed-arity, so a variadic function cannot form a function value; direct
variadic calls remain supported. Indirect calls retain their existing exact
argument-count rule.

## Import behavior

Qualified lookup returns the complete overload set from its target module:

```luna
math::convert(value);
```

Selective imports from different modules do not merge overload sets:

```luna
import left::{parse};
import right::{parse}; // ambiguous_import
```

This remains an import-time conflict even when the two modules export disjoint
parameter signatures. Callers use module qualifiers to select the intended
set. Luna does not adopt C++ `using`-declaration overload merging or ADL.

## Default parameters

### Syntax

```luna
export fn open(
    path: *const u8,
    flags: u32 = 0,
    mode: u32 = 0
) -> i32;
```

The implementation omits defaults:

```luna
fn open(path: *const u8, flags: u32, mode: u32) -> i32 { ... }
```

### Declaration rules

- defaults appear exactly once, on the interface or unique declaration;
- an implementation definition cannot repeat or change them;
- after the first defaulted parameter, every later named parameter has a
  default;
- the variadic tail cannot have a default;
- parameter names remain outside interface identity;
- named arguments are not introduced, because they would make parameter names
  part of the public call contract.

### Expression rules

The first implementation accepts only expressions fully resolved during
interface checking:

- literals and typed constants;
- enum members;
- constant arithmetic/conversions;
- `sizeof`, `alignof` and other accepted constant layout queries;
- valid `const fn` results.

Defaults cannot reference `self`, another parameter, a local, mutable state, a
runtime call or a source-position intrinsic.

### Type and ABI rules

- defaults are not part of an overload key;
- defaults are not part of a function-pointer type;
- defaults are not part of symbol mangling;
- changing a default is a source API change requiring importer recompilation;
- the caller inserts omitted values; the callee always receives its full
  parameter list.

### Interaction with overloads

Defaults expand accepted arities but add no priority:

```luna
fn draw(color: Color);
fn draw(color: Color, width: i32 = 1);

draw(red); // ambiguous_call
```

Multiple viable candidates remain ambiguous. M2 performs no C++ preference for
the overload requiring fewer inserted defaults.

Default parameters are initially rejected on `extern`, variadic and
`@export_name` functions. M3 initially rejects defaults on virtual/abstract or
override methods, avoiding C++'s split between statically chosen defaults and
dynamically chosen bodies.

## Callable attributes and identity

| Property | Overload key | Function type | Link identity |
| --- | ---: | ---: | ---: |
| parameter types/order | yes | yes | yes |
| result type | no | yes | yes |
| variadic | yes | not currently expressible | yes |
| calling convention/extern | yes | yes | yes |
| receiver/owner (reserved for M3) | yes | no | yes |
| parameter names | no | no | no |
| default values | no | no | no |
| `@inline` | no | no | no |
| `@noreturn` | no | no | semantic contract |
| `asm fn` body form | no | no | no |
| `@export_name` spelling | separate binding | no | exact external name |

The implementation must validate semantic-contract mismatches separately from
overload selection.

## Value-category foundation for M3

M2 also consolidates the existing expression flags needed by future receivers
without adding method syntax:

```text
ValueCategory {
    type_id
    address_id
    lvalue
    mutable
    volatile
    temporary
}
```

The service must answer whether an expression is addressable, mutable,
read-only or a temporary. M3 will build `ReceiverBinding` from this service for
`object.method()` and `pointer->method()` rather than embedding another set of
ad-hoc checks in member lowering.

M2 does not add reference types. Existing explicit pointers remain sufficient
for future receivers and avoid introducing another alias/lifetime surface.

## Diagnostics

At minimum M2 appends stable kinds for:

- duplicate overload signature;
- overload result/ABI mismatch;
- missing overload definition;
- ambiguous call;
- no matching overload;
- ambiguous function value;
- invalid default placement;
- invalid default expression;
- repeated default on implementation;
- external/variadic default rejection;
- internal canonical-signature collision/invariant failure.

The related candidate location is chosen canonically and points to its exact
declaration. New enum values append at the end so existing diagnostic ordinals
remain stable.

## Determinism and limits

- candidate counts are bounded by source and symbol limits;
- canonical type recursion uses the existing maximum type depth;
- canonical encodings use checked byte buffers;
- candidate probing is bounded and cannot emit IR repeatedly;
- source-unit permutation must reproduce byte-identical assembly;
- declaration order within one interface must not change overload identity;
- malformed or colliding internal encodings are compiler invariants, never
  silently accepted user programs.

## Explicitly excluded from M2

- classes, methods, constructors and inheritance surface syntax;
- operators and bound methods;
- virtual dispatch, vtables and RTTI;
- ownership, borrowing, lifetime or move semantics;
- automatic destruction or RAII;
- implicit conversions and C++ overload ranking;
- user-defined conversions;
- generics/templates/concepts;
- named arguments;
- ADL or cross-module overload-set merging;
- module-scope variables;
- exceptions.

These are either M3 consumers of the callable foundation or separate future
language designs.

## Delivery sequence

Every slice lands behind `audit`, `refmt`, `verify` and `test`, followed by
anchor promotion before compiler sources adopt new syntax.

### M2.0: canonical identity and mangling

- canonical type/signature encoding;
- collision-free signature-based Luna symbol names;
- deterministic signature-aware callable sorting;
- byte-exact symbol and source-order permutation tests.

This slice may change every Luna export symbol and therefore requires a full
toolchain/object rebuild and dedicated anchor promotion.

### M2.1: overload bindings and declaration matching

- `Binding` and `FunctionOverloadSet` representation;
- overloaded free-function declarations;
- exact duplicate/result mismatch rules;
- interface/multi-implementation matching by overload key;
- import and visibility integration.

### M2.2: exact call and function-value resolution

- side-effect-free argument compatibility/probing;
- exact candidate filtering with no ranking;
- literal/null/aggregate ambiguity rules;
- deterministic candidate diagnostics;
- expected-function-type resolution for function values;
- lower selected arguments once, left-to-right.

### M2.3: default parameters

- parser and interface representation;
- trailing-default and constant-expression validation;
- call-site insertion after overload selection;
- overload/default ambiguity diagnostics;
- source API and function-pointer tests.

### M2.4: value-category consolidation

- one reusable addressability/mutability/temporary service;
- regression coverage for existing field, pointer and aggregate temporaries;
- no class syntax yet;
- final foundation consumed by M3 receiver binding.

## Required test matrix

- overloads by arity, scalar type, pointer qualification, array and function
  pointer shape;
- duplicate overload and result-only mismatch;
- declarations/definitions split and reordered across implementation units;
- exact and ambiguous numeric literals, `null` and `{}`;
- nested overloaded calls without duplicate evaluation;
- function pointer target-type selection and untyped ambiguity;
- qualified overload calls and rejected selective-set merging;
- default arities, invalid gaps and non-constant defaults;
- overload/default ambiguity;
- extern/variadic/export-name restrictions;
- deterministic callable order and mangling under declaration/source-unit
  permutation;
- linker rejection of incompatible signature symbols;
- full fixed-point rebuild before any M3 implementation begins.

## M2 completion gate

M2 is complete only when one callable pipeline handles free functions and
provides stable extension points for M3 owner/receiver kinds. M3 must not need a
parallel method overload resolver, method-specific mangle format or separate
default-argument engine.
