# Luna semantic pipeline

## Status and scope

The semantic pipeline validates the parsed source-module graph, constructs the
canonical type model and lowers checked functions into `luna.compiler.ir`.
This document records the first thirteen ownership/domain batches and the
current lowering-state batch of its modernization.

The root module is still named `luna.bootstrap.middleend.sema` while the
downstream semantic dependency graph is contracted incrementally. The current
batch does not rename the graph or claim that the large transitional Context
has already become private.

## Domain foundation

`luna.compiler.sema.domain` is the first modern semantic dependency boundary.
It replaces the historical callable and value-category modules and now owns
the stable passive class/generic/callable metadata in one 208-line interface
with two same-module behavior implementation units.

The module contains only closed, passive compiler-domain values:

- `CallableKind`, `ReceiverKind` and `CallableIdentity` describe free
  functions, methods and receiver qualification;
- `ValueCategory` and `ReferenceRank` describe expression storage and
  reference-binding rank;
- `ClassAccess`, dispatch/flag enums, `DispatchPlan` and the `Class*` records
  describe class policy and contiguous metadata slices;
- `GenericDeclarationKind`, `GenericInstanceState` and the `Generic*` records
  describe generic declarations, substitutions and concrete instances;
- `Function`, `Parameter`, `Binding` and their closed enums describe stable
  callable declarations and published overload slices without owning storage;
- stateless predicates validate identities, compare ownership, project value
  categories and compute reference compatibility.

Names are intentionally explicit after the merge: generic `Kind`, `Identity`
and `Category` became `CallableKind`, `CallableIdentity` and `ValueCategory`.
Closed-kind behavior uses `switch`; the module owns no allocation or mutable
session state and therefore needs no class hierarchy, virtual dispatch or
runtime RTTI.

The domain module depends only on `luna.compiler.types`. ClassTable,
GenericTable and CallableTable depend downward on domain records; Context
composes all three, and every higher pass consumes records through `domain::`
plus const owner projections. The resulting order is strictly acyclic: types,
domain, table owners, Context, then semantic passes.

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
12. entry selection, reachability and generic/callable-owner validation;
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

Public-type visibility is likewise a parent `types` responsibility. The
141-line `types/visibility.la` implementation retains its recursive generic and
anonymous-field checks, but only `validate_public_types` is exported through
the 68-line parent interface. The two recursive helpers are module-private;
sema imports no visibility child facade, and the child interface/registry/object
boundary is removed.

Field lookup is also a parent `types` operation. The 84-line
`types/lookup.la` implementation retains direct, anonymous-promotion and base
search as one recursive algorithm family. Only `lookup_field` is exported
through the 71-line parent interface; direct and promoted helpers are private.
Consteval and expression passes already consume `types`, so their child lookup
imports and the remaining visibility-independent interface/object/registry
boundary are removed.

## Symbol ownership and name lookup

`SymbolTable` is the move-only owner of unit, module, import and symbol records.
Each family uses a typed vector, so Context no longer mirrors byte lengths with
four separately mutable counters. Its public data projections are const;
interface/implementation registration, contiguous import-slice publication,
graph visit/reachability state and symbol flag/value changes pass through bound
methods with sticky runtime failure.

Name lookup is behavior of that owner. `LookupResult` distinguishes `found`,
`not_found`, `ambiguous` and `invalid`, removing the former qualifier contract
that paired a no-id return with an output boolean. Lookup still borrows Context
for syntax text and diagnostics; it does not own source or diagnostic storage.
Bound scans use `for` for typed record ranges, while the two remaining `while`
loops follow syntax sibling links. No condition in either SymbolTable
implementation exceeds two logical clauses.

The old `luna.bootstrap.middleend.semantic.context.lookup` child module had no
independent consumer or acyclic boundary: every operation required Context.
Its interface and registry object are deleted. `context/symbols.la` implements
storage/publication and `context/lookup.la` implements bound lookup in the
parent module. The parent interface is now 522 lines because the
transitional Context still exports unrelated passive records and builder
state; owner extraction continues to reduce that interface rather than hiding
the debt behind another facade.

`Binding`, ordinary candidates and generic candidates remain outside
SymbolTable because they encode overload ordering and contiguous callable
slices, not source-name ownership. They now belong to the separate
`CallableTable` owner below Context.

## Callable ownership

