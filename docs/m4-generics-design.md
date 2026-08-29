# M4 native generics design

## Status and purpose

M4 is reserved for Luna's native generic foundation. It follows the completed
M2 callable infrastructure and M3 object model, and precedes automatic object
lifetime, copy/move hooks and destruction. The older uppercase M4 in
`roadmap.md` is the archived Luna 0 self-hosting milestone and is unrelated to
this phase.

The language and compiler implementation are complete behind the 409-case
suite and a byte-identical fixed point. The checked-in anchor is the promoted
M4 toolchain, so compiler sources may adopt generic declarations under the
normal fixed-point discipline.

M4 provides static, zero-runtime-cost generic functions and nominal generic
types. It is designed for reusable containers, algorithms and standard-library
utilities, including the later ordinary implementation of
`luna.std.utility::move` once reference types exist.

M4 is not a reconstruction of C++ templates. In particular, it has no textual
instantiation, specialization, SFINAE, argument-dependent lookup, dependent
name lookup or compile-time template language.

## Design principles

1. **One concrete program after substitution.** Every used generic declaration
   is instantiated with exact concrete type arguments before Typed IR is
   emitted. The backend and System V ABI see only existing concrete Luna types.
2. **No runtime generic representation.** There are no dictionaries, boxed
   values, hidden type descriptors, dynamic dispatch or generic IR opcodes.
3. **Hard selected-instance diagnostics.** Non-dependent names bind in the
   definition environment; dependent operations are checked only after exact
   substitution. An invalid selected instance is a hard diagnostic, never a
   candidate-discarding rule.
4. **Exact inference.** Type inference is structural and exact. It performs no
   numeric, pointer, inheritance or user-defined conversion and never ranks
   approximate matches.
5. **Canonical instances.** Declaration identity and concrete type arguments,
   not discovery order or transient IDs, identify an instance.
6. **Modules remain the namespace and visibility boundary.** Generic lookup
   uses the lexical definition environment. Instantiation does not add ADL,
   friend injection or transitive import visibility.
7. **Small first-class surface.** Generic syntax composes with existing
   declarations instead of introducing a separate template language.

## Surface syntax

The core grammar additions are:

```text
TypeParameters ::= "<" Identifier { "," Identifier } ">"
TypeArguments  ::= "<" Type { "," Type } ">"
GenericType    ::= QualifiedIdentifier TypeArguments
GenericName    ::= QualifiedIdentifier TypeArguments
GenericImpl    ::= "impl" QualifiedIdentifier "{" MethodDefinition* "}"
```

Type parameters follow the name of the declaration they parameterize. The use
site keeps C++'s familiar `name<T>` form without adding `template`, `typename`
or a second generic keyword. Module qualification remains Luna's existing
`utility::name<T>` form. Methods of a generic class use the owner's parameters
and retain ordinary calls such as `cell.get()`; M4 does not add method-local
type parameters.

### Generic declarations

Type parameters follow the declared name:

```luna
export fn identity<T>(value: T) -> T {
    return value;
}

export struct Pair<First, Second> {
    first: First;
    second: Second;
}

export union Storage<Value> {
    value: Value;
    bytes: [32]u8;
}

export type Callback<Value, Result> = fn(Value) -> Result;
```

Generic classes use the same parameter list:

```luna
export class Cell<Value> {
    priv value: Value;

    pub init(value: Value);
    pub fn get() const -> Value;
    pub fn replace(value: Value) -> Value;
}

impl Cell {
    init(value: Value)
        : value = value {
    }

    fn get() const -> Value {
        return this->value;
    }

    fn replace(value: Value) -> Value {
        let previous: Value = this->value;
        this->value = value;
        return previous;
    }
}
```

`impl Cell` reopens the generic owner and brings its declared type parameters
into scope; Luna has no specialization-specific `impl`. A type parameter is a
type-only binding and cannot be used as a value.

### Type use and calls

Concrete generic types always spell every type argument:

```luna
let pair: Pair<i32, bool> = {first = 42, second = true};
var cell: Cell<i32> = Cell<i32>(42);
let callback: Callback<i32, bool> = is_positive;
```

Luna does not perform class-template argument deduction. A constructor call
such as `Cell(42)` is invalid because `Cell` names a generic declaration, not a
type. Both type and expression positions use the ordinary `Cell<i32>` generic
specialization form.

Generic function calls normally infer arguments from supplied value
arguments:

```luna
let answer: i32 = identity(42);
```

Explicit type arguments use the C++ spelling directly after the name:

```luna
let wide: i64 = identity<i64>(42);
let other: i64 = utility::identity<i64>(42);
```

