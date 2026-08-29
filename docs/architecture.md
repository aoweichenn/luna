# Compiler architecture

## Non-negotiable boundaries

Stage 0 is C23; the fixed-point compiler is Luna. Luna source is never
translated to C. The initial target is exactly
`x86_64-unknown-linux-gnu`: x86-64 instructions, System V calling convention
and ELF64 objects or executables.

Stage 0 is authoritative only for the frozen Luna 0 reconstruction language.
Luna 1 semantics are owned by the self-hosted compiler and are introduced
with a versioned stage protocol and a two-step bootstrap before compiler
sources may use them. This boundary is defined in
[the bootstrap language version contract](bootstrap-language-versions.md).

Generated target programs are freestanding and never depend on libc. Stage 0
is a hosted C23 tool, but its host-library use is confined to that compiler
process. Later compiler stages are themselves freestanding Luna executables.
The project-owned x86-64 Linux system-call ABI layer
converts System V calls with zero to six arguments to the kernel register ABI
and invokes `syscall` directly. The Luna runtime and standard library must be
built only on that layer. Optional caller-supplied external objects are an
explicit FFI boundary, not a runtime dependency. FFI exists to match
interfaces described as C ABI layouts — the kernel UAPI first — and never
implies linking glibc, musl or any libc; adopting one would be a separate
explicit boundary decision.

The compiler owns the language semantics. The hosted stage-0 path encodes
x86-64 instructions into ELF64 relocatable objects directly. The self-hosted
path emits the same closed instruction dialect, assembles it into the strict
`LUNAOBJ1` bootstrap format and links that format into a static ELF64
executable. Its assembler and linker are Luna modules. LLVM MC and LLD remain
independent test oracles only; neither is used by a production or bootstrap
toolchain stage.

External C functions are represented directly throughout the pipeline; Luna
never generates a C translation unit. The final ELF link may combine a
Luna-generated object with caller-supplied C23 objects or libraries.

Target selection produces an immutable target description before semantic
lowering begins. It records the architecture, operating system, ABI, byte
order, scalar sizes and ABI alignments. The target description is carried by
the typed IR module so target-sized language types never depend on host C
properties.

## Source-tree layering

Production Luna code has three dependency layers: `library` is the foundation,
`compiler` builds on it, and the executable `drivers` compose compiler and
library modules. Interfaces and implementations are physically separated:

- `library/include/luna/**/*.lh` contains exported library interfaces;
- `library/src/**/*.la` contains library implementations;
- `compiler/include/luna/bootstrap/**/*.lh` contains compiler interfaces;
- `compiler/src/**/*.la` contains compiler implementations;
- `drivers/src/*.la` contains source-only executable entry modules.

An interface path mirrors its complete module name, while implementation paths
mirror the subsystem hierarchy. A small module may use one implementation,
such as `luna.std.string_view` with `library/include/luna/std/string_view.lh` and
`library/src/std/string_view.la`; a larger module may register additional `.la` units
under an implementation subdirectory.

Physical adjacency is not module identity. The `LIBRARIES` registry in
`tools/selfhost.py` records the interface and all implementation paths for
every module; dependency order and driver link closures are derived from the
union of declared imports. The source audit rejects missing or unregistered
units, so moving or adding a unit cannot silently remove it from the
fixed-point build.

## Pipeline

```text
source units
    |
    v
lexer -> parser -> syntax tree
                    |
                    v
             module graph resolution
                    |
                    v
             name and type checking
                    |
                    v
             typed Luna IR (CFG)
                    |
                    v
       x86-64 System V lowering
       + deterministic frames
                    |
                    v
          closed assembly dialect
                    |
                    v
             luna assemble
          +---------+---------+
          |                   |
          v                   v
      LUNAOBJ1           ELF64 ET_REL
          |                   |
          +---------+---------+
                    |
                    v
               luna link
                    |
                    v
          static ELF64 executable
```

Assembly is a closed output encoding of the x86-64 backend, not a user-facing
intermediate language. During bootstrap, `luna assemble` consumes this
dialect and emits either the validated `LUNAOBJ1` bootstrap format or a
standard ELF64 relocatable object. `luna link` consumes both supported object
forms and emits the final static ELF64 image. The three commands are provided
by one freestanding executable while their implementation modules remain
independent. No stage invokes a host assembler or linker. See the
[unified driver contract](tools.md).

