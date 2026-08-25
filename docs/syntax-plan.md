# Syntax completion plan

This document records the accepted direction for completing Luna's surface
language, the design decisions behind it and the milestone order. It is a
plan: nothing here is implemented until `docs/roadmap.md` gains the matching
checked entries. Accepted surface syntax still lives only in
`docs/language.md` once a feature lands.

The reference yardstick for completeness is the procedural core of C23:
every capability a working C23 program needs should have a deliberate Luna
equivalent, expressed in Luna's own style — types follow names, assignment
is a statement, conditions require `bool`, conversions are always explicit,
there is no preprocessor.

## Design decisions

1. **Conversion discipline stays fully explicit.** No implicit integer
   promotions, no usual arithmetic conversions, ever. Only literals receive
   contextual typing; every value conversion remains an `as`. This is the
   core safety identity of the language and outweighs C source
   compatibility.
2. **Non-structured jumps return in full.** Both labeled `break outer` /
   `continue outer` on enclosing loops *and* a real `goto label` are part of
   the accepted direction. `goto` requires semantic validation rules (no
   jumping into a scope past an initialization) that must be specified
   before implementation. `defer` is shelved: `goto cleanup` covers the
   error-cleanup idiom for now, and the question may be reopened with
   concrete usage experience.
3. **Variadic functions entered through an explicit FFI decision.** The
   kernel is not a consumer: every Linux syscall Luna makes is fixed-arity,
   and glibc's variadic `ioctl(fd, request, ...)` spelling is user-space sugar
   over a fixed three-argument syscall. m1.7 nevertheless landed both sides
   together for real C-library interop — extern calls with the `%al` protocol
   and definitions with register save areas. Variadic arguments retain
   Luna's declared widths instead of receiving C's default promotions.
4. **Milestone order** below is fixed by the self-hosting discipline: each
   group lands green (`tools/selfhost.py verify` + `test`) before compiler
   sources adopt any of it, one group at a time.
5. **The `@` namespace absorbs C's double-underscore surface.** C spells
   declaration modifiers as `__attribute__((...))` and compile-time hooks as
   `__builtin_*`; Luna expresses both with `@`. Attributes mount on the
   declaration they qualify (`@noreturn fn f()`, `@align(64) var frame`);
   compile-time intrinsics are call-shaped (`@add_overflow(a, b)`,
   `@file()`). Every attribute and intrinsic shares one grammar — `@name`
   or `@name(argument-list)` — validated against its legal mounting points
   by one framework, introduced with the first attributes in m1.2.
6. **Atomics are a standard-library concern, not a language feature.** No
   `_Atomic` qualifier and no language-level memory model. When the runtime
   grows synchronization primitives, the standard library implements them
   over target-specific builtins; the language surface stays unchanged.
7. **Raw assembly ships as naked functions, never as GNU extended asm.**
   Systems code needs real instructions (syscalls, context switches, CPU
   control, vector primitives); Luna answers with `asm fn` (m1.8): a whole
   function whose body is raw assembly, validated and encoded by the
   project's own assembler. GNU's constraint-letter operand language is
   rejected as C baggage, and structured asm blocks inside ordinary
   functions are shelved until profiling argues for them. Semantic hooks
   that read flags (`@add_overflow`) stay call-shaped intrinsics.
8. **FFI matches C-described binary interfaces; it does not pull in libc.**
   The `extern fn` surface exists to talk to interfaces specified as C ABI
   layouts — the kernel UAPI first (bitfields in `perf_event_attr`,
   flexible members in `inotify_event`, packed on-disk formats), external
   objects a caller explicitly supplies second. Generated programs stay
   freestanding and libc-free; adopting glibc, musl or any libc is a
   separate explicit boundary decision, never a side effect of FFI work.

## C23 disposition

Feature-by-feature accounting against the yardstick. *Adopted* lands as
specified, *modernized* lands in Luna's own form, *alternative* is covered
by a different mechanism, *shelved* waits for a stated trigger, *rejected*
is deliberate.

