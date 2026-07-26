# Bootstrap execution semantics

This document freezes the behavior needed to test the current `i8`, `i16`,
`i32`, `i64`, `u8`, `u16`, `u32`, `u64` and `bool` subset. It is an explicit
execution contract, not an optimization policy.

## Evaluation

Expressions and function arguments are evaluated from left to right. `&&` and
`||` short-circuit. Conditions accept only `bool`; integers never convert to
truth values.

Every statement is name-checked and type-checked, including statements that
cannot be reached at run time. Unreachable code is lowered into detached IR
blocks and cannot affect live control flow.

An integer literal takes the exact integer type required by an enclosing
declaration, return, argument or integer expression. Without an integer
context it defaults to `i32`. Contextual literal typing is not an implicit
conversion: values and variables of different integer types never mix without
an explicit `as` conversion.

## Signed integers

`i8`, `i16`, `i32` and `i64` are 8-bit, 16-bit, 32-bit and 64-bit
two's-complement integers. Addition, subtraction, multiplication and unary
negation wrap modulo the type's power of two. This behavior is defined; there
is no signed-overflow undefined behavior.

Division truncates toward zero. The remainder has the dividend's sign and
satisfies:

```text
left == (left / right) * right + (left % right)
```

Division by zero traps. Dividing the minimum value by `-1` also traps for every
signed width: `-128`, `-32768`, `-2147483648` and
`-9223372036854775808`, respectively. The corresponding remainder operations
trap for the same inputs.

Shift counts use only the low `log2(width)` bits: three for `i8`, four for
`i16`, five for `i32` and six for `i64`. Left shift wraps to the operand type;
right shift is arithmetic and preserves the sign.

Ordering comparisons are signed. Equality is defined separately for each
integer type and `bool`; these types never compare or convert implicitly.

## Unsigned integers

`u8`, `u16`, `u32` and `u64` are 8-bit, 16-bit, 32-bit and 64-bit unsigned
integers. Addition, subtraction, multiplication and unary negation wrap modulo
the type's power of two. Division and remainder use unsigned arithmetic and
trap only when the divisor is zero.

Left shift wraps to the operand type. Right shift is logical and shifts in
zero bits. Shift-count masking follows the same three-, four-, five- and
six-bit rule as the corresponding signed widths. Ordering comparisons are
unsigned.

## Explicit integer conversions

Every pair among `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32` and `u64` can
be converted explicitly. Widening sign-extends a signed source and zero-extends
an unsigned source, regardless of the target signedness. Narrowing keeps the
low bits. A same-width signedness change preserves the bit pattern. These
conversions never trap.

Conversions involving `bool` or `void` are rejected. A conversion to the same
integer type is permitted and has no effect.

## Optimization boundary

The bootstrap compiler performs no optimization. Its stack-homed x86-64
backend is the reference implementation used by differential tests. Any
future optimized backend must reproduce this contract exactly.
