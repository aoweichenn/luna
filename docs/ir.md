# Luna typed IR

## Status and scope

`luna.compiler.ir` is the single typed intermediate-representation module used
between semantic lowering and the x86-64 backend. It replaces the historical
`luna.bootstrap.middleend.ir` store and the separately imported
`luna.bootstrap.middleend.ir.verify` module.

The IR is deliberately non-SSA and correctness-first. Mutable bindings and
merge results use typed slots; virtual values are single-definition records;
basic blocks form an explicit CFG. The module does not perform optimization,
instruction selection, register allocation or target object emission.

## Ownership and lifecycle

The public boundary has three roles:

| type | role |
| --- | --- |
| `Module` | move-only RAII owner of one complete IR module |
| `Builder` | phase object that exclusively constructs a `Module` and retains the first failure |
| `View` | borrowed, read-only pointer/count projection used by semantic queries and code generation |

`Module` composes typed `vector<Value>` stores for records and
`byte_buffer` stores for global bytes and canonical callable signatures. Its
constructor establishes empty valid storage and the absent entry-function ID.
Move construction and move assignment transfer every store together; copying
is intentionally unavailable because duplicated mutable IR identity would be
ambiguous. Field destructors release the composed storage, so `Module::deinit`
does not repeat container cleanup.

`Builder` owns a `Module` while lowering is active. Every mutating operation
first checks its sticky error and the composed storage. The first invalid
request or allocation failure is retained; later calls cannot accidentally
resume construction. `take_module` transfers the finished owner to the
semantic result and permanently closes that builder phase. A builder may also
be constructed from `Module&&` for the narrow case where a test or a later
pass must resume append-only construction.

The semantic result owns both `TypeTable` and `Module`. Verification and code
generation borrow them by const reference. A `View` never extends either
lifetime and must not survive a move of its source module.

## Passive records

IR records remain structs because they are transparent, index-addressed data
without independent resource ownership:

- `Global` and `GlobalReference` describe immutable byte regions and symbolic
  machine-word placeholders;
- `Function`, `Parameter` and `Slot` describe callable identity, signatures
  and local storage;
- `Value`, `Block`, `Instruction` and `Argument` describe the typed CFG;
- `FunctionResult`, `BlockResult` and `InstructionResult` are bounded lookup
  results;
- `View` is a transparent borrowed projection.

All cross-record relationships use `usize` IDs. `no_id()` is the sole absent
ID. No record stores a pointer into growable storage, so vector relocation
cannot invalidate the graph.

## Construction invariants

`Builder` exposes operations in dependency order:

1. append immutable globals and their zero-filled symbolic references;
2. append functions and their canonical signature bytes;
3. append parameter slots and local slots owned by a function;
4. append blocks and values owned by that function;
5. emit linked instructions and flattened call arguments;
6. select the single executable entry function;
7. transfer the completed module.

The builder maintains the invariants it can establish locally:

- record ranges extend append-only storage;
- IDs refer to records already present;
- one instruction result binds one previously undefined value;
- instructions form a per-block linked list;
- no instruction follows a terminator;
- jump and branch emission updates cached predecessor counts;
- global references occupy aligned, non-overlapping, zero-filled machine-word
  placeholders;
- the entry function is selected once and has a body.

Construction checks do not replace final verification. Cross-table properties
such as exact type compatibility, complete function ownership, predecessor
recomputation and call-signature matching require the finished module.

## Read-only view

`View` exposes one typed pointer and count for each record table, plus global
bytes, callable-signature bytes and the entry-function ID. Ordinary consumers
receive only const pointers.

The natural generic spelling would be `span<Value>` for every table. The
current self-hosted generic implementation emits strong monomorphized method
symbols in each consuming compiler module; using exported span methods in IR,
semantic analysis and code generation therefore produced duplicate link
definitions. The pointer/count representation is an intentional temporary
zero-code-generation boundary, not permission to add untyped byte access.
It can become typed spans once cross-module generic symbol ownership is fixed.

`global_bytes_data` and `global_reference_data` are the only mutable escape
hatches. They exist solely for the relocation corruption test that proves the
independent verifier rejects damaged state. Production semantic and backend
code must use `View`.

## Verification architecture

`Module::storage_is_valid` checks only the RAII containers. `Module::is_valid`
first checks that storage, then constructs the private `Verifier` session with
a borrowed `View` and `TypeTable`.

`Verifier` owns no resources. It caches typed table pointers and four ownership
totals, then validates the module in dependency order:

