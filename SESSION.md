# Luna session handoff

This file is the durable recovery point for the current unfinished Luna work.
It complements the Codex transcript; it does not replace `AGENTS.md`, the
source tree or validation evidence. On resume, read `AGENTS.md` first, read
this file second, then reconcile every statement below with `git status` and
`git diff` before editing.

## Codex session identity

- Session/thread UUID: `01a03871-65ea-7db0-bc5f-c0d9f1cb9ff5`
- Saved working directory: `/root/demos/stu/cpp/compiler/luna`
- Raw transcript:
  `/root/.codex/sessions/2026/08/25/rollout-2026-08-25T10-22-30-01a03871-65ea-7db0-bc5f-c0d9f1cb9ff5.jsonl`
- Transcript state inspected on 2026-08-29: 204,608,945 bytes, 18 context
  compactions, last recorded event at `2026-08-29T00:58:29.586Z`.
- Protected transcript snapshot:
  `/root/.codex/session-backups/luna-01a03871-65ea-7db0-bc5f-c0d9f1cb9ff5-2026-08-29.tar.gz`
  (35,376,771 compressed bytes; 204,704,326 transcript bytes; SHA-256
  `c815f38620810b3bd4cc74f7de3398aaa28e49ed3af17261170fb48d5e13da17`).
- Exact recovery command from any directory:

  ```sh
  codex resume -C /root/demos/stu/cpp/compiler/luna \
    01a03871-65ea-7db0-bc5f-c0d9f1cb9ff5
  ```

`codex resume --last` filters by the current working directory. Prefer the
explicit UUID above when entering through Remote, after changing directories,
or when several Luna sessions are present.

## Repository state

- Branch: `main`
- Base commit: `520fbb6e63cae396c276eba30b9e96e3afe55fe0`
  (`refactor: make semantic ownership RAII`)
- `origin/main` matched that commit when this snapshot was written.
- The IR modernization and its anchor promotion are committed and pushed.
- The semantic ownership batches and their anchor promotion are committed and
  pushed.
- The semantic domain batch and its anchor promotion belong to the same commit
  containing this handoff. Compare HEAD with `origin/main` to determine whether
  the requested push completed.
- `advice.md` is an untracked user-owned file and has not been modified as part
  of the IR or semantic work.
- Anchor before this work:
  `c8e6dbac2dac2b97c146efb761943fbd2da70634731daa59f3e1817afc0e4f5a`,
  4,994,899 bytes.
- Promoted IR anchor:
  `282845d879d1b67169604ebc08481c3a4c286b2a737dd7db84fb3c520c609d94`,
  5,085,011 bytes. It is the byte-identical caw stage-fixed artifact described
  in `anchor/PROVENANCE.md`.
- Promoted semantic ownership anchor:
  `6cb79e335847a054aa4322139fe3a212a6598b18b778d34dfdbd5219f00d3846`,
  5,113,683 bytes. It is the byte-identical caw stage-fixed artifact described
  in the latest provenance entry.
- Promoted semantic domain anchor:
  `da919a6f4f0e7e278e0561714df6f11fcb80c2efe4d0cd07fd061d6f4ff04db9`,
  5,113,683 bytes. It is the byte-identical domain stage-fixed artifact.

## Engineering direction established in this conversation

The project has already completed the main language-foundation stages and is
now using those features to modernize its own implementation:

1. M2 established callable infrastructure, overloads, folded default
   parameters and unified expression value categories.
2. M3 implemented the class system, implicit `this`, abbreviated access
   control (`pub`, `priv`, `prot`), virtual dispatch, RTTI and class value
   composition.
3. M4 implemented native generics with Luna-specific declaration syntax shaped
   by modern C++ without adopting `::`.
4. The toolchain drivers were unified into one `luna` executable.
5. M5 implemented object lifetime, copy/move special members, destruction and
   RAII; `move` is a standard-library facility rather than a built-in type.
6. Generic standard containers, modular batched tests, cached parallel
   self-hosting, backend module contraction and RAII rewrites of the assembler,
   ELF object I/O, linker, code generator, object model, lexer, syntax tree,
   parser and type table followed.

The non-negotiable implementation rules are in `AGENTS.md` and the global
`cpp-engineering-standards` skill. In particular: use modern Luna classes and
generics where they strengthen a boundary, follow C++ standard-library naming,
use LLVM/Clang-style compiler-domain names, prefer `for` and `switch`, keep at
most two logical clauses in every condition, split implementation units along
real responsibilities, and perform all builds/tests on the isolated caw host.

## Active task: IR modernization

The goal is to replace the historical bootstrap IR store and separate verifier
submodule with one cohesive `luna.compiler.ir` module built around explicit
ownership and construction boundaries.

### Design decisions already made

