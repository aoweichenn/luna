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