## Frontend

Source locations are byte spans into immutable source files. Tokens and syntax
nodes retain spans so every parser, type and IR error can point to the original
text. `luna.compiler.parser` owns one private recursive-descent `Parser`
session; it borrows TokenView and never owns isolated syntax nodes.

`luna.compiler.syntax` owns that arena as a move-only typed `SyntaxTree`.
Parser mutates it only through `SyntaxBuilder`; semantic analysis and
Codegen receive a non-owning `SyntaxView`, so no consumer can release or resize
the completed tree.

The separately compiled Luna lexer, parser, type, IR, semantic and x86-64
backend modules use owned typed byte buffers and stable indices, avoiding
pointers into growable storage. Their structured diagnostics and verification
run in the freestanding toolchain and never call back into the archived hosted
C23 frontend, middle end or code generator. The indexed Luna syntax tree,
verified Luna typed IR and stack-homed x86-64 emission are the current
contracts. Historical reconstruction details remain in the
[bootstrap frontend contract](bootstrap-frontend.md) and
[bootstrap middle-end contract](bootstrap-middleend.md), followed by the
[bootstrap x86-64 backend contract](bootstrap-x86-64-backend.md) and the
[stage reproducibility contract](bootstrap-reproducibility.md).

The syntax tree represents `if`, the three conditional loop forms, Luna 1
unconditional `loop` and non-fallthrough switch arms directly. Semantic
lowering uses one ordered control-frame stack:
`break` selects its innermost loop or switch frame, while `continue` searches
for the innermost loop frame. This preserves nesting semantics without adding
target-specific control constructs.

Parsed type references preserve named types, pointer qualification and
recursive fixed-array shape. Semantic lowering first collects every named
type, then resolves fields and enum members, and finally computes layouts with
a recursion-state check. This permits forward references and pointer
recursion while rejecting every by-value cycle. Pointer and array references
are interned into canonical types, and each structure, union and enum retains
a distinct semantic identity. Context-directed aggregate initialization lowers
directly into exact-layout local storage after validating every field before
evaluating any initializer expression. Exact same-typed local memory objects
copy by representation. Composite parameters and results retain exact semantic
type identity while lowering through explicit aggregate signature descriptors
and address-backed snapshots. Type-only `sizeof`, `alignof`
and `offsetof` expressions are resolved against the same target layout records
and lowered directly to typed `usize` constants; the IR has no host-dependent
layout-query instruction.

`luna.compiler.types::TypeTable` owns those records and fields as typed vectors.
Semantic Result receives the table by move; IR verification and x86-64 lowering
borrow it by const reference. No consumer owns or releases a raw type buffer.

Compilation is split into global and local phases:

1. load, lex and parse every source unit;
2. group the optional interface and all implementation units by exact module
   name;
3. collect direct imports and reject unknown, repeated and self imports;
4. validate an acyclic module graph;
5. validate attributes and collect named types;
6. collect and resolve compile-time constants and type layouts;
7. collect functions and match interface declarations to definitions;
8. validate imported names and public type visibility;
9. construct verified typed IR and lower checked function bodies;
10. select the executable or library root and validate reachability.

This order removes source-order dependencies and ordinary forward declarations.
For executables, the module resolver identifies the unique implementation
containing `main` and requires every supplied module to be reachable from it.
For libraries, the first input unit's module is the selected root. Interface
imports enter interface scope and the shared implementation scope; imports
written in any implementation unit enter only that shared implementation
scope. Module-private declarations are likewise shared by every implementation
unit. Only exported declarations enter an importer's scope, and visibility is
never transitive.

The compiler never discovers or loads a module from its name. Its caller
supplies the complete source-interface closure; module declarations establish
identity and the build driver owns path mapping. Every supplied implementation
must satisfy its matching interface, while an interface-only dependency is
resolved by a separately linked module object.

