# Bootstrap execution semantics

This document freezes the behavior needed to test the current `i8`, `i16`,
`i32`, `i64`, `isize`, `u8`, `u16`, `u32`, `u64`, `usize`, `f32`, `f64`
and `bool` subset. It is an explicit execution contract, not an optimization
policy.

The selected target and its data layout are explicit compiler inputs. The
bootstrap target is `x86_64-unknown-linux-gnu`, with little-endian byte order,
64-bit pointers and 64-bit pointer alignment.

## Evaluation

Expressions and function arguments are evaluated from left to right. `&&` and
`||` short-circuit. The conditional operator evaluates its `bool` condition
first and then exactly one of its two same-typed result operands. Conditions
accept only `bool`; integers never convert to truth values.

An assignment evaluates its target address before its right-hand expression
and performs the write afterward. A compound assignment additionally reads
and preserves the old target value before evaluating the right-hand
expression. The target expression and its index expressions are evaluated
exactly once.

A named aggregate initializer evaluates its field expressions once in source
order. The binding being declared is not visible in its own initializer.
When braces are used as the right side of assignment, all fields are
initialized in separate temporary storage before the destination object is
changed. Thus a field expression that aliases the destination observes the
old complete object.

Every statement is name-checked and type-checked, including statements that
cannot be reached at run time. Unreachable code is lowered into detached IR
blocks and cannot affect live control flow.

`do` executes its body before the first condition test. A `for` statement
executes its initializer once, then repeats condition, body and update in that
order. An omitted condition behaves as `true`; omitted initializer and update
clauses perform no work. The scope introduced by `for` includes all three
clauses and the body.

A switch evaluates its integer or scoped-enum controlling expression once and
compares it against case values in source order. Enum cases must name a member
of the exact controlling enum. It executes exactly one matching case, the
default case when no value matches, or no body when neither exists. Cases
never fall through. `break` exits the innermost loop or switch; `continue`
always targets the innermost loop. In a `for` loop that target runs the update
before testing the condition again.

An integer literal takes the exact integer type required by an enclosing
declaration, return, argument or integer expression. Without an integer
context it defaults to `i32`. Contextual literal typing is not an implicit
conversion: values and variables of different integer types never mix without
an explicit `as` conversion.

A decimal floating-point literal similarly takes the exact `f32` or `f64`
type required by its context and defaults to `f64`. Decimal-to-binary
conversion rounds directly to the destination format. A finite literal that
overflows its destination format is rejected; underflow and subnormal results
are accepted.

## Signed integers

`i8`, `i16`, `i32` and `i64` are 8-bit, 16-bit, 32-bit and 64-bit
two's-complement integers. `isize` is a two's-complement integer whose width is
the selected target's pointer width, which is 64 bits on the bootstrap target.
Addition, subtraction, multiplication and unary negation wrap modulo the
type's power of two. This behavior is defined; there is no signed-overflow
undefined behavior.

Division truncates toward zero. The remainder has the dividend's sign and
satisfies:

```text
left == (left / right) * right + (left % right)
```

Division by zero traps. Dividing the minimum value by `-1` also traps for every
signed width: `-128`, `-32768`, `-2147483648` and
`-9223372036854775808`, respectively. `isize` traps at the minimum value for
its target width; on the bootstrap target this is also
`-9223372036854775808`. The corresponding remainder operations trap for the
same inputs.

Shift counts use only the low `log2(width)` bits: three for `i8`, four for
`i16`, five for `i32` and six for `i64`. `isize` uses the rule for its target
width. Left shift wraps to the operand type; right shift is arithmetic and
preserves the sign.

Ordering comparisons are signed. Equality is defined separately for each
integer type and `bool`; these types never compare or convert implicitly.

## Unsigned integers

`u8`, `u16`, `u32` and `u64` are 8-bit, 16-bit, 32-bit and 64-bit unsigned
integers. `usize` is unsigned and has the selected target's pointer width,
which is 64 bits on the bootstrap target. Addition, subtraction,
multiplication and unary negation wrap modulo the type's power of two.
Division and remainder use unsigned arithmetic and trap only when the divisor
is zero.

Left shift wraps to the operand type. Right shift is logical and shifts in
zero bits. Shift-count masking follows the same three-, four-, five- and
six-bit rule as the corresponding signed widths. Ordering comparisons are
unsigned.

## Explicit integer conversions

