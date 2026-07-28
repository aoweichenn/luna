# Bootstrap language versions

Luna separates the language needed to reconstruct the first self-hosted
compiler from the language implemented by that compiler. This prevents every
post-bootstrap language feature from requiring a second implementation in the
hosted C23 seed.

## Authority boundary

`Luna 0` is the frozen bootstrap source language. The hosted C23 compiler is
the reference implementation for Luna 0 and remains the initial trusted seed.
All source needed to build the first self-hosted compiler must remain valid
Luna 0.

`Luna 1` is the language owned by the self-hosted compiler. New Luna 1
features are implemented in the Luna compiler modules under
`runtime/luna/bootstrap/`; they do not require a matching C23 implementation.
The first implementation of a feature must itself use Luna 0 syntax so the
existing seed can build it.

An extension follows this order:

1. an existing compiler builds a new compiler whose implementation still uses
   the previous accepted language;
2. the new compiler is tested on programs using the extension;
3. only then may compiler sources adopt the extension;
4. two subsequent self-compilations must reach the byte-for-byte fixed point.

The hosted compiler remains a reconstruction seed and a Luna 0 differential
oracle. It is not the language authority for Luna 1.

## Versioned stage protocol

Every self-hosted compiler invocation requires
`bootstrap-stage-version` with the exact 20-byte content:

```text
LUNA-STAGE/1 LUNA/1
```

The final newline is required. Missing, truncated, extended or mismatched
content is rejected with stage status 9 before the mode or any source is read.
This makes the protocol version and requested language version explicit
instead of silently interpreting a source graph with whichever compiler
happens to be present.

The existing `bootstrap-stage-mode` and indexed source files remain protocol
version 1. Changing their names, limits, success status or output contract
requires a new protocol version rather than an in-place reinterpretation.

## First Luna 1 feature

Luna 1 adds an unconditional loop statement:

```luna
loop {
    if (finished) {
        break;
    }
    step();
}
```

The body is required to be a block. `continue` transfers to the beginning of
the body and `break` transfers to the statement following the loop. A loop
with no reachable `break` has no reachable successor, so it satisfies a
non-`void` function's return requirement without fabricating a return value.

`loop` is reserved in Luna 1. Luna 0 does not recognize the statement. The
bootstrap gate proves that the C23 seed rejects the Luna 1 probe, the
seed-built stage 1 accepts it, all three self-hosted stages produce identical
probe assembly, and the linked probe executes successfully.

## Remaining trusted tools

Language authority transfer does not claim that the complete build toolchain
is already written in Luna. The first seed is still built by the hosted C23
compiler, and the current bootstrap fixed-point harness still uses the
project-owned C implementations of the closed x86-64 assembler and static
ELF64 linker. Those are explicit later migration boundaries; they do not
participate in parsing, typing, IR construction or code generation decisions
for Luna 1.
