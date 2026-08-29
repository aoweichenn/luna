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
- Base commit: `39a2d87187970be33d4c1cdb3ae7cd34dc23ecaa`
  (`refactor: modernize semantic metadata ownership`)
- `origin/main` matched that commit when this snapshot was refreshed.
- The IR modernization and its anchor promotion are committed and pushed.
- The semantic ownership batches and their anchor promotion are committed and
  pushed.
- The semantic domain batch and its anchor promotion are committed and pushed.
- The ClassTable, GenericTable and passive-metadata domain batches plus anchor
  promotion are committed and pushed.
- The documentation credibility pass, including six `docs/codebase-*.md`
  audit volumes and updates to current/historical documentation, is committed
  and pushed as `e362685c74ba3c0d518814519a21142a1da06eb8`.
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
- Promoted typed semantic metadata anchor:
  `7dcc139c0b50ad7361faa5581fd25ae90b0c4d6f3a87243b5c1f13f6553eccb5`,
  5,179,219 bytes. It is the byte-identical caw stage-fixed artifact from the
  combined ClassTable, GenericTable and passive-record extraction stack.

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

## Completed task: IR modernization

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

## Completed task: semantic ownership foundation

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

The public result deliberately remains a struct because it is a passive,
one-shot phase result with no invariant or behavior beyond its RAII members.
The language could express a movable class here, but that would add a
decorative boundary rather than a concrete responsibility. Behavior stays in
the private SemanticSession.

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

## Completed task: semantic domain foundation

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
- class/generic model Stores remained separate in that batch; their completed
  RAII migrations are recorded in the local tasks below.

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

The batch and anchor promotion are complete. `origin/main` contains the change
and `advice.md` stays excluded.

## Completed local task: ClassTable RAII ownership

The class-model storage migration is implemented locally on top of
`3b782cb6b42aea1079687e1af0b7b5de0a4133be`:

- `class_model::Store` and its four raw `bytes::Buffer` fields are replaced by
  a move-only `ClassTable` owning `vector<Record>`, `vector<Field>`,
  `vector<Method>` and `vector<Friend>`;
- bound append methods own contiguous per-class field, method and friend slice
  construction and retain the first runtime error;
- moved-from tables are valid and empty, and automatic vector destruction
  removes the manual class-model release path from Context cleanup;
- all semantic consumers use typed count/data methods instead of accessing raw
  buffers and synchronized counts;
- mutable typed data remains a documented transitional pass boundary until
  hierarchy/layout/vtable/descriptor mutation moves behind focused methods;
- `tests/relocation_data/ir_codegen.la` now verifies all four typed sequences,
  the three slice families, move/moved-from state and sticky failure without
  adding another per-case toolchain launch;
- `docs/sema.md`, `docs/architecture.md`, `docs/modernization.md`,
  `docs/roadmap.md`, `docs/test-architecture.md` and
  `docs/bootstrap-middleend.md` record the new ownership contract.

Current isolated caw workspace:
`/home/aoweichen/codex-workspaces/luna-class-table-final-pJLiT7`

Final evidence on caw (`x86_64`, WSL2 Linux 6.6.87.2, Python 3.13.9):

- anchor hash, 66-module/one-driver graph, 32-interface/56-object closure and
  formatter/token-drift gates pass;
- the interface is 99 lines and the implementation is 184 lines; the new
  owner/test logic has no `while` and no condition above two logical clauses;
- an incremental build recompiling only `sem_class_model` completes in 1.39
  seconds and the suite passes `450 passed, 0 failed, 0 skipped`;
- cold `verify --fresh` completes the trusted transition, stage-next in 14.59
  seconds and stage-fixed in 14.58 seconds; every assembly, object and final
  executable is byte-identical;
- the post-fixed-point suite again passes `450 passed, 0 failed, 0 skipped`,
  including the new ClassTable relocation contract;
- direct uncached class-model compilation takes 0.249059, 0.244997 and 0.244889
  seconds (median 0.244997), emitting 911,011 assembly bytes with SHA-256
  `070e58de972ee41c4b62811e4effb85bd6060e848fe5eb94e11f869e454ca592`;
- the old raw Store baseline was 0.033601 seconds and 107,727 assembly bytes.
  The increase is current generic-vector monomorphization cost; it is visible
  but adds only about 0.24 seconds to this cold graph and is not hidden behind
  a weakly typed storage fallback;
- the verified intermediate ClassTable stage-fixed `luna` is 5,142,355 bytes with SHA-256
  `1a1e38fc3e96097885391c1d7d3f65d7ac46902630a04d8ba2abb4e51a704ba6`.

