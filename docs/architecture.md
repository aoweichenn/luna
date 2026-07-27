# Compiler architecture

## Non-negotiable boundaries

The bootstrap compiler is C23. Luna source is never translated to C. The
initial target is exactly `x86_64-unknown-linux-gnu`: x86-64 instructions,
System V calling convention and ELF64 objects or executables.

Generated target programs are freestanding and never depend on libc. The
bootstrap compiler is a hosted C23 tool, but host-library use is confined to
the compiler process. The project-owned x86-64 Linux system-call ABI layer
converts System V calls with zero to six arguments to the kernel register ABI
and invokes `syscall` directly. The Luna runtime and standard library must be
built only on that layer. Optional caller-supplied external objects are an
explicit FFI boundary, not a runtime dependency.

The compiler owns the language semantics. Luna now encodes its x86-64
instructions and writes ELF64 relocatable objects directly. LLVM MC remains
only an independent test oracle. The project-owned `lunalink` resolves symbols,
applies static relocations and writes ELF64 executables directly. LLD remains
only an independent final-link oracle in differential tests. Neither external
tool is used by the production compiler or linker path.

External C functions are represented directly throughout the pipeline; Luna
never generates a C translation unit. The final ELF link may combine a
Luna-generated object with caller-supplied C23 objects or libraries.

Target selection produces an immutable target description before semantic
lowering begins. It records the architecture, operating system, ABI, byte
order, scalar sizes and ABI alignments. The target description is carried by
the typed IR module so target-sized language types never depend on host C
properties.

## Pipeline

```text
source units / validated .lmi metadata
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
          x86-64 machine lowering
                    |
                    v
       verified x86-64 machine IR
                    |
                    v
      verified System V ABI analysis
                    |
                    v
         verified liveness analysis
                    |
                    v
      verified register allocation
                    |
                    v
    verified instruction rewrite
                    |
                    v
          x86-64 instruction emission
                    |
          +---------+---------+
          |                   |
          v                   v
 optional assembly     native instruction encoder
 review output                |
                              v
                  verified ELF64 relocatable object
                              |
                              v
            project-owned static linker
            + verified syscall ABI object
                              |
                              v
                  verified static ELF64 executable
```

Assembly is a closed output encoding of the x86-64 backend, not a user-facing
intermediate language. During bootstrap, native object emission renders this
same closed dialect in memory and consumes it with Luna's internal assembler.
It never invokes an external assembler and rejects forms the backend does not
own. See [the ELF object contract](elf-object.md).

## Frontend

Source locations are byte spans into immutable source files. Tokens and syntax
nodes retain spans so every parser, type and IR error can point to the original
text. The parser uses an arena and never owns isolated syntax nodes.

The syntax tree represents `if`, all three loop forms and non-fallthrough
switch arms directly. Semantic lowering uses one ordered control-frame stack:
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

Compilation is split into global and local phases:

1. load source units and structurally validate every `.lmi`;
2. parse source units and reconstruct metadata interface declarations;
3. group interface and implementation units by exact module name;
4. resolve imports and validate one rooted, reachable, acyclic graph;
5. verify every metadata dependency content fingerprint;
6. topologically lower dependencies into one shared semantic world;
7. collect exported and module-private declarations;
8. match source or metadata interfaces with implementation definitions;
9. type-check function bodies;
10. lower checked bodies to IR.

This order removes source-order dependencies and ordinary forward declarations.
Source order on the command line is irrelevant. The module resolver identifies
the unique implementation containing `main`, requires every supplied module
to be reachable from it and lowers dependencies before importers. Interface
imports enter both interface and implementation scope; implementation-only
imports never enter interface scope. Only exported declarations enter an
importer's scope, and visibility is never transitive.

All modules in one invocation share canonical named-type and function records.
This preserves type identity across module boundaries, rejects ambiguous
unqualified imports and lets compatible repeated external declarations share
one IR symbol while rejecting conflicting signatures. The interface type
graph is resolved before implementation-private types are collected, making
the interface independently valid. Function matching uses canonical semantic
types, so nested arrays, named-type identity and pointer read-only qualifiers
cannot match accidentally by spelling or layout.

