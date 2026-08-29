# Luna semantic pipeline

## Status and scope

The semantic pipeline validates the parsed source-module graph, constructs the
canonical type model and lowers checked functions into `luna.compiler.ir`.
This document records the first nine ownership/domain batches of its modernization.

The root module is still named `luna.bootstrap.middleend.sema` while the
downstream semantic dependency graph is contracted incrementally. The current
batch does not rename the graph or claim that the large transitional Context
has already become private.

## Domain foundation

`luna.compiler.sema.domain` is the first modern semantic dependency boundary.
It replaces the historical callable and value-category modules and now owns
the stable passive class/generic metadata in one 158-line interface with two
same-module behavior implementation units.

The module contains only closed, passive compiler-domain values:

- `CallableKind`, `ReceiverKind` and `CallableIdentity` describe free
  functions, methods and receiver qualification;
- `ValueCategory` and `ReferenceRank` describe expression storage and
  reference-binding rank;
- `ClassAccess`, dispatch/flag enums, `DispatchPlan` and the `Class*` records
  describe class policy and contiguous metadata slices;
- `GenericDeclarationKind`, `GenericInstanceState` and the `Generic*` records
  describe generic declarations, substitutions and concrete instances;
- stateless predicates validate identities, compare ownership, project value
  categories and compute reference compatibility.

Names are intentionally explicit after the merge: generic `Kind`, `Identity`
and `Category` became `CallableKind`, `CallableIdentity` and `ValueCategory`.
Closed-kind behavior uses `switch`; the module owns no allocation or mutable
session state and therefore needs no class hierarchy, virtual dispatch or
runtime RTTI.

The domain module depends only on `luna.compiler.types`. ClassTable and
GenericTable now depend downward on domain records, Context alone owns both
tables, and every higher pass consumes records through its existing `domain::`
dependency. Direct compiler imports of class/generic owner modules contract
from 5/8 to 1/1 without adding a module or object. The resulting order is
strictly acyclic: types, domain, table owners, Context, then semantic passes.

## Class metadata ownership

`ClassTable` replaces the transparent class-model Store and its four raw byte
buffers. It is a move-only RAII class that privately composes vectors of the
domain `ClassRecord`, `ClassField`, `ClassMethod` and `ClassFriend` values plus
a sticky runtime error. Context owns one table; normal Context destruction
destroys its vectors, so semantic cleanup has no class-model release call.

The four append methods are the only storage-growth boundary. `append_record`
accepts only a type identity and declaration flags, then constructs every empty
slice and unpublished metadata field internally. The first field, method or
friend captures the current typed-vector end; every later append must continue
exactly at that end. This preserves the existing contiguous per-class slices
without exposing byte sizes, casts or synchronized storage counts to callers.
An allocation or invariant failure records the first error and blocks later
mutation.

The move constructor transfers all four vectors and the sticky error. Each
moved-from vector returns to a valid empty state and the source error resets to
`none`, making destruction and explicit storage inspection well-defined. The
relocation contract verifies this state as well as all three slice families
and sticky failure behavior.

TypeTable is the single source of truth for both the base relation and hidden
vptr offset. ClassRecord no longer mirrors either value; it contains only class
policy, member slices and published runtime metadata. Consumers receive const
typed projections. Hierarchy and runtime-metadata phases mutate the table only
through `mark_polymorphic`, `mark_abstract`, `enable_rtti`,
`assign_virtual_slot`, `publish_descriptor` and `publish_vtable`. These bound
operations validate the phase-specific precondition before publication, so no
caller can retain a writable vector pointer across append or reallocation.

## Generic metadata ownership

`GenericTable` replaces the transparent generic Store, eight raw byte buffers
and eight manually synchronized counts. It is a move-only RAII owner of domain
generic declarations, parameters, instances and active bindings plus private
index entries. Context destruction releases the table automatically; the
common semantic cleanup path has no generic-model release call.

The table owns four related invariants:

- declarations accept parameters only while they are the latest open
  declaration, preserving one contiguous parameter slice;
- instance argument slices are appended transactionally and an existing full
  identity returns the canonical instance instead of duplicating it;
- the open-addressed instance index is rebuilt completely in a replacement
  vector and moved into place only after every existing instance is indexed;
- type/function reverse maps reject conflicting entries and prepare both maps
  before publishing either result mapping.

All consumers receive const typed record/argument pointers and counts. The one
former external mutation, generic declaration `binding_id`, is now a bound
method with conflict validation. A sticky first error blocks later mutation;
active-binding truncation remains available to complete an in-flight rollback
without clearing that error.

The current compiler emits strong generic method symbols per concrete type.
`vector<usize>` already exists in the initializer module, so the table uses one
module-specific `IndexEntry` value for arguments, buckets and reverse maps.
The exported-class ABI currently requires that private generic argument type
to be visible; `IndexEntry` is therefore exported but is not returned by the
public operation surface. An exact-size assertion protects the read-only
argument projection. This is a bounded toolchain adaptation, not a public raw
storage contract.

