# Next steps

Working plan for the pure-Luna branch after m1.1 (foundation trio) and
the post-m1.1 refactor. Sequencing rationale lives here; accepted
surface syntax is recorded in `syntax-plan.md` as it lands, and
implementation status is tracked in `roadmap.md`.

## Status snapshot

| Area | State |
| --- | --- |
| m1.1 aliases / pointer arithmetic / function pointers | landed; 80/80 cases; verify fixed point |
| m1.2 qualifiers / attributes / static assertions | landed; 96/96 cases; verify fixed point |
| built-ins package (bit ops, float helpers, overflow) | landed; 106/106 cases |
| m1.3 kernel UAPI layout package | landed; 122/122 cases; verify fixed point |
| Refactor R0-R4 (formatting, module graph, service/pass decoupling) | landed; every `.la` below 1,000 lines |
| C23 disposition review | landed in `syntax-plan.md`; m1.2-m1.10 sequenced |
| `anchor/` | promoted to the unified multi-command `luna` stage-fixed toolchain |
| Negative tests | `FAIL <diagnostic-kind>` harness landed (0.3 done) |
| M3.0-M3.6 object model and class-value composition | implemented and promoted; 383/383 cases |
| M4 native generics | implemented and promoted; 409/409 cases; verify fixed point |
| Unified `luna` tool driver | implemented and promoted; 416/416 cases; verify fixed point |

## Step 0 — close m1.1 completely

### 0.1 Record m1.1 in `docs/language.md`

Authoritative chapters for:

- type aliases: transparency, chain resolution, cycle rejection,
  interface export rules;
- pointer arithmetic: `p + n`, `n + p`, `p += n`, `p -= n`, element
  distance `p - q`, same-type ordering; the rule that expression-form
  counts require an explicit `as usize` while compound assignments
  accept bare integer literals (deliberate divergence from the earlier
  sketch, chosen to keep pre-existing programs lowering byte-for-byte
  identically);
- function pointers: first-class scalar semantics, bare-name and
  `&name` value forms, null-call trap, equality against null and same
  shapes, explicit `as` between arbitrary shapes and `usize`, storage
  in aggregates/arrays/parameters/returns, no closures.

Fix the `syntax-plan.md` examples that show unadorned literal counts.

### 0.2 Promote the anchor

On the x86-64 host: copy `out/stage-next/bin/{lunac,luna-as,luna-link}`
into `anchor/`, regenerate `SHA256SUMS`, append a provenance note
describing the source revision and the m1.1 capability delta. Commit
and push. Then re-run build/test/verify locally as a smoke test; local
aarch64 development keeps using `LUNA_TOOL_RUNNER=qemu-x86_64-static`.

This unlocks step 2 of the iteration discipline: compiler sources may
adopt aliases, pointer arithmetic and function pointers (dispatch
tables included).

### 0.3 Expected-failure cases — done

`tools/selfhost.py` accepts a second expectation form,
`<case>.la FAIL <diagnostic-kind>`, asserting that compilation fails
with exactly that leading `BootstrapSemanticDiagnosticKind` (exit status
`64 + kind`, compile-only, no linking). Kind ordinals are parsed from
`compiler/include/luna/bootstrap/middleend/semantic/context.lh`.

## Step 1 — m1.2 qualifiers, attributes and static assertions — done

All four slices landed behind build/test/verify, 96/96 cases:

1. Attribute infrastructure: `@name`/`@name(args)` parsing on module-scope
   declarations, fields and local variables; the
   `middleend/semantic/attributes` module owns the known-attribute table
   and the mounting-point validation pass. `@inline` records the
   `inline_hint` function flag only.
2. `@noreturn`: direct calls end the block with a new `unreachable` IR
   opcode (ud2) and no successor edge; `return` inside or a reachable
   body end is `noreturn_returns`. Dead blocks are swept to
   `unreachable` at end of lowering.
3. `assert(const bool)`: the `consteval` module tree-walks literals,
   enum members, arithmetic/bitwise/comparison/logical operators,
   conditionals, integer casts and layout queries; module scope and
   function bodies. Seed of the m1.5 interpreter.
4. `volatile`: object qualification plus pointee qualification
   (`*volatile T`) modelled on the read-only flag, recorded on semantic
   locals and IR slots for any future optimizer.

## Step 2 — decision: built-ins package or FFI completeness

Built-ins package landed (recommended option X): bit operations, float
helpers and overflow intrinsics, 106/106 cases behind verify. Next main
line is m1.3 (kernel UAPI layout package) per the revised syntax plan.

