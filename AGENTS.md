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
python3 tools/selfhost.py verify   # anchor -> transition -> stage-next ->
                                   # stage-fixed; next/fixed must be identical
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
- `test` runs independent expectation cases with four workers by default;
  use `--jobs N` to tune concurrency. Shared CLI, relocation and FFI suites
  remain serialized, and result reporting preserves expectation-file order.

## Iteration discipline

1. A new feature's first implementation may use only syntax the current
   toolchain accepts (the anchor builds stage-next).
2. Land the feature behind the green `verify` + `test` gate first.
3. Only then may compiler sources adopt the feature; re-run `verify`.
4. Promote a verified toolchain into `anchor/` with refreshed
   `SHA256SUMS` and a provenance note.

## Style

- `docs/modernization.md` is the mandatory architecture contract for every
  new or rewritten library, compiler, backend and driver source. Review it
  before changing subsystem structure. A migration batch must move toward
  that design and must not introduce new historical bootstrap patterns.
- Prefer object-oriented Luna for stateful abstractions. Lexers, parsers,
  semantic sessions, builders, code generators, assemblers, readers/writers,
  linkers, command services and resource owners are classes with private state
  and methods. Do not organize them as a state struct passed through a family
  of free procedures.
- Use `struct` only for passive values, syntax/IR/ABI records and transparent
  views. Types with invariants, behavior or owned resources are classes.
  Resource classes use explicit `init`, move construction where transfer is
  required, and `deinit`.
- Use generics for reusable results, typed views, containers and algorithms.
  Do not maintain multiple type-specific ownership/container implementations
  when one generic abstraction expresses the contract.
- Every subsystem plan must review current Luna classes, generic
  classes/functions, composition, access control, constructors/destructors,
  copy/move special members, overloads/defaults, operators, bound methods,
  friends, virtual dispatch and RTTI. Adopt every feature that naturally
  strengthens that boundary and record why the others do not apply. A rewrite
  that only modernizes loops while retaining a suitable state-pointer/free-
  function design is not accepted.
- Standard-library public APIs follow the C++ standard-library naming model:
  lowercase module/type/function names such as `luna.std.vector`,
  `vector<Value>`, `span<Value>`, `list<Value>`, `deque<Value>`,
  `map<Key, Value>`, `queue<Value>`, `expected<Value, Error>`, `move`,
  `push_back`, `size`, `capacity`, `data`, `reserve`, `clear`, `value` and
  `error`. Match C++ spelling and
  semantics whenever Luna supports them; document intentional differences.
- Compiler and toolchain domain classes follow LLVM/Clang-style cohesive
  domain naming rather than mechanically copying std lowercase names. Module
  names still remain short and map to meaningful C++ subsystem concepts.
- Apply Strategy, State, Builder, Facade and RAII only for real variation,
  phase or lifetime boundaries. Pattern-shaped indirection without a concrete
  responsibility is rejected.
- Prefer the highest-level, most expressive feature accepted by the current
  Luna toolchain. Use `for` for bounded/indexed traversal, `switch` for kind
  dispatch, and stateful abstractions for error propagation; do not simulate
  them with manual `while` indices or serial `if` chains.
- Column budget is 120; `tools/refmt.py` reflows sources to it
  (quote-aware splitting, breaking at commas and `&&`/`||` first) and
  must keep every file's whitespace-insensitive token stream identical.
- Enumerative mappings (keywords, token-to-kind tables) are data tables
  plus a loop, never long if-chains.
- Every `if`, loop condition or conditional expression may contain at most two
  logical clauses. More complex validation must use named predicates, named
  boolean values, early returns or a table-driven validator; formatter line
  breaks and mechanically nested unnamed `if` statements do not satisfy this
  rule.
- A source file is a soft 2,000-line ceiling; split along pass
  boundaries. Additional `.la` files may implement the same module and share
  its private declarations/imports; use a new submodule only for a real
  dependency boundary. Register every implementation path in `LIBRARIES`.
- A facade should normally stay below 250 lines and an implementation unit
  should normally stay in the 150-800 line range. Split at coherent class
  method families or passes, not arbitrary line counts. Empty/one-line
  implementation units and `common`, `misc`, `helpers`, `utils`, `api` or
  `model` dumping-ground files are forbidden.
- Directories follow the same rule in every tree: they represent either a real
  module or multiple cohesive implementation families of one substantial
  module. Collapse repeated single-child directories; do not create a
  directory for one trivial file.
- Module names are short dependency names, not file paths. The modernization
  target uses `luna.compiler.*`; the historical `luna.bootstrap.*` prefix is
  being removed and must not appear in new modules. `api`, `model`, `state`,
  `lookup`, `visibility`, `support`, `value` and `call` are implementation-file
  concerns unless an import-graph proof shows an independent public boundary.
- Prefer same-module implementation splits over submodules. Before retaining
  or adding a submodule, prove that it has a coherent contract, has a consumer
  independent of the parent facade, clarifies an acyclic import boundary, and
  is not merely shortening a file.
- A submodule may never import its parent facade (imports are acyclic);
  when a pass must recurse through the facade, hand the entry point
  down as a function pointer (see the `Lowerer` type in
  `compiler/include/luna/bootstrap/middleend/semantic/expr/api.lh`).
- Semantic lowering implementations live under
  `compiler/src/middleend/semantic/`, with the pipeline entry remaining
  in `compiler/src/middleend/sema.la`; imports flow strictly downward in
  this order: `callable`, `value`, `classes/model`, `context` (+ `context/{lookup,builder}`),
  `attributes`, `modules`, `types` (+ `types/{lookup,visibility}`),
  `classes`, `consteval` (+ `consteval/{model,engine}`), `intrinsics`,
  `functions` (+ `functions/ir`), `lifetime`, `expr` (+ `expr/{base,numeric,
  strings,api,probe,initializer,access,operators}`), `stmt` (+
  `stmt/{api,labels}`). The same-module files `types/layout.la`,
  `consteval/engine/execute.la`, `functions/const.la`,
  `functions/signature.la`, `functions/overloads.la` and
  `functions/{bindings,defaults,special}.la`, plus
  `expr/probe/{operators,call}.la`, are implementation splits, not
  additional dependency-graph modules.

## Layout

- `anchor/` — the single fixed-point multi-command `luna` executable with
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
- `drivers/include/luna/tools/` and `drivers/src/` — the shared CLI service,
  independent compile/assemble/link command services and single freestanding
  `luna` entry point.
- `tests/cases/` — executable behavior programs returning their verdict
  as the exit status.
- `tests/ffi/` — hand-encoded ELF64 ET_REL fixtures
  (`generate_fixtures.py` regenerates them) linked against Luna cases;
  expectations accept `<exit-code>` or `link:<luna link status>`.
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
- Luna modules have at most one interface and one or more implementation units
  (`include/**/*.lh` declares exports; `src/**/*.la` defines); paths need not
  be co-located and are grouped explicitly by `LIBRARIES`.
- New modules must be registered in `tools/selfhost.py`'s `LIBRARIES`
  (the single registry — build order and driver closures are derived
  from source imports) so the fixed point covers them; `audit` must
  stay green.
