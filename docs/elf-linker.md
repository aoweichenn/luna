# Project-owned ELF64 static linker

> **Historical m0 contract.** This document records the hosted `lunalink`
> reconstruction. The current freestanding driver is `luna-link`; its source
> and current CLI are authoritative, and current Luna symbols carry module and
> function names rather than `.lmi` fingerprints.

`lunalink` turns x86-64 ELF64 relocatable objects into a static Linux
executable without invoking LLD, GNU ld, an assembler or a host compiler:

```sh
lunac --emit obj -o app.o app.la
lunalink -o app app.o
```

The default entry symbol is `_start`; `-e name` selects another symbol. The
output is always static and has no ELF interpreter, dynamic section, startup
object or external implicit library. The linker does append its canonical
project-owned Linux system-call ABI object; that object contains only direct
`syscall` wrappers and has no further dependencies. `--static` and `-static`
are accepted only for driver compatibility.

The bootstrap `lunalink` process is a hosted C23 tool and may use its host
library for file I/O and allocation. Those host dependencies do not appear in
the generated executable. Target code still enters project-owned `_start` and
uses the verified zero-to-six-argument direct Linux system-call layer without
libc.

## Input contract

The first linker version deliberately implements a strict, auditable ELF
subset:

- ELF64, little-endian, `ET_REL`, `EM_X86_64` objects;
- allocated `PROGBITS` code, read-only data and writable data;
- writable `NOBITS` storage for BSS;
- one regular symbol table with local, global, weak, undefined, section and
  absolute symbols;
- explicit-addend `RELA` relocations;
- section alignment no greater than the x86-64 Linux page size;
- Luna-generated objects and compatible freestanding Clang C23 objects.

The bootstrap boundary limits one input object to 256 MiB, accepts at most
65,535 objects and limits merged allocated contents to 512 MiB. These limits
make malformed or adversarial inputs fail deterministically before unchecked
resource growth.

Malformed headers, ranges, strings, symbol ordering, section references and
relocation references are rejected before their data is used. Allocated TLS,
compressed or writable-executable sections are rejected. `COMMON` symbols
must be compiled with `-fno-common`.

Archives, shared objects, dynamic linking, PLT/GOT synthesis, TLS, COMDAT,
linker scripts, symbol versioning and section garbage collection are not
silently approximated. Debug input is limited to Luna's versioned
`.luna.debug`; foreign `.debug_*` and `.zdebug_*` sections are rejected rather
than partially merged.

## Layout

Input order and section order define a deterministic layout. The linker merges
input allocations into four logical output sections:

| Output | ELF properties | Segment |
| --- | --- | --- |
| `.text` | `PROGBITS`, allocatable, executable | read/execute |
| `.rodata` | `PROGBITS`, allocatable | read-only |
| `.data` | `PROGBITS`, allocatable, writable | read/write |
| `.bss` | `NOBITS`, allocatable, writable | read/write zero fill |

Each input contribution retains its alignment. Load segments begin on a
4096-byte boundary, use a fixed `0x400000` image base and never combine write
and execute permission. The canonical syscall ABI object is appended after
caller inputs, and its seven symbols cannot be overridden by another strong
definition. The output also contains `.shstrtab` so ordinary ELF
inspection tools can report its sections. When an input carries project Debug
IR, non-allocatable `.debug_abbrev`, `.debug_info`, `.debug_line`,
`.debug_str` and `.debug_line_str` sections follow the allocated payload. They
never enter a load segment.

## Symbol resolution

Local symbols remain scoped to their input object. Global resolution follows
normal static rules:

- two strong definitions are an error;
- a strong definition replaces a weak definition;
- the first weak definition is deterministic when no strong definition
  exists;
- an unresolved weak reference has address zero;
- every referenced non-weak undefined symbol is an error.

The selected entry must resolve to an allocated executable section. Module
metadata fingerprints remain part of exported Luna symbol names, so stale
separately compiled modules fail as ordinary undefined-symbol errors.

## Relocations

The linker currently implements the static x86-64 relocations needed by Luna
and the freestanding C23 integration boundary:

| Relocation | Calculation | Range |
| --- | --- | --- |
| `R_X86_64_64` | `S + A` | unsigned 64-bit |
| `R_X86_64_PC32` | `S + A - P` | signed 32-bit |
| `R_X86_64_PLT32` | `S + A - P` | signed 32-bit |
| `R_X86_64_32` | `S + A` | unsigned 32-bit |
| `R_X86_64_32S` | `S + A` | signed 32-bit |

`S` is the resolved symbol address, `A` the explicit addend and `P` the
relocation field address. Since this is a non-PIC static link,
`R_X86_64_PLT32` resolves directly like `PC32`. Every calculation is checked
before writing; overflow is a hard diagnostic rather than truncation.

## Output and verification

The result is a little-endian `ET_EXEC` file containing one to three
`PT_LOAD` segments. File output is transactional: the complete executable is
linked and verified in memory, written to a unique temporary file, flushed,
made executable and atomically renamed over the destination. A failed link
does not replace the prior output.

The public executable verifier independently reparses the serialized bytes and
checks:

- ELF identity, target, fixed header sizes and bounded table counts;
- program and section table ranges;
- load-segment ordering, alignment, file/memory sizes and W^X;
- entry-point membership in the executable segment;
- the null section and bounded, terminated section names;
- supported section types, flags, ranges and alignments.

GoogleTest covers deterministic output, cross-object calls, undefined and
duplicate symbols, malformed inputs and corrupted executables. Integration
tests execute the complete language corpus, a three-object module graph and
real C23 objects with text, constants, data and BSS, including a zero-fill-only
writable segment. Random programs use `lunalink` for their executable path.
Deterministic object and executable mutations plus libFuzzer exercise the
untrusted-input boundary. LLD is retained only as an independent test oracle.
