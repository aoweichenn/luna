# Modern Luna architecture and refactoring contract

## Status and authority

This document defines the mandatory architecture for the whole-project
modernization. It applies to every new or rewritten source under `library/`,
`compiler/` and `drivers/`. The migration is incremental, but an edited
subsystem must move toward this design rather than preserve its historical
bootstrap shape.

The current branch has established a green baseline with 443 tests and
a byte-identical fixed point. Every migration batch must preserve those gates.

## Non-negotiable design rules

1. Use the highest-level Luna feature supported by the current anchor.
   Stateful subsystems are classes with methods and explicit invariants, not a
   loose state structure passed through a family of free functions.
2. Use a `struct` only for passive data, wire/ABI records, syntax/IR records or
   deliberately transparent views. A type that owns a resource, protects an
   invariant or has meaningful behavior is a class.
3. Use generics for reusable containers, views, results and algorithms. Do not
   duplicate byte-oriented containers or result carriers for every subsystem.
4. Resource-owning classes use deterministic lifetime: `init`, move
   construction where transfer is required, and `deinit`. Copy behavior is
   explicit; a resource is never accidentally copied through an aggregate.
5. Prefer composition and narrow method contracts. Inheritance and virtual
   dispatch are used only for genuine runtime substitution, never as a
   namespace or code-sharing mechanism.
6. Apply a design pattern only when it names a real variation or lifecycle:
   Strategy for replaceable lowering/encoding rules, State for phaseful
   sessions, Builder for IR/object construction, Facade for a subsystem entry,
   and RAII for resources. Pattern-shaped indirection without a variation axis
   is forbidden.
7. Conditions contain at most two logical clauses. Bounded/indexed traversal
   uses `for`; state-driven consumption uses `while`; kind dispatch uses
   `switch`; enumerative mappings use data tables plus a loop.
8. Names describe domain roles. Avoid `data`, `info`, `helper`, `manager`,
   `util`, `api`, `model`, `state` and `context` when a concrete domain name is
   available. A class is named for the abstraction it represents, not for its
   implementation mechanism.

## Object, generic and procedural selection

| Need | Required form |
| --- | --- |
| Stateful compiler pass or cursor | class with private fields and methods |
| Owned allocation, file, text or path | move-aware RAII class |
| Reusable typed sequence | generic `vector<Value>` class |
| Non-owning typed range | generic `span<Value>` value class |
| Success/error carrier | future `expected<Value, Error>` after variant lifetime support |
| Token, syntax node, IR instruction, ELF record | passive `struct` |
| Closed domain alternatives | `enum` plus `switch` or a strategy table |
| Stateless one-step transformation | free function |
| Platform/ABI primitive | narrow module-level function or `asm fn` |

Free functions remain valid for true algorithms and ABI entry points. They are
not the default organization for a subsystem with mutable state.

## Latest-feature adoption gate

Every subsystem design must explicitly review the complete current Luna
feature set. A review is incomplete if it merely replaces `while` with `for`
while retaining a procedural architecture.

| Luna feature | Required project use |
| --- | --- |
| Generic classes and functions | `span<Value>`, `vector<Value>`, reusable searches and validators; later `expected` |
| Class composition | sessions composed from buffers, builders, diagnostics and pass-specific strategies |
| Access control | invariants behind `priv`; only narrow subsystem operations are `pub` |
| Constructors | establish every valid session/resource state; no post-construction initialization protocol |
| Destructors / RAII | vector storage, owned text/path, files and temporary output resources |
| Copy/move special members | explicit deep copy where meaningful; move-only transfer for unique resources |
| Overloads and default arguments | domain operations with one semantic family, not name suffixes |
| Operators | only natural value/container operations such as indexed access; never decorative DSL syntax |
| Bound methods | parse/lower/encode strategy tables and callbacks that need an owning session |
| Virtual dispatch and RTTI | runtime class hierarchies only; closed kinds keep `enum` + `switch` |
| Friends | narrowly scoped collaborating builders/inspectors when a public escape hatch would weaken invariants |

