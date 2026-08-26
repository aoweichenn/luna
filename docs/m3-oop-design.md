# M3 object-oriented programming design

## Status and scope

This is the accepted design for the M3 phase. M3.0 direct classes, M3.1 single
inheritance/contracts, M3.2 relocation data and M3.3 dynamic dispatch are implemented; M3.4-M3.5 remain planned. The design consumes the
canonical signature, overload, default-parameter and value-category foundation
delivered by [`m2-callable-infrastructure.md`](m2-callable-infrastructure.md).
The older uppercase M2/M3 headings in `roadmap.md` belong to archived Luna 0
bootstrap milestones.

M3 takes modern C++'s useful zero-cost object model as a reference while
deliberately rejecting its historical compatibility surface. The phase adds no
ownership, lifetime or automatic resource-management semantics.

For this design, "composition is deferred" means:

- no trait, mixin, component, delegation or automatic forwarding feature;
- no class-valued fields or arrays of class values in M3;
- raw pointers to other classes remain legal and non-owning;
- existing structure/union/array nesting remains unchanged.

The primary M3 features are classes, implicit-receiver methods, overloaded
constructors/methods, access control, single inheritance, opt-in virtual
dispatch, restricted operators, non-owning bound methods and minimal RTTI.

## Design principles

1. **One object model, no hidden ownership.** Classes are ordinary target-layout
   values. Stack, aggregate return and raw-pointer behavior reuse existing Luna
   rules. Nothing allocates, retains or destroys an object implicitly.
2. **Zero-cost when unused.** A class with no virtual method has no vtable or
   hidden pointer. Non-virtual methods lower to direct calls.
3. **Explicit source contracts.** Access, virtual dispatch and overrides are
   written explicitly. There are no C++ default-access differences or implicit
   virtual overrides.
4. **Exact types.** M3 reuses M2 overload/default machinery but adds no
   conversion ranking, implicit numeric/pointer conversion or object slicing.
5. **Modules remain the outer namespace.** `::` continues to mean module
   qualification only. `.` and `->` select fields or methods.
6. **Separate interface and implementation.** `pub`/`prot` class contracts
   live in the module interface; method bodies may be distributed across the
   module's implementation units.
7. **No C++ lookup inheritance.** There is no ADL, friend injection, using
   declaration, two-phase lookup or accidental member hiding.

## Type split: struct versus class

Luna keeps the two declarations semantically distinct:

- `struct` and `union` remain data/ABI types: aggregate initialization, public
  fields, anonymous members, bitfields, flexible members and C interoperability;
- `class` is the OOP type: access-controlled fields, methods, inheritance and
  optional virtual dispatch.

This is not C++'s `struct`/`class` default-access duplication. M3 methods and
inheritance apply only to `class`. A class is not accepted by value in an
`extern fn` signature; a raw pointer to a class has the normal pointer ABI.

M3 classes do not support `@packed`, `@bits`, anonymous members or flexible
members. `sizeof` and `alignof` include hidden class storage when present;
`offsetof` remains a structure/union facility and is rejected for classes.

## Surface syntax

The proposed declaration grammar is intentionally small:

```text
ClassDeclaration ::= [ "export" ] [ "final" ] "class" Identifier
                     [ ":" QualifiedType ] "{" ClassMember* "}"

ClassMember      ::= Access FieldDeclaration
                   | ExposedAccess MethodDeclaration
                   | ExposedAccess ConstructorDeclaration
                   | ExposedAccess OperatorDeclaration
                   | "friend" "class" Identifier ";"

Access           ::= "pub" | "prot" | "priv"
ExposedAccess    ::= "pub" | "prot"

MethodSignature  ::= Identifier "(" [ ParameterList ] ")"
                     [ "const" ] [ "volatile" ] [ "->" Type ]

MethodDeclaration ::= "static" "fn" Signature ";"
                    | "fn" MethodSignature ";"
                    | "virtual" [ "final" ] "fn" MethodSignature ";"
                    | "abstract" "fn" MethodSignature ";"
                    | "override" [ "final" ] "fn" MethodSignature ";"

ConstructorDeclaration ::= "init" "(" [ ParameterList ] ")" ";"
OperatorDeclaration    ::= "operator" OperatorToken MethodSignature ";"

ImplDeclaration  ::= "impl" QualifiedType "{" MethodDefinition* "}"

MethodDefinition ::= "static" "fn" Signature FunctionBody
                   | "fn" MethodSignature FunctionBody
                   | "init" "(" [ ParameterList ] ")" FunctionBody
                   | "operator" OperatorToken MethodSignature FunctionBody
                   | "priv" "static" "fn" Signature FunctionBody
                   | "priv" "fn" MethodSignature FunctionBody
```

