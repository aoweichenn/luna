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
