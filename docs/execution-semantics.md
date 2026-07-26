# M0 execution semantics

This document freezes the behavior needed to test the current `i32` and
`bool` subset. It is an explicit execution contract, not an optimization
policy.

## Evaluation

Expressions and function arguments are evaluated from left to right. `&&` and
`||` short-circuit. Conditions accept only `bool`; integers never convert to
truth values.

Every statement is name-checked and type-checked, including statements that
cannot be reached at run time. Unreachable code is lowered into detached IR
blocks and cannot affect live control flow.

## `i32`

`i32` is a 32-bit two's-complement integer. Addition, subtraction,
multiplication and unary negation wrap modulo 2^32. This behavior is defined;
there is no signed-overflow undefined behavior.

Division truncates toward zero. The remainder has the dividend's sign and
satisfies:

```text
left == (left / right) * right + (left % right)
```

Division by zero and `-2147483648 / -1` trap the program. The corresponding
remainder operation traps for the same inputs.

Shift counts use their low five bits, equivalent to `count & 31`. Left shift
wraps to `i32`; right shift is arithmetic and preserves the sign.

Comparisons are signed. Equality is defined separately for `i32` and `bool`;
the two types never compare or convert implicitly.

## Optimization boundary

The bootstrap compiler performs no optimization. Its stack-homed x86-64
backend is the reference implementation used by differential tests. Any
future optimized backend must reproduce this contract exactly.