### Option X (recommended): built-ins package — done

- bit operations: `clz`, `ctz`, `popcount`, `rotate_left/right`,
  `byte_swap`;
- float helpers backed by SSE2: `sqrt`, `floor`, `ceil`, `trunc`,
  `round`, `min`, `max`, `abs`;
- arithmetic modes: `wrapping_add/sub/mul` and overflow-reporting
  `checked_add/sub/mul` alongside the existing trap semantics.

Purely additive intrinsics; low risk.

### Option Y: m1.3 kernel UAPI layout package

Anonymous struct/union members, psABI-conformant bitfields via
`@bits(...)`, `@align(N)` alignment control, `@packed` structures and the
`[?]T` flexible trailing array member (header types). Heaviest single item
is the bitfield layout engine; deferred until after X. Variadic support later
landed as m1.7 by explicit FFI decision, with declared-width arguments.

## Deferred (explicitly)

- Further pass splitting: R4 separated parser state/grammar stages; semantic
  lookup, construction, interpretation and policy services; IR construction
  from verification; code-generation value/call dispatch; assembler operand,
  encoding and source layers; and ELF format/reader/writer services. Every
  implementation is below 1,000 lines; split again only at a real extension
  boundary rather than to satisfy a line-count target.
- Source-level module variables initialized with function addresses remain
  deferred. M3.2 provides the compiler-owned read-only `.quad` relocation
  path for vtables, but does not expose general module-scope variables; the
  `@align` object mount in m1.3 still mounts on locals until then.
- Threads/atomics (standard library per syntax-plan decision 6),
  `_BitInt(N)`, macros: remain out of scope per `syntax-plan.md`.

## Completed M2 callable foundation

M2.0-M2.4 have landed the versioned canonical type/signature encoding,
signature-bearing Luna symbols, explicit callable bindings, deterministic
candidate slices, interface/multi-implementation matching by overload key,
side-effect-free argument probing, exact call resolution and expected
function-type selection, plus folded trailing defaults inserted after overload
selection and one shared value-category service, as described in
[`m2-callable-infrastructure.md`](m2-callable-infrastructure.md). The callable
foundation is complete and M3.0 now consumes it directly. `Function` owns the
kind/owner/receiver identity, bindings are owner-scoped and the versioned
signature writer keeps existing free-function signatures byte-identical. The
class store owns records plus contiguous field/method policy slices. M3.0 adds
`pub`/`prot`/`priv` fields, implicit `this`, receiver qualifiers, same-module
`impl` blocks, overloaded/defaulted constructors and direct methods, exact class copies and
ordinary non-polymorphic layout/ABI classification. Cross-module execution,
negative policy cases and implementation-unit permutation are executable gates.

M3.1 single inheritance and explicit override contracts are now implemented as
specified in [`m3-oop-design.md`](m3-oop-design.md): base-at-zero layout,
inherited lookup without hiding, `prot`, mandatory first-statement
`super.init`, direct `super` calls, explicit pointer upcasts, final checks and
deterministic virtual/abstract slot metadata.

M3.2 read-only function-address relocation data is implemented: verified
IR reference slices lower to `.quad <symbol>`, remain `absolute64` through the
bootstrap object and ELF64 paths, and are covered by deterministic,
malformed-input and executable-link tests. M3.3 now adds one vptr only to
polymorphic hierarchies, concrete final-overrider tables, virtual pointer calls
through `call_indirect`, exact-value/final devirtualization and most-derived
construction dispatch. M3.4 adds restricted operators, bound methods, RTTI and
friendship; M3.5 adds optional opaque classes; M3.6 completes embedded
class-value composition and ordered member initialization. None of these
milestones changes M3.0 ownership semantics: there is still no automatic
lifetime or resource management.

M4 native generics are implemented as specified in
[`m4-generics-design.md`](m4-generics-design.md): exact static inference,
canonical monomorphization, generic data/class types and cross-module interface
definitions are green behind the 409-case suite and fixed-point verification.
The exact M4 source revision is promoted into `anchor/`, so compiler sources
may now adopt generic declarations under the ordinary fixed-point discipline.
Reference types and object lifetime work form the next language-design phase.
The independent packaging cleanup in [`unified-tool-driver.md`](unified-tool-driver.md)
replaces the three emitted tool binaries with one command-dispatched `luna`
before that language work begins.

## Discipline reminders

Every slice: language documentation first, implementation behind green
build/test/verify, roadmap checkbox, then compiler sources may adopt
the new syntax once the anchor is refreshed.