Type arguments are all explicit or all inferred in M4. Partial explicit
argument lists and default type arguments are not supported.

In a type position or directly after a declaration name, `<...>` is
unconditionally generic. In an expression, a side-effect-free syntax probe
recognizes a generic ID only
when a name/member plus a valid type-argument list is followed by a call or
member-access postfix. A context-typed `&name` may infer a generic function
from the function pointer's parameter types. If the bounded call probe fails,
the ordinary comparison grammar consumes `<` and `>` instead. No semantic
lookup or dependent parsing enters the parser.

The type-argument cursor treats a `shift_right` token as two closing `>`
delimiters only while it owns nested generic contexts. It records the remaining
close delimiter without mutating the token buffer; ordinary expression parsing
continues to see `>>` as one shift operator.

### Methods of generic classes

Constructors and ordinary methods use their owner's type parameters directly:

```luna
export class Converter<Target> {
    pub init(value: Target);
    pub fn retain(value: Target) -> Target;
}

impl Converter {
    init(value: Target) {
    }

    fn retain(value: Target) -> Target {
        return value;
    }
}
```

M4 does not let a method introduce another type-parameter list. That separate
feature needs owner-parameter plus method-parameter inference, declaration/body
matching and bound-method selection; it is not required by containers or the
later `utility::move`.

## Declaration model

### Parameters and scope

Type parameters:

- are ordered and identified canonically by zero-based declaration position;
- have ordinary identifier spelling only for source lookup and diagnostics;
- may not duplicate another parameter or shadow an owner declaration's type
  parameter;
- are visible in the declaration signature, body and nested declarations;
- are not module exports and cannot be qualified through `::`;
- may be used recursively inside pointers, arrays, function types and generic
  specializations.

Renaming `T` to `Value` does not change declaration identity. Reordering
parameters does change identity whenever their positions occur differently in
the declaration.

M4 accepts type parameters on:

- free functions;
- structures, unions, classes and transparent type aliases;
- class-level parameters used by constructors and ordinary direct methods.

M4 rejects them on:

- enums, because current enums have no payload and therefore no generic use;
- `extern fn`, `asm fn`, variadic functions and `@export_name` declarations;
- virtual, abstract or override methods;
- module declarations, imports, fields and local declarations.

Non-type parameters, parameter packs and higher-kinded parameters are outside
M4.

### Generic patterns versus concrete types

A type containing a type parameter is a generic pattern, not a runtime type.
It has no size, alignment, ABI class, vtable, RTTI descriptor or IR type ID.

Substituting every parameter produces an ordinary concrete Luna type. Layout,
class validity and ABI classification then reuse the existing implementation
without a generic special case. For example:

```text
Pair<T, *T> + [T = i32] -> Pair<i32, *i32>
```

The type argument itself may be any visible Luna type. Existing use-site rules
still apply after substitution: `void` cannot become a stored field, an
incomplete opaque class cannot be used by value outside its defining module,
an abstract class cannot be instantiated, and recursive value layout remains
invalid.

## Exact inference

Inference unifies each provided argument type with the corresponding parameter
pattern. It recursively handles:

- exact built-in and nominal types;
- transparent aliases after resolution;
- pointer qualification and pointee patterns;
- fixed array counts and element patterns;
- function-pointer parameter and result patterns;
- concrete generic declaration identity and nested arguments.

Every occurrence of one type parameter must infer the same canonical type.
Conflicting occurrences reject the candidate. Qualifiers and fixed array
counts are never discarded.

Inference uses only provided call arguments. It does not use:

- the expected result type;
- a return statement's destination;
- omitted default arguments;
- integer or floating conversion;
- base/derived relationships;
- user-defined constructors or operators.

An uncontextualized integer or floating literal first receives Luna's existing
default type (`i32` or `f64`) and can then infer a parameter. `null` cannot
infer a pointee type; the caller must supply explicit type arguments or a
typed pointer expression.

Examples:

```luna
fn first<T>(values: *const T, fallback: T) -> T;
fn from_pointer<T>(value: *const T) -> T;

let a: i32 = first(&values[0], 0);          // T 推导为 i32。
let b: i64 = first<i64>(&wide[0], 0);       // 整数字面量获得 i64 上下文。
let c: i32 = first(null, 0);                // fallback 独立确定 T 为 i32。
let d: i32 = from_pointer(null);             // 错误：null 无法独立确定 T。
```

Inference collects constraints from all provided arguments before validating
context-dependent literals such as `null`; argument order therefore cannot
change the result.

