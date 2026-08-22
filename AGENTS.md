# AGENTS.md

Luna: a C23-derived systems language. Stage-0 compiler is C23 (`src/`); the
self-hosting compiler/assembler/linker are written in Luna (`runtime/luna/`,
`tools/bootstrap/`). Sole target: `x86_64-unknown-linux-gnu`, System V, ELF64,
no libc in generated programs.

## Build & test

Presets hardcode Clang + Ninja (see `CMakePresets.json`): `debug`, `sanitize`
(UBSan), `asan` (ASan+UBSan), `release`, `fuzz`, `fuzz-asan`.

```sh
cmake --preset debug && cmake --build --preset debug
ctest --preset debug                 # full suite
ctest --preset debug -L unit         # labels: unit | integration | random | fuzz
ctest --preset debug -R SemaTest     # single test by name (gtest prefix "unit.")
```

- GTest >= 1.14 and Python >= 3.10 required when `BUILD_TESTING=ON`.
- Warnings-as-errors everywhere (`-Wconversion -Wsign-conversion -Wshadow ...`)
  for both C23 sources and C++20 test code.
- CI also runs `clang-format --dry-run --Werror` over all `.c/.h/.cpp/.hpp` in
  `include/ src/ tests/` — format before finishing (LLVM style, 4-space indent,
  80 columns).

### Test host requirements

- Integration/random/differential runners execute generated x86-64 binaries:
  they need `qemu-x86_64-static` on non-x86-64 hosts and skip with exit 77
  otherwise. On native x86-64 Linux they run binaries directly without qemu.
- Differential tests use `llvm-mc`, `ld.lld` and `clang` as independent
  oracles; missing tools also cause skip 77.
- The `asan` preset cannot reserve shadow memory on some Android/PRoot AArch64
  environments.

## Self-hosted toolchain

```sh
cmake --build --preset debug --target luna_selfhost_toolchain
```

Reconstructs stage 1 from the C23 seed, builds stage 2 with it, stage 3 with
stage 2, then byte-compares every compiler/assembler/linker artifact between
stages 2 and 3. Slow (1h timeout) but the core correctness gate for any change
to Luna sources under `runtime/luna/` or `tools/bootstrap/`.

Gotcha: `luna_bootstrap_seed` verifies the built archive checksum against the
pinned file
`release/seeds/luna-bootstrap-seed-<VERSION>-x86_64-unknown-linux-gnu.tar.sha256`.
Bumping `VERSION` invalidates that pin until the seed checksum is regenerated
(`tools/release/bootstrap_seed.py create`).

## Layout

- `include/luna/` + `src/{frontend,middleend,backend,target,runtime}` — stage-0
  C23 compiler. Executables: `lunac` (src/frontend/compiler/main.c),
  `lunalink` (src/backend/x86_64/linker/main.c).
- Backend pipeline is strictly ordered: typed IR → machine IR → liveness →
  register allocation → allocation-aware rewrite → assembly → ELF object →
  static link. Each phase has `_print.c` and `_verify.c` variants; assembly
  consumes only a verified rewrite. Snapshot tests assert deterministic output.
- `runtime/luna/**` — pure-Luna sysroot modules (runtime, `std/*`, bootstrap
  frontend/middleend/backend). Built into `build/<preset>/sysroot/` by the
  always-built `luna_sysroot` target; each module needs its `.lmi` metadata
  registered via `luna_add_sysroot_module` in the root `CMakeLists.txt`.
- `tools/bootstrap/stage_{compiler,assembler,linker}.luna` — self-hosted tool
  sources.
- `tests/unit` = GTest/C++; `tests/integration|random|differential` = Python
  drivers; `tests/fuzz` = libFuzzer + corpus.
- `web/` — separate nested git repo (Next.js site). Not part of the CMake
  build; see its own README (`npm ci && npm run sync && npm test && npm run build`;
  sync requires a clean main-repo checkout).

## Non-negotiable boundaries

From `docs/architecture.md` — do not violate:

- Never translate Luna to C or through any hosted toolchain after stage 1;
  LLVM MC / LLD are test oracles only.
- Generated programs are freestanding; runtime/syscall wrappers live in
  `runtime/luna/` and link no libc.
- Only target is `x86_64-unknown-linux-gnu`; `isize`/`usize` are target-sized.
- Luna modules are matched interface/implementation pairs
  (`foo.interface.luna` declares exports; `foo.luna` defines).
- `docs/` holds authoritative per-subsystem design docs; consult them before
  changing IR, ABI, ELF, linker or bootstrap behavior.