This grammar describes the complete accepted M3 direction. The implemented
M3.0 subset has no base/final clause, virtual/abstract/override/operator/friend
member or bound-method form. It accepts explicit-access fields, `pub` or
`prot` direct method/constructor contracts, `priv` body-only methods,
static methods and same-module `impl` blocks. Later milestones activate the
remaining productions without changing the M3.0 spellings.

`abstract` implies virtual. `static` is mutually exclusive with `virtual`,
`abstract` and `override`. `final` on a method is legal only on a virtual
introduction or override. Parser modifier-order flexibility is not part of the
contract: the canonical order above is the only accepted spelling.

### Class declarations

```luna
export class Shape {
    prot x: f64;
    prot y: f64;

    pub init(x: f64, y: f64);
    pub virtual fn area() const -> f64;
    pub fn translate(dx: f64, dy: f64);
}

export final class Circle : Shape {
    priv radius: f64;

    pub init(x: f64, y: f64, radius: f64);
    pub override fn area() const -> f64;
}
```

A class has at most one base class. Every field carries explicit access;
interface method declarations are explicitly `pub` or `prot`. Private
methods live only in `impl` blocks, avoiding C++'s need to expose private method
declarations in public headers. There is no default access based on declaration
spelling.

### Method definitions

```luna
impl Shape {
    init(x: f64, y: f64) {
        this->x = x;
        this->y = y;
    }

    fn area() const -> f64 {
        return 0.0;
    }

    fn translate(dx: f64, dy: f64) {
        this->x += dx;
        this->y += dy;
    }

    priv fn coordinate_sum() const -> f64 {
        return this->x + this->y;
    }
}
```

An `impl Type` block is legal only in the module that declares `Type`. It does
not open an extension-method mechanism. Matching `pub`/`prot` class
declarations omit their access and dispatch modifiers on the body definition;
the declaration owns the contract. A method introduced with a body only in an
`impl` block must say `priv` explicitly, preventing a misspelled public
method definition from silently creating another method.

Method definitions may be spread across any implementation units of the
declaring module. Exactly one body is permitted for each non-abstract declared
method.

### Receivers

An instance method has an implicit receiver and the body accesses it through
the reserved `this` pointer. The receiver never appears in the source parameter
list. A method is writable by default; trailing `const`, `volatile` or
`const volatile` qualifies the pointee. A `static fn` has no receiver and cannot
use `this`. `Self` is an ordinary unresolved type name unless a declaration
actually introduces that name; it has no contextual behavior.

Receiver kind remains part of callable compatibility and canonical identity.
A mutable object may bind to a `const` method; the reverse is invalid. This
narrow binding rule does not create a general implicit pointer conversion. An
address-backed temporary may bind only to a `const` method and remains valid for
the existing enclosing-expression lifetime; a writable method requires a
mutable lvalue.

Variadic, `const fn`, `asm fn` and `@export_name` methods are outside the first
M3 implementation. `@inline` and `@noreturn` may apply once their existing
contracts are extended to method declarations. Direct/static methods and
constructors may use M2 default parameters; virtual, abstract and override
methods initially may not.

### Calls

```luna
var circle: Circle = Circle(0.0, 0.0, 2.0);
circle.translate(1.0, 1.0);

let pointer: *const Shape = (&circle) as *const Shape;
let value: f64 = pointer->area();
```

- `object.method(args)` binds the address of a compatible lvalue or read-only
  temporary;
- `pointer->method(args)` performs the existing null check and uses the pointer
  as receiver;
- `Type.static_method(args)` calls an associated static method;
- `qualifier::Type.static_method(args)` composes module qualification with
  type-level `.` selection;
- before the bound-method slice, a method selected without an immediate call is
  invalid.

M3 later adds a non-owning two-word `method fn` value containing one raw
receiver pointer and one resolved entry function. It does not add C++
pointer-to-member types or general closures.

## Construction and object values

M3 adds overloaded `init` constructors on top of the M2 callable resolver:

```luna
pub init();
pub init(value: i32);
pub init(text: *const u8, length: usize = 0);
```

```luna
var empty: Counter = Counter();
var value: Counter = Counter(42);
```

