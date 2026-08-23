# Luna language

This document records the accepted surface design. Luna 0 is the frozen C23
bootstrap language; Luna 1 and later versions are owned by the self-hosted
compiler. The authority and upgrade rules are defined in
`bootstrap-language-versions.md`. Implementation status is tracked in
`roadmap.md`.

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
x86-64 machine IR, assembly or object emission requires that exact `.lmi` as the
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

## Attributes

Declarations may carry attributes in the `@` namespace:

```luna
@inline fn clamp(value: i32, low: i32, high: i32) -> i32 {
    return value < low ? low : value > high ? high : value;
}
```

An attribute is `@name` or `@name(argument-list)`, written after any
`export`/`extern` modifiers and before the declaration keyword. Attributes
mount on module-scope declarations, structure and union fields, and local
variable declarations. Every attribute has a fixed set of legal mount
points and a fixed argument shape; an unknown name, an illegal mount or an
unexpected argument list is a compile-time error.

`@inline` mounts on function declarations and takes no arguments. It
records an inlining hint on the semantic function record. Nothing consumes
the hint yet; it is metadata for a future optimizer.

`@noreturn` mounts on function declarations and takes no arguments. It
declares that the function never returns to its caller:

```luna
@noreturn fn fail(message: *const u8) {
    report(message);
    runtime_exit(1);
}
```

A `return` statement inside a `@noreturn` body is a compile-time error, as
is a body whose end is reachable; an infinite `loop` or a tail call to
another `@noreturn` function satisfies the contract. A direct call to a
`@noreturn` function ends the current control-flow block without a
successor: subsequent statements are unreachable, and a function whose
remaining path only calls a `@noreturn` function is not required to
`return`, even with a non-`void` result type. The attribute is not part of
any function-pointer type, so indirect calls carry no such knowledge.

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

The second pointer qualifier is `volatile`, which composes with `const` in
either order: `*volatile T`, `*const volatile T`. Every access through a
volatile-qualified pointer is a real memory access — never elided, coalesced
or reordered against another volatile access. A pointer may gain `volatile`
through an explicit `as`, just as it may gain `const`; dropping either
qualifier is rejected. Taking the address of a volatile lvalue produces
`*volatile T` (`*const volatile T` when the lvalue is also immutable), and
member access and indexing propagate the pointee's qualification to the
selected object.

Local object declarations carry the same qualification:

```luna
volatile var status: u32 = 0;        // every read and write is real
volatile let config: u32 = 3;        // real loads, no writes
```

A volatile object behaves like any other binding of its type, except that
every source-level access is guaranteed to reach memory exactly as written:
a read is a load, a write is a store, and a compound assignment is exactly
one load followed by one store. The qualification belongs to the
declaration, not to the type — `status` above has type `u32` — and it is
recorded on the variable's storage slot so that any future optimizer must
honor it.


A function-pointer type names a function signature as a first-class
scalar type:

```luna
type Binary = fn(i32, i32) -> i32;

struct Handler {
    tag: u8;
    op: Binary;
}
```

A function pointer has target pointer width and belongs to the System V
INTEGER class at the ABI boundary. It can be stored in structure, union
and array elements, passed as a parameter, returned from a function and
reassigned between values of the same exact type. `null` is a valid
value of every function-pointer type. There are no closures: a function
pointer carries a code address only, never an environment. Distinct
function-pointer shapes are distinct types with no implicit conversion
between them.

Fixed-array lengths are positive integer literals. Arrays may be nested and
their total target layout must fit in the compiler's supported object-size
range. Arrays remain memory objects rather than scalar expressions, but Luna
functions may pass and return them by value. A local array may be initialized
from or assigned from an lvalue of the exact same array type. Elements remain
ordinary lvalues and may be read, written or addressed.

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
local lvalues and may be passed or returned by value. Aggregate call arguments
are snapshotted in source order, and returned temporaries support immutable
member or array-element access. Aggregate conditional expressions require one
exact result type and evaluate only the selected branch. Aggregates still
cannot be compared or used with scalar operators. Reading a union field
interprets the bytes currently in the overlapping storage; Luna does not
track an active union member.

