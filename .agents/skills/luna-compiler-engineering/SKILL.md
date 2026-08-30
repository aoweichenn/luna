---
name: luna-compiler-engineering
description: Work on the Luna self-hosting systems-language repository, including language design, compiler/library/driver implementation, refactoring, tests, documentation, build orchestration, fixed-point verification, benchmarking, and anchor promotion. Use for every task under this repository; do not use for unrelated projects merely named Luna.
---

# Luna Compiler Engineering

Treat this as a self-hosting compiler project, not as an ordinary C++ build.
Luna source uses `.la` implementation units and `.lh` interfaces. The only
target is freestanding x86-64 Linux, and `anchor/` is the sole binary trust
root.

## Establish the working state

1. Read the repository-root `AGENTS.md` completely. It is authoritative.
2. Read `SESSION.md` for any nontrivial task, resumed work or dirty worktree.
3. Inspect `git status`, relevant diffs and untracked files before proposing
   or making changes. Existing changes belong to the user unless the current
   task clearly says otherwise.
4. Reconcile conflicts in this order: current source and working tree,
   `AGENTS.md`, current subsystem documentation, then historical snapshots.
   Never overwrite an active batch with an older handoff or design note.
5. Classify the request correctly. Planning, diagnosis and review do not
   authorize implementation; implementation does not authorize commit, push
   or anchor promotion unless the user requests that workflow.

## Read only the relevant project material

- Read [references/project-map.md](references/project-map.md) when locating a
  subsystem, choosing an architectural precedent or deciding which documents
  are current.
- Read [references/orchestration.md](references/orchestration.md) before using
  subagents or splitting a material implementation.
- Read [references/validation.md](references/validation.md) before running any
  project build, test, audit, formatter, benchmark or anchor workflow.
- Before changing source under `library/`, `compiler/` or `drivers/`, read
  `docs/modernization.md` and the specific subsystem contract selected by the
  project map.

## Design and implementation contract

- Verify what the current anchor accepts before using a Luna feature. A new
  feature's first implementation may use only already-supported syntax; only
  a promoted fixed-point toolchain may adopt the new feature in compiler
  sources.
- Start from domain objects, ownership, state transitions, invariants, error
  channels and module dependency direction. Use classes for behavior or owned
  state, structs for passive values and transparent views, generics for real
  reusable typed abstractions, and design patterns only when they remove a
  concrete variation or lifecycle problem.
- Follow existing Luna conventions rather than mechanically translating C++.
  Preserve the project's `for`/`switch`/two-clause rules, 120-column budget,
  module/interface boundaries, deterministic ordering, freestanding resource
  costs and Chinese code comments.
- Prefer same-module `.la` implementation splits. Add a submodule only after
  proving an independent contract and acyclic consumer boundary. Register any
  module or implementation-path change in `tools/selfhost.py`'s `LIBRARIES`.
- Do not translate through C, invoke a hosted compiler/assembler/linker, add a
  libc dependency, broaden the target beyond `x86_64-unknown-linux-gnu`, or
  weaken fixed-point reproducibility.
- Add the smallest test that observes the intended semantic or ownership
  contract. Preserve negative diagnostic identity, explicit `UNITS` ordering
  and specialized frontend/relocation/FFI protocols.

## Sol and Luna roles

For material work, the main Sol agent owns repository discovery, architecture,
the approved plan, acceptance criteria, diff review and final evidence acceptance.
Delegate bounded execution only after the design and file ownership are clear.
For substantive Luna work, a three-role `gpt-5.6-luna` pipeline is the project
default: `luna_implementer` owns settled file-bounded code, `luna_validator`
owns independent caw execution and evidence, and `luna_documenter` owns
source-backed architecture and handoff updates. Use `luna_explorer` before
implementation when an independent source map or review sharpens the contract.
Keep only tiny, tightly coupled edits in the main thread.

- Use `luna_explorer` for independent read-only source or dependency mapping.
- Use `luna_implementer` for one narrowly scoped implementation with explicit
  files, invariants and acceptance criteria.
- Use `luna_validator` after main-agent diff acceptance; it executes the
  delegated caw gates without editing local source.
- Use `luna_documenter` after the implementation shape is accepted; give it
  validation evidence before it records a gate as complete.
- Do not let concurrent writers touch overlapping files. In an already dirty
  subsystem, prefer one writer at a time.
- The main agent must inspect the resulting diff and correct architectural
  drift; a successful worker report is not acceptance evidence.
- Handle tiny, tightly coupled or judgment-heavy edits in the main thread when
  delegation would add more ambiguity than useful focus.

## Verification and handoff

Keep local work to reading, searching, editing and diff inspection. All Luna
project execution and generated validation artifacts belong in a new isolated
workspace on `caw`; never silently fall back to a local build. Scale the
remote gates to the change, but source changes normally require the static
gates, cold fixed point and complete test suite described in the validation
reference.

Update current documentation when a language, ABI, architecture, module graph,
test protocol or trust boundary changes. Refresh `SESSION.md` for substantial
unfinished work or a material milestone, without replacing concrete evidence
with a conversational summary. Preserve user-owned files and report exactly
what was and was not validated.