The constructor receiver is always mutable and omitted at the call site. An
`init` has no result type, cannot be static/virtual/abstract and cannot fail
implicitly; fallible construction uses a named static factory returning an
explicit result type.

The compiler allocates and zero-initializes complete storage, initializes the
hidden vtable pointer when required, and invokes the selected constructor.
Constructor overload/default resolution is exactly the M2 algorithm. Class
aggregate initialization is unavailable outside methods of that class.

A derived constructor calls `super.init(...)` exactly once as its first
effective statement. The base constructor is selected through its overload set
before direct-field initialization proceeds. M3 has no C++ initializer-list,
delegating-constructor or inherited-constructor grammar.

Exact same-class copy, assignment, parameter passing and return reuse Luna's
existing representation-copy rules. There are:

- no copy or move constructors;
- no assignment operators;
- no implicit deep copy;
- no destructor or RAII;
- no `new`, `delete`, garbage collection or reference counting.

Resource-bearing classes expose ordinary explicit methods such as `close`.
Raw pointers carry no ownership or lifetime guarantee.

## Names, lookup and overloading

Each class has one member namespace. Field and method names may not collide.
Methods, constructors and operators reuse the M2 callable signature and
overload-set representation. Static and instance methods may share a source
name only when their callable keys remain distinct and every call resolves
exactly; return type alone never distinguishes candidates.

Lookup is deterministic:

1. search direct members of the static class;
2. continue through the single base chain;
3. reject access violations;
4. reject a total miss.

A derived lookup automatically combines direct and inherited overload
candidates instead of applying C++ name hiding. An exact key matching a virtual
base method requires `override`; an exact non-virtual duplicate is an error.
Other distinct overload keys extend the visible set deterministically.

Free-function lookup never considers argument classes. There is no ADL.

## Access control

| Access | Available from |
| --- | --- |
| `pub` | all valid importers and class implementations |
| `prot` | methods of the declaring class and derived classes |
| `priv` | methods of the declaring class and explicit same-module friend classes |

Being in the same Luna module does not bypass class access without an explicit
restricted `friend class` declaration. An `impl` block establishes the current
class for access checks.

Public and protected members of an exported class are interface contract.
Private fields must initially remain in the source interface because complete
by-value layout is required. A later opaque-class slice can hide representation
for pointer-only APIs without importing C++ header layout baggage.

## Single inheritance

```luna
export class Derived : Base {
    // direct members
}
```

Rules:

- inheritance is always public and single;
- the base class must be complete, accessible and not `final`;
- a class cannot derive from a structure, union or opaque class;
- the base subobject begins at offset zero;
- direct fields follow base storage and any newly introduced vtable pointer;
- a derived field cannot reuse any inherited field name;
- derived-to-base pointer conversion uses explicit `as` and preserves address
  bits because the base is at offset zero;
- base-to-derived `as` remains an unchecked raw-pointer cast; the later opt-in
  RTTI operation provides a separate checked, null-returning downcast;
- by-value derived-to-base conversion is forbidden, preventing slicing.

Inside a derived method, `super.method(args)` performs a statically dispatched
call to the immediate base implementation. It never re-enters virtual dispatch.

There is no multiple inheritance, virtual inheritance, private/protected
inheritance, base-pack expansion or inherited-constructor mechanism.

## Virtual methods

### Declaration rules

- `virtual` introduces a virtual slot;
- `abstract` introduces a virtual slot with no implementation body;
- `override` is mandatory for a derived implementation of a virtual method;
- `final` on a virtual method prevents another override;
- `final` on a class prevents inheritance;
- override parameters, receiver qualification and result type match exactly;
- covariant returns are not supported.

A class is abstract while any inherited or direct abstract slot lacks a
concrete final overrider. Abstract classes cannot be instantiated, copied by
value, passed by value or returned by value; raw pointers to them are legal.

### Call rules

- non-virtual methods always use direct calls;
- a virtual call through an object value with `.` has that value's exact
  dynamic type and is emitted directly;
- a virtual call through `->`, including `this->method()`, loads the target
  from the vtable and uses the existing indirect-call IR;
- a `final` method or a call through a pointer to a `final class` is emitted
  directly because its final overrider is statically known;
- `super.method()` is always direct;
- pointer calls retain the existing null trap before method lookup/dispatch.

M3 has no virtual destructor because it has no destructor feature. A resource
hierarchy that needs polymorphic cleanup declares an ordinary virtual `close`
method and calls it explicitly.