### Types

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| `_Bool`, `true`, `false` | adopted | `bool`, already in the language |
| `char` family (three distinct types) | modernized | explicit `i8`/`u8`; projects may add a transparent `char` alias |
| `short`/`int`/`long` width soup | modernized | `i8`..`i64`, `isize`/`usize`, done |
| `long double` (f80/x87) | rejected | x87 legacy, 16-byte ABI friction |
| `_Decimal32/64/128` | rejected | no hardware support on the sole target |
| `_BitInt(N)` | rejected | large backend cost, narrow use |
| `_Complex`/`_Imaginary` | rejected | a structure of two floats suffices |
| `typedef` | modernized | transparent `type` aliases, m1.1 |
| `const` qualification | alternative | `let`/`var` bindings plus `*const` pointers |
| `volatile` | adopted | m1.2 |
| `restrict` | shelved | waits for the optimizer |
| `_Atomic`, memory orders | alternative | standard library over builtins, decision 6 |
| `alignas` | adopted | `@align(N)`, m1.3 |
| packed layout (universal extension) | adopted | `@packed`, m1.3 |
| bit-fields | modernized | `@bits`, m1.3 |
| anonymous struct/union members | adopted | m1.3 |
| flexible array member | modernized | `[?]T` header types, m1.3 |
| VLAs | rejected | fixed arrays only |
| array-to-pointer decay | removed | arrays are values, passable by value |

### Literals and compile-time surface

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| `0b` binary literals | adopted | m1.4 |
| digit separators | modernized | `_` separators, already in the language |
| hexadecimal floating literals | adopted | m1.4 |
| literal suffixes (`UL`, `F`, ...) | modernized | contextual literal typing, done |
| multi-character constants | rejected | C relic |
| string width prefixes | modernized | `u16"`/`u32"`, m1.4 |
| adjacent literal concatenation | adopted, restricted | same width only, m1.4 |
| `\u`/`\U` escapes | modernized | `\u{...}`, m1.4 |
| `#embed` | modernized | `@embed("path")`, m1.10 |
| `constexpr` objects | modernized | typed `const`; `const fn` (m1.5) goes further |
| `static_assert` | adopted | `assert(...)`, m1.2 |
| `nullptr` | modernized | `null`, already in the language |
| `typeof`/`typeof_unqual` | shelved | `let`/`var` inference first; revisit on need |

### Expressions

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| implicit promotions and conversions | rejected | decision 1, the language identity |
| comma operator, `++`/`--`, assignment as expression | rejected | statements stay statements |
| `?:` conditional | kept | `bool` condition, one exact result type |
| `sizeof(expression)` | rejected | type-only `sizeof`/`alignof`/`offsetof` |
| `_Generic` | rejected | native generics if ever |
| `<stdckdint.h>` checked arithmetic | modernized | `@add_overflow` and kin, built-ins package |
| compound literals | modernized | context-directed `{}` initialization |
| designated initializers | adopted | named fields done; array lists in m1.4 |

### Statements and control flow

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| switch fallthrough | removed | scoped, non-falling cases |
| `goto` | adopted, validated | m1.6 |
| labeled `break`/`continue` | adopted | m1.6 |
| cleanup patterns | shelved | `defer` shelved; `goto cleanup` stands, m1.6 |
| loops | modernized | `while`/`do`/`for` plus unconditional `loop` |

### Functions

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| K&R declarations, `f()` vagueness | rejected | exact prototypes only, done |
| variadic calls | adopted with explicit widths | m1.7; `%al` vector-count protocol |
| variadic definitions, `va_list` | adopted with explicit widths | m1.7; register save areas and typed `va_arg` |
| `noreturn` | modernized | `@noreturn`, m1.2 |
| `inline` | adopted | `@inline` metadata, m1.2 |
| default arguments, overloading | rejected | one exact signature per name |
| multiple return values | alternative | aggregate returns, already in the language |