1. pointer/count storage shape, type-table validity and packed function
   signature coverage;
2. contiguous global bytes and global-reference slices;
3. function identity, parameter/slot/value ownership and aggregate totals;
4. executable entry shape and optional `argc`/`argv` types;
5. slot types and explicit alignment;
6. block instruction chains, terminator placement and total instruction
   ownership;
7. predecessor counts recomputed from jump and branch instructions;
8. instruction identity, operands, results and opcode-specific type rules;
9. direct/indirect call argument counts and carrier compatibility;
10. final argument ownership.

Each phase runs only after the records it dereferences have passed their range
checks. Bounded traversal uses `for`; opcode families and record-kind dispatch
use `switch`; no control condition contains more than two logical clauses.

Instruction validation is deliberately separate from the verifier session.
Opcode rules are stateless predicates over one instruction, the immutable type
table and borrowed IR records, so turning them into another stateful class
would add indirection without an invariant. Closed opcode sets use `switch`
rather than virtual dispatch.

## Type and call contracts

Every value-producing instruction names an exact semantic type. Scalar loads,
stores and arithmetic require exact type identity. Address-backed aggregate
carriers may use a pointer to the exact aggregate type where the source-level
operation transports memory rather than a scalar value.

Direct calls obtain return and parameter types from the target `Function`.
Indirect calls obtain them from a verified function-pointer type record.
Arguments remain flattened `Argument` records owned by an instruction ID. The
verifier checks definition order, caller ownership, fixed arity, the fixed
prefix of variadic calls and each carrier type.

IR pointer values are opaque addresses, but memory instructions still carry
the access type or element stride needed for independent checking. Member
address verification walks nested anonymous records and the single class base
chain with a bounded recursion depth. Read-only qualification may be preserved
or strengthened, never removed.

## Global references

A global owns one contiguous byte range and an optional contiguous slice of
`GlobalReference` records. Each reference names either another global or a
function and occupies one aligned target-word placeholder in the owner's
bytes. The placeholder must remain zero in typed IR; assembler/object lowering
materializes the eventual relocation.

Referenced globals are read-only. The verifier checks target identity, range,
alignment, ordering, non-overlap and placeholder bytes independently of the
builder.

## Implementation units

All files below implement the same `luna.compiler.ir` module:

| file | responsibility |
| --- | --- |
| `storage.la` | `Module`/`Builder` lifecycle, views and bounded lookup |
| `globals.la` | global bytes and symbolic-reference construction |
| `functions.la` | functions, parameters and slots |
| `control.la` | blocks, values, entry selection and terminator classification |
| `instructions.la` | instruction emission, linked block tails and call arguments |
| `validation.la` | stateless operand/type predicates and opcode dispatch |
| `verify.la` | private `Verifier` session and whole-module validation phases |

`verify.la` is an implementation split, not an `ir.verify` submodule. No peer
imports it independently, and it imports no parent facade.

## Current Luna feature review

| feature | disposition |
| --- | --- |
| Classes | `Module`, `Builder` and private `Verifier` own real lifetime, construction and validation invariants |
| Generics | record storage uses `vector<Record>`; byte storage uses `byte_buffer`; exported spans are deferred for the linker reason above |
| Composition | `Module` composes all owned stores; `Builder` composes the module it constructs |
| Access control | storage and sticky failure remain `priv`; consumers receive narrow methods and const views |
| Constructors/destructors | constructors establish valid empty/moved states; composed RAII fields provide deterministic cleanup |
| Copy/move | `Module` is move-only; move assignment is the only natural assignment operator |
| Overloads/defaults | constructor overloads express empty versus moved ownership; mutation methods avoid defaults because each argument is an IR invariant |
| Operators | only move assignment is natural; record manipulation does not use decorative operators |
| Bound methods | construction and phase validation are bound to their owning `Builder`/`Verifier` state |
| Friends | only `Builder` is a friend of `Module`, avoiding public mutable container access |
| Virtual dispatch/RTTI | rejected: opcodes and record kinds are closed enums with exhaustive `switch` dispatch and no runtime substitution boundary |

## Validation gates

Any change to IR records, construction, semantic lowering or a backend
consumer must pass, on the isolated caw host:

```sh
python3 tools/selfhost.py audit
python3 tools/refmt.py --check
python3 tools/selfhost.py verify --fresh
python3 tools/selfhost.py test
```

The relocation-data suite additionally corrupts a global reference and its
placeholder bytes, verifies rejection, restores both and verifies acceptance.
That white-box proof is why the two mutable test seams remain visible today.
