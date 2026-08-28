# Anchor provenance

The three executables in this directory are the fixed-point Luna toolchain
from the archived `m0` branch, byte-identical to the tools inside the
canonical seed archive:

```text
luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar
SHA-256: see ../release/seeds/
```

They are freestanding static x86-64 Linux ELF64 files built from pure Luna
sources by the m0 bootstrap pipeline (C23 seed -> stage 1 -> stage 2 ->
stage 3, with stage 2 and stage 3 byte-for-byte identical). They contain no
libc, no dynamic loader and no hosted assembler or linker output.

This directory is the sole trusted binary input of the repository. Every
later toolchain is built from the Luna sources in `library/`, `compiler/`
and `drivers/` using these anchors, then verified against its own rebuild
(`python3 tools/selfhost.py verify`).

Replace the anchor only after a green `verify` run, together with refreshed
`SHA256SUMS` and a new provenance note.

## 2026-08-23: promotion to the m1.1 self-built toolchain

The anchor now holds the stage-fixed toolchain built from commit
`06d15e0a35b43b06824e0ab936d4d939e9e7f685`
(`refactor: reflow to 120 columns, table-driven mappings, split semantic
monolith`). Provenance chain: the previous m0 anchor (above) built
`out/stage-next`, which rebuilt itself into `out/stage-fixed`; `verify`
confirmed every assembly, object and executable artifact byte-identical
between the two stages, and `test` passed 80/80. The stage-fixed
executables were copied here and `SHA256SUMS` refreshed.

## 2026-08-23: promotion to the m1.2 self-built toolchain

The anchor now holds the stage-fixed toolchain built from commit
`d7590d81a0218c41b2eeadfdc587d7f10ba6b840`
(`feat(m1.2): volatile object and pointee qualification`). Provenance
chain: the previous m1.1 anchor (above) built `out/stage-next`, which
rebuilt itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 96/96. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
Compiler sources may now adopt m1.2 syntax: declaration attributes
(`@inline`/`@noreturn`), compile-time `assert` and `volatile`
qualification.

## 2026-08-23: promotion to the built-ins self-built toolchain

The anchor now holds the stage-fixed toolchain built from commit
`39d2df3bed094acc8be762e0bd5ba7189e5fde61`
(`feat(builtins): overflow-reporting arithmetic intrinsics`). Provenance
chain: the previous m1.2 anchor (above) built `out/stage-next`, which
rebuilt itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 106/106. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
Compiler sources may now adopt the `@`-intrinsics: bit operations,
float helpers and overflow-reporting arithmetic.

## 2026-08-23: promotion to the m1.3 self-built toolchain

The anchor now holds the stage-fixed toolchain built from commit
`8a9e0f168cfd3318a43052eff8ed407c170dab2f`
(`feat(m1.3): @bits bitfields`). Provenance chain: the previous
built-ins anchor (above) built `out/stage-next`, which rebuilt itself
into `out/stage-fixed`; `verify` confirmed every artifact byte-identical
between the two stages, and `test` passed 122/122. The stage-fixed
executables were copied here and `SHA256SUMS` refreshed. Compiler
sources may now adopt m1.3 syntax: `@align`, `@packed`, anonymous
members, `[?]T` header types and `@bits`.

## 2026-08-24: promotion to the m1.6 self-built toolchain

The anchor now holds the stage-fixed toolchain built from commit
`203172434e8666936f00df5ab7077b778ac15a3b`
(`feat(m1.6): labeled break/continue and validated goto`). Provenance
chain: the previous m1.3 anchor (above) built `out/stage-next`, which
rebuilt itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 146/146. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
Compiler sources may now adopt m1.4-m1.6 syntax: character/binary/hex
literals, wide strings, array initializer lists, let/var inference,
typed const, const fn, labels and goto.

## 2026-08-24: promotion to the m1.8-feature toolchain

The anchor now holds the stage-fixed toolchain built from commit
`521a40641f8e1a582f16733ad86eaf333a3b3ceb`
(`feat(m1.8): asm fn naked assembly functions`). Provenance chain: the
previous m1.6-era anchor (above) built `out/stage-next`, which rebuilt
itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 165/165. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
The new anchor can parse `asm fn`, unlocking the syscall stub migration.

## 2026-08-24: promotion to the conditional-injection toolchain

The anchor now holds the stage-fixed toolchain built from commit
`0f5f316aa560cf842a08c47965a9762e7a990586`
(`feat(linker): inject syscall stubs only when undefined`). Provenance
chain: the previous anchor (above) built `out/stage-next`, which rebuilt
itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 165/165. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
This linker's injection is conditional, allowing the syscall stubs to
move to asm fn source without duplicate definitions.

## 2026-08-24: promotion to the corrected conditional-injection toolchain

The anchor now holds the stage-fixed toolchain built from commit
`9c5be703ed974ad183a0929718d7aeddbea05c32`
(`fix(linker): correct syscall0 probe length in conditional injection`).
Provenance chain: the previous anchor (above) built `out/stage-next`,
which rebuilt itself into `out/stage-fixed`; `verify` confirmed every
artifact byte-identical between the two stages, and `test` passed
165/165. The stage-fixed executables were copied here and `SHA256SUMS`
refreshed. This linker skips its syscall injection when an input object
already defines the stubs.

## 2026-08-24: promotion to the m1.8 toolchain