## Layout and vtable ABI

Classes use the existing x86-64 target layout rules with these additions:

0. an otherwise empty complete class has size and alignment one;
1. the single base subobject is at offset zero;
2. a class that first introduces virtual methods receives one hidden vtable
   pointer after its base subobject, or at offset zero when it has no base;
3. a class derived from a polymorphic base reuses the base vtable pointer;
4. direct fields follow the hidden pointer when one is introduced;
5. the final size and alignment use ordinary target padding rules.

This supports a non-polymorphic base followed by a polymorphic derived class
without pointer adjustment: the base remains at offset zero, while virtual
calls through the derived static type know that hierarchy's fixed vptr offset.

Vtable slots are deterministic:

1. inherited slots retain their indices;
2. an override replaces the target in its inherited slot;
3. new virtual methods append in declaration order;
4. no offset-to-top prefix is emitted; an opt-in RTTI hierarchy adds one
   canonical type-descriptor pointer without changing slot indices.

Each concrete polymorphic class owns one compiler-generated read-only vtable.
The table is a module-local IR global allocated in canonical class-record
order. Every slot is a function address with an absolute relocation. Abstract
classes need no instantiable vtable until all slots have concrete final
overriders; each concrete descendant materializes its own final-overrider
table.

Construction does not reproduce C++'s temporary construction-phase dynamic
types. Luna zeroes the complete most-derived storage, installs that concrete
class's vptr, then enters its constructor. `super.init(...)` operates on the
same base-at-zero object and never replaces the vptr, so a virtual call made
during construction observes the most-derived class. The zero-before-vptr
ordering and absence of implicit destruction make this rule deterministic
without construction vtables or pure-virtual runtime stubs.

## Lowering model

### Semantic records

The semantic layer needs explicit records rather than encoding class state in
ordinary structure flags:

```text
ClassRecord {
    type_id
    base_type
    first_field / field_count
    first_method / method_count
    vptr_offset
    vtable_global
    flags: exported, final, abstract, polymorphic
}

ClassFieldRecord {
    field_id
    access
}

MethodRecord {
    function_id
    access
    dispatch: static, direct, virtual, abstract
    virtual_slot
}
```

`Function` owns the shared callable identity: kind, owner type and receiver
kind. `MethodRecord` contains only class-specific policy and dispatch metadata,
so declaration matching, mangling and overload selection cannot disagree with
a duplicated method identity. Class and method names enter owner-scoped
bindings backed by the M2 candidate slices and canonical-signature services.
The dependency-root `semantic.classes.model` module owns these records in one
`Store`; semantic context carries that store as a single lifecycle unit instead
of exposing unrelated class buffers throughout the compiler.

The ordinary type record mirrors `vptr_offset` as layout metadata. The generic
type-table validator therefore recomputes hidden-pointer placement together
with bases, direct fields, alignment and tail padding, while `ClassRecord`
retains the same value for dispatch-policy queries. A disagreement is an
internal invariant failure rather than an unchecked hidden gap.

### IR and backend

An instance method lowers to an ordinary IR function whose first parameter is
the receiver pointer. A static method lowers to an ordinary function without a
receiver. Existing ABI classification handles remaining parameters/results.

Virtual dispatch reuses existing operations where possible:

```text
receiver null_check
    -> byte-offset to the hierarchy's fixed vptr
    -> load read-only vtable pointer
    -> byte-offset to the canonical slot
    -> load the erased function pointer
    -> call_indirect(receiver, arguments...)
```

The indirect function-pointer type contains the receiver as parameter zero,
followed by the M2 canonical parameter sequence. The slot's concrete target
may own a derived receiver type, but single inheritance and base-at-zero make
the machine representation identical; the vtable is the sole erasure boundary.
Defaults remain call-site data and never enter a slot.

The missing backend foundation is deterministic read-only global relocation
data. Before virtual dispatch lands, IR/global emission and `luna-as` must be
able to express a `.quad`-equivalent function-symbol reference and serialize it
as the already-supported `absolute64` object relocation. This facility is
compiler-owned and is not exposed as a general module-scope variable feature.

M3.2 models this without teaching raw bytes about class policy:

```text
Global {
    byte_offset / byte_count / alignment
    first_function_reference / function_reference_count
    read_only
}

GlobalFunctionReference {
    global_id
    byte_offset
    function_id
}
```

