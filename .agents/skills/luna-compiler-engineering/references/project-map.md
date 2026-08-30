# Luna project map

Use this reference to choose source roots, current design documents and mature
precedents. Confirm fast-changing counts, module names and status against the
working tree rather than copying them from this file.

## Authority and trust model

- `AGENTS.md` is the live repository contract. `SESSION.md` records the active
  or most recent substantial batch and must be reconciled with `git diff`.
- Luna is a C23-derived systems language influenced by modern C++ design, but
  the current branch is implemented entirely in Luna.
- `anchor/luna`, its checksum and provenance are the only binary trust root.
  The normal chain is anchor -> transition -> stage-next -> stage-fixed, with
  next/fixed compared byte for byte.
- Generated programs are freestanding System V ELF64 for
  `x86_64-unknown-linux-gnu`; no libc or hosted assembler/linker participates.
- Documents explicitly labelled archived, historical, snapshot or describing
  `m0` are context, not authority for the current source.

## Source ownership

| Area | Source roots | Start with |
| --- | --- | --- |
| Runtime and standard library | `library/include/luna`, `library/src` | `docs/codebase-runtime-tooling.md`, then the relevant library design |
| Lexer and syntax tree | `compiler/include/luna/compiler/{lexer,syntax}.lh`, `compiler/src/frontend` | `docs/codebase-frontend.md`, `docs/lexer.md`, `docs/syntax.md` |
| Parser | `compiler/include/luna/compiler/parser.lh`, `compiler/src/frontend/parser` | `docs/parser.md`, `docs/language.md`, `docs/syntax-plan.md` |
| Type system and IR | `compiler/include/luna/compiler/{types,ir}.lh`, `compiler/src/middleend/{types,ir}` | `docs/types.md`, `docs/ir.md`, `docs/codebase-middleend.md` |
| Semantic analysis | `compiler/include/luna/{compiler/sema,bootstrap/middleend/semantic}`, `compiler/src/middleend/semantic` | `docs/sema.md`, `docs/codebase-middleend.md`, current `SESSION.md` |
| x86-64 backend | `compiler/include/luna/compiler/x86`, `compiler/src/backend/x86_64` | `docs/codebase-backend.md`, then `docs/codegen.md`, `docs/object.md`, `docs/assembler-design.md`, `docs/elf.md` or `docs/linker.md` |
| Tool commands | `drivers/include/luna`, `drivers/src` | `docs/tools.md`, `docs/codebase-runtime-tooling.md` |
| Build graph and bootstrap | `tools/selfhost.py`, `tools/build.py`, `anchor`, `release` | `docs/build-system.md`, `docs/bootstrap-toolchain.md`, `docs/bootstrap-reproducibility.md` |
| Tests | `tests`, test portions of `tools/selfhost.py` | `docs/test-architecture.md`, `docs/codebase-tests.md` |
| Language design and roadmap | compiler, library, tests and docs together | `docs/language.md`, `docs/roadmap.md`, the milestone-specific design document |

Always read `docs/architecture.md` for cross-subsystem or trust-boundary work
and `docs/modernization.md` before changing source architecture.

## Architectural precedents

- Lexer and Parser demonstrate phase-owned classes, move-only result storage,
  const views and accumulated diagnostics.
- `ir::Module` plus `ir::Builder` demonstrates one-way construction,
  move-owned storage, sticky failure and independent validation.
- `TypeTable`, semantic domain values and the table owners demonstrate a
  canonical source of truth, typed projections and publication through
  focused methods rather than writable storage escape.
- `Object`/`ObjectBuilder`, `Assembler`, `ElfReader`/`ElfWriter` and
  `StaticLinker` demonstrate binary-format boundaries, stateful tool passes
  and malformed-input handling.
- `luna.std.vector`, spans, strings and buffers define the intended generic
  ownership vocabulary. Standard-library APIs follow C++ spelling where Luna
  supports the semantics; compiler-domain classes use LLVM/Clang-style names.

These are design precedents, not copy targets. Check their current sources and
costs before reusing a pattern.

## Module graph rules

- An interface `.lh` declares exported contracts. One module may have one
  interface and multiple `.la` implementation units.
- Prefer cohesive same-module implementation files over thin child modules.
  Same-module implementation files do not import each other.
- Module names are dependency identities rather than directory paths. New
  compiler modules use `luna.compiler.*`; do not introduce new historical
  `luna.bootstrap.*` modules.
- `tools/selfhost.py`'s `LIBRARIES` registry is the source-to-module registry.
  Imports derive dependency order, implementation order and driver closure.
- A child module may not import its parent facade. If a lower pass must invoke
  an upper entry, use a narrow callback only when that is the established
  dependency solution.

## Tests as contracts

- Ordinary executable cases live in `tests/cases` and declare exact expected
  exits in `tests/expectations.txt`.
- `FAIL <diagnostic-kind>` cases preserve the leading semantic diagnostic and
  remain compile-only.
- `UNITS <ordered-source>...` replaces import-derived discovery when order or
  graph shape is itself the contract.
- Frontend, callable identity, relocation/object, CLI and FFI suites have
  dedicated protocols. Extend an existing protocol when it already observes
  the invariant instead of creating many expensive one-off toolchain runs.
