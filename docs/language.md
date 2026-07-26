# Luna 0 language draft

This document records the accepted first-stage surface design. It is broader
than the currently implemented compiler subset; implementation status is
tracked in `roadmap.md`.

## Source units and modules

An interface unit starts with:

```luna
export module compiler.token;
```

An implementation or executable unit starts with:

```luna
module compiler.token;
```

Imports follow the module declaration:

```luna
import core.text;
import compiler.ast;
```

A module has at most one interface and one implementation unit. Only
declarations marked `export` in the interface are visible to importers. Module
names organize visibility and dependencies but do not create a qualification
syntax. Imported names are used directly.

There are no module partitions, header units, re-exports, aliases, selective
imports, wildcard imports or cyclic dependencies in Luna 0.

## Declarations

Types follow names:

```luna
let count: usize = 0;
var cursor: *Token = null;
const BUFFER_SIZE: usize = 4096;
```

All declarations have an explicit type and initializer. `let` bindings cannot
be reassigned; `var` bindings can.

```luna
struct Token {
    kind: TokenKind;
    text: Str;
}

union Number {
    integer: i64;
    real: f64;
}

enum TokenKind: u8 {
    identifier,
    integer,
    end,
}
```

Enums have an explicit underlying integer type, are scoped by their type name
and never convert implicitly to integers.

## Types

```text
bool
i8 i16 i32 i64
u8 u16 u32 u64
isize usize
f32 f64
void
```

`isize` and `usize` have the target pointer width. They remain exact,
independent language types: even when the current target gives them the same
representation as `i64` and `u64`, mixing those types still requires an
explicit conversion.

Composite type syntax:

```luna
value: i32;
pointer: *i32;
readonly: *const i32;
buffer: [256]u8;
matrix: [4][4]f32;
callback: fn(i32, i32) -> i32;
```

Arrays do not decay to pointers. There are no implicit integer promotions,
implicit pointer/integer conversions or truthiness conversions.

## Functions

```luna
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn report(message: *const u8) {
    write_line(message);
}

extern fn allocate(size: usize) -> *void;
```

Parameters are passed by value and their bindings are immutable. Pointer
parameters express mutation explicitly. There is no overloading, default
argument or variadic Luna function.

## Statements

Conditions require `bool`; control-flow bodies require braces.

```luna
if (ready) {
    run();
} else {
    wait();
}

while (running) {
    step();
}

do {
    step();
} while (running);

for (var index: usize = 0; index < count; index += 1) {
    process(index);
}
```

Switch cases are scoped and never fall through:

```luna
switch (token.kind) {
    case TokenKind.identifier, TokenKind.integer {
        parse_literal();
    }

    default {
        report_error();
    }
}
```

The jump statements are `break`, `continue`, `return` and `return value`.
There are no labels or `goto` in Luna 0.

## Expressions

Postfix operations are calls, indexing, value-member access and
pointer-member access:

```luna
function(argument)
array[index]
value.field
pointer->field
```

Unary operators are `+ - ! ~ * &`.

An unsuffixed integer literal takes the integer type required by its
declaration, return, argument or enclosing integer expression. It defaults to
`i32` when no integer context exists. This rule applies only to literals and
does not convert an already typed value.

A decimal floating-point literal has either a decimal point with digits on
both sides or an exponent:

```luna
let single: f32 = 1.25;
let double: f64 = 6.022_140_76e23;
let default_comparison: bool = 1e-9 == 1e-9;
```

Underscores may separate digits within the integer, fractional and exponent
parts. A floating-point literal takes the `f32` or `f64` type required by its
declaration, return, argument or enclosing floating-point expression. It
defaults to `f64` when no floating-point context exists. An `f32` literal is
rounded directly to binary32 rather than first being rounded to binary64.

Binary operators, from tighter to looser groups, are:

```text
* / %
+ -
<< >>
< <= > >=
== !=
&
^
|
&&
||
?: 
```

Explicit conversions use `as`:

```luna
let wide: i64 = small as i64;
let raw: *void = pointer as *void;
```

Integer widening sign-extends a signed source and zero-extends an unsigned
source. Narrowing keeps the low bits; changing signedness at the same width
keeps the bit pattern. Integer conversions never trap. A conversion does not
happen implicitly merely because the destination type could represent the
source value. These rules include `isize` and `usize`; their source or
destination width comes from the selected target data layout.

The implemented floating-point operators are unary `+` and `-`, arithmetic
`+`, `-`, `*` and `/`, and all equality and ordering comparisons. Normal and
compound assignment preserve the exact declared type. There are no implicit
integer/floating-point conversions and no implicit conversion between `f32`
and `f64`.

Every numeric scalar pair supports an explicit conversion. `f32` to `f64` is
exact. `f64` to `f32`, and every integer-to-floating conversion, round to
nearest with ties to even. Floating-to-integer conversion first requires a
finite source value inside the target integer's mathematical range, then
discards the fractional part toward zero. NaN, infinity and an out-of-range
source trap. Positive and negative zero both convert to integer zero.

An explicit conversion supplies contextual literal typing only when the source
and target remain in the same numeric category. Thus `1.0 as f32` rounds the
literal directly to `f32`, while `1 as f64` first creates the default `i32`
literal and then performs an integer-to-floating conversion. Likewise,
`1.0 as i32` converts the default `f64` literal. This preserves the rule that
integer and floating literals are distinct syntax categories.

Assignment and compound assignment are statements, not expressions. There are
no increment, decrement or comma operators.

## Initialization

```luna
var buffer: [256]u8 = {};

let point: Point = {
    x = 10.0,
    y = 20.0,
};

return Point {
    x = 10.0,
    y = 20.0,
};
```

`{}` is explicit zero initialization. Local variables are never implicitly
uninitialized.

## Compile-time surface

The only compile-time declarations in Luna 0 are typed constants and enum
values. There is no preprocessor, textual macro system, conditional
compilation, generic selection, type reflection or user-defined attribute
syntax.
