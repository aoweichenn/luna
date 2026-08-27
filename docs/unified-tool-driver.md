# Unified Luna tool driver

## Status

The pure-Luna source tree builds one freestanding executable named `luna`.
The checked-in M4 anchor remains a three-tool transition input until the
verified unified executable is promoted. The source implementation is green
behind a byte-identical fixed point and the 416-case suite.

## Command surface

The executable exposes three explicit commands:

```sh
luna compile --executable -o app.s app.la dependency.lh
luna assemble -o app.lo app.s
luna link -o app app.lo
```

`luna --help` and `luna --version` describe the complete tool. Each command
also owns its command-specific help, version, validation and exit-status
contract. Unknown or missing commands return status 125.

The fixed bootstrap protocol remains available without an ambiguous root
mode: `luna compile`, `luna assemble` and `luna link` with no following
arguments select the existing fixed filenames and retain their successful
status 42. Normal argument-driven calls always include command options or
input paths.

## Architecture

The executable is unified; the three operations are not:

```text
luna.tools
  ├─ luna.tools.compile
  ├─ luna.tools.assemble
  └─ luna.tools.link
          │
          └─ luna.tools.cli
```

The root owns only command lookup, top-level help/version and `switch`
dispatch. `luna.tools.cli` owns the small table-driven command vocabulary,
C-string comparison, output and first-error propagation. Each command module
retains its own domain state, resource limits, fixed protocol and underlying
compiler/assembler/linker dependencies.

The compiler, assembler, object, ELF and linker modules remain unchanged
dependency boundaries. No command imports the root facade, and no operation
can fall through into another command.

## Self-hosting transition

`tools/selfhost.py` resolves a toolchain directory through one compatibility
boundary:

- a directory containing `luna` runs `luna compile`, `luna assemble` and
  `luna link`;
- otherwise the current transition anchor supplies `lunac`, `luna-as` and
  `luna-link`.

The old anchor can therefore build a transition stage containing only
`bin/luna`. Every later stage uses the unified commands, and fixed-point
comparison requires one driver assembly, object and executable. The module
objects remain independently compared, preserving component-level
determinism even though distribution has one binary.

The old anchor linker accepts only 64 input objects, while the unified driver
closure contains 75. During that first transition only, `selfhost.py` builds a
small linker bridge from `luna.tools.link`, uses it to link `bin/luna`, and
removes the bridge work directory. Unified stages raise the explicit linker
input bound to 128 and require no bridge.

## Deliberate limits

This change does not add an in-process `luna build` pipeline. Compilation
still emits the closed assembly dialect, assembly still emits an object and
linking still consumes explicit objects. Keeping those observable boundaries
preserves the current failure isolation, atomic output guarantees and simple
self-hosting graph. A future build command may orchestrate the three services
after module/package discovery has its own design.