Each batch description must state which of these features it adopts and why
the remaining features do not fit that boundary. Rewritten stateful code that
could reasonably be a class but remains a state pointer plus free functions is
rejected.

## C++ naming and interface alignment

The standard library follows the C++ standard-library surface as far as Luna's
current semantics permit:

- modules mirror focused C++ headers/concepts: `vector`, `span`, `expected`,
  `utility`, `memory`, `string`, `charconv`, `filesystem`/`path` and `io`;
- public standard-library types and methods use lowercase names;
- established operations keep C++ spellings such as `push_back`, `size`,
  `capacity`, `data`, `reserve`, `clear`, `empty`, `value` and `error`;
- deviations caused by the absence of exceptions, iterators, concepts or a
  supported operator must be explicit in the subsystem contract.

Compiler-facing classes instead follow LLVM/Clang's domain-oriented style:
one class owns one pass/session invariant, names identify compiler concepts,
and modules represent dependency boundaries rather than C++ header filenames.

## Module naming and dependency policy

### Namespaces

- Standard library: `luna.std.*`.
- Linux target services: `luna.linux.*`.
- Compiler: `luna.compiler.*`.
- Command services: `luna.tools`.

The historical `luna.bootstrap.*` prefix has no meaning in the pure self-hosted
branch and must be removed during migration. No new module may use it.

A module name should normally contain no more than four components including
`luna`. Every component must identify a real dependency boundary. File split
names such as `api`, `model`, `state`, `lookup`, `visibility`, `support`,
`value` and `call` do not justify modules by themselves.

### Interface versus implementation files

One module owns at most one interface and may own many implementation units.
Implementation file paths describe responsibilities; they do not create
modules. A new submodule is allowed only when all of the following hold:

1. it has a coherent public contract;
2. at least one peer can depend on it without depending on its parent facade;
3. the split makes the import graph strictly clearer or prevents a cycle;
4. it is not merely a way to shorten a source file.

Parent facades never flow downward. Same-module method bodies and private
declarations may be distributed across implementation units.

A non-generic interface normally remains below 250 lines and contains no
algorithm bodies. Exported generic bodies are the only current exception,
because consumers must monomorphize them. New or rewritten source directories
contain files or child module/family directories at one level, never both.

### Target compiler module contraction

| Historical group | Target module | Same-module implementation units |
| --- | --- | --- |
| `bootstrap.frontend.lexer` | `luna.compiler.lexer` | facade, session, keywords, literals, token |
| `bootstrap.frontend.syntax` | `luna.compiler.syntax` | storage, builder when needed |
| `bootstrap.frontend.parser.*` | `luna.compiler.parser` | facade, session, expression, statements, declarations |
| `bootstrap.middleend.type` | `luna.compiler.types` | storage, construction, validation, layout |
| `bootstrap.middleend.ir` + `ir.verify` | `luna.compiler.ir` | model, builder, validation |
| semantic foundational records | `luna.compiler.sema.domain` | callable, value, classes, generics |
| semantic `context.*` | `luna.compiler.sema.session` | session, names, builder |
| semantic `types.*` | `luna.compiler.sema.types` | resolution, lookup, visibility, layout |
| semantic `consteval.*` | `luna.compiler.sema.consteval` | model, engine, execution |
| semantic `functions.*` | `luna.compiler.sema.functions` | signatures, overloads, bindings, generics, methods, IR |
| semantic `expr.*` | `luna.compiler.sema.expr` | base, numeric, strings, probe, initializer, access, operators |
| semantic `stmt.*` | `luna.compiler.sema.stmt` | lowering, labels |
| backend checked assembly text | `luna.std.string` + `luna.std.charconv` | generic owned characters and allocation-free conversion; no backend wrapper |
| `backend.x86_64.codegen.*` | `luna.compiler.x86.codegen` | session, ABI, frame, values, calls, instructions |
| `backend.x86_64.elf.*` | `luna.compiler.x86.elf` | format, reader, writer |
| `backend.x86_64.assembler.*` | `luna.compiler.x86.assembler` | session, operands, encoding, source |
| backend object/linker | `luna.compiler.x86.object`, `luna.compiler.x86.linker` | split by real pass when needed |
| `luna.tools.cli/compile/assemble/link` | `luna.tools` | cli, compile, assemble, link, entry |

