# C FFI and the exit strategy

Luna programs stay freestanding and link no libc. The C FFI described
here is an explicit boundary for calling external objects, not a runtime
dependency — see `architecture.md`. This document records the boundary
contract and the planned way back off C dependencies.

## Luna calling C

`extern fn` declarations name C symbols with System V ABI classification
(scalars, aggregates, variadic calls with the `%al` protocol). The linker
consumes both the project's own LUNAOBJ1 objects and standard ELF64
relocatable objects (`compiler/src/backend/x86_64/elf/reader.la`, behind the
`compiler/src/backend/x86_64/elf/facade.la` facade), mapping
sections onto the four-region model and applying `PC32`, `PLT32`, `64`,
`32` and `32S` relocations. TLS, COMDAT, REL relocations and other
unsupported content are rejected; notes and debug sections are dropped.

External objects must be built within the supported subset. The
reference flag set is:

```sh
gcc -ffreestanding -fno-stack-protector -fno-pic -fno-common \
    -fno-asynchronous-unwind-tables -mcmodel=small -c
```

A small set of libc symbols is provided by Luna code under verbatim C
names (`@export_name`): the memory and string functions plus a
header-mapped `malloc`/`free`/`calloc`. These shims are what makes a
freestanding C library self-sufficient in a Luna link; they are not a
libc and are not linked into anything that does not ask for them.

musl or glibc wholesale static linking is deliberately not a goal:
TLS setup, archives, crt startup and environ/auxv handling make it an
order of magnitude more work than the boundary above, for capabilities
the runtime already owns.

## C calling Luna

`luna assemble --emit elf` turns Luna assembly into a standard ELF64 `ET_REL`
object that any host linker accepts. `elf::save`
(`compiler/src/backend/x86_64/elf/writer.la`, behind the
`compiler/src/backend/x86_64/elf/facade.la` facade)
serializes the shared object model
with one section per region (`.text`/`.rodata`/`.data`/`.bss`), a
locals-first symbol table, `RELA` relocations with explicit addends and
an empty `.note.GNU-stack`; the default `lunaobj` output is unchanged.

A Luna function becomes visible to C by carrying
`@export_name("symbol")`, which emits the verbatim unmangled global.
Definitions use the same System V ABI as the `extern fn` call side, so
scalar and narrow-integer arguments, `i64` values and pointers round-trip
without shims:

```sh
luna compile --library -o answer.s answer.la
luna assemble --emit elf -o answer.o answer.s
gcc -no-pie -o app cmain.c answer.o
```

Objects destined for a host link come from `luna compile --library` (no
`_start`); Luna objects are small-model and non-PIC by construction, so
`-no-pie` (or `-fno-pic` on the C side) is the matching host mode. The
writer's output is validated two ways in the test suite: round-trips
through the project's own ELF reader and linker (`elf-link` expectation
shape) and an end-to-end gcc link of a C `main` against Luna
`@export_name` definitions (`host-link` shape, `tests/ffi/c_calls_luna`).

## The exit strategy

The shim layer is also the replacement surface. Every C-ABI symbol
(`memcpy`, `malloc`, ...) is implemented in Luna from day one, so a C
dependency is always one interface module behind a Luna-native
replacement:

1. A C library is introduced behind one interface module declaring its
   functions as `extern fn`, with shims covering its libc needs.
2. When a Luna-native implementation of the library exists, the
   interface module's declarations are rebound to it; call sites do not
   change.
3. When no C objects remain in a link, the ELF reader stays as a thin
   interop boundary and the runtime/std stack (already pure Luna) is
   the sole runtime.

Two things on the standard-library roadmap make the exit complete: a
real free-list allocator in `luna.std.memory` (the shim's mmap-per-call
is a stand-in), and graduating the shim symbols from test fixtures into
a proper library module once a consumer outside tests exists.