The anchor now holds the stage-fixed toolchain built from commit
`bce9e4107b859eca9de695ed28565fd47488bcf2`
(`feat(m1.8): migrate syscall stubs to asm fn source`). Provenance
chain: the previous anchor (above) built `out/stage-next`, which
rebuilt itself into `out/stage-fixed`; `verify` confirmed every
artifact byte-identical between the two stages, and `test` passed
165/165. The stage-fixed executables were copied here and `SHA256SUMS`
refreshed. The syscall stubs now come from asm fn source in
library/linux/syscall.luna; the linker injects nothing.

## 2026-08-24: promotion to the m1.10 toolchain

The anchor now holds the stage-fixed toolchain built from commit
`bffea2717a470e7991acad650e6f9a0ab0d0c6be`
(`feat(m1.10): @embed compile-time file embedding`). Provenance chain:
the previous anchor (above) built `out/stage-next`, which rebuilt
itself into `out/stage-fixed`; `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 170/170. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.
The m1 milestone series (m1.1-m1.10) is complete with this toolchain.

## 2026-08-24: promotion to the m1.12 toolchain

The anchor now holds the stage-fixed toolchain built from commit
`435103b` (`feat(m1.12): selective imports import a.b.c::{x, y}`),
which includes m1.11 (`::` module qualification, `as` alias imports)
and the `.luna`/`.interface.luna` → `.la`/`.lh` extension rename.
Provenance chain: the previous anchor (above) built `out/stage-next`,
which rebuilt itself into `out/stage-fixed`; `verify` confirmed every
artifact byte-identical between the two stages, and `test` passed
181/181. The stage-fixed executables were copied here and `SHA256SUMS`
refreshed. Compiler sources may now adopt qualified names and alias or
selective imports per the iteration discipline.

## 2026-08-25: promotion to the multi-implementation-unit toolchain

The anchor now holds the stage-fixed toolchain built from commit
`d0da3560a5d5c940b3a10f05b6f7b83b0fc44cb4`
(`feat: support multiple module implementation units`). Provenance chain: the
previous anchor built `out/stage-next`, which rebuilt itself into
`out/stage-fixed`; remote x86-64 `verify` confirmed every artifact
byte-identical between the two stages, and `test` passed 205/205. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed. Production
modules may now split one named module across multiple registered `.la`
implementation units while retaining one shared private declaration/import
scope.

## 2026-08-26: promotion to the M2.0 callable-identity toolchain

The anchor now holds the stage-fixed toolchain built from commit
`5e344edd3831df0f5fbba00189dd80bb6893ff25`
(`feat: add canonical callable identities`). Provenance chain: the previous
anchor built `stage-transition`, those tools built `stage-next`, and
`stage-next` rebuilt the complete graph into `stage-fixed`; remote x86-64
verification on `caw` confirmed every stage-next/stage-fixed assembly, object
and executable artifact byte-identical. The final test run passed 208/208,
including exact version-1 signature symbols, implementation-unit order
permutation and matching/mismatched separately compiled callable identities.
The stage-fixed executables were copied here and `SHA256SUMS` refreshed.

## 2026-08-26: promotion to the M2.1 overload-binding toolchain

The anchor now holds the stage-fixed toolchain built from commit
`104d7337c585a9bc0fd493998e75d16a181f1c66`
(`feat: add callable overload bindings`). The promoted compiler has explicit
module-local callable bindings, canonical candidate slices, overload-key
interface/implementation merging and per-candidate visibility. The semantic
functions module is split across collection, signature, overload and binding
implementation units behind one interface. Remote x86-64 verification on
`caw` confirmed every stage-next/stage-fixed assembly, object and executable
artifact byte-identical; the final test run passed 215/215, including overload
duplicates, result/contract mismatches, missing definitions, private/exported
visibility and implementation-order permutation. The stage-fixed executables
were copied here and `SHA256SUMS` refreshed.

## 2026-08-26: promotion to the M2.2 exact-overload toolchain

The anchor now holds the stage-fixed toolchain built from commit
`74e6d8509d37aaaf1c73e080d4f49267a4cfd59b`
(`feat: resolve callable overloads exactly`). The promoted compiler resolves
multi-candidate calls through a non-emitting compatibility probe, preserves
nested ambiguity, memoizes stable call selections by syntax node and selects
overloaded function values from an exact expected function-pointer type. The
probe is split across core, operator and call implementation units behind one
interface. Remote x86-64 verification on `caw` confirmed every
stage-next/stage-fixed assembly, object and executable artifact byte-identical;
the final test run passed 233/233, including literal/null/initializer
ambiguity, array shape, pointer qualification and arithmetic, function values,
nested calls and single evaluation. The stage-fixed executables were copied
here and `SHA256SUMS` refreshed.

## 2026-08-26: promotion to the M2.3 default-parameter toolchain

The anchor now holds the stage-fixed toolchain built from commit
`064dce904591c38f6228c0958d040a9a9b64fadb`
(`feat: add folded default parameters`). The promoted compiler parses and
validates trailing defaults, stores folded integer/boolean/null parameter
values, expands callable arity without ranking and appends omitted values only
after overload selection. Interface defaults remain caller-side source API;
implementations omit them, and function types and link identities remain
unchanged. Remote x86-64 verification on `caw` confirmed every
stage-next/stage-fixed assembly, object and executable artifact byte-identical;
the final test run passed 252/252, including interface/private defaults,
constant and layout folding, exact enum typing, function-pointer arity,
overload ambiguity and invalid placement/expression/mount diagnostics. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.

## 2026-08-26: promotion to the M2.4 value-category toolchain

The anchor now holds the stage-fixed toolchain built from commit
`87014f16abd7281da2e3ec875dd652849f834748`
(`feat: unify expression value categories`). The promoted compiler replaces
three expression booleans with one closed, one-word value-category enum shared
by emitting lowering and overload probing. Pointer qualification and
member/index projection use centralized strategies; writable bitfields remain
assignable but non-addressable, while aggregate temporaries remain readable
but cannot be assigned or addressed. Remote x86-64 verification on `caw`
confirmed every stage-next/stage-fixed assembly, object and executable artifact
byte-identical; the final test run passed 256/256, including temporary
member/index overload probing and rejected temporary address/assignment cases.
The stage-fixed executables were copied here and `SHA256SUMS` refreshed. M2 is
complete with this anchor; M3 may consume the shared callable and receiver
foundations.

## 2026-08-27: promotion to the M3.6 class-system toolchain

The anchor now holds the stage-fixed toolchain built from commit
`3511b490cf1c1114805ba00b36a0878fa94afb2f`
(`feat: add class value composition`). Starting from the previous M2.4 anchor,
the transition toolchain implemented the complete M3.0-M3.6 class system:
direct classes and constructors, single inheritance, virtual/abstract dispatch,
restricted operators, non-owning bound methods, descriptor-chain RTTI,
same-module friendship, pointer-only opaque classes and declaration-order class
value composition with in-place member construction.

Remote x86-64 verification on `caw` built `stage-transition`, `stage-next` and
`stage-fixed`; every stage-next/stage-fixed assembly, object and executable
artifact was byte-identical. The final verified-toolchain test run passed
383/383, including direct, inherited, polymorphic, imported, opaque-internal
and nested-array composition plus negative initialization diagnostics. The
stage-fixed executables were copied here and `SHA256SUMS` refreshed.

## 2026-08-27: promotion to the M4 native-generics toolchain

The anchor now holds the stage-fixed toolchain built from commit
`ff021a4` (`feat: implement M4 native generics`). Starting from the previous
M3.6 anchor, the transition toolchain implemented concise angle-bracket
generic syntax, exact function inference, canonical monomorphization, generic
structures, unions, transparent aliases and direct generic classes with
ordinary, static and bound methods.

Remote x86-64 verification on `caw` built the exact committed source snapshot
through `stage-transition`, `stage-next` and `stage-fixed`; every
stage-next/stage-fixed assembly, object and executable artifact was
byte-identical. The promoted stage-fixed toolchain passed 409/409 tests,
including cross-module generic bodies and ABI identities, source-order
determinism, recursive expansion limits and rejected unsupported generic class
cross-products. The stage-fixed executables were copied here and
`SHA256SUMS` refreshed. Compiler sources may now adopt M4 generic syntax under
the normal fixed-point discipline.

## 2026-08-27: promotion to the unified Luna toolchain

The anchor now contains the single stage-fixed `luna` executable built from
commit `587a929` (`refactor: unify Luna toolchain driver`). Its `compile`,
`assemble` and `link` commands retain independent modules and command
contracts behind one table-driven entry point. The project linker input bound
is 128 objects, covering the unified driver's 75-object closure.

Remote x86-64 verification on `caw` started from the previous three-tool M4
anchor, used the bounded first-transition linker bridge, and built
`stage-transition`, `stage-next` and `stage-fixed`. Every stage-next/stage-fixed
assembly, object and the sole `bin/luna` executable was byte-identical. The
promoted stage-fixed executable passed 416/416 tests, including root and
command CLI contracts, all language behavior, relocation determinism and the
ELF/host FFI matrix. The three previous anchor executables were replaced by
this one file and `SHA256SUMS` was reduced to its single hash.

## 2026-08-27: promotion to the M5 lifetime toolchain

The anchor now contains the stage-fixed `luna` executable built from the M5
source snapshot accompanying this provenance note. Starting from the unified
M4 anchor, the transition toolchain added C++-shaped references and xvalues,
binding-aware overload ranking, deterministic RAII, explicit copy/move special
members, full-expression temporaries, reference lifetime extension and
destination-based construction and aggregate return.

Remote x86-64 recovery on `caw` used the preceding trusted unified anchor to
build the final M5 source snapshot before any library source adopted reference
syntax. The recovered toolchain then compiled the registered
`luna.std.utility` module and passed 432/432 tests, including control-flow
cleanup, arrays/fields/bases, inaccessible destruction, move-only resources,
short-circuit temporaries, copy elision, rejected incomplete nontrivial
aggregates and the complete ELF/host FFI matrix. Compiler and library sources
may now adopt M5 reference syntax under the normal fixed-point discipline.
The recovered compiler built the complete 77-module graph through
`stage-transition`, `stage-next` and `stage-fixed`; every next/fixed assembly,
object and executable artifact was byte-identical before the final stage-fixed
executable was copied here and `SHA256SUMS` refreshed.

## 2026-08-27: promotion to the container-foundation toolchain

The anchor now contains the stage-fixed `luna` executable built from the
container-foundation and modernization source snapshot accompanying this
provenance note. The compiler adds the compile-time
`@is_trivially_relocatable(Type)` property, type-expression resolution for
generic type parameters, receiver-qualification overload ranking and correct
special-member accessibility for imported generic wrappers around private
move-only types. The module registry now represents interface-only generic
modules directly instead of requiring empty implementation units.

The standard-library foundation adds C++-shaped lowercase `span<Value>`,
`const_span<Value>`, move-only `byte_buffer` and trivially-relocatable
`vector<Value>` abstractions. Stateful ownership is isolated in the RAII byte
buffer; typed views and the vector wrapper remain zero-overhead generic
interfaces. The same snapshot also applies the bounded-loop, named-predicate
and table-driven validation rules to the touched runtime, standard-library and
semantic sources.

Remote x86-64 verification on `caw` built the complete 80-module graph through
`stage-transition`, `stage-next` and `stage-fixed`; every next/fixed assembly,
object and executable artifact was byte-identical. The promoted executable is
4,536,150 bytes with SHA-256
`86c69dabc50fb57b05fe7e082d6dec3d7822d185b2f92552ea1eba94f528821f`.
The final test run passed 438/438, including mutable/const receiver selection,
trivial-relocation classification, rejected nontrivial vectors, cross-module
private move construction and the complete ELF/host FFI matrix.

## 2026-08-28: promotion to the first compiler-vector toolchain

The anchor now contains the final stage-fixed `luna` executable from the first
compiler adoption of `luna.std.vector`. The initializer lowering unit owns a
private `InitializerIndexSet` class composed from `vector<usize>`; it replaces
three raw byte buffers, pointer casts, byte-count conversions and manual
release paths with one typed uniqueness and RAII boundary.

The adoption exposed and fixed a pre-existing generic-class ordering defect.
A concrete imported generic class can be instantiated during type layout when
it is a field of an ordinary class. Ordinary class-method collection now skips
such concrete generic instances; their methods remain lazily materialized by
`instantiate_generic_class` only while the instance's type-parameter bindings
are active. The regression suite includes a non-generic class containing an
imported `Box<i32>` field.

The change followed a two-step bootstrap on remote x86-64 `caw`: the previous
anchor first built a byte-identical fixed point containing the method-ordering
fix and local vector adoption, and that verified toolchain then built the final
class-composed source through `stage-transition`, `stage-next` and
`stage-fixed`. Every final next/fixed assembly, object and executable artifact
was byte-identical. The final test run passed 439/439 with no skips. The
promoted executable is 4,556,630 bytes with SHA-256
`5e4530aa12706080fad52733656c6d310de6d426e92262eae32e703fdc59ec38`.

## 2026-08-28: promotion to the standard-container toolchain

The anchor now contains the stage-fixed compiler supporting the first
C++-shaped `list<Value>`, `deque<Value>`, `map<Key, Value>` and `queue<Value>`
library surfaces. Public names and operations follow the C++ standard-library
spelling. The reusable red-black tree and stable slot pool remain implementation
dependencies under `luna.internal`, because C++ exposes neither as a standard
container.

All owning containers currently require trivially relocatable elements. The
node pool keeps stable addresses and moves its block/bitmap/free-list algorithm
into one non-generic `slot_storage` implementation, while list and map retain
typed generic wrappers. The tree validates ordering, parent links, red-parent
rules, black height and reachable node count after insertion and deletion.

The container work exposed two general semantic defects. Member access now
completes an incomplete concrete generic record before field lookup. Call probe
and emitting lowering refresh parameter and field views around argument
expressions that can lazily instantiate generics, preventing stale pointers
after store growth. Neither fix contains a container-specific exception.

Remote x86-64 `caw` verification built the 86-module graph through
`stage-transition`, `stage-next` and `stage-fixed`; every next/fixed assembly,
object and executable artifact was byte-identical. The final suite passed
443/443 with no skips. Its expectation phase produced identical results with
one and four workers; wall time fell from 83.24 seconds to 49.44 seconds. The
promoted executable remains 4,556,630 bytes with SHA-256
`c2c36ff54455138416afdd2004ea2294c8cfbc68a6c375fc13e7a6987dabb500`.

## 2026-08-28: promotion to the contracted-backend toolchain

The anchor now contains the stage-fixed `luna` executable from the x86-64
backend module contraction snapshot accompanying this provenance note. The
historical 20-module `luna.bootstrap.backend.x86_64.*` graph is reduced to six
short dependency modules under `luna.compiler.x86`: text, codegen, object,
ELF, assembler and linker. Each module owns one interface and retains its
facade, ABI, frame, reader, writer, operand, symbol, encoding and instruction
families as separate implementation files.

The source layout now groups each backend module in one directory containing
only implementation files; no `x86_64` directory level mixes source files and
child source directories. Non-generic backend interfaces remain between 13
and 175 lines. Audit enforces this uniform migrated layout and a 250-line
ceiling for contracted compiler interfaces.

Remote x86-64 verification on `caw` used the preceding trusted anchor to build
the transition toolchain, then rebuilt stage-next and stage-fixed with the
indexed assembler. Every next/fixed assembly, object and executable artifact
was byte-identical. The final test run passed 443/443 with no skips. Reassembling
the 3,070,612-byte `sem_funcs.s` took 0.75 seconds and reproduced the transition
object byte-for-byte. The promoted executable is 4,654,934 bytes with SHA-256
`86a6c5e2102d036e417171102cd6e58e43be79cbccfabc80ae9b11daba1ecf0f`.

## 2026-08-28: promotion to the standard-string toolchain

The anchor now contains the stage-fixed `luna` executable from the standard
character-foundation snapshot accompanying this provenance note. The former
backend-specific byte builder and the transitional `luna.std.text` module are
removed. Their responsibilities are separated into the C++-shaped
`luna.std.string_view`, `luna.std.string` and `luna.std.charconv` modules.
The owning string is a move-only RAII class with contiguous NUL-terminated
storage; integer conversion is allocation-free, and code generation transfers
its final assembly buffer without copying.

Remote x86-64 verification on `caw` built the complete 75-module graph through
`stage-transition`, `stage-next` and `stage-fixed`; every next/fixed assembly,
object and executable artifact was byte-identical. The final test run passed
443/443 with no skips. A single stage build took 86.73 seconds and the complete
fixed-point verification took 251.72 seconds. Compiling the 3,069,988-byte
`sem_funcs.s` took 10.00 seconds and assembling it took 0.75 seconds. The
promoted executable remains 4,654,934 bytes with SHA-256
`b05126bfd8d3cee85431df92cd828d65a3b794122bf6af8820f11b288da89c07`.

## 2026-08-28: promotion to the object-oriented assembler toolchain

The anchor now contains the stage-fixed `luna` executable from the Assembler
RAII snapshot accompanying this provenance note. The historical public
`State` record and `*State` procedure families are replaced by one private
`Assembler` class composed from `Object`, `SymbolTable`, `vector<Fixup>` and
`vector<NumericLabel>`. Construction, deterministic destruction and
`take_result()` now own every success, failure and transfer path.

The closed x86-64 subset is represented by 87 instruction rules and 30
condition-code aliases initialized once per assembly session and dispatched by
`EncodingKind`. Directives, sections, symbol kinds and legacy registers use
the same table-driven policy. All bounded traversals use `for`; only source
consumption retains state-driven `while` loops, and every condition has at
most two logical clauses.

Remote x86-64 verification on `caw` built the complete 75-module graph through
`stage-transition`, `stage-next` and `stage-fixed`; every next/fixed assembly,
object and executable artifact was byte-identical. The final test run passed
443/443 with no skips, including forward/backward numeric labels and rejected
unresolved fixups. Three assemblies of the 3,069,988-byte `sem_funcs.s` took
0.767, 0.774 and 0.762 seconds and produced an object byte-identical to the
previous assembler. The promoted executable is 4,704,081 bytes with SHA-256
`001fb90a511648f25d255320ae9761045625832ef4ee8145784d5b1a2c78142a`.

## 2026-08-28: promotion to the object-oriented ELF toolchain

The anchor now contains the stage-fixed `luna` executable from the ELF object
I/O RAII snapshot accompanying this provenance note. The former Reader and
Writer state records and free-procedure families are replaced by private
`ElfReader` and `ElfWriter` operation classes. Constructors establish every
owned vector, scratch buffer and partial object; `take_result()` transfers a
completed result; deterministic destruction releases every unfinished path.

Untrusted wire integers are decoded once into closed enums. Section, symbol,
special section-index and relocation kinds use `switch` in the Reader;
object sections, symbol kinds, relocation kinds and content sources use
`switch` in the Writer. Range and resource checks retain short named
predicates with at most two logical clauses. A sticky writer state replaces
long chains of conditional field emissions. The implementation also fixes a
synthetic-symbol-name view that previously could outlive its local array.

Typed `vector<InputSection>`, `vector<OutputSection>` and
`vector<SymbolMapping>` storage replaces raw byte tables. The domain-specific
mapping record deliberately prevents a duplicate cross-module
`vector<usize>` monomorph until the Luna object format supports COMDAT or weak
ODR merging. Format decoding, Reader, Writer and facade responsibilities stay
in separate implementation files behind one `luna.compiler.x86.elf`
interface.

Remote x86-64 verification on `caw` built the complete 75-module graph through
`stage-transition`, `stage-next` and `stage-fixed` with `verify --fresh` in
130.58 seconds; every next/fixed assembly, object and executable artifact was
byte-identical. The final test run passed 443/443 with no skips, including six
ELF/relocation integration probes and malformed-header, section-table,
symbol-table and relocation-symbol boundaries. Audit and formatting gates
were green. The promoted executable is 4,728,657 bytes with SHA-256
`248fa7666daea68c5e23b0c783ec4c566582327ff2855e4d5897ce6810ab963d`.

## 2026-08-28: promotion to the object-oriented static-linker toolchain

The anchor now contains the stage-fixed `luna` executable from the static
Linker RAII snapshot accompanying this provenance note. The former monolithic
`Image`/`BinaryWriter` records and free-procedure pipeline are replaced by one
private `StaticLinker` operation class. It composes typed placements, an
ordered global-symbol table, RAII region buffers, deterministic ELF emission
and sticky first-error/input-index state.

The implementation is divided into symbols, layout, relocation, writer and
facade method families behind one `luna.compiler.x86.linker` interface.
`vector<Placement>` replaces the fixed stack array, while
`map<GlobalName, Global>` replaces raw-byte global records and repeated linear
lookup. Section and relocation alternatives use closed `switch` dispatch;
layout arithmetic uses checked addition/alignment, and output fields share the
Linker error state rather than a second weak writer protocol. Static ET_EXEC
semantics remain the only mode; no placeholder dynamic-link strategy was
introduced.

Remote x86-64 verification on isolated `caw` built the complete 75-module
graph through `stage-transition`, `stage-next` and `stage-fixed` with
`verify --fresh` in 94.40 seconds. Every next/fixed assembly, object and
executable artifact was byte-identical. The final suite passed 443/443 with no
skips in 5.16 seconds, including direct deterministic links, static program
headers, duplicate globals, missing and invalid entry points and unresolved
symbols. Audit and formatting gates were green.

For the saved dispatch probe, the old and new 429-byte objects and 8,200-byte
executables were byte-identical. Relinking the complete toolchain input set
three times produced the same 4,794,193-byte SHA-256 output with both linkers;
median wall time fell from 21.754 seconds with the preceding anchor to 0.488
seconds with `StaticLinker`. The promoted executable is 4,794,193 bytes with
SHA-256
`f11c8b0325edd80374531f85a9ed65d31df437bb604958eea192236438a9ad14`.

## 2026-08-28: promotion to the object-oriented Codegen toolchain

The anchor now contains the stage-fixed `luna` executable from the Codegen
RAII snapshot accompanying this provenance note. The former public ABI/frame
byte stores and `*CodegenContext` procedure families are replaced by private
`AbiLayout`, `FramePlan` and `CodeGenerator` classes. Typed vectors own every
ABI and frame record, class composition owns phase order and cleanup, and
`take_result()` transfers only the final assembly buffer. The public
`BackendResult` consequently contains only assembly and error.

The implementation is divided into ABI, frame, support, value, conversion,
caller, callee, instruction, module and facade method families behind one
`luna.compiler.x86.codegen` interface. Bounded traversals use `for`, closed
opcode, register, width and escape alternatives use `switch`, and only the
state-driven inline-assembly string scanner retains `while`. Assembly writes
share one sticky first-error state instead of long conditional write chains;
every condition remains within the two-clause limit. No optimizer, machine IR
or artificial target-strategy hierarchy was introduced.

Remote x86-64 verification on isolated `caw` built the complete 75-module
graph through `stage-transition`, `stage-next` and `stage-fixed` with
`verify --fresh` in 62.47 seconds. Every next/fixed assembly, object and
executable artifact was byte-identical. The final suite passed 443/443 with no
skips in 4.30 seconds; six direct Codegen/relocation probes also covered empty
results, rejected invalid IR, deterministic repeated emission and cleanup.
Audit and formatting gates were green.

Three compilations of the unchanged `sem_funcs` module took 6.014, 6.040 and
6.024 seconds and each reproduced the previous 3,069,988-byte assembly with
SHA-256
`b9f50e44fa6a98aaed59c883a0ac97858a6a5c18d6e35ce40b761a4a041e6b0c`.
Median wall time fell from 9.565 to 6.024 seconds, about 37.0 percent. The
promoted executable is 4,798,279 bytes with SHA-256
`cfdacf4855d86d70925fc7766cd8fe057ceaba329655ba093eda331ac4e0b65b`.

## 2026-08-28: promotion to the RAII Object model

The anchor now contains the stage-fixed `luna` executable from the x86-64
Object ownership snapshot accompanying this provenance note. The former
public six-buffer Object record and free-procedure mutation/release API are
replaced by a move-only `Object`, a single `ObjectBuilder` mutation boundary,
private LUNAOBJ1 Reader/Writer operations and an RAII `ObjectSet` for linker
inputs. Symbol and Relocation remain passive records; their storage is now
typed `vector` rather than byte tables and pointer casts.

Assembler and ELF Reader own Builders, while ELF Writer and StaticLinker read
passive domain views. Link drivers move completed Objects into ObjectSet, so
no owning handle is shallow-copied and no caller maintains a manual release
tree. The duplicate-input test uses the explicit const-copy overload. A
narrow `vector::detach` migration boundary transfers typed data, element
shape and exact allocation size before resetting the source vector; ObjectSet
uses it together with byte-buffer detach and releases every allocation in its
destructor.

Public Object views deliberately use non-generic Byte/Symbol/Relocation view
records. Directly embedding generic spans in a cross-module record produced
duplicate specializations because LUNAOBJ1 does not yet support COMDAT or weak
ODR merging. Internal Object storage and algorithms still use typed vectors
and spans. The real 128-input linker bound is represented by a fixed typed
descriptor array rather than a raw byte table or an unnecessary dynamic
allocation.

Remote x86-64 verification on isolated `caw` built the complete 75-module
graph through `stage-transition`, `stage-next` and `stage-fixed` with
`verify --fresh` in 52.78 seconds. Every next/fixed assembly, object and
executable artifact was byte-identical. The final suite passed 443/443 with no
skips in 4.34 seconds, and all six Object/ELF/relocation probes passed. Audit
and formatting gates were green.

The final 429-byte LUNAOBJ1 probe, 944-byte ELF object and 8,200-byte
executable are byte-identical to the preceding anchor outputs, with SHA-256
values `c5b3ebe6c59c33489e2088d41626437eb89237f5d2b3b10a16688471108ca62d`,
`c86f305ce26f4c66ee9ff1c2afe36b81d37942607019ee059daaca4c335cfc76`
and `8f7b2e1fbda758462d5f709993c2dc53fb477db491cccecdb71a3e479816d592`.
For the complete toolchain input set, recorded old/new link samples were
0.509/0.500/0.507 and 0.742/0.737/0.742 seconds; no further performance scope
was added in this batch. The promoted executable is 4,876,112 bytes with
SHA-256
`a264d40411ce8c88086f0477497c25cbd5d59be7356025dd16cba60838337870`.

## 2026-08-28: promotion to the unified Tools module

The anchor now contains the stage-fixed `luna` executable from the unified
command-tools snapshot accompanying this provenance note. Four shallow
`luna.tools.cli`, `luna.tools.compile`, `luna.tools.assemble` and
`luna.tools.link` modules are replaced by one cohesive `luna.tools` module and
one narrow public interface. The freestanding entry point remains an
independent driver module and delegates only to the tools facade.

`CommandLine`, `ToolDriver`, `CompileCommand`, `AssembleCommand` and
`LinkCommand` now own argument traversal, command dispatch, paths and stage
cleanup. Closed root dispatch uses an enum and `switch`; bounded input
traversal uses `for`; command resources are released by their destructors.
The three observable compile, assemble and link stages, their files, exit
codes, diagnostics and fixed protocol remain unchanged. No artificial command
inheritance hierarchy or hidden in-process build pipeline was introduced.

Remote x86-64 verification on isolated `caw` built the complete graph through
`stage-transition`, `stage-next` and `stage-fixed` with `verify --fresh` in
53.03 seconds. The graph contracted from 75 to 72 modules and from 65 to 62
library objects; every next/fixed artifact was byte-identical. Audit and
formatting gates were green. The expanded final suite passed 447/447 with no
skips in 4.39 seconds, including 11 exact CLI and fixed-protocol checks.

The fixed compile/assemble/link protocol returned 42 at every stage and
produced a 951-byte assembly, 597-byte object and 4,190-byte executable with
SHA-256 values
`aa5023c18d89eb0ea04a0764d4cc7602afbc784774d4845141fcd2c6f338d3a6`,
`d7c2ff72e8065eecc7a9ae56c43404dd576841723b8c47cdd18d637bb5e79b55`
and
`09a52ee70e72522c7b8d2c39df588d8291683470c9befe9502abcede66260cae`.
The promoted executable is 4,884,298 bytes with SHA-256
`ca1b1c80e8c03b7d0a56b880ce5ef18f69a3fb35fa41e18b3445b28cb1c9d786`.

## 2026-08-28: promotion to the RAII Lexer toolchain

The anchor now contains the stage-fixed `luna` executable from the Lexer
modernization snapshot accompanying this provenance note. The historical
`luna.bootstrap.frontend.lexer` module and its public byte-buffer ownership
records are replaced by `luna.compiler.lexer`, one narrow interface and five
same-module facade, session, keyword, literal and token implementation units.

`Lexer` owns its source cursor, source position, one session-wide
`KeywordTable` and a sticky result. `LexResult` and `DiagnosticBuffer` are
move-only RAII resources over typed vectors, while `TokenView` and
`DiagnosticView` make borrowing explicit. Token, source-span and diagnostic
records remain passive values. Keyword matching is table-driven, punctuation
uses a strongly typed ASCII enum and `switch`, and bounded scans use `for`.
All 28 direct consumers migrated atomically; no compatibility forwarding
module remains.

The necessary parser boundary now returns move-only `ParseResult` and
`FrontendResult` resources without otherwise replacing the recursive-descent
passes. A private Tools `FrontendStorage` owns the multi-unit token,
diagnostic and syntax-node pools and exposes stable views to semantic
analysis, replacing the former shallow `FrontendResult[64]` array. The
frontend source tree now contains only lexer, parser and syntax directories.
Closed token-kind mappings use `switch`, and a dedicated scan confirmed that
every changed frontend/control condition contains at most two logical
clauses.

Remote x86-64 verification on isolated `caw` built the complete 72-module,
62-library-object graph through `stage-transition`, `stage-next` and
`stage-fixed` with `verify --fresh` in 61.59 seconds at 34,368 KiB maximum
RSS. Every next/fixed assembly, object and executable artifact was
byte-identical. Audit and formatting gates were green. The expanded final
suite passed 448/448 with no failures or skips in 4.90 seconds at 27,200 KiB
maximum RSS.

The separately compiled Lexer contract links the real module object and
covers all 67 keywords, 46 punctuation/operator forms, literals, trivia,
exact spans, structured diagnostics, invalid UTF-8/NUL and RAII cleanup. With
the preceding anchor compiling both source shapes, the old Lexer median was
0.263503 seconds and 490,491 assembly bytes; the RAII Lexer median was
0.525948 seconds and 805,721 bytes. This unoptimized structural cost is
recorded rather than hidden by premature tuning.

The final Lexer assembly has SHA-256
`4b69e57ee46d0d6ef10e9b96d54801d5807fa0e21d06ad9990b55d242ed4f3f8`;
its 280,701-byte object has SHA-256
`6d0b203f92b94d605681c77233264022142d2154e9035a4820cd95df71b84f38`.
The promoted executable is 4,962,131 bytes with SHA-256
`ed19edea6b21ffd169850d079b674e866a61088249bb227ff5fbd0061fbc0f37`.

## 2026-08-28: promotion to the RAII Syntax toolchain

The anchor now contains the stage-fixed `luna` executable from the Syntax
modernization snapshot accompanying this provenance note. The historical
`luna.bootstrap.frontend.syntax` module is replaced atomically by the short
`luna.compiler.syntax` dependency and its same-module tree, builder and
verification implementation units. No forwarding module remains.

`SyntaxNode`, `SyntaxKind` and `SyntaxFlag` remain passive syntax records.
`SyntaxTree` is a move-only RAII owner over `vector<SyntaxNode>`, `SyntaxView`
is its read-only borrowing boundary and `SyntaxBuilder` is the only mutation
boundary. Builder marks support parser rollback, and moving a builder leaves
the source destructible but observably invalid. Parser state no longer exposes
a mutable syntax-node pointer or owns the tree as raw bytes; `ParseResult`
transfers the completed tree. Semantic analysis, code generation and Tools
consume views, while Tools owns frontend storage explicitly.

All 32 former direct consumers migrated in the same change. The dedicated
Syntax contract covers tree construction, child/sibling topology, spans,
tokens, flags, rollback, verification, ownership transfer and moved-from
behavior. A focused scan of 18 relevant files confirmed that no changed
condition exceeds two logical clauses.

Remote x86-64 verification on isolated `caw` built the complete 72-module,
62-library-object graph through `stage-transition`, `stage-next` and
`stage-fixed` with `verify --fresh` in 57.42 seconds at 34,200 KiB maximum
RSS. Every next/fixed assembly, object and executable artifact was
byte-identical. Audit and formatting gates were green. The expanded final
suite passed 449/449 with no failures or skips in 4.81 seconds at 27,040 KiB
maximum RSS.

With the preceding anchor compiling both source shapes, the historical Syntax
module had a median compile time of 0.063769 seconds and produced 93,435
assembly bytes. The final RAII module had a median compile time of 0.191968
seconds and produced 400,578 assembly bytes. The correctness-first structural
cost is recorded explicitly; no premature optimization is part of this
promotion.

The final Syntax assembly has SHA-256
`d17042b2daf0618ee37cbf6a32ed2d7e8f723f4bb8ce7ca1b784a336ca5f794d`;
its 146,877-byte object has SHA-256
`7e84c13e9b503aa0db4e5a9edbae175754e0a23c9a99d17fa0b4d26306ec0fc7`.
The promoted executable is 4,986,707 bytes with SHA-256
`77f0e575626bad58f91caa59fc2f6261d7b68cffcca6192ed9bcd11124a3d979`.

## 2026-08-28: promotion to the RAII Parser toolchain

The anchor now contains the stage-fixed `luna` executable from the Parser
modernization snapshot accompanying this provenance note. The five historical
`luna.bootstrap.frontend.parser*` modules are replaced atomically by one
`luna.compiler.parser` module, one 85-line interface and nine same-module
implementation units. No state, expression, statement or declaration
forwarding module remains.

A private `Parser` class now owns source and token views, token traversal,
`SyntaxBuilder`, diagnostics, sticky failure and nesting state. Every operation
that depends on the parsing session is a bound method; `ParserMark` is only a
passive rollback value. `ParseResult` and `FrontendResult` retain move-aware
RAII ownership. Aggregate parsing uses a non-owning `method fn` strategy to
select field or class-member parsing without passing a raw state pointer.

Primary-expression, statement and top-level declaration dispatch use closed
`switch` statements. Token classifications and operator precedence are
centralized in one rules unit. Bounded traversal uses `for`; the remaining
`while` loops consume parser state. The dedicated Parser contract covers tree
shape, generic-versus-less-than rollback, exact recovery diagnostics, invalid
token views and moved-from lifetimes. Lexer and Syntax contracts remain green.

Remote x86-64 verification on isolated `caw` contracted the graph from 72 to
68 modules and from 62 to 58 library objects. `verify --fresh` built the full
transition, next and fixed graph in 56.05 seconds at 33,384 KiB maximum RSS;
every next/fixed assembly, object and executable artifact was byte-identical.
Audit and formatting gates were green, and a focused scan of 11 files found no
condition above two logical clauses. The expanded suite passed 450/450 with no
failures or skips in 5.34 seconds at 26,560 KiB maximum RSS.

With the preceding anchor compiling both source shapes, the old five-module
Parser compiled sequentially in a median 1.379638 seconds and produced
2,176,417 total assembly bytes. The final single module compiled in a median
2.482031 seconds and produced 1,993,549 assembly bytes. Assembly volume fell
by about 8.4%, while compile time rose about 79.9% because the unoptimized
compiler now processes one larger declaration domain. This cost is recorded
without restoring false dependency modules or adding premature optimization.

The final Parser assembly has SHA-256
`e3c97c2d4c58526fad2e375408ef01310af2a2aab07d62be6215935f8d34db69`;
its 756,785-byte object has SHA-256
`23f32b330d098d95719aabf9b9241d528992e7dd022972b1540f74d7ed0fabf0`.
The promoted executable is 4,998,995 bytes with SHA-256
`1b10d3581f344af84de632990460e8479fe384e8d26245ede01376c203335694`.