A module-scope `type` declaration gives an existing type a transparent
alias:

```luna
type Size = usize;
type Handle = i32;
type Chain = Size;
type Pixels = [4]Size;
type Cursor = *Size;
type Callback = fn(i32, i32) -> i32;
```

The alias names exactly its target: every occurrence resolves to the
target type during type checking, so an alias is interchangeable with
its target in declarations, assignments, explicit conversions, exact
type matching and the `sizeof`, `alignof` and `offsetof` layout
queries. Alias chains are allowed and resolve through to a non-alias
target; an alias whose resolution reaches itself, directly or through a
chain, is rejected as a recursive type. The target must be a complete
non-`void` type.

Aliases follow the same module visibility rules as structure, union and
enum declarations: an alias written in an interface unit may be marked
`export`, and an exported alias must resolve to a type that is itself
valid in a public signature. An alias may appear in every type
position, including pointee, array element, field, parameter, return
and function-pointer positions.

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

An executable entry point has one of exactly two signatures:

```luna
fn main() -> i32;
fn main(argc: usize, argv: **const u8) -> i32;
```

For the command-line form, `argc` is the kernel-provided argument count,
`argv[0]` through `argv[argc - 1]` are non-null NUL-terminated byte strings,
and `argv[argc]` is null. The pointed-to command-line bytes are read-only.
The x86-64 `_start` shim reads both values from the initial process stack
before aligning that stack, passes them with the System V integer argument
registers, and terminates the process with the returned `i32`.

An `extern fn` is a declaration of one function implemented outside Luna. It
has no body, must end in `;`, and names the exact C/ELF symbol to call:

```luna
extern fn hash(bytes: *const u8, length: usize) -> u64;
```

The current target uses the x86-64 System V C ABI. External declarations may
use `void` as the return type and may pass and return scalars, pointers,
structures or unions. The backend classifies aggregates into System V
eightbytes, performs whole-value register rollback, stack copies,
multi-register results and hidden-pointer results. Fixed arrays are rejected
at this boundary because C function types do not pass arrays by value.
Variadic calls remain deferred. Luna does not implicitly link a C runtime or
any library—the final linker invocation must supply an object or library that
defines every referenced
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

The target sysroot provides the `luna.linux.syscall` module. Its seven
external declarations accept a system-call number plus zero to six `usize`
argument bit patterns and return the raw `isize` Linux result. `lunalink`
provides their project-owned direct-`syscall` definitions; caller objects may
not override those symbols. This is a target ABI boundary rather than a
general language intrinsic or high-level I/O interface.

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

loop {
    step();
    if (finished) {
        break;
    }
}

do {
    step();
} while (running);