Implementation, documentation, structural review, cold fixed point, tests and
benchmark are complete. The work is included in the requested combined change.

## Completed local task: GenericTable RAII ownership

The generic-model storage migration is implemented locally on top of the
ClassTable batch:

- `generic_model::Store`, eight raw buffers, eight synchronized counts and the
  manual release path are replaced by one move-only `GenericTable`;
- typed vectors own Declaration, Parameter, Instance, IndexEntry and
  ActiveBinding values; consumers receive const typed projections only;
- declaration parameters remain one append-only contiguous slice, and the
  former external `binding_id` mutation is a validated bound method;
- instance append reserves both sequences, rolls back partial appends, returns
  canonical duplicates and rebuilds open-addressed buckets in a replacement
  vector before move-publication;
- type/function reverse maps validate conflicts and prepare both maps before
  publishing result entries;
- active-binding truncation can finish rollback without clearing a sticky
  construction error;
- `compiler/src/middleend/semantic/generics/model.la` is replaced by the
  same-module `storage.la`, `instances.la` and `validation.la` units registered
  together under `sem_generic_model`;
- all semantic consumers use bound methods and count/const-data accessors;
- the relocation-data large contract covers parameter slices, duplicate
  instances, a 16-to-32 bucket rebuild, reverse maps, active-binding rollback,
  deep validation, move/moved-from state and sticky failure.

The current compiler emits strong generic method symbols per concrete type.
`vector<usize>` already exists in `sem_expr_initializer`, so using it in a
second module failed final linking. GenericTable instead uses one
module-specific `IndexEntry` vector specialization. Current exported-class ABI
rules require that private generic argument type to be exported; a Chinese
interface comment records that it is not part of the operation surface, and
the constructor asserts its exact `usize` representation.

Current isolated caw workspace:
`/home/aoweichen/codex-workspaces/luna-generic-table-ljcZQ1`

Final evidence on caw (`x86_64`, WSL2 Linux 6.6.87.2, Python 3.13.9):

- pre-change Store compilation: 0.140794, 0.140943 and 0.141449 seconds
  (median 0.140943), 391,518 assembly bytes, SHA-256
  `910a159daeb66cb335b0a889a9b337c493d75bd3eb18309f4dca3325b8f9e65e`;
- anchor hash, 66-module/one-driver graph, 32-interface/56-object closure and
  formatter/token-drift gates pass;
- the interface is 105 lines and the storage/instances/validation units are
  211/341/127 lines; new owner/test logic has no `while` and no condition above
  two logical clauses;
- the current anchor builds and links the complete changed graph, and the
  expanded suite passes `450 passed, 0 failed, 0 skipped`;
- final direct module compilation takes 0.531638, 0.532183 and 0.527502
  seconds (median 0.531638), emitting 1,500,862 assembly bytes with SHA-256
  `33380b0d46729fe05aaec0a0813093d49c7a4d05d014eee2c5ec42676ebd8255`;
- cold transition/next/fixed complete in 15.03/15.20/15.04 seconds, and every
  assembly, object and final executable is byte-identical;
- the post-fixed-point suite again passes `450 passed, 0 failed, 0 skipped`;
- the verified intermediate GenericTable stage-fixed `luna` is 5,179,219 bytes with SHA-256
  `7dcc139c0b50ad7361faa5581fd25ae90b0c4d6f3a87243b5c1f13f6553eccb5`.

Implementation, documentation, structural review, benchmark, cold fixed point
and tests are complete. The work is included in the requested combined change.
The next semantic step is to contract Context/lookup/builder into a cohesive
session boundary.

## Completed local task: passive semantic metadata extraction

The import-graph proof and atomic type migration are implemented locally:

- stable class enums/records now use explicit domain names such as
  `ClassAccess`, `MethodDispatch`, `ClassRecord`, `ClassField`, `ClassMethod`
  and `ClassFriend`;
- stable generic enums/records now use `GenericDeclarationKind`,
  `GenericInstanceState`, `GenericDeclaration`, `GenericParameter`,
  `GenericInstance` and `GenericActiveBinding`;
- owner-specific `InstanceResult` and the exported-class ABI adaptation
  `IndexEntry` remain with GenericTable rather than polluting domain;
- ClassTable/GenericTable depend downward on domain values, Context alone
  imports the two owner modules, and all higher passes consume `domain::`
  values through Context;
- compiler direct class/generic owner imports contract from 5/8 to 1/1 while
  direct domain imports grow from 15 to 20;
- the dependency order is acyclic: types, domain, table owners, Context, then
  semantic passes;
