# AGENTS.md

Luna: a C23-derived systems language whose compiler, assembler and linker
are written in Luna itself. Sole target: `x86_64-unknown-linux-gnu`,
System V, ELF64, no libc in generated programs.

This branch is pure Luna. The C23 reconstruction seed and its C/C++ test
infrastructure are archived on the `m0` branch; the only trusted binary
input here is `anchor/`.

## Build & test

Requires Python 3 and an x86-64 Linux host (or `qemu-x86_64-static` via
`--runner qemu-x86_64-static`). No compiler, assembler or linker from the
host is ever invoked.

```sh
python3 tools/selfhost.py build    # anchor -> out/stage-next toolchain
python3 tools/selfhost.py verify   # rebuild with stage-next; every artifact
                                   # must be byte-identical (fixed point)
python3 tools/selfhost.py test     # compile+run tests/cases against
                                   # out/stage-next/bin, exact exit codes
```

- `verify` is the core correctness gate for any change under `library/`,
  `compiler/` or `drivers/`. It takes a few minutes.
- Test expectations live in `tests/expectations.txt`
  (`<case>.luna <exit-code>`); negative codes are fatal signals, e.g. `-8`
  for a division trap. Expected-failure cases use
  `<case>.luna FAIL <diagnostic-kind>`: compilation must fail with that
  leading `BootstrapSemanticDiagnosticKind`, compile-only, no linking.

## Iteration discipline

1. A new feature's first implementation may use only syntax the current
   toolchain accepts (the anchor builds stage-next).
2. Land the feature behind the green `verify` + `test` gate first.
3. Only then may compiler sources adopt the feature; re-run `verify`.
4. Promote a verified toolchain into `anchor/` with refreshed
   `SHA256SUMS` and a provenance note.

## Style

- Column budget is 120; `tools/refmt.py` reflows sources to it and must
  keep every file's whitespace-insensitive token stream identical.
- Enumerative mappings (keywords, token-to-kind tables) are data tables
  plus a loop, never long if-chains.
- A source file is a soft 2,000-line ceiling; split along pass
  boundaries into interface/implementation module pairs and register
  the new modules in `tools/selfhost.py`.
- Semantic lowering lives under `compiler/middleend/semantic/`
  (`context`, `attributes`, `modules`, `types`, `consteval`,
  `intrinsics`, `functions`, `expr`, `stmt`) with the pipeline entry remaining in
  `sema.luna`; imports flow strictly downward in that order.

## Layout

- `anchor/` — fixed-point `lunac`, `luna-as`, `luna-link` from m0, with
  `SHA256SUMS` + `PROVENANCE.md`. Sole binary trust root.
- `library/` — runtime, Linux syscall layer (`linux/`), standard library
  (`std/`): allocation, byte buffers, UTF-8 text, paths, file I/O.
- `compiler/` — `frontend/` (lexer, parser), `middleend/` (type, ir,
  sema), `backend/x86_64/` (text, abi, frame, codegen, object,
  assembler, linker, elf). Correctness-first, no optimization.
- `drivers/` — freestanding argument-driven tool programs.
- `tests/cases/` — executable behavior programs returning their verdict
  as the exit status.
- `tests/ffi/` — hand-encoded ELF64 ET_REL fixtures
  (`generate_fixtures.py` regenerates them) linked against Luna cases;
  expectations accept `<exit-code>` or `link:<luna-link status>`.
- `docs/` — authoritative per-subsystem design record (language,
  semantics, seed contract). Some docs describe the archived m0 seed;
  treat them as history where paths no longer exist.

## Non-negotiable boundaries

From `docs/architecture.md` — do not violate:

- Never translate Luna to C or through any hosted toolchain; hosted
  assemblers/linkers/LLVM are not part of the pipeline anymore.
- Generated programs are freestanding; runtime/syscall wrappers live in
  `library/` and link no libc.
- Only target is `x86_64-unknown-linux-gnu`; `isize`/`usize` are target-sized.
- Luna modules are matched interface/implementation pairs
  (`foo.interface.luna` declares exports; `foo.luna` defines).
- New modules must be registered in `tools/selfhost.py` (`LIBRARIES`,
  `LIBRARY_ORDER`, driver interface lists) so the fixed point covers them.
