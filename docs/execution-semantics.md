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

Every statement is name-checked and type-checked, including statements that
cannot be reached at run time. Unreachable code is lowered into detached IR
blocks and cannot affect live control flow.

`do` executes its body before the first condition test. A `for` statement
executes its initializer once, then repeats condition, body and update in that
order. An omitted condition behaves as `true`; omitted initializer and update
clauses perform no work. The scope introduced by `for` includes all three
clauses and the body.

A switch evaluates its integer controlling expression once and compares it
against case values in source order. It executes exactly one matching case,
the default case when no value matches, or no body when neither exists. Cases
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

## Optimization boundary

The bootstrap compiler performs no optimization. Its stack-homed x86-64
backend is the reference implementation used by differential tests. Any
future optimized backend must reproduce this contract exactly.