References form one contiguous, offset-ordered slice per global. Each reference
occupies one aligned, zero-filled eight-byte placeholder and targets the
canonical IR function identity selected by M2; defaults, overload sets, access
and virtual policy never enter this layer. The IR builder accepts references
only in read-only globals and the verifier independently recomputes the slices,
ranges, ordering, placeholder bytes and target bounds. This keeps ordinary
class storage and future dispatch capability metadata separate: M3.3 consumes
the record but owns vtable construction.

Code generation replaces each placeholder with exactly one `.quad <symbol>`
line. `luna-as` accepts this form only in `.rodata`, always preserves it as an
`absolute64` relocation with addend zero, and never folds even a same-object
target to a section-relative number because the final virtual address belongs
to the linker. Object serialization and ELF64 `ET_REL` writing reuse the
existing absolute relocation representation; no ownership, allocator or
general source-level global facility is introduced.

## Restricted operators

Operators reuse M2 method overload sets and exact candidate resolution. The
first supported set is unary `-`, `!`, `~` and binary arithmetic, bitwise and
comparison operators. An operator is a non-virtual instance method of the left
operand class; lookup never searches the right operand, another module or ADL.

Assignment/compound assignment, `&&`, `||`, comma, address, conversion,
allocation and call/index operators are excluded initially. There is no global
or friend operator form. This deliberately accepts that `vector * scalar` may
exist while `scalar * vector` does not unless the scalar type itself declares
the operation.

## Non-owning bound methods

```luna
let callback: method fn(Event) -> void = object.handle;
```

A bound method is exactly two machine words:

```text
BoundMethod {
    receiver: *void
    entry: fn(*void, arguments...) -> result
}
```

The expected `method fn` type selects one method overload. Binding copies a raw
receiver pointer and does not extend lifetime. A virtual method resolves its
current final entry from the vtable at bind time. Copying the bound method copies
two words; calling it after the receiver ceases to exist is a program error.
There are no additional captures and therefore no general closure feature.

## Minimal opt-in RTTI

RTTI is available only to polymorphic hierarchies explicitly marked `@rtti`.
Their vtables reference a descriptor containing canonical class identity and
one base-descriptor link. Single inheritance and offset-zero bases make checked
pointer downcasts a descriptor-chain walk with no pointer adjustment.

M3 exposes a boolean type test and a non-throwing pointer cast whose failure is
`null`. Exact surface spelling is selected before implementation; it must not
introduce C++ `typeid`, implementation-defined name strings, reference casts,
exceptions or general reflection.

## Restricted friendship

M3 may declare `friend class Other;` only when both classes are declared in the
same module. Friendship grants private access to methods of the named class; it
does not inject names, grant transitive friendship or apply to free functions.
Cross-module friendship is rejected, keeping module interfaces and dependency
direction explicit.

## Diagnostics

M3 should use specific leading diagnostic kinds for at least:

- invalid/duplicate class or member declaration;
- invalid receiver or method definition mismatch;
- inaccessible private/protected member;
- invalid class initialization;
- class-valued composition in the deferred M3 subset;
- invalid/final base class;
- missing or invalid override;
- override of final method;
- abstract class instantiation;
- invalid `super` use;
- invalid class use in C ABI;
- invalid class layout query;
- missing method definition;
- invalid virtual slot or vtable invariant.

New diagnostic enum values append at the end so existing stable ordinals do not
change.

## Explicitly excluded from M3

- ownership, borrowing, lifetime analysis or move semantics;
- destructors and RAII;
- `new`/`delete`, garbage collection and reference counting;
- conversion operators and implicit user-defined conversions;
- multiple or virtual inheritance;
- trait, mixin, component, delegation and class-value composition;
- extension methods and ADL;
- arbitrary free-function or cross-module friends;
- `typeid` strings, reflection and throwing downcasts;
- exceptions;
- C++ pointer-to-member values and general closures;
- templates/generics/concepts;
- virtual/static data members and module-scope variables;
- nested/local classes;
- implicit re-export or wildcard import.

## Delivery sequence

Each slice follows the existing two-step bootstrap discipline: first implement
the feature using syntax accepted by the current anchor, pass `verify` and
`test`, promote the anchor, and only then adopt the new syntax in compiler
sources.

### M3.0: class metadata and direct methods

Status: implemented and covered by executable, negative, cross-module, ABI and
source-order tests.

- shared callable identities and owner-scoped bindings, with unchanged
  free-function ABI;
