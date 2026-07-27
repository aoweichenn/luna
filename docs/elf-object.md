# Native ELF64 relocatable objects

Luna emits x86-64 Linux relocatable objects without invoking LLVM, GNU
binutils or a host C compiler. The command-line boundary is:

```sh
lunac --emit obj -o program.o program.luna
```

The result is an ELF64 little-endian `ET_REL` object for `EM_X86_64`. It can be
linked with LLD while the project-owned static linker remains unfinished.
Generated target code is freestanding and the object introduces no libc
dependency.

## Backend boundary

The instruction selector and allocation-aware rewrite still own semantics and
instruction choice. Object emission does not optimize, select replacement
instructions or change ABI decisions. It encodes the same closed x86-64
instruction dialect used by `--emit asm`.

The current bootstrap implementation renders that compiler-owned dialect into
an in-memory buffer and immediately consumes it with Luna's internal
assembler. This is a deliberately closed interface, not support for arbitrary
GNU assembly:

- only directives and instruction forms produced by the Luna backend are
  accepted;
- unknown directives, operands, instructions, duplicate labels and unresolved
  branches are hard errors;
- there is no external-assembler fallback;
- assembly remains available as a review artifact, but LLVM MC is not in the
  `--emit obj` compiler path.

Replacing the textual in-memory boundary with a structured encoded-instruction
stream is permitted later, provided the verified rewrite and object contracts
remain unchanged.

## Sections

Objects contain the smallest deterministic subset required by the module:

| Section | Purpose |
| --- | --- |
| `.text` | executable x86-64 bytes, aligned to at least 16 bytes |
| `.rodata` | immutable static bytes when present |
| `.data` | writable static bytes when present |
| `.rela.text` | explicit-addend relocations when present |
| `.symtab` | section, local, exported and undefined symbols |
| `.strtab` | symbol names |
| `.note.GNU-stack` | marks the target stack non-executable |
| `.shstrtab` | section names |

Local symbols precede global symbols as required by ELF `sh_info`. Function and
object sizes come from the backend's `.size` boundaries. Separately compiled
exports retain their module metadata fingerprint in the ELF symbol name, so
the final link still rejects stale module objects.

## Relocations

The writer currently needs two x86-64 relocation kinds:

- `R_X86_64_PC32` with addend `-4` for RIP-relative static-data addresses;
- `R_X86_64_PLT32` with addend `-4` for unresolved module and external C
  calls.

Named and numeric branches, plus calls to functions defined in the same text
section, are resolved directly as signed 32-bit displacements. A branch that
does not resolve inside `.text` is rejected rather than converted into an
unexpected linker contract.

## Verification

Every object is structurally verified before it leaves the compiler. The
verifier treats the serialized bytes as untrusted and checks:

- ELF identity, class, byte order, machine, type and fixed header sizes;
- absence of program headers in a relocatable object;
- overflow-safe section ranges, alignment and section-name termination;
- symbol-table ordering, names, bindings, types and section bounds;
- relocation entry sizes, symbol indices, target offsets and supported types;
- all linked-section cross references.

The same verifier is public to unit and mutation tests. Deterministic byte
mutations, truncations and corrupted cross references exercise rejection
paths under GoogleTest and libFuzzer.

## Correctness gates

The native object path is tested at four levels:

1. byte-exact unit tests for representative GPR, extended-register, SSE,
   string, branch and syscall encodings;
2. ELF section, symbol, relocation, determinism and malformed-dialect tests;
3. end-to-end links for the complete integration corpus, external C ABI and
   three-object metadata-backed module graph, with LLVM MC objects retained as
   an independent encoding oracle;
4. deterministic random and instruction-level differential programs linked
   from native Luna objects and executed natively or under
   `qemu-x86_64-static`.

LLD remains only the final-link test and development tool. It is not used to
encode instructions or construct relocatable objects.