Every pair among `i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `u16`, `u32`,
`u64` and `usize` can be converted explicitly. Widening sign-extends a signed
source and zero-extends an unsigned source, regardless of the target
signedness. Narrowing keeps the low bits. A same-width type or signedness
change preserves the bit pattern. These conversions never trap.

`isize` remains distinct from a fixed-width signed type, and `usize` remains
distinct from a fixed-width unsigned type, even when their widths match.
Values do not mix implicitly.

## Aggregates and scoped enums

Structures place each field at the first offset satisfying that field's
target ABI alignment and round the final size to the maximum field alignment.
Union fields all have offset zero; the union size is the aligned maximum field
size. Local aggregate `{}` initialization writes zero to every byte, including
padding. A named structure initializer also begins from an all-zero object,
then writes each named field in source order. Fields may appear once in any
order; omitted fields remain zero. A union initializer names zero or one
field. Nested named initializers follow the same rules.

Member selection computes the base address once. Pointer-member access checks
for null before deriving the field address. Mutability follows the base lvalue
for `.` and the pointer qualification for `->`; a field or element selected
from a returned temporary is immutable. Structures, unions and fixed arrays
may be initialized from or assigned from an lvalue of the exact same type. A
whole-object copy transfers every byte, including padding, and has `memmove`
overlap semantics. Whole memory objects cannot be compared or used by scalar
operators, but exact same-typed objects can be passed and returned by value.

Each aggregate call argument is evaluated from left to right and copied into
an independent snapshot before the call. Mutating or aliasing the original
object afterward cannot change the parameter value. An aggregate return
expression is likewise copied into an exact-layout return snapshot before the
ABI boundary reads it. Aggregate conditional expressions allocate exact-layout
result storage, evaluate exactly one branch and copy that branch into the
result. Returned aggregate temporaries remain valid for the enclosing function
evaluation and support immutable `.` or fixed-array indexing.

A scoped enum has the storage width, alignment and signedness of its explicit
underlying integer type but remains a distinct language type. Enum members are
compile-time values. Equality is defined only for the same enum type; numeric
operators and ordering are rejected. Explicit conversion is permitted only
between an enum and its exact underlying type and preserves the stored bit
pattern.

`sizeof(Type)`, `alignof(Type)` and `offsetof(Type, field)` are compile-time
`usize` values derived from the selected target data layout. They do not
evaluate a run-time expression, access an object or emit a memory operation.
`offsetof` reports the byte offset of an immediate structure field; every
union field has offset zero. Types without a complete object layout, including
`void`, are rejected.

Conversions involving `bool` or `void` are rejected. A conversion to the same
integer type is permitted and has no effect.

## Floating-point numbers

`f32` and `f64` use IEEE-754 binary32 and binary64 representations. Each
arithmetic operation rounds to its declared type using round-to-nearest,
ties-to-even. Subnormal operands and results are preserved; signed zero,
infinities and NaNs follow IEEE-754 scalar arithmetic. Floating-point division
by zero does not trap. The bootstrap entry point masks floating-point
exceptions and explicitly establishes this environment before calling Luna
code.

Unary negation flips the sign bit, including for zero, infinities and NaNs.
The arithmetic operators are `+`, `-`, `*` and `/`. `%`, bitwise operators and
shifts are rejected for floating-point operands.

Equality and ordering use ordered IEEE-754 comparisons. If either operand is
NaN, `==`, `<`, `<=`, `>` and `>=` are false, while `!=` is true. Positive and
negative zero compare equal.

`f32` and `f64` are exact, independent types. They do not mix with each other
or with integer types implicitly.

## Explicit floating-point conversions

Converting `f32` to `f64` is exact. Converting `f64` to `f32` rounds once to
binary32 using round-to-nearest, ties-to-even. A finite value too large for
binary32 becomes an infinity with the source sign. Zeros, infinities and NaNs
otherwise follow IEEE-754 format-conversion behavior.

Converting any signed or unsigned integer to `f32` or `f64` rounds directly to
the destination format using round-to-nearest, ties-to-even. The result is
always finite because every implemented integer range fits within both
floating-point formats' finite exponent range. Signed sources use their
mathematical signed value; unsigned `u64` and `usize` values above `INT64_MAX`
do not pass through a signed interpretation.

Converting `f32` or `f64` to an integer requires the source to be finite and
inside the closed mathematical range of the target type before its fractional
part is discarded toward zero. Therefore `127.0 as i8` is valid, while
`127.5 as i8` traps even though truncating first would produce `127`. NaN,
either infinity and every out-of-range source trap. Positive and negative zero
both convert to integer zero. These rules apply independently to all
fixed-width and target-sized integer types.

## Raw pointers and fixed arrays

Every pointer is a target-width address value. The bootstrap target uses
64-bit pointers. `null` has the all-zero address representation. Pointer
equality compares address bits after semantic checking has established the
same exact pointer type.

Taking an address does not read the referenced object. Dereferencing reads or
writes exactly the target size of the pointee scalar type. A null check occurs
before every raw-pointer dereference and raw-pointer indexing operation; a
failure traps. A pointer-to-`void` value is opaque and cannot participate in
either operation.

Pointer indexing computes the address modulo the target pointer width using a
`usize` index and the target byte size of the pointee. It carries no bounds
metadata. Fixed-array indexing first requires the index to be less than the
declared element count and traps otherwise. Nested arrays apply one check per
dimension.

Fixed arrays are laid out contiguously with no padding between equal element
types. Their alignment is the element alignment. Nested-array size
calculation is checked for overflow. Whole-array zero initialization writes
zero to every byte, including padding should a future aggregate element type
introduce any. Exact same-typed local arrays use the same overlap-safe
whole-object copy semantics as structures and unions.

String literals are immutable module data. Escape decoding happens at compile
time, and one zero byte is appended after the decoded bytes. Repeated equal
literals may share an address; programs cannot depend on whether they do.

`bool` occupies one byte in memory. Typed stores write exactly `0` or `1`;
typed loads map a zero byte to `false` and every non-zero byte to the canonical
`true` value. This keeps values canonical even after an explicit raw-pointer
reinterpretation.

Pointer-to-pointer conversions preserve address bits and cannot remove
read-only qualification. Conversion between a pointer and `usize` also
preserves the target-width bit pattern. These conversions do not validate
that the resulting address refers to a live object.

## Call ABI

On `x86_64-unknown-linux-gnu`, scalar calls use the System V ABI. Integer,
boolean and pointer parameters use `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8` and
`%r9`; `f32` and `f64` parameters independently use `%xmm0` through `%xmm7`.
After one class exhausts its bank, each further parameter of that class uses a
dense eight-byte stack slot. A later parameter of the other class may still
use its remaining registers.

The caller reserves a 16-byte-aligned argument area before the call and
restores it afterward. Internal Luna calls, compiled-module calls and
non-variadic external C calls share this placement. Integer-like scalar
results use `%rax`, and floating results use `%xmm0`.

An aggregate of at most 16 bytes is classified into one or two `INTEGER` or
`SSE` eightbytes. It uses general-purpose and SSE argument registers only if
all required registers are available; otherwise the complete value rolls
back to an aligned stack copy. Small results use `%rax`/`%rdx` and
`%xmm0`/`%xmm1` with independent class indices. Larger or otherwise
`MEMORY`-classified results use a hidden destination pointer in `%rdi`.

Internal Luna calls support structures, unions and fixed arrays by value.
External C calls support structures and unions by value and are checked
against the same System V classifier. Fixed-array parameters and results are
rejected at the external boundary because C function types do not pass arrays
by value. Variadic calls are not part of the bootstrap language.

## Module contracts

An interface and implementation form one compilation module only when their
module names are identical. Interface function declarations are matched before
any body is lowered. Matching is exact for function kind, parameter count and
order, semantic parameter types including every pointer read-only qualifier,
and return type; parameter names may differ. Every ordinary interface function
has one implementation body. An interface `extern fn` remains an external
symbol declaration and cannot also have an implementation declaration.

Interface type definitions are canonical definitions shared with the
implementation. Repeating one in the implementation is a duplicate
declaration. The interface type graph and function signatures are checked
before implementation-private types enter scope, so an interface cannot
silently depend on implementation details. Command-line source order has no
semantic effect.

An interface import is visible while checking both the interface and its
implementation. An implementation-only import is visible only in that
implementation. Only declarations explicitly exported by the imported
interface enter scope, and imports are not transitively visible or re-exported.
Conflicting unqualified imported names and local shadowing of imported names
are rejected.

The executable module is the unique implementation containing `main`. Every
supplied module must be reachable from it through direct imports. Every
imported module has either an interface/implementation source pair or a
validated metadata interface, and the dependency graph must be acyclic.
Dependencies are checked before importers in an order independent of
command-line input order. Compatible declarations of one external C symbol
share that symbol globally; incompatible signatures are rejected.

This contract is compile-time only and adds no run-time initialization or
dispatch. A metadata interface contributes the same canonical public types and
function signatures as source, but its Luna functions are bodyless IR imports.
The final link resolves them against separately compiled module objects.
Imported and exported Luna symbols include the defining interface's metadata
fingerprint, making an object built from a different interface an unresolved
symbol rather than a silently accepted ABI mismatch.

A separately compiled library has exactly one selected implementation root,
has no `main` or `_start`, and accepts dependencies only as metadata. Metadata
records its target, format and language ABI versions, complete interface and
the content fingerprints of direct dependencies. A fingerprint mismatch,
corrupt payload, unsupported version or target mismatch is a compile-time
error before the metadata can affect type checking. Code generation requires
the selected root's own compiled metadata, ensuring its exported object
symbols use the same interface identity consumed by dependents.

## Runtime boundary

Generated programs enter through project-owned `_start` code and use no libc.
The runtime and standard library will call the x86-64 Linux kernel through a
project-owned direct system-call layer. This target-side restriction does not
apply to the hosted C23 bootstrap compiler and does not prohibit an application
from explicitly linking a caller-supplied object through `extern fn`.

## Optimization boundary

The bootstrap compiler performs no optimization. Its verified x86-64 rewrite
uses assigned physical registers and dense spill slots while retaining the
direct correctness-first instruction expansions used by differential tests.
Any future optimizing backend must reproduce this contract exactly.