### Concurrency, signals and non-local jumps

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| `threads.h` | shelved | a runtime concern, not language surface |
| signal handler semantics | shelved | runtime boundary |
| `setjmp`/`longjmp` | rejected | conflicts with structured control flow |
| `fenv` floating-point environment | rejected | fixed rounding, trapping conversions |

### Preprocessor replacements

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| `#include` | modernized | the module system, done |
| object-like and function-like macros | modernized | typed `const`, `const fn` (m1.5), functions |
| conditional compilation | shelved | candidate: const-driven dead-code folding |
| X-macro generation | rejected | data tables, per house style |
| `__FILE__`/`__LINE__` | modernized | `@file()`/`@line()`, m1.9 |
| `#pragma` | modernized | the attribute framework, m1.2 |

### ABI and FFI

| C23 feature | Disposition | Landing point or rationale |
| --- | --- | --- |
| SysV aggregate classification | adopted | already in the backend |
| over-aligned type interop | adopted | rides on `@align`, m1.3 |
| variadic default promotions | modernized | declared widths, documented divergence, m1.7 |
| GNU extended asm | rejected | constraint-letter language, C baggage |
| raw assembly capability | modernized | `asm fn` naked functions, m1.8; blocks shelved |

## Milestone m1.1 — foundation trio

Type aliases, function pointers and pointer arithmetic. Together they
unlock callbacks, vtables and cursor-style traversal, which every later
group builds on.

### Type aliases

```luna
type Size   = usize;
type Handle = i32;
```

Transparent aliases, resolved away during type checking. Alias chains are
allowed; recursive aliases are rejected. Declared at module scope,
exportable from interfaces like any other declaration. Usable in every type
position, including pointer, array element and function-pointer positions.

### Function pointers

```luna
var hook: fn(Event, *void) -> bool = null;
let compare: fn(*const u8, *const u8) -> i32 = bytes_compare;
hook(event, context);              // indirect call through the pointer
let entry: fn(usize) -> bool = &predicate;   // explicit address-of form
```

A function-pointer type may be compared for equality against `null` or
another value of the same exact type. Calling a null function pointer
traps, mirroring null-pointer dereference rules. Function pointers are
plain scalar values under the System V ABI (INTEGER class): they can be
stored in aggregates, passed, returned and re-pointed. No closures: a
function pointer carries code, never environment. Taking the address of an
overload-free function by name is unambiguous; there is no conversion
between distinct function-pointer shapes without `as`.

### Pointer arithmetic and ordering

```luna
cursor += 1;                        // advance one T, not one byte
let remaining: usize = end - start; // element count, unsigned
if (start < end) { ... }
```

`p + n`, `n + p`, `p += n` and `p -= n` require `n: usize` and yield the
same pointer type, scaled by the element size. Expression forms require
an already-`usize` count, while the compound assignments additionally
contextually type a bare integer literal as `usize`. `p - q` requires
the same exact pointer type and yields the element distance as `usize`
(the program has established order when it matters). Ordering
comparisons `< <= > >=` require the same exact pointer type and compare
address values as unsigned integers. Equality already exists. Address
computation wraps modulo 2^64; dereferencing or indexing through an
invalid address retains machine fault behavior, and null checks still
precede every dereference. Pointer arithmetic does not imply array
bounds knowledge — raw pointers stay raw.

## Milestone m1.2 — qualifiers, attributes and static assertions

```luna
volatile var status: u32 = 0;          // every access is a real load/store
@noreturn fn fail(message: *const u8) { ... }
@inline fn clamp(value: i32, low: i32, high: i32) -> i32 { ... }
assert(sizeof(Packet) == 64);          // compile-time assertion
```

