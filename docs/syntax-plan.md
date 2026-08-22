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
   before implementation.
3. **Variadic functions reach full parity**: call side (`extern fn ...`)
   first because it unlocks real FFI targets such as `ioctl`; define side
   (`fn f(fmt: *const u8, ...)` with `va_list`) later because its consumers
   — other variadic functions and `vprintf`-style implementations — barely
   exist in a no-libc world, and the System V register-save-area ABI plus
   `%al` vector-count protocol make it several times costlier than the call
   side.
4. **Milestone order** below is fixed by the self-hosting discipline: each
   group lands green (`tools/selfhost.py verify` + `test`) before compiler
   sources adopt any of it, one group at a time.

## Milestone M1 — foundation trio

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

`p + n`, `n + p` and `p += n` require `n: usize` and yield the same pointer
type, scaled by the element size. `p - q` requires identical pointee types
and yields the element distance as `usize` (the program has established
order when it matters). Ordering comparisons `< <= > >=` require the same
exact pointer type and compare address values as unsigned integers.
Equality already exists. Address computation wraps modulo 2^64; dereading
or indexing through an invalid address retains machine fault behavior, and
null checks still precede every dereference. Pointer arithmetic does not
imply array bounds knowledge — raw pointers stay raw.

## Milestone M2 — qualifiers, attributes and static assertions

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

## Milestone M3 — FFI completeness package

Anonymous struct/union members, bitfields, flexible trailing array member,
variadic extern calls:

```luna
union Value {
    struct { low: u8; high: u8; }      // anonymous struct member
    whole: u16;
}

struct Flags {
    raw: u32 @bits(ready: 1, error: 1, level: 6, reserved: 24);
}

struct Buffer {
    length: u32;
    data: []u8;                        // flexible member, last position only
}

extern fn ioctl(descriptor: i32, request: u64, ...) -> i32;
```

Bitfield allocation follows the x86-64 System V psABI so FFI layouts match
C bit-for-bit; `@bits` avoids colliding with the `:` type annotation. The
flexible member is valid only as the final field of a structure whose use
is otherwise restricted to the C interop rules. Variadic extern calls
implement the `%al` protocol at call sites; variadic definitions arrive in
a later milestone.

## Milestone M4 — characters, strings and inference

```luna
type char = i8;                         // SysV x86-64 spelling of char
let wide: *const u16 = u16"...";
let greeting: *const u8 = "Hel" "lo";   // adjacent literal concatenation
let cursor = list_head(list);           // let/var inference from initializer
```

String prefixes select the element width (`u8"` default, `u16"`, `u32"`);
adjacent same-width literals concatenate at translation time. Inference is
deliberately narrow: only bindings with an initializer, never parameters or
returns. A `typeof(expr)` type position may be added here if needed.

## Milestone M5 — constant functions

```luna
const fn align_up(value: usize, alignment: usize) -> usize {
    return (value + alignment - 1) & ~(alignment - 1);
}
var scratch: [align_up(4096, 64)]u8 = {};
```

A tree-walking interpreter over a pure scalar subset: arithmetic, bitwise,
comparisons, calls to other const functions. No memory access, no loops
until proven terminating (fixed step budget). Results feed array lengths,
enum discriminants, assertions and typed constants.

## Milestone M6 — labels and goto

```luna
search: for (row: usize = 0; row < rows; row += 1) {
    if (found(row)) {
        break search;
    }
}

cleanup:
    std_memory_release(allocation);
    goto cleanup;                       // forward/backward, validated
```

Labeled loops first; then `goto` with explicit semantic rules: labels share
function scope, no jump into the extent of any initialization, jump table
validated per function before lowering.

## Milestone M7 — variadic definitions

`fn format(out: *mut File, fmt: *const u8, ...) { ... }` plus `va_list`
exposure equivalent to `<stdarg.h>`: register save areas in the prologue,
typed `va_arg` expansion by argument class, `%al` handling both sides. The
default-promotion question is resolved toward explicitness: Luna passes
each argument at its declared width and documents the divergence from C's
promotions at this boundary.

## Explicitly out of scope

Implicit conversions in any form; assignment/`++`/comma as expressions;
the preprocessor and textual macros; `_Generic` (generics, if ever, will be
designed natively instead); K&R declarations; VLAs; threads and atomics
until the runtime grows a scheduling story.