The self-hosted middle end builds explicit callable bindings after canonical
signatures are available. An iterative heap sort orders function IDs by
complete module name, canonical owner, source name and signature; adjacent
equal module/owner/name identities become one binding with a contiguous
candidate slice. Each function owns one callable identity containing its kind,
optional owner type and receiver kind. IR function construction and statement
lowering consume that same sequence rather than sorting independently.
Function, parameter, slot, block and global construction therefore remains
deterministic when source units are supplied in a different order, without
recursive sorting or downstream order drift.

Multi-candidate calls are resolved by a non-emitting expression probe split
across one interface and three same-module implementation units. The probe
classifies expressions as compatible, incompatible, ambiguous or invalid,
accumulates candidate state without ranking, and memoizes selected nested calls
in a lazily allocated syntax-node table. The emitting lowerer then consumes the
selected function and evaluates each argument exactly once. Single-candidate
calls retain the established lowering path.

Value-category semantics live in the dependency-root `semantic.value` module,
below semantic context. Emitting expressions and non-emitting probe results
carry the same closed one-word category enum. Central constructors and
predicates derive pointer qualification, project member/index storage and
decide addressability or assignability; bitfields are represented as
non-addressable storage rather than special-casing boolean triples at every
consumer.

Class metadata uses the same dependency-root pattern. The
`semantic.classes.model` module defines class, field-access and method records
and groups their buffers into one lazily empty store. Semantic context owns that
store as one validated lifecycle unit; collection, layout and access passes sit
above context rather than leaking policy into the record model.
The class pass records nominal identities after named-type collection, lets the
shared record-layout pass resolve complete non-polymorphic storage, then builds
contiguous per-class field and method slices. Field slices follow layout order;
method slices are selected from the canonical callable order one owner at a
time, so class-record order and global function order are never accidentally
coupled. The validator checks those slices, explicit access, dispatch policy,
the single-base relation, absence of vptrs and class-value composition.

M4 generic metadata follows the same split. The dependency-root
`semantic.generics.model` module owns declaration parameters, concrete
argument slices, instance states, direct TypeId/FunctionId maps and an
open-addressed instance index whose full key remains the equality authority.
`semantic.generics` validates declarations and manages immutable active
substitution views below type resolution. Type layout and callable inference
consume those views; only the uniquely selected callable is materialized into
ordinary typed IR. Statement lowering processes canonical ordinary roots first
and then a bounded concrete-instance worklist, so recursive instantiation does
not require a generic backend representation.

M3.1 adds one canonical `base_type` field to the semantic type record. The class
metadata record mirrors that relation, but layout, field lookup, IR member
verification and x86-64 ABI classification all consume the type relation rather
than copied inherited fields. Layout resolves the base first, begins direct
field placement at the base's padded size and keeps the base address at offset
zero. A bounded DFS over class records validates cycles and final bases.

After direct methods are collected, a base-first DFS state machine analyzes
each hierarchy independently of source order. Exact method keys ignore owner
but retain receiver and explicit parameter types. Valid overrides reuse the
inherited slot; new virtual or abstract declarations receive the next canonical
slot. The same pass propagates polymorphic and abstract flags, rejects missing
or final overrides and preserves distinct inherited overloads. M3.1 stores no
vptr and emits no indirect call; `super` and static-type calls lower directly.

Class methods reuse the ordinary `Function`, signature, binding, default and IR
records. Their callable identity supplies owner and receiver policy. Semantic
parameter slices contain only source parameters; IR construction synthesizes
the anonymous receiver as the first ABI parameter, so it never affects source
arity or default insertion. The non-emitting call probe classifies a call target as
constructor, instance, static or free before filtering an owner-scoped binding.
The emitting lowerer evaluates an instance receiver once; constructors create
and zero a complete slot before passing its address. Direct calls then use the
same aggregate argument/result pipeline as free functions. The x86-64 ABI
classifier treats classes and structures through the same field recursion;
class values at an external-C boundary are rejected earlier by semantics.

Default parameters reuse that callable pipeline. One `DefaultProfile` records
the required arity and validates trailing placement and declaration mounts;
each accepted parameter stores a folded integer, boolean or null value. The
shared arity predicate participates in candidate filtering, while the call
argument collector appends omitted values only after selection. Defaults never
enter canonical signatures, mangling or function-pointer types.

