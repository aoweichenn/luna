# Bootstrap reproducibility

M4 closes its self-hosting loop with a freestanding Luna compiler driver and
a stage 1/stage 2/stage 3 fixed-point test. The compiler path is Luna source
to the indexed syntax tree, verified Typed IR and the correctness-first
x86-64 backend. It never translates through C or C++, invokes libc, or runs an
optimization pass.

## Stage definitions

- Stage 0 is the hosted C23 compiler used to establish the first trusted Luna
  executable.
- Stage 1 is `tools/bootstrap/stage_compiler.luna` compiled by stage 0 and
  statically linked with the Luna bootstrap modules.
- Stage 2 is the complete compiler source graph compiled by stage 1.
- Stage 3 is the same ordered source graph compiled by stage 2.

Stage 1 and stage 2 use different code generators, so their bytes are not
expected to match. The fixed point is stage 2 equal to stage 3. The test
compares every generated assembly file, every project-assembled ELF64
relocatable object and the final statically linked compiler executable
byte-for-byte.

## Freestanding driver protocol

The stage driver deliberately has no command-line parser. Each invocation has
an isolated working directory containing:

```text
bootstrap-stage-mode
bootstrap-stage-unit-0.luna
bootstrap-stage-unit-1.luna
...
```

The mode file is exactly `E\n` for an executable or `L\n` for a library.
Source names are contiguous and ordered, with unit zero defining the module
being compiled. At most 64 units are accepted. Successful compilation writes
`bootstrap-stage-output.s` in the project-owned closed x86-64 assembly
dialect and exits with status 42.

The protocol makes every input explicit and avoids host argument, environment
and library dependencies. It is a narrow bootstrap interface, not the future
user-facing compiler CLI.

## Separate module compilation

Each library invocation receives:

1. the root module implementation;
2. the root module interface;
3. the interfaces in its reachable import closure.

An imported interface may omit its implementation. Only the root module must
provide the definitions that its interface promises. Supplying an
implementation that is not reachable from the root is rejected. The compiler
driver is compiled in executable mode from its implementation followed by its
dependency interfaces, while separately generated module objects provide the
linked definitions.

This boundary replaces stage-0 `.lmi` consumption inside the self-hosted
compiler. Stage 0 metadata is used only to create stage 1; stage 1 and later
stages compile a deterministic source graph directly.

## Assembly and linking

The Luna backend emits the closed assembly representation already owned by
the project. `luna_bootstrap_assembler` converts it to verified ELF64
relocatable objects, and `lunalink` produces the static compiler executable.
Neither GNU `as`, LLVM MC, a system linker, LLD nor libc participates in the
production bootstrap chain.

Every library object is checked not to define `_start`; the driver object must
define it. Final executables are checked for unresolved symbols, `PT_INTERP`
and dynamic-loader state.

## Verification

`integration.bootstrap_reproducibility` performs:

1. stage-1 driver construction through stage 0;
2. missing-mode, missing-input, malformed-source, semantic-error and
   65-unit-limit negative tests with stable exit statuses;
3. independent compilation and assembly of all 15 runtime, standard-library,
   frontend, middle-end and x86-64 backend modules;
4. stage-2 and stage-3 static compiler links;
5. byte-for-byte comparison of 16 assembly files, 16 ELF objects and the
   compiler executable;
6. an additional recursive-program probe compiled independently by stage 2
   and stage 3, project-assembled, statically linked and executed with status
   42;
7. 117 exact-rational binary32/binary64 cases generated independently from
   IEEE encodings, covering deterministic random adjacent-value midpoints,
   ties-to-even, both sides of each midpoint, subnormal/normal and binade
   boundaries, exact extrema, huge exponents and more than 1200 significant
   digits;
8. execution of that floating-point probe through stage 0, stage 2 and stage
   3, plus byte equality of stage-2/stage-3 output and stable rejection of the
   maximum-finite/infinity midpoint for both widths.

The regular middle-end and backend gates separately retain source-order,
module-boundary, type-layout, IR-verifier, ABI, execution, random and fuzz
coverage. Fixed-point equality is a reproducibility property and does not
replace those semantic correctness tests.
