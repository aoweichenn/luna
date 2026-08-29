# Luna semantic pipeline

## Status and scope

The semantic pipeline validates the parsed source-module graph, constructs the
canonical type model and lowers checked functions into `luna.compiler.ir`.
This document records the first three ownership/domain batches of its modernization.

The root module is still named `luna.bootstrap.middleend.sema` while the
downstream semantic dependency graph is contracted incrementally. The current
batch does not rename the graph or claim that the large transitional Context
has already become private.

## Domain foundation

`luna.compiler.sema.domain` is the first modern semantic dependency boundary.
It replaces the historical callable and value-category modules with one
51-line interface and two same-module implementation units.

The module contains only closed, passive compiler-domain values:

- `CallableKind`, `ReceiverKind` and `CallableIdentity` describe free
  functions, methods and receiver qualification;
- `ValueCategory` and `ReferenceRank` describe expression storage and
  reference-binding rank;
- stateless predicates validate identities, compare ownership, project value
  categories and compute reference compatibility.

Names are intentionally explicit after the merge: generic `Kind`, `Identity`
and `Category` became `CallableKind`, `CallableIdentity` and `ValueCategory`.
Closed-kind behavior uses `switch`; the module owns no allocation or mutable
session state and therefore needs no class hierarchy, virtual dispatch or
runtime RTTI.

Both historical modules depended only on `luna.compiler.types`. The combined
module preserves that downward dependency, removes one interface and one
linked object, and lets consumers that previously imported both concepts use
one `domain::` dependency. Class and generic model Stores are deliberately not
included: they still own raw buffers, indexes and hash buckets and must become
RAII abstractions before their passive records can move into this boundary.

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

The record is intentionally a struct in the current ABI. It has no behavior or
independent invariant beyond bundling the three already-validated RAII owners
for return-value transfer. The current unoptimized class ABI does not yet
reliably transfer a large public class containing both TypeTable and Module;
forcing a decorative result class would lose IR state. The private session
therefore owns behavior, while the public record remains transparent and
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
| `middleend/sema.la` | private SemanticSession, phase orchestration, entry selection and result transfer |
| `semantic/context.la` | transitional Context storage and shared low-level services |
| `semantic/context/input.la` | Input owner, InputView validation and Context unit/path access |
| `semantic/context/diagnostics.la` | DiagnosticView, DiagnosticBuffer and final success predicate |

The two domain files implement one real `luna.compiler.sema.domain` module.
`context/input.la` and `context/diagnostics.la` are additional implementations
of the existing context module, not new submodules; no consumer imports them
independently.

## Current Luna feature review

| feature | disposition |
| --- | --- |
| Classes | SemanticSession owns phase/lifetime; Input and DiagnosticBuffer own resources; their views protect borrowing |
| Generics | input units and diagnostics use typed vectors instead of byte arithmetic |
| Composition | Input composes vector/byte_buffer; Context composes TypeTable, IR Builder and DiagnosticBuffer |
| Access control | session phase and state are private; diagnostic vector storage is private |
| Constructors/destructors | constructors establish ready/empty states; destructors close each resource path once |
| Copy/move | Input and DiagnosticBuffer are move-only; views are copied borrows; SemanticResult transfers through RAII fields |
| Overloads/defaults | no operation has one semantic family needing overloads or optional arguments in this batch |
| Operators | no natural value operator exists for a semantic session or diagnostic stream |
| Bound methods | run, finish, entry selection and diagnostic mutation are bound to their owning state |
| Friends | unnecessary because the public methods preserve the required boundaries |
| Virtual dispatch/RTTI | rejected: pass order and diagnostics are closed compile-time domains without runtime substitution |

The domain module adopts enums, structs and switch-based free predicates
because its values are passive and stateless. Adding a class merely to group
those functions would introduce pattern-shaped indirection without an owner.

## Deliberately deferred work

The next semantic batches should remain independently green:

1. make class and generic model Stores move-only RAII owners, separating their
   passive records from mutable indexes;
2. extend `luna.compiler.sema.domain` only with the passive class/generic
   records whose ownership has been separated;
3. contract context/lookup/builder into a coherent session module after its
   interface can remain narrow;
4. migrate pass families to bound methods or focused strategy objects where
   they own real state;
5. rename the remaining `luna.bootstrap.middleend.semantic.*` graph only when
   the new dependency boundary is acyclic and independently useful.

## Validation gates

Every semantic ownership change must pass on the isolated caw host:

```sh
python3 tools/selfhost.py audit
python3 tools/refmt.py --check
python3 tools/selfhost.py verify --fresh
python3 tools/selfhost.py test
```

The relocation-data contract exercises Input move/moved-from storage, borrowed
InputView use, successful result transfer, IR ownership transfer and moved-from
IR storage. The ordinary negative corpus exercises DiagnosticBuffer capacity,
DiagnosticView lookup and stable leading diagnostic kinds through the command
driver.