- Passive IR enums and records remain transparent values: `Limit`, `Linkage`,
  `Opcode`, `Global`, `GlobalReference`, `Function`, `Parameter`, `Slot`,
  `Value`, `Block`, `Instruction` and `Argument`.
- `ir::Module` is the move-only RAII owner of all IR storage.
- `ir::Builder` owns a module while it is being constructed and carries a
  sticky construction error. Semantic lowering mutates IR only through bound
  builder methods.
- `ir::View` is a non-owning typed pointer/count view. An earlier generic
  `const_span<T>` design emitted duplicate strong generic method symbols in
  multiple modules and failed final linking, so the zero-code-generation view
  is intentional.
- Verification is a responsibility of `Module::is_valid(TypeTable const&)`,
  not an independently imported `ir.verify` module.
- The module interface is narrow and the implementation is split into
  same-module `.la` units; these files must not become artificial submodules.

### Implemented working-tree changes

- Removed historical interfaces:
  `compiler/include/luna/bootstrap/middleend/ir.lh` and
  `compiler/include/luna/bootstrap/middleend/ir/verify.lh`.
- Added `compiler/include/luna/compiler/ir.lh`.
- Replaced the old monolithic IR source and verifier with:
  `storage.la`, `globals.la`, `functions.la`, `control.la`,
  `instructions.la`, `validation.la` and `verify.la` under
  `compiler/src/middleend/ir/`.
- Updated `tools/selfhost.py` to register one `luna.compiler.ir` module and
  removed the `ir_verify` registry entry.
- Migrated semantic context/result ownership from the historical store to
  `ir::Builder` and `ir::Module`.
- Migrated semantic lowering mutations to bound builder methods.
- Migrated x86-64 code generation to consume `ir::Module const&` and `ir::View`.
- Migrated `tests/relocation_data/ir_codegen.la` to resume construction by
  moving the semantic module into a builder, then taking the finished module.
- `Builder::take_module` is a one-shot phase transition; the moved-from builder
  becomes inactive and the relocation contract verifies that state.
- All indexed `while` loops have been removed from the new IR implementation.

### Final validation evidence on caw

Isolated workspace:
`/home/aoweichen/codex-workspaces/luna-ir-final-eGYdtKbg`

Before the rewrite, sequential compilation of the historical `ir` plus
`ir_verify` modules took 0.884254, 0.884206 and 0.883955 seconds; median
0.884206 seconds. Their generated assembly totaled 1,087,410 bytes with
combined SHA-256
`2c3e9dd508804792c87a39bf160cae1d220c45301150a1b27d2d83069d4bd060`.

The final IR implementation has zero conditions above the two-clause limit and
zero `while` loops. The interface is 230 lines; opcode validation is 545 lines
and whole-module verification is 654 lines. No newly introduced complex
condition or `while` exists in any migrated consumer.

Final static gates passed: anchor hashes, 67-module/one-driver graph audit,
33-interface/57-library-object driver closure, formatter stability and zero
token drift. The final cold fixed point completed with these caw timings:

- trusted transition: 18.75 seconds;
- stage-next: 14.13 seconds;
- stage-fixed: 14.03 seconds;
- every assembly file, object and `bin/luna` was byte-identical.

The post-fixed-point test run passed `450/450`, including behavior suites,
semantic failures, relocation corruption, callable identity and FFI.

The final uncached IR-only compilation took 3.13, 3.26 and 3.24 seconds; median
3.24 seconds. It emitted 2,545,821 deterministic assembly bytes with SHA-256
`14bf0b90897a6623a182c46fd8b9a8192b4b0458eb93e064183b8d255dcbc2b2`.
The previous integrated Module/Builder snapshot had a 2.89-second median and
2,401,080 assembly bytes. Most cost relative to the historical two-module
baseline comes from current per-record generic-vector monomorphization, not
the final Verifier split; generic code-generation deduplication is a separate
future compiler task and was not mixed into this correctness refactor.

## Completion state and next action

The IR modernization implementation, documentation, structural review, cold
fixed point, test suite, benchmark and anchor promotion are complete. The
promotion belongs to the same pushed change as the source. If `git status` is
clean and the branch contains that change, no IR task remains.

## Active task: semantic ownership foundation

The first two semantic modernization batches are complete in the working tree.
They do not rename or blindly merge the 23,000-line semantic dependency graph.

Implemented boundaries:

- a private `SemanticSession` owns one ready/complete/transferred pipeline and
  centralizes transient-work cleanup, entry selection and one result transfer;
- `DiagnosticBuffer` is a move-only RAII owner over `vector<Diagnostic>`;
- `DiagnosticView` is the bounded read-only consumer surface;
- `Input` is a move-only RAII owner over `vector<Unit>` and `byte_buffer`;
- `InputView` is the read-only unit/path/mode boundary stored by Context and
  CodeGenerator while the driver retains the owner;