This table is a target, not permission for a blind rename. Before merging a
group, record its current import graph, prove the combined module remains
acyclic, migrate all imports atomically, and pass `audit`.

## Standard-library architecture

The standard library is the first consumer of generics, classes and lifetime.
It must not remain a collection of unrelated procedures over public structs.
The detailed container contract is `docs/container-foundation.md`.

### Core value abstractions

| Module | Responsibility | Principal abstraction |
| --- | --- | --- |
| `luna.std.expected` | deferred value-or-error carrier | `expected<Value, Error>` |
| `luna.std.utility` | generic language-level operations | `move` and future narrow utilities |
| `luna.std.checked` | checked integer arithmetic/alignment | stateless functions |
| `luna.std.span` | non-owning typed contiguous range | `span<Value>` |
| `luna.std.buffer` | owning byte storage | move-only `byte_buffer` |
| `luna.std.vector` | typed trivially-relocatable storage | `vector<Value>` |
| `luna.std.deque` | double-ended indexed sequence | `deque<Value>` |
| `luna.std.list` | stable-address linked sequence | `list<Value>` |
| `luna.std.map` | unique-key ordered association | `map<Key, Value>` |
| `luna.std.queue` | FIFO container adaptor | `queue<Value>` |
| `luna.internal.pool` | stable raw node slots | non-generic storage + typed wrapper |
| `luna.internal.tree` | shared ordered index implementation | red-black `ordered_tree<Key, Value>` |
| `luna.std.memory` | raw allocation and byte primitives | narrow platform-independent functions |
| `luna.std.ascii` | ASCII classification and conversion | stateless functions/tables |
| `luna.std.binary` | endian-aware binary reading/writing | reader/writer value classes |
| `luna.std.charconv` | allocation-free numeric/text conversion | `to_chars` and `to_chars_result` |
| `luna.std.string` | owned mutable character sequence | move-only `string` over `byte_buffer` |
| `luna.std.string_view` | non-owning UTF-8 validated character view | `string_view` |
| `luna.std.path` | validated NUL-terminated path | move-aware `path` |
| `luna.std.io` | high-level whole-file and stream operations | algorithms over file, span and vector |

The current `luna.std.bytes` module is transitional. `byte_buffer` owns shared
byte algorithms once, while `vector<Value>` delegates storage to it to avoid
duplicating growth code in every consuming module. The legacy module is
deleted after its last atomic subsystem migration.

### Linux target services

| Module | Responsibility |
| --- | --- |
| `luna.linux.syscall` | raw x86-64 syscall ABI only |
| `luna.linux.process` | process id and termination |
| `luna.linux.memory` | typed `mmap`/`munmap` service |
| `luna.linux.file` | descriptors, open/read/write/close/chmod |
| `luna.linux.path` | rename/remove target operations |

The current `luna.runtime` implementation mixes all five responsibilities. It
is transitional. Error/result migration waits for `expected`; Linux services
move independently into `luna.linux.*`. After the last caller migrates, the
module is deleted rather than retained as a forwarding facade.

### Standard-library directory target

```text
library/
  include/luna/
    std/{expected,span,buffer,vector,deque,list,map,queue}.lh
    internal/{pool,tree}.lh
    linux/{syscall,process,memory,file,path}.lh
    std/{utility,checked,memory,ascii,binary,charconv,string,string_view,path,io}.lh
  src/
    linux/{syscall,process,memory,file,path}.la
    internal/pool.la
    std/{buffer,memory,ascii,binary,charconv,string,string_view,path,io}.la
    std/io/{read,write}.la              # same module when the facade grows
```

Do not create a directory for a single trivial implementation. A directory is
justified when it contains at least two cohesive method/pass families or when
the split is required immediately to keep a facade small.

## Stateful subsystem designs

### Frontend

