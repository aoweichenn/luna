# Complete Luna-owned bootstrap toolchain

Luna's fixed-point toolchain consists of three freestanding x86-64
executables, all implemented in Luna:

1. the compiler lowers Luna source to the project-owned closed assembly
   dialect;
2. the assembler encodes that dialect into a validated `LUNAOBJ1` object;
3. the linker resolves those objects and writes a static Linux ELF64
   executable.

The tools use the Luna runtime and direct Linux system calls. They do not call
libc, GNU `as`, GNU `ld`, LLVM MC, LLD or a C compiler. They perform no
optimization.

## Reconstruction boundary

Stage 0 is the frozen hosted C23 reconstruction seed. It compiles and links
the three stage-1 Luna drivers once. That is the complete hosted boundary.

Stage 1 compiles every stage-2 library and driver, assembles every generated
assembly file with the Luna assembler and links all three stage-2 tools with
the Luna linker. Stage 2 performs the same operations to construct stage 3.
The test then requires byte equality for:

- every stage-2/stage-3 assembly file;
- every serialized stage-2/stage-3 `LUNAOBJ1` file;
- the compiler, assembler and linker executables.

Consequently a passing fixed point does not hide a hosted assembler or linker
between the self-hosted stages.

## Bootstrap object format

`LUNAOBJ1` is deliberately a small bootstrap format, not an approximation of
the historical ELF relocatable-object surface. All integers are unsigned
little-endian unless noted otherwise.

The 112-byte header contains:

| Offset | Field |
| ---: | --- |
| 0 | eight-byte magic `LUNAOBJ1` |
| 8 | 32-bit version, currently 1 |
| 12 | 32-bit header size, currently 112 |
| 16 | text byte count |
| 24 | read-only-data byte count |
| 32 | writable-data byte count |
| 40 | zero-filled BSS byte count |
| 48 | symbol-name byte count |
| 56 | symbol-record count |
| 64 | relocation-record count |
| 72 | text alignment |
| 80 | read-only-data alignment |
| 88 | writable-data alignment |
| 96 | BSS alignment |
| 104 | reserved, required to be zero |

The four stored byte regions follow the header in text, read-only-data,
writable-data and names order. Each 56-byte symbol record then stores the name
range, section, value, size, flags and a zero reserved field. Flags represent
defined, global, external, function and object state; unknown flag bits and
conflicting function/object flags are rejected. Each 40-byte relocation
record stores section, offset, symbol index, relocation kind and signed
two's-complement addend.

Only text, read-only data, writable data and BSS are representable. The
supported relocations are signed PC-relative 32-bit, call-compatible
PC-relative 32-bit and absolute 64-bit. Names must be nonempty valid UTF-8.
Section ranges, symbol ranges, relocation widths, record counts, exact file
extent, reserved fields and power-of-two alignments are validated before use.
Payload regions are individually capped at 64 MiB, record counts at 65,536
and alignment at 4096.

This format is internal to fixed-point reconstruction. The final program is a
standard static ELF64 executable.

## Assembler contract

`luna.bootstrap.backend.x86_64.assembler` accepts only the directives,
registers, operands and integer/SSE instruction forms emitted by the
correctness-first Luna backend. It supports named and numeric labels,
compiler-owned section directives, symbol declarations and explicit
relocations. Unknown directives or instructions, malformed operands,
duplicate definitions, unresolved local branches, overflow and unsupported
encodings are hard errors.

The freestanding stage driver reads exactly
`bootstrap-assembly-input.s`, writes `bootstrap-object-output.lo` on success
and exits with status 42. Assembly errors exit with status 2 and report the
stable `assembler:<line>` diagnostic. Input, serialization, output and
release failures have distinct non-success statuses. Input is capped at
64 MiB and a line at 1 MiB.

## Linker contract

`luna.bootstrap.backend.x86_64.linker` accepts at most 64 `LUNAOBJ1` inputs.
It merges aligned text, read-only-data, writable-data and BSS regions,
resolves input-local and global symbols, rejects duplicate strong definitions
and unresolved references, applies all relocations with range checks and
requires `_start` to be a defined function in text.

The linker assembles its canonical zero-to-six-argument syscall wrappers with
the same Luna assembler and appends that object to the link. It serializes
ELF64 `ET_EXEC` directly with image base `0x400000`, page-aligned RX, R and RW
load segments and no section-header table, dynamic loader or implicit
library. The output is capped at 256 MiB and is created with executable mode
through direct Linux system calls.

The stage driver reads contiguous files named
`bootstrap-link-input-0.lo` through `bootstrap-link-input-63.lo`. A present
65th input is rejected rather than ignored. Success writes executable
`bootstrap-link-output` and exits with status 42. Input, object decoding,
linking, output and release failures have distinct statuses.

## Verification

`integration.bootstrap_reproducibility` is the ownership gate. In addition to
the complete three-tool fixed point, it:

- compiles and executes a Luna driver that calls the object, assembler and
  linker APIs directly;
- round-trips a serialized object byte-for-byte;
- links a two-object cross-symbol program and validates the produced ELF;
- checks malformed assembly, duplicate symbols, unresolved references,
  relocation overflows, non-text entry points and input limits;
- runs fixed-seed accepted and rejected assembly cases;
- runs deterministic truncation, extension, magic, version, alignment and
  reserved-field mutations against the object decoder;
- checks unresolved and duplicate link definitions;
- verifies final ELF program headers and executes semantic probes.

The general stage-0 ELF writer and hosted linker remain useful for rebuilding
stage 1 and for broader compatibility testing. They are not dependencies of
stage 2 or stage 3.
