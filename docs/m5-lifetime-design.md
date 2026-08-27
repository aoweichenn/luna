# M5 references, object lifetime and move semantics

## Status and scope

M5 adds deterministic object lifetime to the completed M2 callable, M3 class
and M4 generic foundations. It implements C++-style references and move
selection without importing ownership, borrowing, exceptions or template
metaprogramming.

M5 is complete for the storage and control-flow forms Luna currently has:
automatic locals, parameters, temporaries, arrays, direct class fields, one
base class, concrete generic specializations and every accepted structured or
`goto` exit. Luna has no module variables, thread-local objects, exceptions,
coroutines, allocation expressions or nontrivial union active-member model;
those absent language surfaces have no M5 lifetime contract.

## References and value categories

References use C++ suffix spelling:

```luna
T&
T const&
T&&
T const&&
```

Prefix `const T&` is accepted as an equivalent spelling. A reference is a
non-null alias in the source type system and one pointer-sized INTEGER value
at the System V/IR boundary. It has no ownership and receives no borrow or
escape analysis. Reference fields, arrays of references, `void` references
and C ABI references are rejected in M5.

The suffix is lexically attached to its type: `T&` and `T&&` are references,
while spaced `&`/`&&` remain expression operators. This keeps the compact
`value as T&&` category cast unambiguous with `value as usize && condition`.

The semantic value categories are lvalue, xvalue and prvalue. A mutable
lvalue reference binds only a writable lvalue. A read-only lvalue reference
also binds a read-only lvalue, xvalue or temporary. An rvalue reference binds
an xvalue or temporary. Overload selection prefers mutable `T&` over
`T const&` for writable lvalues and `T&&` over `T const&` for expiring values;
no numeric, inheritance or user-defined conversion ranking is introduced.

Reference collapsing is deterministic after generic substitution:

```text
T&  &  -> T&
T&  && -> T&
T&& &  -> T&
T&& && -> T&&
```

## Move is a library operation

The language has no `move` expression or intrinsic. The standard utility is
an ordinary M4 generic function:

```luna
import luna.std.utility as utility;

export fn move<T>(value: T&) -> T&& {
    return value as T&&;
}

let target: Resource = utility::move(source);
```

`move` changes only the value category. Moving data or resources occurs when
the selected constructor or assignment overload accepts `T&&`. The source
object remains fully constructed and is destroyed normally.

## Lifetime members

`init` remains the constructor name. `deinit` is its unique destructor
counterpart:

```luna
class Buffer {
    pub init(size: usize);
    pub init(other: Buffer&&);
    pub deinit();
}

impl Buffer {
    init(size: usize) { ... }
    init(other: Buffer&&) { ... }
    deinit() { ... }
}
```

A destructor has no explicit parameters, result, qualifier, default, generic
parameter or overload. It is non-static and direct in M5. Since raw pointers
are non-owning and Luna has no `delete`, automatic destruction always knows
the exact complete type; virtual destruction is not required by an accepted
M5 operation.

Copy/move constructors are ordinary `init` overloads whose sole explicit
parameter is `Owner const&` or `Owner&&`. Copy/move assignment uses the
restricted `operator =` member with the same parameter forms. Assignment is
still a statement and the operator has no source-visible result.

## Triviality and generation

Scalars, pointers, references, function values and records containing only
trivial fields retain representation-copy behavior and require no cleanup.
A concrete type is nontrivial when its base, element or field is nontrivial,
or when it declares a destructor or copy/move special member.

Each operation is classified independently; Luna does not reproduce C++'s
rule-of-three/rule-of-five suppression history:

- an explicitly declared operation uses its definition;
- an undeclared operation is trivial only for a trivial type and otherwise
  unavailable.

M5 deliberately has no `= default` or `= delete` surface. Explicit special
members plus the trivial/unavailable rule cover the early systems-language
use cases without importing C++'s implicit-generation history. A later
generation feature can build on the same classification service without
changing M5 overload or lifetime semantics.

## Destruction order

Every fully constructed automatic object is destroyed exactly once on every
accepted normal exit. Destruction order is:

1. locals in reverse construction order;
2. array elements from the last index to zero;
3. the class destructor body;
4. direct fields in reverse declaration order;
5. the direct base subobject.

Parameters are destroyed after body locals. A moved-from object remains in
the cleanup set. Traps terminate the process and do not unwind; Luna has no
exception edge.

`return`, `break` and `continue` emit cleanup for every scope they leave.
`goto` may not enter an initialization as before. In M5 it also may not cross
the lifetime boundary of a nontrivial local; accepted gotos therefore require
no deferred cleanup patching and cannot bypass destruction.

## Temporaries and elision

Nontrivial temporaries are registered at their full-expression checkpoint and
destroyed in reverse creation order. Binding a temporary to a local read-only
lvalue reference or rvalue reference extends it to that reference's scope.

Construction into a named local, direct field, array element, call result or
return result uses the final destination address. Mandatory copy elision does
not require a copy/move constructor and creates exactly one cleanup owner.
When elision does not apply, lvalue sources select copy construction and
xvalue/prvalue sources select move construction before any representation
copy fallback.

## Delivery sequence

- M5.0: references, canonical identity, ABI lowering, xvalues and binding
  ranking;
- M5.1: destructor declarations, exact-type cleanup and all normal exits;
- M5.2: copy/move construction and assignment plus type classification;
- M5.3: temporaries, lifetime extension and mandatory result-slot elision;
- M5.4: generic reference inference, `utility::move` and move-only resource
  proving types;
- M5.5: anchor promotion before compiler/library sources adopt M5 syntax.

All six delivery steps are implemented and covered by the fixed-point and
behavior gates.