- the domain interface grows from 51 to 161 lines while class/generic owner
  interfaces contract from 99/105 to 35/61 lines;
- no empty classes/generics domain implementation files were created because
  the migrated values are passive declarations with no behavior.

Current isolated caw workspace:
`/home/aoweichen/codex-workspaces/luna-sema-metadata-uYxaUt`

Final evidence on caw (`x86_64`, WSL2 Linux 6.6.87.2, Python 3.13.9):

- anchor hashes, 66-module/one-driver graph, 32-interface/56-object closure and
  formatter/token-drift gates pass;
- the reviewed owners/contracts have no `while` and no condition above two
  logical clauses;
- the current anchor builds and links the complete graph in 14.84 seconds, and
  the full suite passes `450 passed, 0 failed, 0 skipped`;
- final domain/class/generic module medians are 0.056784/0.277308/0.578233
  seconds, 0.912325 combined versus the 0.828352 pre-extraction sum;
- their assembly total contracts from 2,527,531 to 2,363,419 bytes. Final
  individual SHA-256 values are
  `ed3b1abc40726a8d335d0f426714fc522a553372035d3f12bca612245f3327cc`,
  `146f8d7269a2296bf0fef25ca5060513371a23bfece44f4dca9bb5c69d2a2b5e`
  and `5e77759da419aab8fcbb7e128734c3f44d6ab3fdce1aa75109dd431a55ea03d8`;
- cold transition/next/fixed complete in 14.89/14.89/14.86 seconds, and every
  next/fixed assembly, object and executable artifact is byte-identical;
- the post-fixed-point suite again passes `450 passed, 0 failed, 0 skipped`;
- the promoted stage-fixed `luna` remains byte-identical to the
  pre-extraction GenericTable result: 5,179,219 bytes and SHA-256
  `7dcc139c0b50ad7361faa5581fd25ae90b0c4d6f3a87243b5c1f13f6553eccb5`.

Implementation, documentation, graph proof, benchmark, cold fixed point and
tests are complete. The byte-identical fixed artifact has been promoted and
belongs to the requested combined commit/push. The next batch should contract
Context/lookup/builder into the planned `luna.compiler.sema.session` boundary.

## Completed task: codebase audit documentation credibility pass

The user added six untracked audit volumes and asked for a careful source-based
review. The current documentation-only working tree does the following:

- adds a uniform non-normative, commit-bound snapshot banner to all six
  `docs/codebase-*.md` files;
- corrects repository, lexer, syntax, IR, Context, ClassTable and test counts
  against commit `39a2d87` and the current source;
- removes the false syntax-tree contiguous-child invariant and records the
  actual `next_sibling` traversal contract;
- separates confirmed defects, unproven risks and explicit correctness-first
  non-goals, especially for duplicated `base_type`, semantic pass `bool`
  results, backend register allocation and quadratic-path claims;
- corrects invalid remediation advice: same-module `.la` files cannot import
  one another, probe/emitter flows should share pure predicates rather than be
  merged, and an additional mutator must not preserve a duplicated source of
  truth;
- updates `docs/architecture.md`, `docs/execution-semantics.md` and the top of
  `anchor/PROVENANCE.md` to describe the current pure-Luna/single-anchor branch;
- labels seven unmarked MIR/register-allocation documents, `next-steps.md` and
  `backend-modules.md` as historical snapshots;
- repairs the removed `unified-tool-driver.md` links and the roadmap's backend
  module count.

Static validation already completed:

- 692 tracked Luna `.la`/`.lh` files and 60,829 lines;
- 122 `TokenKind`, 79 `SyntaxKind` and 38 IR `Opcode` entries;
- all relative Markdown links resolve across all 57 docs;
- `git diff --check` and no-index whitespace checks for all six untracked audit
  volumes pass.

No build, fixed-point verification or test suite was run: this pass changes
documentation only, and the user explicitly does not want a full build for a
documentation edit. The complete diff and links were inspected, commit
`e362685c74ba3c0d518814519a21142a1da06eb8` was pushed to `origin/main`, and
untracked user-owned `advice.md` remains untouched.

## Recovery checklist

1. Confirm the session UUID and working directory shown above.
2. Read `AGENTS.md`, this file and the global engineering skill completely.
3. Run `git status --short`, `git diff --stat` and inspect the table/domain stack.
4. Preserve `advice.md` and all existing comments/changes.
5. Do not redo the completed IR, semantic ownership or domain work.
6. The compiler/domain and documentation work is already pushed. Do not redo
   implementation, promotion, documentation correction or cold validation.
