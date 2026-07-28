# Luna bootstrap seed

This archive is the versioned, freestanding x86-64 Linux reconstruction seed
for Luna. It contains:

- the fixed-point `lunac`, `luna-as` and `luna-link` executables;
- every Luna source unit needed to rebuild those executables;
- the canonical `LUNA-BOOTSTRAP-SEED/1` manifest;
- the standalone verifier and rebuilder.

The executables are static ELF64 files, use direct Linux system calls and do
not depend on libc, a dynamic loader, GNU binutils, LLVM or LLD.

Verify the archive before extracting it:

```sh
python3 bootstrap_seed.py verify \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar \
  --checksum-file \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar.sha256
```

Rebuild all three tools entirely from the archive and require byte equality:

```sh
python3 bootstrap_seed.py rebuild \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar \
  --checksum-file \
  luna-bootstrap-seed-0.1.0-x86_64-unknown-linux-gnu.tar.sha256 \
  --work-dir seed-rebuild
```

Python only orchestrates bounded file operations and process execution. All
language compilation, assembly and linking in the rebuild are performed by
the packaged Luna tools.