`Lexer` owns the source cursor, token buffer, diagnostics and error state. Its
methods implement trivia, identifiers, literals and token dispatch. A keyword
strategy table maps text to token kinds. The free `lex` function is only a
facade that constructs a `Lexer` and returns its result.

`Parser` owns token traversal, nesting, diagnostics and syntax construction.
Expression, statement and declaration methods are split across same-module
implementation units. The historical exported `parser.state` module and the
long family of `parser_*(*State, ...)` procedures are deleted.

### Semantic analysis

`SemanticSession` owns the shared model and phase state. Domain records remain
plain structures; resolution and lowering behavior belongs to focused classes
or strategy objects. Builders own append/rollback invariants. Expression and
statement recursion is expressed through methods or a narrow strategy object,
not parent-facade imports.

### Backend and binary tools

`CodeGenerator`, `Assembler`, `ElfReader`, `ElfWriter` and `Linker` are
stateful classes. Encoding and ABI choices are strategies or tables. Object and
ELF records remain passive ABI structures. Output buffers are RAII containers.

### Drivers

The single `luna` executable uses one `luna.tools` module. Compile, assemble
and link are command objects or methods selected by a table. Separate command
binaries and one-interface-per-command modules are forbidden.

## File and directory rules

1. A facade contains contracts, construction and orchestration, not detailed
   algorithms. Aim for at most 250 lines.
2. An implementation unit owns one class method family, pass or encoding
   concern. The normal target is 150-800 lines; 1,200 is a review trigger and
   2,000 remains the hard soft-ceiling from `AGENTS.md`.
3. Split before a file contains three unrelated responsibilities or a function
   exceeds roughly 80 lines. A short coherent file is acceptable; an empty or
   one-line implementation unit is not.
4. Do not create `common`, `misc`, `helpers`, `utils`, `api` or `model` files.
   Name the file for its domain responsibility.
5. A directory mirrors either a real module or a multi-file implementation of
   one substantial module. Repeated single-child directories are collapsed.
   A rewritten directory level contains files or child directories, not both.
6. Every implementation path is registered in `LIBRARIES`; build order remains
   derived from source imports.

## Migration sequence

1. **Contract freeze:** keep the current green fixed point as the comparison
   baseline and land this design before more rewrites.
2. **Namespace and module contraction:** remove `bootstrap`, merge obvious fake
   submodules, update the registry and imports without behavior changes.
3. **Generic library foundation:** implement typed spans, move-only byte storage
   and a trivially-relocatable `vector<Value>`; reject nontrivial elements.
4. **RAII standard library:** migrate text, path, file and buffers; split Linux
   services; delete `luna.runtime` after its last caller moves.
5. **Object-oriented frontend:** introduce `Lexer` and `Parser`, then remove the
   procedural state APIs and fake parser submodules.
6. **Object-oriented semantic pipeline:** introduce `SemanticSession`, focused
   builders and lowering strategies while contracting semantic modules.
7. **Object-oriented backend:** migrate codegen, object/ELF, assembler and
   linker; include the observed large-assembly performance hotspot.
8. **Unified tools module:** migrate command services into `luna.tools`.
9. **Enforcement:** make module-depth, fake-module, file-size and condition
   rules executable `audit` checks after the existing codebase is clean.

Each numbered phase is split into independently green batches. A batch runs
`audit` and formatting first, then the smallest targeted behavior tests, then
the full `verify` and `test` gates before the next layer adopts it.

## Review checklist

Before any refactoring batch is accepted, answer all of these:

- Which class owns the mutable state and invariant?
- Which data remains a passive struct, and why?
- Which duplication is replaced by a generic abstraction?
- Which pattern represents a real variation or lifecycle?
- Which current Luna class/generic/lifetime features does this batch adopt?
- If a current feature is not used, why is it not appropriate here?
- Why is each module an independent dependency boundary?
- Why is each implementation file a coherent method/pass family?
- Were meaningless module components and single-child directories removed?
- Are all conditions at most two clauses and all traversals expressed at the
  right level?
- Do new resource types have explicit move/destruction behavior?
- Are `audit`, formatter, targeted tests, full tests and fixed point green?
