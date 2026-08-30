# Sol/Luna orchestration

Use this workflow only when delegation materially improves focus, latency or
context isolation. Every child shares the working tree, so task boundaries
must be physical as well as conceptual.

## Roles

The checked-in project configuration selects `gpt-5.6-sol` with `xhigh`
reasoning for the main agent and `gpt-5.6-luna` with `max` reasoning for all
default and named subagents.

### Main Sol agent

The main agent owns:

- reading `AGENTS.md`, this skill, `SESSION.md`, status and existing diffs;
- source-backed architecture and an explicit acceptance contract;
- deciding whether the task is safe to delegate and partitioning files;
- resolving contradictions between a worker proposal and project invariants;
- reviewing the complete diff and owning remote validation and the final
  handoff.

Do not delegate the architectural decision merely because its implementation
will be delegated.

### `luna_explorer`

Use for a bounded read-only question such as tracing an import path, locating
all consumers of a record, mapping an existing test protocol or comparing two
independent subsystems. Ask for file/symbol evidence and a concise conclusion.
Do not use it to produce speculative redesigns without source evidence.

### `luna_implementer`

Use after the design is settled. Give it one cohesive responsibility and an
explicit writable file set. It may make the requested edits, but it must stop
and report if the task requires a new module boundary, changes an ABI or
language rule, conflicts with dirty user work, or exceeds the supplied plan.

The implementer does not commit, push, promote `anchor/`, run a local Luna
build or broaden the task. The main agent remains responsible for integration.

## Delegation decision

- Keep tiny edits, a single tightly coupled fix and plan-only/review-only work
  in the main thread.
- Use one or more explorers when independent evidence can be gathered without
  edits.
- Use an implementer for a mechanical or well-specified slice whose files and
  acceptance conditions are already known.
- Parallel writers are allowed only for disjoint file sets with no shared
  generated files, registry, docs, tests or anchor. Otherwise serialize them.
- In a dirty subsystem, default to one writer and tell it exactly which
  existing changes are user-owned.

## Child task contract

Every implementation task should include:

```text
Goal:
Approved design and invariants:
Files you may edit:
Files or existing changes you must preserve:
Required tests or documentation:
Out of scope:
Return: changed files, decisions, unresolved risks, validation performed.
```

Give the child the smallest context that makes the contract unambiguous.
Point it to repository sources and docs instead of pasting a large parent
transcript. A child that discovers a material ambiguity should report it; do
not ask it to guess the architecture.

## Acceptance loop

1. Inspect the child's actual diff, not only its summary.
2. Recheck module direction, ownership, failure propagation, determinism,
   source feature availability, complexity and resource cost.
3. Confirm only assigned files changed and existing dirty work was preserved.
4. Add or correct tests and documentation at the owning boundary.
5. Run the applicable `caw` gates from the validation reference.
6. If validation fails, send one focused diagnosis/fix request. Re-plan in the
   main thread when the failure invalidates the design rather than repeatedly
   asking a worker to improvise.