Generic type construction never participates in this inference rule; Luna has
no CTAD.

## Overload integration

M4 extends the M2 exact overload resolver without adding a conversion score:

1. Probe ordinary overloads with the existing exact rules.
2. If multiple ordinary overloads are viable, report `ambiguous_call`.
3. If exactly one ordinary overload is viable, select it and do not consider
   inferred generic overloads.
4. Only when no ordinary overload is viable, infer every generic candidate
   independently.
5. Select exactly one successfully inferred generic pattern.
6. Zero survivors are `no_matching_overload`; multiple survivors are
   `ambiguous_call`.

This is a two-tier exact fallback, not C++ overload ranking. Luna performs no
generic-pattern partial ordering. For a pointer argument, `fn use<T>(T)` and
`fn use<T>(*T)` both match and are ambiguous; the API must use distinct names
or the caller must select a declaration whose explicit type arguments make the
set unique.

Explicit `<...>` calls consider only generic declarations of the requested
arity. Return type remains outside overload identity and selection. Defaults
are inserted only after one concrete callable instance has been selected, as
in M2.

Expected function-pointer and bound-method types may infer a generic callable
only from their complete parameter types. The expected result validates the
selected concrete function type but does not independently infer a missing
type parameter.

An instance-body error occurs after overload selection and is a hard error.
Luna never silently discards it and retries another overload; there is no
SFINAE.

## Definition checking

Generic declarations are parsed, registered and structurally checked once. A
selected concrete instance is checked in the declaration's lexical definition
environment; instantiation never performs unqualified lookup in the caller.

Non-dependent module and declaration names therefore have definition-site
meaning. Operations whose operands depend on `T` are checked against the
selected concrete types during instantiation. Failure is reported at the call
selected concrete types during instantiation. Failure remains attached to the
selected specialization; it does not remove the candidate or retry overload
resolution.

A generic body may call another known generic declaration with explicit
symbolic type arguments:

```luna
fn second<T>(value: T) -> T {
    return identity<T>(value);
}
```

Non-dependent module and function names resolve against the definition unit.
Lookup never searches declarations from the instantiating module. User-defined
capability constraints or concepts are a later, separate design; they will
extend the operations admitted on `T` without changing M4 concrete
substitution or runtime representation.

## Module and source model

### Why exported bodies must be available

The current pure-Luna compiler builds a module from its implementation units
plus dependency interfaces, then links already compiled dependency objects.
A caller therefore cannot instantiate an exported generic whose definition is
present only in a dependency `.la` file.

M4 makes this source boundary explicit:

- an exported generic function definition has its body in the module
  interface;
- the bodies of an exported generic class specialization live in its
  `impl ClassName` blocks in that interface;
- a private generic declaration may live in an implementation unit and may be
  instantiated only while compiling that module;
- a non-generic exported declaration retains the existing bodyless-interface
  plus implementation-definition split.

This is the only interface-body exception. It exposes the semantic definition
needed for static instantiation but does not make private names accessible.
An exported generic body may reference declarations visible from its interface
and its direct imports; it cannot depend on an implementation-only helper.

A future compiled generic-IR module format may replace source-body consumption
without changing Luna source syntax or semantics. M4 does not revive the
historical `.lmi` format or make filesystem paths part of type identity.

### Instance ownership

Each consuming module owns one private emitted copy of every concrete generic
callable instance it uses. All calls within that module share the copy.
Different modules may contain identical private instances; M4 requires no ELF
COMDAT, weak symbol or linker deduplication support.

This rule preserves independent module compilation and the current static
linker. It may increase code size, but it has no runtime indirection. Optional
deduplication is an optimization and cannot affect program semantics or
canonical public identity.

Concrete generic type specializations have the same canonical identity in all
modules. They may therefore appear in ordinary exported Luna signatures, and
their ABI is recomputed deterministically from the same concrete fields.

## Canonical identity

Generic identity uses a versioned, length-delimited encoding and never relies
on a fixed-width hash.

A declaration key contains:

```text
canonical declaring module
declaration kind and source name
owner generic declaration when present
ordered type-parameter count
callable receiver/kind/ABI flags when present
signature or field patterns encoded with parameter-position references
```

A concrete instance key contains:

```text
generic declaration key
ordered canonical concrete type arguments
```

Ordinary non-generic canonical encodings and symbols remain byte-for-byte
unchanged. A generic callable instance includes its generic origin and
arguments even when its substituted function type equals an ordinary
overload, so explicit generic selection cannot collide at link time.