All modules in one invocation share canonical named-type and function records.
This preserves type identity across module boundaries and rejects ambiguous
unqualified imports. Function overload keys use canonical parameter types,
variadic state and the extern/calling-convention boundary; result type and
`@noreturn` form a separately checked declaration contract. Interface and
implementation candidates merge by that key, while distinct implementation
keys extend the module-private overload set. The interface type graph is
resolved before implementation-private types are collected, making the
interface independently valid. Nested arrays, named-type identity and pointer
qualifiers therefore cannot match accidentally by spelling or layout.

Separate compilation is source-interface based. Each library invocation
receives all of the selected module's implementation units, its interface and
the reachable dependency interfaces. Imported implementations may be absent;
their independently generated objects provide definitions at final link time.
This source graph is the stage-1-and-later replacement for the archived
stage-0 `.lmi` path.

The current compiler emits assembly in `--library` or `--executable` mode.
Ordinary Luna symbols encode the canonical module name, source function name
and the versioned canonical ABI signature. This makes separately compiled
caller/callee signature disagreements fail as unresolved symbols without a
compiled-interface fingerprint. The fixed-point build prevents stale object
combinations by rebuilding all module objects from the same registered source
graph. The old `.lmi` byte layout remains documented only as an
[m0 reconstruction artifact](module-metadata.md).

## Luna IR

The bootstrap IR is deliberately non-SSA. It is a typed control-flow graph
with virtual values and explicit local slots:

- `const`
- `load` and `store`
- type-directed integer arithmetic shared by fixed-width and target-sized
  integer types
- binary32 and binary64 constants, arithmetic and ordered comparisons
- explicit integer conversions whose extension or truncation follows source
  and target type metadata
- exact floating widening, rounded floating narrowing and type-directed
  signed/unsigned integer-to-floating and checked floating-to-integer
  conversions
- comparisons
- direct calls to internal definitions and typed external C declarations
- opaque object addresses and explicit constant-offset member addressing
- whole-slot zeroing and explicit sized, overlap-safe memory copies
- unconditional and conditional branches
- return

Conditional expressions use a typed temporary slot at their merge. Switch
lowering stores the controlling value once and emits an ordered chain of typed
equality branches. `do` and `for` are expressed entirely with ordinary basic
blocks. No conditional, loop or switch opcode is hidden from the verifier or
backend.

IR pointers are intentionally opaque address values, while every indirect
load, store and element-address instruction carries the scalar access type or
element size it needs. This follows the same separation as modern opaque
pointer IRs: source-level pointee compatibility is established by semantic
checking, and the verifier independently checks address operands, access
types, bounds checks, slot layouts and global-data references.

Local slots record byte size and ABI alignment instead of assuming one
eight-byte home. Fixed arrays, structures and unions therefore occupy their
exact target layout while scalar and enum virtual values remain abstract until
the verified x86-64 rewrite assigns registers or spill slots.
The `member_address` instruction derives an opaque pointer from a verified
base pointer and a bounded byte offset; scalar field loads and stores remain
ordinary typed indirect memory operations. `memory_copy` takes destination
and source pointers plus a verified positive object size; semantic lowering
emits it only after exact source-type checking. Immutable string bytes live in
a module global-data table and are referenced through target-neutral global
address instructions.

Every reachable basic block has exactly one terminator. Detached empty merge
blocks are permitted, while non-empty detached blocks must also terminate.
Virtual values are defined once, remain local to one basic block and must be
defined before use. IR verification runs before the backend in every build
mode. Mutable variables use slots, avoiding phi nodes until optimization work
demonstrates that SSA is worth its compiler cost.

The verifier independently checks exact operand and result types, call
signatures and flattened argument ownership, terminator placement, cached
predecessor counts and graph reachability. Backend emission never receives
unchecked compiler-generated IR.

Function linkage is explicit IR metadata. Internal and module-export
definitions own parameter slots, values and a CFG body. Bodyless declarations
from dependency source interfaces and external C functions own only a typed
signature and have no body blocks. No declaration may be the module entry
point. Library IR has no entry function at all.