`luna.compiler.sema.callables::CallableTable` replaces Context's raw function,
parameter, binding, ordinary-candidate, generic-candidate and signature
buffers plus their synchronized counts. Stable `Function`, `Parameter` and
`Binding` values live in the domain module; the owner privately composes five
typed vectors, one `byte_buffer` and the first runtime failure. Context owns
the table and exposes neither writable records nor raw allocation state.

The table owns these related invariants:

- each ordinary function has one contiguous parameter slice; const-function
  parameters are explicitly unowned, reuse the same parameter store with
  `function_id = no_id()`, and are cross-validated against `ConstFunction`
  records by `functions/const.la`;
- every function signature is non-empty and signatures occupy one canonical,
  gap-free byte sequence in function-ID order;
- initial ordinary candidate order is validated for bounds and uniqueness in
  a replacement vector, then moved into place in one publication step; a
  second publication is rejected;
- ordinary binding slices are in range and map candidates back to the same
  binding; generic declaration candidates are globally unique, while each
  declaration's reverse `binding_id` relation is checked by the cross-owner
  `functions` validation;
- runtime free-generic instances may bind directly without entering the
  ordinary candidate sequence, while concrete generic-class methods append
  through the ordered-candidate method;
- allocation or invariant failure is sticky and blocks later mutation.

CallableTable function, parameter and binding records are mutated only through
its bound methods; GenericTable remains the owner of generic declarations and
instances. Consumers receive const typed projections. `CallableTable::is_valid`
checks ordinary parameter ownership, signature contiguity, unique candidate
identities and ordinary candidate-to-binding back-links. Generic declaration
reverse bindings remain a cross-owner `functions` invariant, and const-function
parameter slices are checked separately by `functions/const.la` after
collection.

Canonical signature construction uses the private move-only `SignatureWriter`
resource class in `functions/signature.la`; only its completed bytes enter the
CallableTable signature buffer. The direct relocation-data contract explicitly
covers duplicate generic candidates, repeated order publication and invalid
binding extension, including sticky failure and no partial order publication.

The owner is a real dependency boundary with a 64-line interface and three
same-module implementation families: `storage.la` for lifetime/declarations,
`bindings.la` for ordered publication and slice mutation, and `validation.la`
for deep final-state verification. `call_selections` is not callable metadata;
it remains transitional expression-lowering state alongside the new
`LoweringState`; it is intentionally not mixed into callable ownership.

## Function lowering state

The current batch adds `context/lowering.la` as another implementation unit of
the existing `luna.bootstrap.middleend.semantic.context` module. It is not a
new `context.lowering` dependency: the interface remains
`compiler/include/luna/bootstrap/middleend/semantic/context.lh`, and the
registry records the implementation path under the parent module.

`LoweringState` is a move-only class with private typed vectors for locals,
temporaries, labels, label-local snapshots, pending gotos, goto-local snapshots
and loop labels. It also owns the current function/unit/block cursors, scope
depth, a pending loop label, control targets with local-count watermarks and a
sticky first runtime error. Its public surface is deliberately split between
const projections (`*_data`, counts and cursors), focused mutations and deep
validity checks. There are no writable vector projections or public raw byte
counts.

The state is function-scoped: `begin_function` clears the previous function
state before publishing the new owner, and `clear_function` returns an empty,
valid state. Move construction transfers all typed vectors and cursors, then
resets the source to a valid empty state. Scope exit and local truncation reject
operations that would cross any active control or loop watermark. Label and
goto snapshots are checked for valid, non-overlapping ownership; a pending
goto publishes one contiguous snapshot range. Goto lowering can append an
unpublished tail and explicitly discard that tail before the next publication,
so speculative snapshots cannot remain orphaned.