Transparent aliases normalize before entering an instance key. Named types
retain module-qualified nominal identity. A generic type specialization is
nominal by generic declaration plus arguments; equal layout does not make
`Pair<i32, bool>` equal to another module's structurally identical type.

## Instantiation engine

Instantiation is a compiler state machine, not recursive ad-hoc lowering:

```text
requested -> planning -> planned -> lowering -> complete
                       \-> failed
```

The instance manager owns one record per canonical key. A substitution is an
immutable view from parameter positions to concrete type IDs; all instances
share the original syntax tree instead of cloning bodies or constructing
specialized ASTs. Planning resolves the concrete signature and walks the body,
validates concrete type uses and discovers nested generic requests without
emitting IR. After the reachable instance graph is closed, canonical key order
assigns concrete type/function/IR IDs; lowering then emits ordinary concrete
IR.

Canonical key bytes are the equality authority. A deterministic open-addressed
index may cache a stable 64-bit hash for amortized constant-time lookup, but a
hash match always compares the complete key. The hash never becomes type or
link identity. Canonical sorting remains the final emission-order authority.

This separation guarantees that source order, implementation-unit order and
call discovery order cannot affect emitted bytes.

A request for the same key while it is being planned creates an ordinary
recursive edge and reuses the existing record. A request that continually
changes its arguments, such as recursively requesting `grow<*T>`, is not a
cycle and is stopped by explicit depth and instance-count budgets.

Layout recursion continues to use the existing declared/resolving/complete
type state:

```luna
struct Node<T> {
    next: *Node<T>;
}

struct Invalid<T> {
    next: Invalid<T>;
}
```

`Node<i32>` is valid pointer recursion. `Invalid<i32>` is an invalid recursive
value layout.

## Compiler decomposition

M4 extends the existing pass graph rather than placing parsing, inference,
instance state and lowering in one generic manager:

- the dependency-root `luna.compiler.sema.domain` module defines passive
  pattern, substitution, request and instance-state records;
- one move-only `GenericTable`, composed by semantic `Context`, owns their
  checked contiguous storage, canonical hash index and reverse maps;
- the lower `semantic.generics` service owns declaration validation and active
  substitution views;
- the existing `types` implementation owns concrete type specialization and
  activates the corresponding view during layout;
- `functions/generics.la` is a same-module implementation split for generic
  declarations, exact unification and concrete callable signatures;
- `expr/probe/call.la` keeps candidate inference side-effect-free, then creates
  only the uniquely selected concrete function;
- statement lowering visits the canonical ordinary roots first and then the
  bounded concrete-instance worklist, including instances discovered by other
  instances.

A lower submodule never imports its parent facade. Every new module or
implementation path is registered once in `LIBRARIES`; import-derived ordering
remains the build authority.

## Generic classes and M3 interaction

Every concrete generic class is an ordinary M3 class specialization:

- fields are substituted before declaration-order layout;
- constructor member initialization uses the substituted field types;
- class-containing generic fields participate in M3.6 composition rules;
- direct constructors and ordinary methods are materialized per specialization;
- bound methods may select those already-concrete ordinary methods.

Generic inheritance, virtual methods, RTTI, opaque generic classes, method-local
type parameters and generic friendship are rejected in M4. Every accepted
specialization has the same declared members and access policy after
substitution.

An `@rtti` generic class is rejected in M4. Current M3 RTTI deliberately uses
one descriptor address as canonical runtime identity, while M4 permits private
callable/vtable instances in each consuming module. Duplicating a descriptor
would break cross-module identity. Supporting generic RTTI therefore first
requires canonical cross-module data coalescing or a separately reviewed RTTI
representation; M4 does not weaken the existing descriptor invariant.
This also rejects a generic class that would inherit RTTI from an enabled
base.

M4 does not add ownership or lifetime behavior. A generic field is copied by
the same representation rule as its concrete substituted type. Later
copy/move/destruction work will classify each concrete specialization from its
substituted base and fields; the generic engine itself will not own resources.

## ABI and code generation

After substitution:

- generic records use ordinary target layout;
- generic class instances use the ordinary M3 object model;
- calls use the existing System V scalar/aggregate classifier;
- function values point to ordinary concrete functions;
- the IR verifier sees no generic pattern or unspecialized type;
- the assembler and linker require no new generic relocation kind.

An exported generic declaration has no single ELF ABI symbol. Only concrete
private instances are emitted. An ordinary exported Luna function may use a
concrete generic specialization in its signature, in which case its normal
canonical Luna symbol records that concrete type. Generic declarations remain
invalid at every C ABI boundary.

## Determinism and resource contracts

The implementation defines named limits for:

