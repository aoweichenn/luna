# x86-64 instruction-level differential testing

The instruction-level differential suite is the semantic gate between verified
machine IR and emitted x86-64. It deliberately performs no optimization and
does not reuse the x86-64 emitter to calculate expected results.

Each accepted program follows two independent paths:

```text
Luna source
    -> verified textual machine IR
    -> independent parser and reference interpreter
    -> reference result or trap signal

Luna source
    -> ABI analysis, liveness, allocation and instruction rewrite
    -> Luna native ELF64 object and project-owned static linker
    -> native or QEMU execution
    -> emitted result or trap signal

Luna source
    -> x86-64 assembly review output
    -> LLVM MC object and LLD
    -> independent binary-toolchain oracle
```

The test succeeds only when both paths agree. Generated programs also retain
the existing source-level oracle, so a random case is checked independently at
the source, machine-IR and native-execution levels.

## Reference boundary

`tests/differential/machine_ir_reference.py` parses the public deterministic
machine-IR text rather than compiler-owned in-memory structures. This gives the
reference path an independent serialization boundary and detects malformed or
incomplete textual machine IR.

The interpreter models:

- exact-width signed and unsigned integer wrapping;
- x86-64 masked shift counts and signed division traps;
- IEEE `f32` and `f64` values, arithmetic, ordered comparisons and conversions;
- scalar stack slots, indirect little-endian memory access and read-only data;
- null and bounds traps;
- pointer offsets and pointer/integer round trips;
- overlap-safe memory copies;
- internal calls, recursion, control flow and aggregate by-value snapshots;
- an instruction budget and call-depth limit for deterministic failure.

Target traps are part of the observable contract. Integer division faults must
match `SIGFPE`; explicit safety and conversion traps must match `SIGILL`.

External declarations are intentionally rejected by the reference interpreter.
External ABI behavior remains covered by the dedicated real-C integration
tests and is not an instruction semantic oracle.

## Coverage contract

The deterministic fixture corpus must execute every current machine opcode.
The suite fails if an opcode is absent or an unknown opcode appears. It also
requires instruction-rewrite evidence for:

- at least one physical spill and a later spilled use;
- parallel call-argument moves;
- fixed constraints involving `RAX`, `RCX`, `RDX` and an XMM register.

A dedicated nested-expression program forces register pressure and validates
that spilled code has the same result as the reference interpreter.

The bounded random corpus uses a fixed seed and covers every numeric width,
floating arithmetic, scalar conversions, structured control flow, memory,
aggregates and aggregate-by-value calls. A failure reports the seed, case index
and complete generated source and preserves all machine IR, rewrite, assembly,
object and executable artifacts in the build directory.

## Running

The reference unit tests require only Python:

```sh
python3 tests/differential/test_machine_ir_reference.py
```

The executable differential gate additionally requires `llvm-mc` and `ld.lld`
for the independent oracle, plus either a native x86-64 Linux host or
`qemu-x86_64-static`:

```sh
ctest --test-dir build/debug -R instruction_differential --output-on-failure
```

LLVM MC is now an independent encoding oracle: its object and Luna's native
object are linked through independent linkers and executed against the
reference result. LLVM MC and LLD are not in the native compiler or linker
path.