The Luna IR instruction set is target-neutral. Each module is parameterized by an
explicit target data layout so `isize` and `usize` retain their exact IR types
while width-dependent verification and conversion printing remain
deterministic. Textual IR records the target triple. Target-specific
registers, calling convention, instruction encodings and relocations do not
appear in IR instructions.

Every IR function signature owns one aggregate descriptor for its result and
one descriptor parallel to each parameter. Scalar descriptors are empty.
Aggregate descriptors contain exact size and alignment; register-eligible
descriptors additionally contain flattened scalar leaves, while larger values
are already unconditionally `MEMORY` class. The source type remains a
structure, union or array in semantic checking, while the IR run-time carrier
is an opaque object address. Aggregate arguments must address exact-layout
snapshot slots, aggregate returns must address exact-layout return snapshots,
and aggregate calls name an exact result slot; all three contracts are
independently verified.

## Current x86-64 backend

The pure-Luna backend consumes verified typed IR directly. It computes System
V parameter/result locations and deterministic stack-frame storage, then emits
the closed assembly dialect accepted by `luna assemble`. Correctness takes priority
over optimization; there is no current standalone machine-IR, liveness or
register-allocation command-line boundary.

`luna assemble` owns instruction parsing and encoding. Its default `LUNAOBJ1` output
is the self-host bootstrap object format; `--emit elf` writes standard ELF64
`ET_REL` for the supported freestanding FFI boundary. `luna link` reads the
supported object forms, resolves symbols and relocations, and writes a static
x86-64 Linux executable without a hosted assembler, linker or libc.

Ordinary Luna symbols encode module and function names. `extern fn` uses its C
symbol verbatim, and `@export_name` deliberately exposes a Luna definition
under a validated verbatim C symbol.

## Archived m0 x86-64 machine IR

The machine-IR, liveness, register-allocation, rewrite and native object-writer
sections below record the hosted `m0` reconstruction pipeline. They are kept as
design history and do not describe commands or data structures implemented by
the current pure-Luna `luna compile` command.

The target machine IR is an owned, target-specific boundary between typed Luna
IR and assembly emission. Lowering resolves `isize` and `usize` to fixed
`i64` and `u64` machine types, assigns every scalar virtual register to the
general-purpose or floating-point register class, and preserves target stack
slot sizes, alignments, control-flow blocks, globals, linkage and module
metadata identities. The backend no longer reads typed Luna IR directly.

Machine instructions are pre-allocation pseudos. They express target-specific
integer widths and signedness, scalar register classes, memory widths,
checks, calls and control flow without prematurely hard-coding physical
registers. Every instruction exposes its single definition, if any, and its
complete ordered use list. Call arguments are explicit uses even though they
are stored in a function-owned flattened argument table.

A separate machine verifier runs after every lowering and again at the
assembly-emission boundary. It checks the target and module shape, declaration
and definition ownership, linkage and metadata, parameter homes, stack slot
layouts, virtual-register types, unique definitions, complete uses, opcode
contracts, call signatures, terminators, branch targets and predecessor
metadata. Assembly emission therefore cannot consume an unchecked machine
module.

`lunac --emit mir` prints this verified boundary deterministically. The text
includes target and module kind, function IDs and linkage, fixed target types,
register classes, stack slots, virtual registers, blocks, definitions, uses and
instruction-specific operands. It is a debugging and test contract for the
current x86-64 backend, not a stable cross-version object format.

The current emitter expands machine pseudos only after verified liveness,
allocation and fixed-register rewriting. Scalar virtual registers use their
assigned physical registers or dense spill slots; exact source objects retain
their machine stack slots. Optimization is deliberately absent. The complete
representation and invariants are documented in
[x86-64 machine IR](machine-ir.md).

## Archived m0 x86-64 System V ABI analysis

ABI analysis consumes verified machine IR and assigns every scalar parameter
to one of the six general-purpose registers, eight vector registers or a
dense eight-byte stack slot. The two register banks are counted independently.
The caller frame is rounded to 16 bytes, and the callee reads the first stack
parameter at `16(%rbp)` after establishing its frame pointer.