for (var index: usize = 0; index < count; index += 1) {
    process(index);
}
```

`loop` is a Luna 1 unconditional loop and requires a block body. `continue`
restarts the body and `break` exits the loop. A `loop` with no reachable
`break` has no reachable successor.

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
type. Pointer equality requires the same exact pointer type.

Pointer arithmetic advances in units of the pointee size, not bytes:

```luna
cursor += 1;
let finish: *i32 = begin + (4 as usize);
let next: *i32 = (1 as usize) + begin;
let remaining: usize = end - start;
if (start < end) {
    ...
}
```

For a pointer `p` to a complete, non-array element type, `p + n`, `n + p`
and `p - n` yield the same pointer type as `p`, offset by `n` elements.
In these expression forms the count `n` must already have type `usize`;
a bare integer literal is not contextually typed here and needs an
explicit `as usize`. The compound assignments `p += n` and `p -= n`
equally require a `usize` count but do contextually type a bare integer
literal as `usize`, so `cursor += 1` is accepted. Pointer arithmetic
does not apply to `*void`, `*const void` or pointers to arrays.

`p - q` requires the same exact pointer type on both sides and yields
the element distance between the two addresses as `usize`. The ordering
operators `<`, `<=`, `>` and `>=` also require the same exact pointer
type and compare the address values as unsigned integers; equality keeps
its existing same-exact-type rule.

Address computation wraps modulo 2^64. Dereferencing or indexing through
an invalid address retains the target machine's fault behavior, the null
check still precedes every dereference and index, and arithmetic on a
raw pointer implies no bounds knowledge.

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

A function name in a value position, or the explicit `&name` form,
yields a pointer to that function with its exact signature type:

```luna
var op: fn(i32, i32) -> i32 = add;
let referenced: fn(i32, i32) -> i32 = &mul;
let five: i32 = op(2, 3);
```

A call whose callee is a function-pointer value is indirect and checks
the value against null first; calling a null function pointer traps,
mirroring the null-pointer dereference rule. The equality operators
`==` and `!=` accept `null` or another value of the same exact
function-pointer type; ordering comparisons are not defined on function
pointers. Explicit `as` conversions preserve the address bits between
any two function-pointer shapes and between a function pointer and
`usize`; no other conversions involve function-pointer types.

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
parameters and returns use the same context-directed braces, exact lvalues,
calls or aggregate conditional expressions. Luna intentionally does not add a
redundant typed `Point { ... }` expression form.

For an array, `{}` initializes every byte of the object to zero. Array
elements may be assigned through indexing, and exact same-typed arrays may be
copied locally:

```luna
var values: [4]i32 = {};
values[0] = 42;
var copy: [4]i32 = values;
let first: *i32 = &values[0];
```

## Intrinsics

Call-shaped intrinsics live in the same `@` namespace as declaration
attributes, but appear in expression position. Each has a fixed arity and
typed operand rules; an unknown name, wrong arity or wrong operand class
is a compile-time error.

Bit operations accept any integer scalar and return its type:

```luna
let highest: u32 = @clz(flags);              // 0..32; @clz(0) is 32
let lowest: u64 = @ctz(mask);                // 0..64; @ctz(0) is 64
let bits: u32 = @popcount(mask);
let rolled: u32 = @rotate_left(value, 3);    // usize count, masked mod width
let back: u32 = @rotate_right(value, 3);
let swapped: u64 = @byte_swap(raw);          // widths of 16 bits and up
```

`@clz` and `@ctz` count from the operand's own width, so `@clz(0)` and
`@ctz(0)` yield that width rather than trapping. `@byte_swap` reverses the
byte order of operands of 16 bits or more. All of these are defined for
every input bit pattern.

## Compile-time surface

The compile-time surface in Luna 0 consists of typed constants, enum values
and the type-only `sizeof`, `alignof` and `offsetof` layout queries. There is
no preprocessor, textual macro system, conditional compilation, generic
selection, general type reflection or user-defined attribute syntax.

`assert(expression);` is a compile-time assertion, legal both at module
scope and as a function-body statement:

```luna
assert(sizeof(Packet) == 64);
assert(offsetof(Packet, checksum) == 60 && PacketKind.data == 1);
```

The expression is evaluated by the constant evaluator during semantic
analysis. A constant true assertion vanishes without emitting code; a
constant false one fails compilation, as does an expression the evaluator
cannot fold. The evaluator walks the syntax tree without emitting IR and
supports integer and boolean literals, enum member references, unary
`- ~ !`, wrapping integer arithmetic and bitwise operators, signed
comparisons, short-circuit `&&` and `||` on boolean operands, the
conditional operator, integer `as` casts and the three layout queries.
Arithmetic works on 64-bit two's-complement values; division or remainder
by zero is an evaluation error. Everything else — variables, calls,
indexing, floating-point, strings — is not constant.