## Session lifecycle

The root implementation now uses one private `SemanticSession` class. It owns
the mutable Context for exactly one semantic invocation and has three explicit
phases:

| phase | meaning |
| --- | --- |
| `ready` | Context exists and owns all pass work buffers |
| `complete` | every selected pass has run and work buffers have been released |
| `transferred` | TypeTable, IR Module and diagnostics have moved into the public result |

`run()` accepts only `ready`. Invalid input still creates the structured
`invalid_input` diagnostic, then follows the same completion path as an
ordinary compilation. `finish()` releases transient pass storage once and
preserves the first runtime failure. `take_result()` accepts only `complete`
and performs the single result transfer. The destructor releases transient
storage only when a session never reached `finish()`.

This State pattern represents a real lifetime boundary: it prevents the old
invalid-input early return from bypassing common work cleanup, prevents a
second result transfer and gives later semantic passes one stable owning
object into which methods can migrate.

The 29 fixed pipeline entries share one private `SemanticPass` function-pointer
shape. Their `bool` now means only “the Context runtime channel remains usable”;
ordinary semantic invalidity is represented by diagnostics and does not stop
later diagnostic collection. `SemanticSession::run_pass` skips invocation once
`Context.error` is non-`none` and converts an unexplained `false` without a
runtime error into `invalid_argument`. The pass order remains 29 explicit
method calls rather than an opaque function table.

Reachability is parameterized and may return `false` after successfully adding
a resource-limit diagnostic, so entry selection has a separate bound adapter.
It distinguishes a recorded diagnostic from an unexplained failure using the
diagnostic count and preserves the same runtime/diagnostic channel split.

## Input ownership and borrowing

`Input` is the move-only RAII owner of semantic source-unit records and path
bytes. It privately composes `vector<Unit>` and `byte_buffer`, retains the
first construction failure and exposes append through `add()`. Appending one
unit is transactional: path bytes are appended first, and a failed unit append
rolls the path buffer back to its previous size.

`InputView` is a copyable, read-only projection containing typed Unit storage,
path bytes, counts and the executable/library mode. It provides C++-shaped
`data()` and `size()` access plus bounded `unit()` and `path()` queries.
Validation checks:

- pointer/count storage shape and the module-count limit;
- non-empty final input;
- UTF-8 source text without embedded NUL;
- TokenView and SyntaxView validity with matching token counts;
- every path range and UTF-8 path slice.

The command driver is the sole Input owner. It builds all units, creates one
InputView and keeps the owner alive through semantic analysis and code
generation. Context and `CodeGenerator` store only the borrowed view. A view
must not survive owner move or destruction; the relocation contract moves the
owner before creating the view and verifies the moved-from storage state.

## Diagnostic ownership

`Diagnostic` remains a passive source record. It contains kind, primary and
related locations and numeric detail but owns no memory.

`DiagnosticBuffer` replaces the historical byte buffer plus manually
synchronized count. It is a move-only RAII class over
`vector<Diagnostic>` and owns these invariants:

- storage is a valid typed vector;
- size never exceeds `Limit.maximum_diagnostics`;
- `add()` either appends one complete record or returns an error;
- destruction releases the vector automatically.

`DiagnosticView` is the read-only pointer/count boundary used by the driver.
It validates its borrowed storage and provides `empty()`, `size()` and bounded
`get()`. Consumers can no longer read or mutate diagnostic bytes directly.

The semantic Context owns a `DiagnosticBuffer`; diagnostic production remains
the module-level `add_diagnostic` operation for this batch, but the operation
delegates storage mutation and capacity enforcement to the bound owner method.

## Public result

`SemanticResult` is a transparent one-shot transfer record containing:

- move-only `TypeTable` ownership;
- move-only `ir::Module` ownership;
- move-only `DiagnosticBuffer` ownership;
- the semantic runtime error.

The record is intentionally a struct. It has no behavior or independent
invariant beyond bundling the three already-validated RAII owners for
return-value transfer. A movable class could express the same representation,
but would add a decorative boundary without a responsibility. The private
session owns behavior, while the public record remains transparent and
move-only through its fields.

`result_is_success` is a stateless final predicate. Success requires no runtime
error, no diagnostics, a complete valid TypeTable and independently valid IR.
There is no semantic result release function: normal scope destruction invokes
the three resource destructors.

## Pass order

`SemanticSession::run()` preserves the established correctness order:

1. module and import collection;
2. import-graph validation and attribute validation;
3. named type and class collection;
4. base relationships and preliminary layouts;
5. const-function, constant, array, alignment and bit-field pre-scans;
6. complete type resolution;
7. friends, fields, classes and bit-field validation;
8. constant resolution and enum finalization;
9. functions, methods and method validation;
10. module assertions, imported-name checks and public-type visibility;
11. IR function creation, vtable creation and statement lowering;
12. entry selection, reachability and generic-model validation;
13. common work cleanup and result transfer.

