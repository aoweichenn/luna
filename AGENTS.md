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
python3 tools/selfhost.py audit    # read-only static gate: anchor hashes,
                                   # module graph vs. sources, source rules
python3 tools/refmt.py --check     # formatting gate: zero files needing
                                   # reflow, zero token drift
```

- `verify` is the core correctness gate for any change under `library/`,
  `compiler/` or `drivers/`. It takes a few minutes.
- The build graph is derived, not hand-maintained: `LIBRARIES` in
  `tools/selfhost.py` is the single module registry, and dependencies,
  link order and driver closures are read out of the `import` lines in
  the sources themselves. `audit` fails whenever the registry, the
  sources and the anchor disagree.
- `audit` and `refmt.py --check` are the fast static gates; run them
  before the slow `verify` + `test` pair.
- Test expectations live in `tests/expectations.txt`
  (`<case>.la <exit-code>`); negative codes are fatal signals, e.g. `-8`
  for a division trap. Expected-failure cases use
  `<case>.la FAIL <diagnostic-kind>`: compilation must fail with that
  leading `BootstrapSemanticDiagnosticKind`, compile-only, no linking. Either
  form may append `UNITS <ordered-source>...` to replace import-derived test
  discovery with an explicit source set for module-order and graph cases.

## Iteration discipline

1. A new feature's first implementation may use only syntax the current
   toolchain accepts (the anchor builds stage-next).
2. Land the feature behind the green `verify` + `test` gate first.
3. Only then may compiler sources adopt the feature; re-run `verify`.
4. Promote a verified toolchain into `anchor/` with refreshed
   `SHA256SUMS` and a provenance note.

## Style

- Column budget is 120; `tools/refmt.py` reflows sources to it
  (quote-aware splitting, breaking at commas and `&&`/`||` first) and
  must keep every file's whitespace-insensitive token stream identical.
- Enumerative mappings (keywords, token-to-kind tables) are data tables
  plus a loop, never long if-chains.
- A source file is a soft 2,000-line ceiling; split along pass
  boundaries into interface/implementation module pairs — a `foo/`
  implementation subdirectory behind a thin `foo.la` facade that keeps
  the public entry points — and register the new modules in `LIBRARIES`.
- A submodule may never import its parent facade (imports are acyclic);
  when a pass must recurse through the facade, hand the entry point
  down as a function pointer (see the `Lowerer` type in
  `compiler/include/luna/bootstrap/middleend/semantic/expr/api.lh`).
- Semantic lowering implementations live under
  `compiler/src/middleend/semantic/`, with the pipeline entry remaining
  in `compiler/src/middleend/sema.la`; imports flow strictly downward in
  this order: `context` (+ `context/{lookup,builder}`),
  `attributes`, `modules`, `types` (+ `types/{lookup,visibility}`),
  `consteval` (+ `consteval/{model,engine}`), `intrinsics`,
  `functions` (+ `functions/ir`), `expr` (+ `expr/{base,numeric,
  strings,api,initializer,access,operators}`), `stmt` (+
  `stmt/{api,labels}`).

## Layout

- `anchor/` — fixed-point `lunac`, `luna-as`, `luna-link` from m0, with
  `SHA256SUMS` + `PROVENANCE.md`. Sole binary trust root.
- `library/include/luna/` — library interface units, mirroring module
  names (`luna.std.text` is `luna/std/text.lh`).
- `library/src/` — runtime, Linux syscall and standard-library
  implementations: allocation, byte buffers, checked arithmetic, ASCII
  classification, binary encoding, UTF-8 text, paths and file I/O.
- `compiler/include/luna/bootstrap/` — compiler interface units,
  mirroring the `luna.bootstrap.*` module namespace.
- `compiler/src/` — `frontend/` (lexer, syntax, parser over
  `parser/{state,expression,statements,declarations}`), `middleend/`
  (type, ir + `ir/verify`, sema, `semantic/*` — see Style for the
  lowering order), `backend/x86_64/` (text, abi, frame, codegen over
  `codegen/{support,instruction/{value,call}}`, object, elf over
  `elf/{format,reader,writer}`, assembler over
  `assembler/{operands,encoding,source}`, linker). Correctness-first,
  no optimization.
- `drivers/src/` — freestanding argument-driven tool programs.
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
  (`include/**/*.lh` declares exports; `src/**/*.la` defines); the two
  paths need not be co-located and are paired explicitly by `LIBRARIES`.
- New modules must be registered in `tools/selfhost.py`'s `LIBRARIES`
  (the single registry — build order and driver closures are derived
  from source imports) so the fixed point covers them; `audit` must
  stay green.