`volatile` applies to object declarations and to pointee qualification
(`*volatile u32`), forcing every access to memory exactly once.
`@noreturn` extends the unreachable-successor analysis that `loop` already
uses. `@inline` is recorded now, honored when an optimizer exists.
`assert(...)` takes a constant-evaluable `bool` and fails compilation.
`restrict` waits for the optimizer.

This milestone also introduces the shared attribute framework of decision
5: one grammar (`@name` / `@name(argument-list)`) and one mounting-point
validation pass that later attributes (`@align`, `@packed`, `@bits`) and
intrinsics (`@add_overflow`, `@file`) reuse. The constant evaluator behind
`assert` is written as the seed of the m1.5 `const fn` interpreter, not as
a separate evaluator.

## Built-ins package — between m1.2 and m1.3

Call-shaped intrinsics in expression position, in the same `@` namespace
as declaration attributes. Three groups, all restricted to the baseline
x86-64 + SSE2 instruction set (no BMI, no SSE4):

```luna
let highest: u32 = @clz(flags);              // 0..32, clz(0) == 32
let lowest: u64 = @ctz(mask);                // 0..64, ctz(0) == 64
let bits: u32 = @popcount(mask);
let rolled: u32 = @rotate_left(value, 3);    // count masked mod width
let swapped: u64 = @byte_swap(raw);          // widths >= 16 bits

let root: f64 = @sqrt(area);
let down: f64 = @floor(ratio);               // also @ceil, @trunc, @round
let lo: f64 = @min(left, right);             // also @max, @abs

var product: u64 = 0;
if (@mul_overflow(count, stride, &product)) {
    return -1;                               // wrapped value still stored
}
```

Type rules: bit operations accept any integer scalar and return its type
(`@byte_swap` requires width >= 16 bits); rotate counts are `usize`,
masked modulo the operand width. Float helpers take `f32`/`f64` and return
the same type; `@min`/`@max` follow the hardware NaN semantics, `@round`
rounds to nearest with ties to even. The overflow intrinsics take two
same-typed integer operands plus a mutable pointer for the result and
return `bool` — true on overflow, with the wrapped value always stored.
This out-parameter shape supersedes the earlier struct-return sketch: it
needs no prelude type and mirrors `__builtin_add_overflow`.

Implementation notes: bit operations lower to branch-free arithmetic
expansions over existing IR (SWAR popcount; `clz(x) = popcount(~smear(x))`,
`ctz(x) = popcount(~x & (x - 1))` — both correct at zero). `@sqrt`,
`@min`/`@max` gain six SSE2 encodings (`sqrtsd`/`sqrtss`,
`minsd`/`minss`/`maxsd`/`maxss`); `@abs` masks the sign bit in a GPR; the
rounding family uses the `cvtt`/`cvt` conversions with the `|x| >= 2^52`
guard. Overflow intrinsics are one `add`/`sub`/`imul` plus `seto`.
Wrapping arithmetic needs no intrinsic: plain operators already wrap.

## Milestone m1.3 — kernel UAPI layout package

Anonymous struct/union members, bitfields, alignment and packing control —
the layout vocabulary of C-described binary interfaces, with the kernel
UAPI as the first consumer (decision 8):

```luna
union Value {
    struct { low: u8; high: u8; }      // anonymous struct member
    whole: u16;
}

struct Flags {
    raw: u32 @bits(ready: 1, error: 1, level: 6, reserved: 24);
}

@align(64) var line_buffer: [64]u8;    // over-aligned object

struct WireHeader @packed {            // no padding, alignment 1
    kind: u8;
    length: u32;                       // intentionally unaligned
}

struct InotifyEvent {
    wd: i32;
    mask: u32;
    cookie: u32;
    length: u32;
    name: [?]u8;                       // flexible member, last position
}
```

