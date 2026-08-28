# ELF object I/O design

## Scope

`luna.compiler.x86.elf` is the single ELF64 ET_REL boundary of the x86-64
backend. It converts between untrusted little-endian ELF bytes and the trusted
`luna.compiler.x86.object::Object` model. The linker consumes only the object
model; it never carries a second ELF-shaped state representation.

The accepted input subset is deliberately narrow:

- `x86_64-unknown-linux-gnu`, ELF64, little-endian, System V, ET_REL;
- allocated `PROGBITS`/`NOBITS`, one `SYMTAB`, string tables and explicit-addend
  `RELA` records;
- `R_X86_64_64`, `PC32`, `PLT32`, `32` and `32S` relocations;
- unallocated metadata may be ignored, while TLS, COMDAT/groups, REL records,
  writable executable sections and unknown allocated sections are rejected.

The public facade remains the two value-oriented operations `load` and `save`.
The stateful work is private and cannot leak a partially constructed object or
buffer through the module interface.

## Object boundaries and lifetime

`ElfReader` owns its growing section table, symbol map and partial `Object`.
`ElfWriter` owns its output-section table, symbol map, scratch string/symbol/
relocation buffers and final output buffer. Constructors establish a valid
empty state. `take_result` transfers the completed result and resets the owned
member to empty; `deinit` therefore releases either an unfinished operation or
an already-transferred empty state safely.

The facade creates one stack-scoped operation object:

```text
load(bytes) -> ElfReader -> validate/decode -> Object -> take_result
save(Object) -> ElfWriter -> plan/encode    -> Buffer -> take_result
```

Errors are sticky: the first failure is retained, later emission becomes a
no-op, and cleanup cannot hide the original error. This makes sequences such
as an ELF header or section record readable field-by-field without a large
boolean expression.

Synthetic names for unnamed local/section symbols are built in caller-owned
stack storage and copied into `Object` before that storage expires. A
`string_view` is never returned into a callee-local array.

## Format dispatch and validation

ELF wire integers are decoded once into private enums. Closed alternatives use
`switch`:

- section type and section placement;
- symbol type and special symbol-section category;
- relocation type;
- object section, symbol kind, relocation kind and writer content source.

`if` is reserved for ranges, resource limits and boolean preconditions. Each
condition has at most two logical clauses; multi-field records use named
predicates and early returns. Indexed traversal uses `for`.

This separation matters for malformed inputs: `switch` proves that every
supported format kind has one explicit policy, while range checks remain close
to the field whose bounds they protect. Unknown unallocated section kinds may
be ignored; unknown allocated kinds and unknown relocations fail explicitly.

## Implementation units

All files implement the same `luna.compiler.x86.elf` module and share its one
interface:

| Unit | Responsibility |
| --- | --- |
| `format.la` | alignment/padding, endian reads, wire-value decoding, ELF and section headers |
| `reader.la` | section classification/layout, symbols, relocations and Reader lifetime |
| `writer.la` | output planning, deterministic tables/records and Writer lifetime |
| `facade.la` | exported `load`/`save` entry points only |

These are implementation families, not import boundaries; no `elf.*`
submodule is introduced.

## Current Luna feature review

| Feature | Decision |
| --- | --- |
| Classes and access control | Adopted for both stateful operations; state and invariants are private. |
| Constructors/destructors and RAII | Adopted for all vectors, scratch buffers, partial objects and output. |
| Generics | `vector<InputSection>`, `vector<OutputSection>` and `vector<SymbolMapping>` replace raw byte-backed tables. A domain mapping record also avoids duplicate cross-module `vector<usize>` monomorphs until the object format gains COMDAT/weak ODR merging. |
| Composition | Reader/Writer compose existing object, buffer and vector abstractions; inheritance adds no value. |
| Copy/move special members | Operation objects never escape the facade. Result transfer is explicit in `take_result`; copying an operation is neither exposed nor required. |
| Overloads/default arguments | No public operation has one semantic family needing variants, so distinct overloads/defaults would add ambiguity. |
| Operators | ELF records have no natural arithmetic or ordering operator contract. |
| Bound methods | Methods are invoked on their owning operation; there is no callback strategy requiring a stored bound method. |
| Friends | The facade needs only public `run`/`take_result`; friendship would widen access without a collaborating builder. |
| Virtual dispatch and RTTI | Format alternatives are closed enums, so `switch` is clearer and allocation-free. |

The design follows LLVM-style cohesive `ELFObjectFile`/writer operation
boundaries rather than copying C++ standard-library lowercase naming into a
compiler domain module.

## Verification contract

The integrated object round-trip case covers deterministic `save -> load ->
save`, relocation/symbol preservation, all truncated ELF headers, corrupt
magic, an out-of-range section table, a non-null section-zero record, an
invalid symbol-table link and an out-of-range relocation symbol. Full changes
must still pass `audit`, formatting, fixed-point `verify` and the complete test
suite on `caw`.
