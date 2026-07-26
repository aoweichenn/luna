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