The same owned module result classifies aggregate signature descriptors into
integer/SSE eightbytes. It handles structures, unions, fixed arrays,
overlapping union fields, unaligned layouts and the two-eightbyte memory
cutoff. Small values use complete register assignments or whole-value
rollback to the stack; larger results use a hidden destination pointer.

The ABI verifier recomputes every function location and every aggregate
classification. `lunac --emit abi` exposes deterministic parameter locations
and frame sizes. The full boundary is documented in
[x86-64 System V ABI analysis](abi.md).

## Archived m0 x86-64 liveness

The backend computes liveness over the verified machine def/use and CFG
contracts before assembly emission. Each block owns `use`, `def`, `live-in`
and `live-out` bit vectors; every instruction owns exact `live-before` and
`live-after` vectors. A reverse-order fixed-point solver implements the
standard successor-union transfer equations.

The result has an independent verifier that recomputes block def/use, checks
every fixed-point equation, replays every instruction transfer and rejects
malformed bit-vector storage or padding. `lunac --emit liveness` prints the
same verified result deterministically. The analysis changes no instruction;
its full contract is documented in
[x86-64 liveness analysis](liveness.md).

## Archived m0 x86-64 register allocation

The first allocation stage builds one inclusive interval for every machine
virtual register and applies deterministic linear scan independently to the
general-purpose and floating-point banks. Values live on both sides of a call
may use only System V callee-saved registers; because System V has no
callee-saved XMM registers, such floating values are explicitly spilled.

The allocation has its own verifier. It reconstructs intervals from verified
liveness, checks class and call-preservation constraints, rejects overlapping
assignments, verifies dense unique spill slots and recomputes used-register
masks. `lunac --emit allocation` exposes the result deterministically. The
assembly boundary requires this verification to succeed and passes the result
to the fixed-register rewrite stage. The full allocation contract is
documented in
[x86-64 register allocation](register-allocation.md).

## Archived m0 x86-64 instruction rewrite

The rewrite stage combines verified machine IR, System V locations, liveness
and allocation into an owned physical-storage plan. It records every ordered
use and result location, division and shift registers, call destinations,
caller-saved clobbers and used callee-saved GPRs.

Its verifier rejects stale locations or instruction order, reserved-register
assignments, malformed parallel-move sets and any live-through value whose
physical register intersects an instruction clobber. The emitter then uses
assigned registers directly, materializes only real spills and saves/restores
used callee-saved registers in disjoint aligned frame slots. `lunac --emit
rewrite` exposes the checked plan. The full contract is documented in
[allocation-aware instruction rewrite](instruction-rewrite.md).

## Archived m0 x86-64 backend

The current machine-IR consumer is correctness-first:

- System V's first six integer argument-register assignments and integer result
  register for every fixed-width and target-sized integer type;
- System V's first eight SSE argument-register assignments and SSE result
  register for `f32` and `f64`, classified independently from integer
  registers so mixed signatures can use both banks;
- dense eight-byte stack slots for scalar arguments that exhaust either
  register bank, with a 16-byte-aligned caller argument area;
- System V aggregate register pieces, whole-value register rollback, exact
  stack copies, multi-register results and hidden-pointer results;
- explicit 16-byte-aligned stack frames;
- register-resident virtual values plus dense, unique spill slots;
- fixed-register constraints for division, variable shifts and pseudo
  expansion;
- verified parallel ABI destinations and complete caller-saved clobber sets;
- explicit save and restore slots for every used callee-saved GPR;
- canonical zero-extended raw bits in the low 32 bits for 8-bit and 16-bit
  arguments, results and stack homes, with explicit sign extension at signed
  comparisons, division, right shifts and widening conversions;
- canonical `bool` values after both direct and indirect memory loads,
  including raw-pointer aliasing;
- deterministic labels and collision-free module/name/canonical-signature
  symbol mangling;
- global definitions for exported Luna functions and unresolved declarations
  for interface-only dependencies, with canonical signature bytes retained in
  IR and encoded identically at definitions and call sites;
- exact, unmangled ELF names for external C functions, with `.extern`
  declarations and unresolved relocations left for the final linker;
