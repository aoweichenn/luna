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