- type parameters on one declaration;
- concrete arguments on one instance;
- active substitution depth;
- reachable concrete instances in one compilation;
- nested diagnostic instance frames.

Every count and encoding length uses checked target-sized arithmetic. Instance
tables use canonical keys and one lookup service; passes do not maintain
parallel caches with different equality rules.

Exceeding a limit reports a deterministic generic resource diagnostic at the
request that crosses the boundary. Allocation or corrupted internal state
remains a runtime/invariant failure rather than a language diagnostic.

## Diagnostics

M4 adds stable diagnostics for at least:

- duplicate or malformed type parameters;
- invalid generic mounting point;
- generic declaration/definition mismatch;
- missing, excess or conflicting type arguments;
- failure to infer every required type parameter;
- ambiguous generic overload;
- invalid concrete substituted type use;
- recursive generic expansion and generic resource exhaustion;
- invalid generic virtual/extern/asm/variadic declaration;
- exported generic body depending on an implementation-only declaration.

Selection, arity and resource failures point at the request; errors discovered
while lowering a selected body point at the definition source. M4 does not add
an instantiation backtrace. The first diagnostic kind remains stable for
`FAIL <diagnostic-kind>` tests.

## M4 delivery sequence

Every slice lands behind `audit`, `refmt`, `verify` and `test`, followed by an
anchor promotion before compiler sources adopt its syntax.

### M4.0: syntax and canonical generic model

- type-parameter and type-argument syntax;
- syntax-tree nodes and flags;
- generic pattern records and canonical declaration/instance encodings;
- substitution and exact structural unification services;
- duplicate, arity and malformed-mount diagnostics.

### M4.1: generic free functions

- inferred and explicit `<...>` calls;
- M2 overload/default/function-pointer integration;
- definition checking and exported interface bodies;
- canonical instance planning and concrete IR emission;
- recursion, resource budgets and cross-module instances.

### M4.2: generic data types

- structures, unions and transparent aliases;
- nested specialization, layout and recursive-type validation;
- aggregate initialization, assignment, parameter/result and ABI coverage;
- concrete specializations in ordinary exported Luna signatures.

### M4.3: generic classes and methods

- class fields, constructors and ordinary direct methods;
- generic class-value composition;
- concrete bound-method selection;
- rejection of method-local parameters, generic inheritance, virtual methods,
  RTTI, opaque classes and friendship.

### M4.4: standard-library proving ground

- `Pair<T, U>`-style data utilities;
- `identity<T>` and `exchange<T>`-style value utilities;
- cross-module nested instances used by real compiler/library code;
- anchor promotion before any compiler source adopts generic declarations.

Reference types, `luna.std.utility::move`, copy/move hooks, destruction and
automatic cleanup begin only after M4 is complete.

## Required test matrix

- inferred and explicit calls for every concrete type category;
- conflicting repeated parameters and non-inferable parameters;
- pointer qualifiers, arrays, function pointers and nested generic patterns;
- ordinary-overload precedence and ambiguous generic patterns;
- defaults inserted after inference and expected function-type selection;
- generic recursion versus expanding recursion;
- private same-module and exported cross-module generic definitions;
- interface/implementation and source-unit permutation determinism;
- generic structures, unions, aliases and nested aggregate initialization;
- generic class construction, composition and ordinary bound methods;
- generic inheritance, virtual method, opaque-class and friendship rejection;
- generic RTTI rejection across direct and imported specializations;
- exact System V parameter/result classification of concrete instances;
- canonical identity across aliases and distinct declaring modules;
- all resource limits and public diagnostic kinds;
- fixed-point rebuild before self-hosted sources use M4 syntax.

## Explicitly excluded from M4

- reference types and value-category conversion;
- copy/move constructors, assignment hooks, destructors and RAII;
- ownership, borrowing or lifetime analysis;
- default or non-type generic arguments;
- variadic type parameters and parameter packs;
- function/class specialization or partial specialization;
- SFINAE, generic-pattern partial ordering and implicit-conversion ranking;
- user-defined constraints, concepts and compile-time reflection;
- dependent unqualified lookup and ADL;
- class-template argument deduction;
- method-local type parameters and generic `const fn`;
- generic inheritance, virtual methods, opaque classes and friendship;
- RTTI-enabled generic class hierarchies;
- runtime dictionaries, boxed universal values and type erasure;
- generic C ABI entry points;
- link-time instance generation or mandatory linker deduplication.

The intended result is a deterministic, separately compiled and genuinely
zero-cost generic foundation. It provides the reusable substitution and
instance machinery later references and object lifetime need, without turning
Luna into a second template metaprogramming language.
