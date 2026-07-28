# Versioned bootstrap seed

`LUNA-BOOTSTRAP-SEED/1` is Luna's reproducible distribution boundary. A seed
archive contains the fixed-point compiler, assembler and linker together with
the complete Luna source graph needed to rebuild them. It is therefore a
bounded reconstruction input, not an opaque binary-only toolchain snapshot.

The canonical target is `x86_64-unknown-linux-gnu`. The target suffix names
the existing x86-64 System V ABI contract; the packaged executables are
freestanding static ELF64 files and do not depend on libc or a dynamic loader.

## Build and verify

Create the seed only after the complete stage-2/stage-3 gate succeeds:

```sh
cmake --preset release
cmake --build --preset release --target luna_bootstrap_seed
```

For version `0.1.0`, the resulting files are:

```text
build/release/selfhost-bootstrap/dist/
├── luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar
└── luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar.sha256
```

The target verifies the archive with the standalone verifier and compares its
checksum file with the versioned checksum under `release/seeds/`.

An offline consumer can verify the downloaded pair:

```sh
python3 tools/release/bootstrap_seed.py verify \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar \
  --checksum-file \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar.sha256 \
  --expected-version 0.1.0 \
  --expected-target x86_64-unknown-linux-gnu
```

The verifier uses only Python's standard library. It does not execute a
compiler during structural verification.

## Canonical archive

The package is an uncompressed POSIX ustar archive. Compression is
deliberately outside version 1 so compressor versions, headers and timestamps
cannot change the seed identity.

Every archive has:

- one root named
  `luna-bootstrap-seed-<version>-<target>`;
- root-owned members with empty owner/group names and timestamp zero;
- mode `0755` on directories and executables, and `0644` elsewhere;
- directories in depth/lexical order, followed by the manifest and
  lexically ordered payload files;
- no links, devices, sparse files, path traversal, duplicate members or
  unmanifested files;
- a maximum archive size of 256 MiB, maximum file size of 64 MiB, maximum
  payload size of 192 MiB and at most 128 members.

The verifier regenerates the complete canonical tar byte stream and requires
equality with the input. Valid contents in a differently ordered or padded
tar are rejected rather than assigned a second representation.

## Manifest

`manifest.json` is newline-terminated canonical ASCII JSON with sorted object
keys and no insignificant whitespace. Duplicate or unknown keys are rejected.
It records:

- format identifier `LUNA-BOOTSTRAP-SEED/1`;
- project, semantic version, target and canonical archive root;
- a sorted file inventory containing path, role, mode, size and SHA-256;
- a source-tree fingerprint over every `source` inventory entry;
- a toolchain fingerprint over the three `tool` inventory entries.

The payload roles are:

- `tool`: `bin/lunac`, `bin/luna-as` and `bin/luna-link`;
- `source`: the Luna runtime, standard library, bootstrap frontend,
  middle-end, x86-64 backend, object, assembler, linker and driver sources;
- `auxiliary`: `VERSION`, license, seed instructions and the standalone
  verifier/rebuilder.

Each tool must be little-endian x86-64 `ET_EXEC`, have no section table,
contain no `PT_INTERP` or `PT_DYNAMIC`, and place its entry inside an
executable load segment.

## Offline rebuild

The stronger verification mode extracts only already-validated regular files,
then rebuilds every library module and all three tools:

```sh
python3 tools/release/bootstrap_seed.py rebuild \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar \
  --checksum-file \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar.sha256 \
  --expected-version 0.1.0 \
  --expected-target x86_64-unknown-linux-gnu \
  --work-dir seed-rebuild
```

The packaged `lunac` compiles the packaged Luna sources to the owned assembly
dialect, packaged `luna-as` emits `LUNAOBJ1`, and packaged `luna-link` emits
the new static executables. Every rebuilt executable must be byte-identical to
its packaged input and report the manifest version. Python only supplies
ordered arguments and bounded filesystem operations.

On a non-x86-64 host, rebuild requires `qemu-x86_64-static`; verification
without execution remains host-independent.

## Trust and release boundary

The manifest detects internal corruption and the external `.sha256` gives the
archive one publishable identity. Neither is a digital signature: an attacker
able to replace both files can recompute both. Source authentication comes
from the separately authenticated Git tag and release channel that publish
the expected checksum.

Fixed-point and offline rebuild equality prove that the distributed tools
reproduce their claimed source graph. They do not by themselves eliminate the
general trusting-trust problem. Independent builders should construct the
archive from the authenticated source tag and compare the complete archive
SHA-256 before accepting a release.

Changing the archive representation, manifest schema, target contract, fixed
limits or rebuild plan requires a new seed format. Changing source or tool
bytes requires a new project version and checksum; a released version's seed
is immutable.
