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

An interface contains complete structure, union and enum definitions plus
function declarations without bodies. Its type definitions are directly
available in the matching implementation and are not repeated there. Every
non-`extern` interface function must have exactly one definition in the
implementation. The function name, parameter count and order, every parameter
type and pointer qualifier, and the return type must match exactly. Parameter
names do not form part of the contract. An `extern fn` in the interface names
an external C symbol and therefore has no Luna definition.

An implementation unit cannot contain `export`. Functions omitted from the
interface are module-private and need no prior declaration. Interface types
and function signatures must resolve using the interface itself; they cannot
depend on a type declared only in the implementation.

An import written in an interface is visible to both units of that module. An
implementation may add imports that remain private to the implementation.
Imports are direct and non-transitive: importing one module does not expose
that module's imports. An exported function or aggregate type cannot expose a
module-private named type in its public signature or fields.

Imported declarations are used by their unqualified names. Importing two
exported declarations with the same name is an error, and a local declaration
cannot shadow an imported declaration. Luna 0 deliberately has no
qualification or overload-resolution rule to hide such ambiguity.

An executable compilation receives the root plus every transitive dependency,
in any order. A dependency may be supplied as its interface/implementation
source pair or as compiled `.lmi` interface metadata. Exactly one source
implementation must contain `main`; it is the executable root. Every supplied
module must be reachable from the root, and the import graph must be acyclic.
Unknown, self, repeated and cyclic imports are rejected before type checking.
A metadata-only dependency contributes declarations but no function bodies;
its separately compiled object must be supplied to the final link.

`--compile-module name` compiles exactly one non-executable implementation and
does not generate `_start`. `--emit metadata` validates the selected source
interface and implementation before writing the `.lmi`; subsequent typed IR,
x86-64 machine IR or assembly emission requires that exact `.lmi` as the
selected module's interface. All imported dependencies must also be `.lmi`,
and passing another module's implementation source is rejected.

Compiled metadata is target-specific, deterministic and versioned. It records
the language ABI, target triple, complete interface declarations and direct
imports. Every direct import carries the content fingerprint of the exact
dependency metadata used when it was built. A stale dependency, corrupt or
truncated file, unsupported format, incompatible language ABI or wrong target
is rejected before semantic lowering. The content fingerprint is a build
consistency check rather than an authenticity signature. Luna import/export
symbols carry the interface fingerprint, so the final linker also rejects an
object compiled from different metadata.

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
and never convert implicitly to integers. Members start at zero and increase
by one unless an integer literal with an optional sign is written explicitly:

```luna
enum Status: i8 {
    unavailable = -1,
    idle,
    running = 7,
}
```

The explicit value and every implicit successor must fit the underlying type.
An enum converts explicitly only to and from its exact underlying integer
type. Distinct enum types remain incompatible even when their underlying
types and values match.

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

In the bootstrap memory milestone, a pointer qualifier applies to writes
through that pointer: `*T` permits reads and writes, while `*const T` permits
only reads. Taking the address of a mutable lvalue produces `*T`; taking the
address of an immutable lvalue produces `*const T`. `*void` and `*const void`
are opaque pointer types and cannot be dereferenced or indexed.

Fixed-array lengths are positive integer literals. Arrays may be nested and
their total target layout must fit in the compiler's supported object-size
range. They are local storage objects in this milestone: arrays cannot be
passed or returned by value or used as scalar expressions. A local array may
be initialized from or assigned from an lvalue of the exact same array type.
Elements remain ordinary lvalues and may be read, written or addressed.

Structures use target ABI alignment with padding before fields and at the end
of the object. Union fields all start at offset zero; the union size is the
aligned size of its largest field. A type may contain a pointer to itself or
participate in mutually recursive pointer graphs, but a recursive by-value
field is rejected because it has no finite layout.

The target layout can be queried with type-only compile-time expressions:

```luna
let bytes: usize = sizeof(Packet);
let alignment: usize = alignof(Packet);
let payload_offset: usize = offsetof(Packet, payload);
```

All three expressions have type `usize` and are replaced by constants during
semantic lowering. `sizeof` and `alignof` accept any type with a complete
target layout. `offsetof` accepts a structure or union type and the name of
one of its immediate fields. These forms take a type, never a run-time
expression; `sizeof(value)` is intentionally not supported.

Local structures and unions support context-directed named initialization.
Fields may appear once in any order; omitted fields and all padding bytes are
zero. A union initializer may name at most one field. Nested records use the
same syntax, and a memory field may be initialized from an lvalue of its exact
type. Structures, unions and arrays may be copied between exact same-typed
local lvalues. They still cannot be compared, passed or returned by value
until aggregate ABI classification is implemented. Reading a union field
interprets the bytes currently in the overlapping storage; Luna does not track
an active union member.

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

An `extern fn` is a declaration of one function implemented outside Luna. It
has no body, must end in `;`, and names the exact C/ELF symbol to call:

```luna
extern fn hash(bytes: *const u8, length: usize) -> u64;
```

The current target uses the x86-64 System V C ABI. External declarations may
use `void` as the return type and may otherwise pass and return only scalar or
pointer types. The bootstrap backend assigns the first six integer-class
arguments and first eight floating-point arguments to their independent
System V register banks, then passes further scalar arguments on the stack.
Aggregate arguments, aggregate results and variadic calls remain deferred.
Luna does not implicitly link a C runtime or any library—the final linker
invocation must supply an object or library that defines every referenced
external symbol.