Bitfield allocation follows the x86-64 System V psABI so FFI layouts match
C bit-for-bit; `@bits` avoids colliding with the `:` type annotation.
`@align(N)` raises the alignment of an object, field or type up to the
target maximum and carries the System V rules for aggregates aligned
beyond 16 bytes, so C's over-aligned types (`max_align_t`, vector types)
gain exact Luna counterparts. `@packed` removes structure padding and
drops field alignment to 1 for wire and hardware layouts; because packed
fields may be unaligned, taking the address of a packed field is rejected
rather than introducing an unaligned-pointer type dimension.

A trailing `[?]T` member — one per structure, last position only — makes
its structure a *header type*, mirroring C's flexible array member bit for
bit. `sizeof(S)` yields the fixed part exactly as C computes it; structure
alignment still accounts for the member's element alignment, so
`offsetof(S, data)` can be strictly smaller than `sizeof(S)` (for example
`{u64 a; u8 b; data: [?]u32}` gives `offsetof` 12 and `sizeof` 16).
`p->data` yields `*T` pointing at the first element and reuses ordinary
raw-pointer indexing. A header type is incomplete: no variables, no
by-value parameters or returns, no fields in other aggregates, no array
elements, no whole-object initialization or assignment — it lives only
behind pointers, with storage sized by the caller
(`sizeof(S) + count * sizeof(T)`).

## Milestone m1.4 — characters, strings, literals and inference

```luna
type char = i8;                         // ordinary project alias, not a built-in type
let wide: *const u16 = u16"...";
let greeting: *const u8 = "Hel" "lo";   // adjacent literal concatenation
let cursor = list_head(list);           // let/var inference from initializer

let mask: u32 = 0b1010_0110;            // binary literals
let exact: f64 = 0x1.8p3;               // hexadecimal floating literals

var table: [4]u32 = {1, 1, 2, 3};       // positional array initializer
var sparse: [8]u32 = {[3] = 7, [6] = 9}; // indexed initializer, rest zero

let inner: usize = offsetof(Packet, header.tag);   // nested field designator
```

String prefixes select the element width (`u8"` default, `u16"`, `u32"`);
adjacent same-width literals concatenate at translation time. Inference is
deliberately narrow: only bindings with an initializer, never parameters or
returns. `typeof(expr)` is shelved until a concrete need survives
inference. Binary literals follow the integer literal rules with base 2.
Hexadecimal floating literals give exact binary constants without a
decimal round-trip. Array initializers accept a positional list shorter
than the array (the rest is zero) or indexed entries in any order, with
duplicates rejected. `offsetof` accepts a nested field designator.

## Milestone m1.5 — constant functions

```luna
const fn align_up(value: usize, alignment: usize) -> usize {
    return (value + alignment - 1) & ~(alignment - 1);
}
var scratch: [align_up(4096, 64)]u8 = {};
```

A tree-walking interpreter over a pure scalar subset: arithmetic, bitwise,
comparisons, calls to other const functions. No memory access, no loops
until proven terminating (fixed step budget). Results feed array lengths,
enum discriminants, assertions and typed constants. The interpreter is the
same evaluator that m1.2's `assert` seeded.

## Milestone m1.6 — labels and goto

```luna
search: for (var row: usize = 0; row < rows; row += 1) {
    if (found(row)) {
        break search;
    }
}

cleanup:
    memory::release(allocation);
    goto cleanup;                       // forward/backward, validated
```

Labeled loops first; then `goto` with explicit semantic rules: labels share
function scope, no jump into the extent of any initialization, jump table
validated per function before lowering. `defer` is shelved (decision 2):
the cleanup idiom rests on `goto` alone until real usage argues otherwise.

## Milestone m1.7 — variadic functions

Both sides of variadic support landed together per decision 3: extern call
sites use the `%al` vector-count protocol, and definitions such as
`fn format(out: *File, fmt: *const u8, ...) { ... }` expose `va_list` with
register save areas in the prologue and typed `va_arg` expansion by argument
class. Luna passes each argument at its declared width and documents the
divergence from C's default promotions at this boundary.

This milestone has no kernel consumer — every Linux syscall is fixed-arity.
It landed by explicit decision to enable C-library FFI; decision 8 continues
to govern what linking such a consumer means.

