# Next steps

Working plan for the pure-Luna branch after m1.1 (foundation trio) and
the post-m1.1 refactor. Sequencing rationale lives here; accepted
surface syntax is recorded in `syntax-plan.md` as it lands, and
implementation status is tracked in `roadmap.md`.

## Status snapshot

| Area | State |
| --- | --- |
| m1.1 aliases / pointer arithmetic / function pointers | landed; 80/80 cases; verify fixed point |
| Refactor R0-R2 (120-column reflow, table mappings, semantic split) | landed; same gates |
| `docs/language.md` | missing the m1.1 chapters (debt, see step 0.1) |
| `anchor/` | still pre-m1.1 toolchain (debt, see step 0.2) |
| Negative tests | harness only expresses positive exit codes (debt, see step 0.3) |

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

### 0.3 Expected-failure cases

Extend `tools/selfhost.py` with a second expectation form,
`<case>.luna FAIL <diagnostic-kind>`, asserting that compilation fails
with exactly that leading diagnostic. Needed by nearly every m1.2
slice (assert failures, volatile misuse, unknown attributes,
unreachable-code misuse). Keep it small: compile-only, no linking.

## Step 1 — m1.2 qualifiers, attributes and static assertions

Four slices in dependency order; each slice updates
`docs/language.md` first, lands behind build/test/verify, and ticks its
roadmap box.

1. Attribute infrastructure: `@name` parsing on declarations, AST
   flag/storage, unknown-attribute diagnostics. Built once; `@inline`
   only records metadata.
2. `@noreturn`: mark callees, feed the existing successor-reachability
   worklist so calls to noreturn functions terminate blocks. Makes
   `missing_return` analysis smarter; executable tests use a trapping
   helper.
3. `assert(const bool)`: narrow constant evaluator over literals,
   comparisons, layout queries and enum members. This is the seed of
   the m1.5 constant-function interpreter; design the interface for
   growth. Failure is a compile-time diagnostic.
4. `volatile`: object qualification plus pointee qualification
   (`*volatile T`) modelled like the read-only flag; every source-level
   access stays a real memory access (already true in the
   correctness-first backend), the IR flag exists so a future optimizer
   must honour it; document how many accesses compound assignment
   implies.

## Step 2 — decision: built-ins package or FFI completeness

Two candidate main lines after m1.2. Recommendation: **built-ins
first** — they unblock practical library work (text formatting, hashing,
decimal printing) that later milestones, including m1.3 test programs,
benefit from.

### Option X (recommended): built-ins package

- bit operations: `clz`, `ctz`, `popcount`, `rotate_left/right`,
  `byte_swap`;
- float helpers backed by SSE2: `sqrt`, `floor`, `ceil`, `trunc`,
  `round`, `min`, `max`, `abs`;
- arithmetic modes: `wrapping_add/sub/mul` and overflow-reporting
  `checked_add/sub/mul` alongside the existing trap semantics.

Purely additive intrinsics; low risk.

### Option Y: m1.3 FFI completeness

Anonymous struct/union members, psABI-conformant bitfields via
`@bits(...)`, flexible trailing array member, variadic extern calls
with the `%al` protocol. Heaviest single item is the bitfield layout
engine; deferred until after X.

## Deferred (explicitly)

- Further splitting of parser.luna/codegen.luna: both are under the
  soft ceiling after R0; revisit only if they regrow.
- Global variables initialized with function addresses: needs a data
  symbol relocation path (`.quad`-style); not required by any accepted
  example.
- Threads/atomics, `_BitInt(N)`, macros, packed layouts, alignment
  attributes: remain out of scope per `syntax-plan.md`.

## Discipline reminders

Every slice: language documentation first, implementation behind green
build/test/verify, roadmap checkbox, then compiler sources may adopt
the new syntax once the anchor is refreshed.