- C ABI sign extension for external `i8` and `i16` arguments and explicit
  canonicalization of external `_Bool` results at the language boundary;
- scalar `movss`/`movsd` arithmetic and ordered `ucomiss`/`ucomisd`
  comparisons, with an explicitly initialized IEEE floating-point
  environment;
- scalar SSE format and integer conversions, including software sequences for
  the unsigned 64-bit range and explicit traps before invalid
  floating-to-integer operations;
- exact-width indirect scalar loads and stores, checked fixed-array indexing,
  raw-pointer scaled addressing, deterministic read-only data emission and
  inline forward/backward `rep movsb` lowering for overlap-safe object copies;
- a Linux `_start` shim for executable IR that exits through syscall 60,
  omitted entirely for separately compiled library modules;
- no dependency on a target C runtime.

The `_start` shim and the canonical zero-to-six-argument wrapper object form
the project-owned system-call boundary. The generated
`luna.linux.syscall` metadata exposes only `usize` argument bits and a raw
`isize` result. The separately compiled `luna.runtime` module is implemented
in Luna on that layer. It provides typed process, file and virtual-memory
services, preserves explicit Linux errors and remains independent of libc.
Its deterministic `.lmi` and `.o` files are sysroot artifacts; applications
compile against the metadata and link the object explicitly.

The minimum standard library is the next typed layer. Its independent
`memory`, `bytes`, `checked`, `ascii`, `binary`, `text`, `path` and `io`
modules are implemented in Luna and
compile to deterministic sysroot metadata and objects. Only `memory` consumes
the virtual-memory runtime API; higher modules build on typed standard
dependencies. None can reach the raw system-call module. The layer deliberately
uses explicit owner/result structures and contains no global allocator state,
runtime initialization or implicit linking.

This backend is intentionally not the performance endpoint. Planned stages are:

1. correct stack-homed code;
2. verified x86-64 machine IR;
3. liveness;
4. linear-scan register allocation;
5. stack arguments and aggregate ABI classification;
6. aggregate by-value IR and ABI lowering;
7. allocation-aware instruction rewrite;
8. instruction-level differential tests;
9. ELF64 relocatable-object emission;
10. minimal project-owned ELF64 static linking;
11. versioned Debug IR and final-address DWARF 5 information.

All eleven stages are complete. The native writer produces deterministic,
self-verified objects with direct x86-64 encoding and explicit relocations.
The static linker consumes Luna and supported freestanding C23 objects without
invoking another linker. The debug pipeline preserves source spans through
Machine IR, encodes relative source-to-code mappings in `.luna.debug`, and
creates standard DWARF 5 after final code layout.

The current direct pseudo expansions remain unoptimized and serve as the
semantic reference for future local instruction-selection work.

## Error handling

Invalid user input must produce a diagnostic and a non-zero exit code, never a
crash or assertion. Internal invariants are checked by the IR verifier.
Allocation and I/O failures are propagated explicitly.

The self-hosted path additionally caps source bytes, aggregate source bytes,
tokens, token length, diagnostics, syntax nodes, module/type/scope recursion
and generated assembly text. Boundary failures have stable machine-readable
diagnostics and are exercised at the exact limit and immediately above it.

## Testing

The current pure-Luna quality gate contains:

- `tools/selfhost.py audit`, which verifies anchor hashes, the registered
  interface/implementation inventory, the derived import graph and source
  rules without changing build outputs;
- `tools/refmt.py --check`, which requires formatting stability and an
  unchanged whitespace-insensitive token stream;
- `tools/selfhost.py verify --fresh`, which rebuilds the complete toolchain with the
  newly built tools and requires every assembly, object and executable to be
  byte-identical;
- `tools/selfhost.py test`, which compiles and runs the behavior corpus with
  exact exit statuses and checks expected semantic failures by diagnostic kind;
- FFI cases using checked-in ELF64 fixtures, Luna assembler ELF round trips and
  optional x86-64 C compiler fixtures at the explicit interoperability
  boundary.

The production build never invokes a hosted compiler, assembler or linker.
Generated programs are freestanding, making native and
`qemu-x86_64-static` execution independent of a target libc or sysroot.