- one class-metadata store for class, field-access and method records;
- nominal empty class identities, module visibility and canonical encoding;
- lexer/parser nodes for `class`, `impl`, access and method modifiers;
- implicit `this`, receiver qualifiers and M2 method overload matching;
- complete non-polymorphic class layout;
- `priv`/`prot`/`pub` access checks;
- overloaded constructors, instance/static direct calls and defaults;
- exact-value copy/parameter/result behavior;
- no inheritance or virtual dispatch yet.

### M3.1: single inheritance

Status: implemented and covered by executable, negative, cross-module, ABI and
source-order tests. M3.1 assigns contract slots and abstractness metadata but
does not itself emit vptrs or perform dynamic dispatch; M3.3 now consumes that
metadata.

- one complete base at offset zero;
- inherited lookup without hiding;
- explicit pointer upcasts;
- `override` validation metadata, `final` class/method checks;
- `super.method()` direct calls;
- abstractness analysis without vtable emission yet.

### M3.2: vtable data foundation

Status: implemented as a class-neutral relocation foundation. It emits no
vptrs or vtables by itself; M3.3 now consumes its read-only reference data.

- read-only IR globals containing symbol references;
- canonical per-global function-reference slices over zero placeholders;
- `.quad <symbol>` assembly representation restricted to `.rodata`;
- `absolute64` assembler/object/ELF round trips;
- deterministic relocation, executable link and malformed-input tests.

### M3.3: virtual and abstract dispatch

Status: implemented with executable, null-trap, cross-module, layout,
devirtualization, relocation and source-order coverage.

- deterministic slot assignment and vtable emission;
- hidden vptr initialization and layout verification;
- virtual call lowering through `call_indirect`;
- abstract instantiation rejection and final overriders;
- direct devirtualization only when exact dynamic type is known.

### M3.4: advanced callable class features

- restricted left-class operators through M2 overload resolution;
- non-owning two-word bound methods;
- opt-in descriptor-chain RTTI and non-throwing checked pointer casts;
- same-module `friend class` without name injection.

### M3.5: optional opaque classes

- `export opaque class Handle;` pointer-only public identity;
- complete private definition in module implementation units;
- no by-value use, layout query, inheritance or field access outside the
  defining module;
- still no ownership or automatic destruction.

Opaque classes are valuable for representation hiding but are not a prerequisite
for direct methods or single inheritance.

## Required test matrix

Every slice needs positive, negative and determinism coverage:

- mutable/read-only object and pointer receivers;
- private/protected/public access across modules and inheritance;
- overloaded constructor/default initialization and exact same-class copies;
- method bodies split across module implementation units;
- base layout and explicit upcasts, with no slicing;
- missing, mismatched, duplicate and final overrides;
- abstract class and final class failures;
- direct, virtual and `super` calls;
- null receiver traps;
- deterministic method/vtable order under source-unit permutation;
- vtable absolute relocation mutation tests;
- System V by-value concrete-class ABI matrices;
- restricted operators and overload ambiguity;
- bound-method overload selection, virtual binding and stale receiver behavior;
- opt-in RTTI positive/negative downcasts and non-RTTI rejection;
- same-module friendship and rejected cross-module/free-function friendship;
- fixed-point rebuild before compiler sources adopt each slice.

## C++ features retained and removed

| Modern C++ idea | Luna M3 decision |
| --- | --- |
| classes with access control | retained, every access explicit |
| member and static functions | retained with implicit `this`/`static` |
| single inheritance | retained |
| virtual/override/final | retained, override always explicit |
| abstract methods | retained as `abstract fn`, no `= 0` syntax |
| constructors | retained as overloaded `init`, no initializer-list baggage |
| destructors | deferred; explicit cleanup until copy/lifetime policy exists |
| RAII and smart pointers | removed; no ownership model |
| overload sets/default arguments | inherited from exact M2 callable rules |
| operator overloading | retained as restricted left-class methods |
| implicit conversions | removed |
| multiple/virtual inheritance | removed |
| ADL/using-based lookup | removed |
| friend | same-module class friendship only |
| RTTI | minimal, opt-in, non-throwing pointer checks |
| exceptions | removed |
| pointer-to-member | replaced by non-owning two-word bound methods |
| templates/concepts | separate future design |
| module partitions/header units | not part of the OOP model |

The intended result is a small, deterministic, zero-cost class system: familiar
to a modern C++ programmer, but without C++'s compatibility-driven lookup,
lifetime, overload-ranking and inheritance complexity.