For `x86_64-unknown-linux-gnu`, the interoperable C23 spellings are:

| Luna | C23 |
| --- | --- |
| `bool` | `_Bool` |
| `i8`, `u8` | `signed char`, `unsigned char` |
| `i16`, `u16` | `short`, `unsigned short` |
| `i32`, `u32` | `int`, `unsigned int` |
| `i64`, `u64` | `long long`, `unsigned long long` |
| `isize`, `usize` | `long`, `unsigned long` |
| `f32`, `f64` | `float`, `double` |
| `*T`, `*const T` | the corresponding C pointer type |

The names `_start` and every name beginning with `_L` are reserved at this
boundary for the bootstrap runtime and Luna's internal symbol encoding.

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

The three `for` clauses are independently optional. The initializer is either
a `let`/`var` declaration, an assignment statement or an expression
statement. The update is an assignment or expression statement. The
initializer declaration is scoped across the condition, update and body, and
is not visible after the loop. `continue` transfers to the condition in
`while` and `do` loops, and to the update clause in a `for` loop before the
condition is tested again.

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

The controlling expression is evaluated exactly once. It must have an integer
or scoped enum type. An integer `case` label is an integer literal with an
optional unary `+` or `-` and is contextually interpreted as the controlling
integer type. An enum `case` label must be a member of that exact controlling
enum. Duplicate values after interpretation are rejected, including spellings
such as `-1` and `255` in a `u8` switch. There may be at most one `default`,
and it may appear anywhere among the cases.

The jump statements are `break`, `continue`, `return` and `return value`.
`break` exits the innermost loop or switch. `continue` targets the innermost
enclosing loop even when it appears inside a switch nested in that loop. There
are no labels or `goto` in Luna 0.

## Expressions

Postfix operations are calls, indexing, value-member access and
pointer-member access:

```luna
function(argument)
array[index]
value.field
pointer->field
```

`.` requires a structure or union lvalue. `->` requires a pointer to a
structure or union, checks that the pointer is non-null, and preserves the
pointer's read-only qualification for the selected field. Member operations
may be chained with indexing. `Enum.member` is the separate scoped-enum
constant form and never denotes an assignable lvalue.

Unary operators are `+ - ! ~ * &`.

Indexing a fixed array performs a checked `usize` bounds test. Indexing a raw
pointer accepts `usize` and has no bounds information. In both cases the
address is scaled by the target size of the element type. Dereferencing or
indexing a null pointer traps. Other invalid raw addresses retain the target
machine's fault behavior.

`null` is a context-dependent pointer literal and has no standalone default
type. Pointer equality requires the same exact pointer type. Pointer ordering
and implicit pointer arithmetic are not part of Luna 0.

A string literal has type `*const u8` and points at immutable static bytes
followed by one terminating zero byte. The supported escapes are `\\`, `\"`,
`\n`, `\r`, `\t`, `\0` and `\xHH` with exactly two hexadecimal digits.
Source text is otherwise preserved as UTF-8 bytes. String literals are never
writable and adjacent literal concatenation is not implicit.

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

The conditional operator is right-associative. Its condition must be `bool`;
its two result operands must have the same exact non-`void` scalar type, and
only the selected operand is evaluated. The surrounding expected type
contextually types literals in both result operands, but it never converts an
already typed value.

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

Explicit raw-pointer conversions preserve the address bits. A pointer may be
converted to another pointer type when the conversion does not remove
read-only qualification. A mutable pointer may therefore become read-only,
but a read-only pointer can never become writable. Pointer values convert to
and from `usize`; no other integer type participates in pointer conversion.
Pointer conversions never happen implicitly. Read-only qualification is
checked through nested pointer and fixed-array layers, so an intermediate
`*void` or differently shaped pointee cannot erase it.

Assignment and compound assignment are statements, not expressions. There are
no increment, decrement or comma operators.

## Initialization

```luna
var buffer: [256]u8 = {};

let point: Point = {
    x = 10.0,
    y = 20.0,
};

var copy: Point = point;
copy = { y = 30.0 };
```

`{}` is explicit zero initialization. A named initializer is interpreted from
the destination type; it does not repeat the type name. Field expressions are
evaluated from top to bottom. Every omitted field and every padding byte is
zero, so initialization never leaves an indeterminate object representation.
Local variables are never implicitly uninitialized.

The same braces can appear on the right of whole-object assignment. Copy
initialization and assignment require an lvalue of the exact same structure,
union or array type; there are no structural conversions. Aggregate
parameters, returns and the typed `Point { ... }` value form remain deferred
to the aggregate ABI milestone.

For an array, `{}` initializes every byte of the object to zero. Array
elements may be assigned through indexing, and exact same-typed arrays may be
copied locally:

```luna
var values: [4]i32 = {};
values[0] = 42;
var copy: [4]i32 = values;
let first: *i32 = &values[0];
```

## Compile-time surface

The compile-time surface in Luna 0 consists of typed constants, enum values
and the type-only `sizeof`, `alignof` and `offsetof` layout queries. There is
no preprocessor, textual macro system, conditional compilation, generic
selection, general type reflection or user-defined attribute syntax.