Separate compilation uses deterministic little-endian `.lmi` files rather
than serializing host memory or parser pointers. The fixed header contains a
magic value, format major/minor version, language ABI version, payload byte
count and a 64-bit content fingerprint. The payload contains the exact target
triple, module name, direct imports, interface types, fields, enum values and
function signatures. Recursive type encodings and all record/string/file
sizes have explicit limits. The decoder rejects malformed flags and tags,
invalid identifiers, truncation, trailing bytes, target mismatches and
fingerprint failures before declarations reach semantic analysis.

Each encoded direct import includes the content fingerprint of its dependency.
The module resolver requires metadata dependencies to match those
fingerprints exactly. Separate code generation also requires the root
module's own compiled metadata instead of its source interface. IR module
imports and exports retain that interface identity, and x86-64 symbol mangling
includes it so the static linker cannot silently combine objects compiled
from different metadata. The fingerprint protects build consistency and
accidental corruption; it is not a cryptographic authentication mechanism.
The exact byte layout and compatibility rules are specified in the
[module metadata format](module-metadata.md).

`--compile-module` selects one non-executable root, forbids implementation
source for its dependencies and produces IR without an entry function. A
normal executable may mix source modules with metadata-only dependencies.
Metadata-only functions become checked bodyless Luna declarations, while the
selected module's implementation alone contributes definitions.

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
functions own parameter slots, values and a CFG body. Module imports and
external C functions own only a typed signature and must have no slots,
values, call-argument storage or blocks. Module exports print as `export fn`;
metadata imports print as qualified `import fn`; C symbols print as bodyless
`extern fn`. No declaration may be the module entry point. Library IR has no
entry function at all.

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

## x86-64 machine IR

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

## x86-64 System V ABI analysis

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

## x86-64 liveness

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

## x86-64 register allocation

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

## x86-64 instruction rewrite

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

## x86-64 backend

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
- deterministic labels and collision-free module symbol mangling;
- global definitions for exported Luna functions and unresolved declarations
  for functions imported from `.lmi` metadata, with the exact module metadata
  fingerprint retained in IR and encoded into both symbol names;
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

## Testing

The quality gate contains:

- GoogleTest unit tests for utilities, source handling, lexing, parsing,
  semantic lowering, typed-IR and machine-IR invariants and x86-64 emission;
- parser, type and module-error negative tests;
- transitive module execution and IR snapshots, full source-order determinism,
  interface self-containment, exact signature matching, import visibility,
  graph validation and cross-source diagnostic-note tests;
- deterministic metadata round trips, re-checksummed binary mutations,
  corrupt/version/stale-dependency negatives and three-object separate-link
  execution tests;
- textual typed-IR, machine-IR, ABI, liveness, register-allocation and
  instruction-rewrite snapshots;
- native x86-64 encoding and ELF64 object verification, with LLVM MC retained
  as an independent differential oracle;
- project-owned static ELF64 linking, executable verification and malformed
  object mutation tests, with LLD retained only as an oracle;
- execution under `qemu-x86_64-static`;
- deterministic generated-program differential tests;
- executable matrices and boundary traps for every numeric scalar conversion
  family;
- executable conditional matrices for every scalar type and switch-boundary
  matrices for every integer type;
- exact-width memory matrices, null and bounds traps, read-only qualification
  negatives and typed-memory IR mutation checks;
- executable nested-aggregate, union-aliasing and scoped-enum cases, exact
  layout-query assertions, named and nested initialization, padding-preserving
  and deliberately overlapping copies, member-address and memory-copy IR
  mutation checks and generated aggregate differential programs;
- real C23-to-Luna static linking tests covering every scalar type, pointers,
  aggregate layout through pointers, no-result calls, narrow signed promotion
  independently classified integer/SSE register banks and scalar stack
  arguments;
- structured-control negative cases, IR snapshots and randomized differential
  programs;
- deterministic mutation tests and a coverage-guided libFuzzer target that
  exercise machine lowering, ABI analysis, liveness, register allocation,
  instruction rewriting, verification, printing and assembly emission;
- an independent textual machine-IR parser and reference interpreter,
  differential execution of all machine opcodes against assembled x86-64,
  exact trap-signal comparison, deterministic generated instruction programs
  and mandatory spill, parallel-move and fixed-register coverage;
- UBSan runs for the host compiler and ASan runs on compatible native hosts;
- warnings treated as errors.

Generated x86-64 programs are freestanding in the first milestone, which makes
cross-target execution deterministic and independent of a target sysroot.