- `SemanticResult` is a transparent one-shot transfer record composed from
  TypeTable, IR Module, DiagnosticBuffer and runtime error;
- manual semantic-result release is deleted from the driver and test;
- invalid input now follows the same work-cleanup path as normal compilation;
- `context/diagnostics.la` is a same-module implementation unit registered
  under `sem_ctx`, not a new submodule;
- `context/input.la` owns Input/InputView validation, transactional append and
  Context unit/path access without creating an input submodule;
- `docs/sema.md` records the feature review, current ABI constraint and the
  incremental path toward domain extraction and session contraction.

The public result deliberately remains a struct. The current unoptimized ABI
does not reliably transfer a large public class containing both TypeTable and
Module; behavior stays in the private session while the public record only
bundles already-RAII fields. Attempts to force the decorative class boundary
were rejected rather than committed as a workaround.

Final caw workspace:
`/home/aoweichen/codex-workspaces/luna-sema-input-a62vqBCj`

Validation evidence:

- audit: 67 modules, one driver, 33 interfaces and 57 library objects;
- formatter: zero files needing reflow and zero token drift;
- no newly introduced condition above two clauses and no new `while`;
- cold transition/next/fixed: 14.17/14.33/14.22 seconds;
- every next/fixed assembly, object and executable artifact byte-identical;
- post-fixed-point tests: `450 passed, 0 failed, 0 skipped`.

Before semantic ownership work, `sem_ctx` plus `sema` compiled sequentially in
0.728413, 0.716067 and 0.714139 seconds; median 0.716067 seconds and 829,959
total assembly bytes. The diagnostic/session batch reached a 0.856256-second
median and 1,063,972 bytes. The final Input/InputView pair compiled in
1.031824, 1.034350 and 1.036203 seconds; median 1.034350 seconds and 1,313,915
total assembly bytes.
Final hashes are
`fd20b3fddbfe4adccac86dff49d62ee874e317e7f2b2a3ade51ee44d2da0f511`
for `sem_ctx.s` and
`6704a1431ac803f68970d1054c0d7e9fa06a9a729f20566b7124a8989252de71`
for `sema.s`.

The ownership batches and anchor promotion are committed and pushed;
`advice.md` remains excluded. Their planned callable/value domain extraction
is recorded in the active batch below.

## Active task: semantic domain foundation

The first semantic module-contraction batch is complete in the working tree:

- historical callable and value modules are replaced by one
  `luna.compiler.sema.domain` dependency;
- `CallableKind`, `ReceiverKind`, `CallableIdentity`, `ValueCategory` and
  `ReferenceRank` are explicit passive domain types;
- callable identity and value-category behavior remains stateless switch-based
  predicates split across `domain/callable.la` and `domain/category.la`;
- all 16 consumers use one `domain::` qualifier; four former double-import
  sites now have one edge;
- old module names, qualifiers, interfaces and registered objects are absent;
- class/generic model Stores remain separate because they still own mutable
  buffers, indexes and hash buckets and require dedicated RAII migrations.

The graph contracts from 67 to 66 modules, 33 to 32 interfaces in the driver
closure and 57 to 56 library objects. The domain interface is 51 lines;
callable/category implementations are 69 and 165 lines. They contain no
`while` and no condition above two logical clauses.

Final caw workspace:
`/home/aoweichen/codex-workspaces/luna-sema-domain-gZkjyFuJ`

Validation evidence:

- audit and formatter/token-drift gates passed;
- cold transition/next/fixed: 14.23/14.29/14.13 seconds;
- every next/fixed artifact was byte-identical;
- post-fixed-point tests: `450 passed, 0 failed, 0 skipped`.

The old callable/value modules compiled sequentially in 0.075489, 0.075557
and 0.074303 seconds; median 0.075489 seconds and 140,335 total assembly
bytes. The final domain module compiled in 0.052045, 0.051286 and 0.051717
seconds; median 0.051717 seconds and 115,658 bytes, SHA-256
`ed3b1abc40726a8d335d0f426714fc522a553372035d3f12bca612245f3327cc`.

The batch and anchor promotion are complete. If the working tree is clean and
`origin/main` contains this change, no domain task remains; `advice.md` stays
excluded. The next implementation work should make class and generic model
Stores move-only RAII owners before considering their passive records for the
domain module.

## Recovery checklist

1. Confirm the session UUID and working directory shown above.
2. Read `AGENTS.md`, this file and the global engineering skill completely.
3. Run `git status --short`, `git diff --stat` and inspect the semantic domain diff.
4. Preserve `advice.md` and all existing comments/changes.
5. Do not redo the completed IR, semantic ownership or domain work or rerun cold
   validation unless the source changes.
6. If the semantic domain commit/push is absent, resume only that interrupted
   operation; do not redo implementation or validation.