## Milestone m1.8 — naked assembly functions

```luna
asm fn std_atomic_xchg_u64(target: *u64, value: u64) -> u64 {
    "xchg %rsi, (%rdi)\n"
    "mov %rsi, %rax\n"
    "ret\n"
}
```

An `asm fn` declares an ordinary function signature — checked and called
under the System V ABI like any other — whose entire body is raw assembly
carried as adjacent string literals (m1.4 concatenation keeps long bodies
readable). The compiler emits no prologue, epilogue or `ret`; register
use, stack discipline and the return sequence belong to the author. The
body text is spliced into the emitted assembly, so every instruction is
validated and encoded by the project's own assembler and malformed bodies
fail at assembly time.

Rules: `asm fn` appears only in implementation units; the body is one or
more adjacent string literals and nothing else; the signature follows the
normal SysV argument and result classification, which is exactly what the
author programs against; debug information degrades to the function entry
address. This answers the systems-programming need for real instructions —
syscall stubs, context switching, CPU control sequences, vector primitives
— without inheriting GNU's constraint-letter language (decision 7).
Structured asm blocks inside ordinary functions stay shelved; if profiling
later shows asm-function call overhead mattering (atomic operations are
the expected case), inlining of asm bodies is revisited.

The same milestone migrates the `luna.linux.syscall` definitions from
their linker-injected special case to honest `asm fn` source in
`library/linux/syscall.la` — including the argument-register shuffle the
`syscall` instruction requires (`rcx` arrives as `r10`).

## Milestone m1.9 — source position intrinsics

```luna
@panic(@file(), @line(), "unreachable"); // position builtins for diagnostics
```

`@file()` and `@line()` yield the source position as `*const u8` and
`usize`, giving `assert` and panic paths real locations. The overflow
intrinsics originally sketched here landed earlier in the built-ins
package with an out-parameter shape.

## Milestone m1.10 — embedded binary data

```luna
const firmware: [312]u8 = @embed("blobs/device.fw");
```

`@embed("path")` reads a file at compilation time and yields its bytes as
an array constant, the deliberate no-preprocessor answer to C23's
`#embed`. Path resolution, determinism (the embedded bytes feed the fixed
point) and rebuild-trigger rules are specified with the milestone.

## Milestones m1.11/m1.12 — module qualification and selective imports

```luna
import luna.std.text;               // flat names + text:: qualifier
import luna.std.path as fs;         // fs:: qualifier only, nothing flat
import luna.std.io::{read_all};     // read_all flat + io:: qualifier

let view: text::View = text::from_c_string(data, 64);
let root: fs::Path = fs::from_text(view);
```

Imports gain use-site qualification. Every import binds a module qualifier —
plain imports the last name segment, `as t` the alias — and nothing else:
flat binding requires an explicit selective import `::{x, y}` whose names are
validated against the target module (this final rule landed in m1.14).
Qualification resolves in expression, type and interface positions and
composes with enum member access (`io::Error.none`). This is the namespace
mechanism that let m1.13 retire the manual `std_text_`-style prefixes.

## Explicitly out of scope

Implicit conversions in any form; assignment/`++`/comma as expressions;
the preprocessor and textual macros; `_Generic` (generics, if ever, will be
designed natively instead); K&R declarations; VLAs; threads and atomics in
the language surface (atomics arrive as standard-library code per decision
6); `long double`, `_Decimal*`, `_BitInt`, `_Complex`; multi-character
constants; `sizeof` on expressions; `fenv`; `setjmp`/`longjmp`; GNU-style
extended assembly (raw assembly lives in `asm fn`, decision 7; structured
asm blocks stay shelved); `defer` (shelved, may
be reopened with usage experience); slices (shelved; no accepted syntax may
foreclose them); conditional compilation (shelved; the
accepted candidate is const-driven dead-code folding, never a textual
preprocessor).
