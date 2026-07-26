# Bootstrap execution semantics

This document freezes the behavior needed to test the current `i32`, `i64`
and `bool` subset. It is an explicit execution contract, not an optimization
policy.

## Evaluation

Expressions and function arguments are evaluated from left to right. `&&` and
`||` short-circuit. Conditions accept only `bool`; integers never convert to
truth values.

Every statement is name-checked and type-checked, including statements that
cannot be reached at run time. Unreachable code is lowered into detached IR
blocks and cannot affect live control flow.

An integer literal is contextually typed as `i64` when an enclosing
declaration, return, argument or integer expression requires `i64`. Without an
integer context it defaults to `i32`. Contextual literal typing is not an
implicit conversion: values and variables of different integer types never
mix without a future explicit conversion operation.

## Signed integers

`i32` and `i64` are 32-bit and 64-bit two's-complement integers. Addition,
subtraction, multiplication and unary negation wrap modulo 2^32 or 2^64.
This behavior is defined; there is no signed-overflow undefined behavior.

Division truncates toward zero. The remainder has the dividend's sign and
satisfies:

```text
left == (left / right) * right + (left % right)
```

Division by zero traps. Dividing the minimum value by `-1` also traps:
`-2147483648 / -1` for `i32` and `-9223372036854775808 / -1` for `i64`.
The corresponding remainder operations trap for the same inputs.

`i32` shift counts use their low five bits (`count & 31`); `i64` shift counts
use their low six bits (`count & 63`). Left shift wraps to the operand type;
right shift is arithmetic and preserves the sign.

Comparisons are signed. Equality is defined separately for `i32`, `i64` and
`bool`; these types never compare or convert implicitly.

## Optimization boundary

The bootstrap compiler performs no optimization. Its stack-homed x86-64
backend is the reference implementation used by differential tests. Any
future optimized backend must reproduce this contract exactly.
