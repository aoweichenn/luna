# Bootstrap reproducibility

Luna closes its self-hosting loop with freestanding Luna compiler, assembler
and linker drivers and a stage 1/stage 2/stage 3 fixed-point test. The path is
Luna source to the indexed syntax tree, verified Typed IR, the
correctness-first x86-64 backend, `LUNAOBJ1` bootstrap objects and a static
ELF64 executable. It never translates through C or C++, invokes target libc,
or runs an optimization pass.

## Stage definitions

- Stage 0 is the hosted C23 compiler used to establish the first trusted Luna
  executable.
- Stage 1 is the compiler, assembler and linker drivers compiled by stage 0
  and statically linked with the Luna bootstrap modules.
- Stage 2 is the complete source graph and all three drivers compiled by the
  stage-1 compiler, assembled by the stage-1 assembler and linked by the
  stage-1 linker.
- Stage 3 is the same graph compiled, assembled and linked by the three
  stage-2 tools.

Stage 1 and stage 2 use different code generators, so their bytes are not
expected to match. The fixed point is stage 2 equal to stage 3. The test
compares every generated assembly file, every serialized `LUNAOBJ1` object and
all three statically linked tool executables byte-for-byte.

## Freestanding driver protocol

An invocation with no user arguments deliberately selects the versioned fixed
protocol. It has an isolated working directory containing:

```text
bootstrap-stage-version
bootstrap-stage-mode
bootstrap-stage-unit-0.luna
bootstrap-stage-unit-1.luna
...
```

The version file is exactly `LUNA-STAGE/1 LUNA/1\n`; it is validated before
any other input. The mode file is exactly `E\n` for an executable or `L\n` for
a library.
Source names are contiguous and ordered, with unit zero defining the module
being compiled. At most 64 units are accepted, each source is capped at
8388608 bytes and their combined size is capped at 33554432 bytes. Successful
compilation writes `bootstrap-stage-output.s` in the project-owned closed
x86-64 assembly dialect and exits with status 42.

Resource exhaustion exits with status 8. Frontend failures use the stable
`frontend:<lex|parse>:<kind>:<offset>:<detail>` encoding. Driver-owned caps
use `resource:<source|total|frontend|semantic>:<unit>:<limit>`. Ordinary
semantic diagnostics retain their existing
`semantic:<kind>:<unit>:<offset>:<detail>` encoding.

The protocol makes every input explicit and avoids host argument, environment
and library dependencies. The same executable also exposes the normal
argument-driven interface documented in
[the complete bootstrap toolchain contract](bootstrap-toolchain.md); the
fixed-point gate continues to use this narrower protocol so CLI policy cannot
silently alter reconstruction inputs.

## Separate module compilation

Each library invocation receives:

1. the root module implementation;
2. the root module interface;
3. the interfaces in its reachable import closure.

An imported interface may omit its implementation. Every implementation that
is supplied must provide the definitions promised by its matching interface;
the production library invocation supplies only the root implementation.
Supplying an implementation that is not reachable from the root is rejected.
The compiler driver is compiled in executable mode from its implementation
followed by its dependency interfaces, while separately generated module
objects provide the linked definitions.

This boundary replaces stage-0 `.lmi` consumption inside the self-hosted
compiler. Stage 0 metadata is used only to create stage 1; stage 1 and later
stages compile a deterministic source graph directly.

## Assembly and linking

The Luna backend emits the closed assembly representation already owned by
the project. Stage 1 and later use
`luna.bootstrap.backend.x86_64.assembler` to produce validated `LUNAOBJ1`
objects and `luna.bootstrap.backend.x86_64.linker` to produce the static ELF64
tools. The linker builds its direct-syscall wrapper object with that same Luna
assembler.

The hosted C23 compiler and `lunalink` create stage 1 only. After that seed
boundary, neither the hosted assembler, hosted linker, GNU `as`, LLVM MC, GNU
`ld`, LLD nor libc participates. The CTest invocation no longer accepts a
seed-assembler argument, so accidentally restoring one is visible in the test
contract.

Every library object is checked not to define `_start`; each driver object
must define it. Final executables are checked for unresolved symbols,
`PT_INTERP` and dynamic-loader state. The precise object format, fixed-file
driver protocols and deliberately narrow linker contract are documented in
[the complete bootstrap toolchain contract](bootstrap-toolchain.md).

## Verification

`integration.bootstrap_reproducibility` performs:

1. construction of the stage-1 compiler, assembler and linker through the
   stage-0 seed;
2. missing/mismatched version, missing mode, missing input, lexical,
   malformed-source, semantic-error and 65-unit-limit negative tests with
   stable exit statuses and encodings;
3. independent compilation and Luna assembly of all 18 runtime,
   standard-library, frontend, middle-end, x86-64 backend, object, assembler
   and linker modules;
4. pure Luna static links of the stage-2 and stage-3 compiler, assembler and
   linker;
5. byte-for-byte comparison of 21 assembly files, 21 `LUNAOBJ1` objects and
   all three tool executables;
6. direct execution of Luna object/assembler/linker APIs, including object
   round-trip, a cross-object link, duplicate and unresolved symbol behavior;
7. fixed-seed accepted/rejected assembly cases, 48 malformed-object mutations
   and exact assembler/linker input-limit behavior;
8. an additional recursive-program probe compiled independently by stage 2
   and stage 3, project-assembled, statically linked and executed with status
   42;
9. a Luna 1 authority-transfer probe rejected by stage 0 but accepted and
   executed identically by stages 1, 2 and 3, including `break`, `continue`
   and a non-returning unconditional loop;
10. 23 fixed semantic execution cases and 32 fixed-seed generated combination
   cases compiled and executed through stage 0, stage 2 and stage 3, with
   byte-identical stage-2/stage-3 assembly;
11. reversed-order compilation of a complete interface/implementation import
   graph, requiring identical self-hosted assembly and execution;
12. one stage-0 rejection plus exact stage-2/stage-3 diagnostic agreement for
   every public semantic diagnostic kind from 1 through 50;
13. 117 exact-rational binary32/binary64 cases generated independently from
   IEEE encodings, covering deterministic random adjacent-value midpoints,
   ties-to-even, both sides of each midpoint, subnormal/normal and binade
   boundaries, exact extrema, huge exponents and more than 1200 significant
   digits;
14. exact boundary and one-over tests for source bytes, total source bytes,
   token length, token count, frontend and semantic diagnostic counts, parser
   nesting and semantic type-layout depth;
15. execution of that floating-point probe through stage 0, stage 2 and stage
   3, plus byte equality of stage-2/stage-3 output and stable rejection of the
   maximum-finite/infinity midpoint for both widths.
16. two independent canonical seed creations, the tracked external checksum,
    strict manifest/tar/static-ELF verification, payload/checksum/truncation/
    trailing-data/path-traversal mutations, and a complete offline rebuild
    from packaged tools and packaged sources with byte-identical results.

The regular middle-end and backend gates separately retain source-order,
module-boundary, type-layout, IR-verifier, ABI, execution, random and fuzz
coverage. Fixed-point equality is a reproducibility property and does not
replace those semantic correctness tests.