These calls remain focused lower-level pass functions for now. Wrapping the
sequence in a session does not falsely turn each existing pass into a method;
methods migrate only when their state and dependency boundary are redesigned.
Alignment and bit-field entry passes now return runtime readiness like every
other pipeline entry; their internal validation helpers may still use local
boolean validity while emitting diagnostics.

Function IR construction is part of the `functions` module rather than a
`functions.ir` child module. Its cohesive implementation remains in
`semantic/functions/ir.la`, while `create_ir_functions` and
`create_ir_function_instance` are declared by the 74-line parent interface.
Sema and expression probe now consume one `functions` dependency; the child
interface, reverse parent import, registry node and linked object are removed.

## Entry selection

Entry selection is now a bound session operation. Function traversal uses
`for`, and main-signature validation uses named predicates with at most two
logical clauses per condition. The accepted executable shapes remain:

- `fn main() -> i32`;
- `fn main(argc: usize, argv: **const u8) -> i32` with the established pointer
  qualification contract.

Library mode continues to select reachability from unit zero. Executable mode
diagnoses missing or duplicate main definitions, rejects external main and
sets the IR entry through `ir::Builder` before graph reachability validation.

## Implementation units

| file | responsibility |
| --- | --- |
| `semantic/domain/callable.la` | callable identity construction and validation |
| `semantic/domain/category.la` | value-category projection and reference binding |
| `semantic/classes/model.la` | ClassTable typed ownership, slice construction, hierarchy publication and sticky failure |
| `semantic/generics/storage.la` | GenericTable lifetime, declarations, bindings and read-only projections |
| `semantic/generics/instances.la` | canonical instance insertion, hash rebuilding and reverse maps |
| `semantic/generics/validation.la` | declarations, slices, indexes, maps and final-state validation |
| `semantic/functions/ir.la` | same-module IR function, receiver and parameter construction |
| `middleend/sema.la` | private SemanticSession, phase orchestration, entry selection and result transfer |
| `semantic/context.la` | transitional Context storage and shared low-level services |
| `semantic/context/input.la` | Input owner, InputView validation and Context unit/path access |
| `semantic/context/diagnostics.la` | DiagnosticView, DiagnosticBuffer and final success predicate |

The two domain files implement one real `luna.compiler.sema.domain` module.
Class/generic records need no artificial implementation file because they are
passive declarations with no behavior.
`context/input.la` and `context/diagnostics.la` are additional implementations
of the existing context module, not new submodules; no consumer imports them
independently.

## Current Luna feature review

| feature | disposition |
| --- | --- |
| Classes | SemanticSession owns phases; Input, DiagnosticBuffer, ClassTable and GenericTable own resource lifetimes |
| Generics | semantic records and private index entries use typed vectors instead of exposed byte arithmetic |
| Composition | Input composes vector/byte_buffer; Context composes the semantic owners |
| Access control | session phase, metadata vectors, diagnostic storage and sticky errors are private |
| Constructors/destructors | constructors establish ready/empty states; destructors close each resource path once |
| Copy/move | Input, DiagnosticBuffer, ClassTable and GenericTable are move-only; views are copied borrows |
| Overloads/defaults | no operation has one semantic family that benefits from overloads or a meaningful default |
| Operators | no natural value operator exists for a semantic session or diagnostic stream |
| Bound methods | owner mutations remain bound; SemanticPass is a plain function pointer because passes capture no receiver |
| Friends | unnecessary because the public methods preserve the required boundaries |
| Virtual dispatch/RTTI | rejected: these are closed compile-time domains without runtime substitution |

The domain module adopts enums, structs and switch-based free predicates
because its values are passive and stateless. Adding a class merely to group
those functions would introduce pattern-shaped indirection without an owner.

## Deliberately deferred work

The next semantic batches should remain independently green:

1. contract context/lookup/builder into a coherent session module after its
   interface can remain narrow;
2. migrate pass families to bound methods or focused strategy objects where
   they own real state;
3. rename the remaining `luna.bootstrap.middleend.semantic.*` graph only when
   the new dependency boundary is acyclic and independently useful.

## Validation gates

Every semantic ownership change must pass on the isolated caw host:

```sh
python3 tools/selfhost.py audit
python3 tools/refmt.py --check
python3 tools/selfhost.py verify --fresh
python3 tools/selfhost.py test
```

The relocation-data contract exercises Input, ClassTable and GenericTable
move/moved-from storage; class-member and generic-parameter slices; hierarchy
flags, virtual-slot assignment and descriptor/vtable publication; generic
rehash, deduplication, reverse maps and rollback; sticky owner failures;
borrowed InputView use; and IR ownership transfer. The ordinary negative corpus
exercises DiagnosticBuffer capacity, DiagnosticView lookup and stable leading
diagnostic kinds through the command driver.