`Context` now retains six legacy raw byte-buffer groups (alignment overrides,
bit-field segments, const functions, const locals, array lengths and call
selections); the former thirteen-group raw storage table no longer contains
function-local locals, temporaries, labels, goto records, loop labels or their
snapshot buffers. `context.la` validates the new owner and checks that its
current function/unit/block still belongs to the Context's callable and IR
owners. `context.builder` remains in place for now: it delegates local/scope
operations and adds a narrow `set_current_block` first-error bridge, but this
batch does not remove that implementation unit or migrate `ir::Builder`, const
metadata, type-depth state or call-selection caches into `LoweringState`. The
old `clear_label_state` API is deleted rather than retained as a compatibility
alias.

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
| `semantic/callables/storage.la` | CallableTable lifetime, declarations, signatures and focused function publication |
| `semantic/callables/bindings.la` | transactional candidate order, binding slices and generic candidate publication |
| `semantic/callables/validation.la` | parameter/signature/candidate/binding deep validation |
| `semantic/types/visibility.la` | same-module exported-type, generic argument and anonymous-field visibility |
| `semantic/types/lookup.la` | same-module direct, anonymous-promotion and inherited field lookup |
| `semantic/functions/ir.la` | same-module IR function, receiver and parameter construction |
| `middleend/sema.la` | private SemanticSession, phase orchestration, entry selection and result transfer |
| `semantic/context.la` | transitional Context storage and shared low-level services |
| `semantic/context/input.la` | Input owner, InputView validation and Context unit/path access |
| `semantic/context/diagnostics.la` | DiagnosticView, DiagnosticBuffer and final success predicate |
| `semantic/context/symbols.la` | SymbolTable typed ownership, publication invariants and sticky failure |
| `semantic/context/lookup.la` | SymbolTable local/imported/qualified lookup methods |
| `semantic/context/lowering.la` | LoweringState function-local cursors, typed lowering storage, snapshots and control watermarks |

The two domain files implement one real `luna.compiler.sema.domain` module.
Class/generic records need no artificial implementation file because they are
passive declarations with no behavior.
The four `context/{input,diagnostics,symbols,lookup}.la` files are additional
implementations of the existing context module, not new submodules; no
consumer imports them independently.

## Current Luna feature review

| feature | disposition |
| --- | --- |
| Classes | SemanticSession owns phases; Input, DiagnosticBuffer, SymbolTable, ClassTable, GenericTable and CallableTable own state/lifetimes |
| Generics | SymbolTable and the other owners use typed vectors instead of exposed byte arithmetic |
| Composition | Input composes vector/byte_buffer; Context composes SymbolTable, ClassTable, GenericTable, CallableTable and LoweringState |
| Access control | session phase, symbol/metadata vectors, diagnostic storage and sticky errors are private |
| Constructors/destructors | constructors establish ready/empty states; destructors close each resource path once |
| Copy/move | Input, DiagnosticBuffer, all four table owners and LoweringState are move-only; views are copied borrows |
| Overloads/defaults | CallableTable owns overload-set storage; its mutation operations have distinct contracts and need no API defaults |
| Operators | no natural value operator exists for a semantic session or diagnostic stream |
| Bound methods | SymbolTable owns name publication/lookup and CallableTable owns callable publication; SemanticPass stays a pointer because passes capture no receiver |
| Friends | unnecessary because const projections and focused mutations preserve the owner boundary |
| Virtual dispatch/RTTI | rejected for table owners: each has one closed implementation with no runtime substitution |

The domain module adopts enums, structs and switch-based free predicates
because its values are passive and stateless. Adding a class merely to group
those functions would introduce pattern-shaped indirection without an owner.

## Deliberately deferred work

The next semantic batches should remain independently green:

1. contract `context.builder` and migrate its remaining IR-facing operations
   toward the planned `luna.compiler.sema.session` boundary; keep IR Builder,
   const/type metadata and call-selection caches out of LoweringState unless a
   later ownership proof requires a different split;
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

The relocation-data contract exercises Input, ClassTable, GenericTable,
SymbolTable, CallableTable and LoweringState move/moved-from storage; class,
generic, name/import, callable and function-lowering slices; hierarchy/runtime
publication; generic indexes and rollback; transactional candidate
publication; signature contiguity; scope/control watermark rejection;
published and explicitly discarded goto snapshot tails; sticky owner
failures; borrowed InputView use; and IR ownership transfer. The ordinary
overload/default/generic/class corpus exercises the same owners through real
lowering. The negative corpus preserves stable leading diagnostic kinds
through the command driver.

For the current LoweringState batch, the independent caw validation workspace
is `/home/aoweichen/codex-workspaces/luna-lowering-validation-paBIOS` on
x86_64 Linux with Python 3.13.9. `audit` reported 63 modules, one driver, 33
interfaces and 53 objects; `refmt --check` reported zero files needing reflow
and zero token drift. `verify --fresh` completed in 64.71 seconds with 54
compiles, 54 assemblies and one link in each transition/next/fixed stage; all
artifacts were byte-identical, with stage-fixed hash
`1bc10a126bdbd2cf524502f444ff04e7897f2f6b33a5f820bfc38f465ff43e51` and size
5,474,131 bytes. The complete test suite reported 450 passed, zero failed and
zero skipped in 6.69 seconds.
