# Minimum standard library

> **Historical m0 bootstrap record.** The API rationale remains useful, but
> sysroot `.lmi` paths and hosted build commands below belong to the archived
> reconstruction. Current library pairs live under `library/include/` and
> `library/src/` and are built directly from source.

The bootstrap standard library is implemented in Luna. It is a small
correctness-first layer over `luna.runtime`; no standard-library module imports
`luna.linux.syscall`, declares a libc function or relies on generated C code.
The initial contract intentionally contains only the facilities needed by the
next self-hosting stage.

## Module graph and artifacts

The source modules and their direct dependencies are:

| Module | Responsibility | Direct dependencies |
| --- | --- | --- |
| `luna.std.ascii` | ASCII classification, digit decoding and hexadecimal rendering | none |
| `luna.std.checked` | checked `usize` arithmetic, range validation and alignment | none |
| `luna.std.memory` | owned byte allocations and byte operations | `luna.runtime` |
| `luna.std.bytes` | growable owned byte buffers | `luna.runtime`, private `luna.std.memory` |
| `luna.std.binary` | explicit little-endian integer reads, appends and stores | `luna.runtime`, `luna.std.bytes`, `luna.std.checked` |
| `luna.std.text` | validated UTF-8 views and owned text | `luna.runtime`, `luna.std.bytes` |
| `luna.std.path` | owned NUL-terminated filesystem paths | `luna.runtime`, `luna.std.bytes`, `luna.std.text` |
| `luna.std.io` | complete writes, reads to EOF and file helpers | `luna.runtime`, `luna.std.ascii`, `luna.std.bytes`, `luna.std.text`, `luna.std.path` |

Build the complete target sysroot explicitly:

```sh
cmake --build --preset debug --target luna_sysroot
```

Each standard module produces deterministic metadata and a separately linked
object under `build/debug/sysroot/luna/std/`, for example `bytes.lmi` and
`bytes.o`. An application importing `luna.std.io` compiles against the public
metadata graph:

```sh
build/debug/lunac --emit obj -o app.o app.la \
  build/debug/sysroot/luna/runtime.lmi \
  build/debug/sysroot/luna/std/bytes.lmi \
  build/debug/sysroot/luna/std/text.lmi \
  build/debug/sysroot/luna/std/path.lmi \
  build/debug/sysroot/luna/std/io.lmi
```

The final static link includes each implementation object, including
`memory.o`, which is a private implementation dependency of `bytes.o`:

```sh
build/debug/lunalink -o app app.o \
  build/debug/sysroot/luna/runtime.o \
  build/debug/sysroot/luna/std/memory.o \
  build/debug/sysroot/luna/std/bytes.o \
  build/debug/sysroot/luna/std/text.o \
  build/debug/sysroot/luna/std/path.o \
  build/debug/sysroot/luna/std/io.o
```

No archive format or implicit library search is involved in this bootstrap
contract.

## Ownership rule

Luna does not yet have move or affine types. Ownership is therefore an
explicit API invariant:

- a successful allocation, buffer, owned text or path result transfers one
  release obligation to the caller;
- copying an owning structure copies a handle, not the allocation;
- after a successful transforming operation, only the returned owner may be
  used; the input copy is invalidated by contract;
- on a failed transforming operation, the owner carried in the result remains
  valid and retains the release obligation;
- a successful release consumes the owner; reuse or a second release violates
  the contract.

All result structures carry `RuntimeError` directly. There is no global error
slot, exception path, hidden allocation or process-wide initialization.

## Memory

`StdAllocation` contains a byte address and its exact mapped length.
`std_memory_allocate(0)` returns the canonical empty allocation without a
system call. A nonempty allocation currently owns one private anonymous
mapping. This one-mapping-per-allocation strategy is deliberately simple and
is not presented as a finished heap allocator.

`std_memory_resize` allocates, copies the common prefix and releases the old
mapping. Success consumes the old owner. Failure returns the original owner
unchanged. Resizing to zero releases the allocation and returns the canonical
empty value.

`std_memory_move` has overlap-safe `memmove` semantics.
`std_memory_fill` and `std_memory_equal` operate on explicit byte lengths.
Zero-length operations do not dereference their pointers; a nonzero operation
rejects a required null address.

## Byte buffers

`StdByteBuffer` maintains this public invariant:

```text
capacity == 0  => data == null and length == 0
capacity != 0  => data != null and length <= capacity
```

Creation, reserve, push, append, truncate, clear, equality and release are
provided. Growth starts at 64 bytes and doubles until it satisfies the
requested capacity, with explicit `usize` overflow checks. This is a policy,
not a language ABI guarantee.

Append accepts a source range inside the buffer's current logical contents,
including self-append that triggers reallocation. A source in spare capacity
is rejected because those bytes are not initialized logical contents.
Truncation and clear retain capacity and never allocate.

## ASCII and checked arithmetic

`luna.std.ascii` centralizes cold-path ASCII classification, base-2 through
base-16 digit decoding and lowercase hexadecimal rendering. Byte-at-a-time hot
loops such as the lexer and assembler keep equivalent module-local predicates:
the correctness-first backend has no inliner, so a cross-module call per source
byte would be a material bootstrap cost.

`luna.std.checked` wraps the overflow-reporting integer intrinsics for shared
`usize` addition and builds power-of-two alignment and bounded-range checks on
that primitive. Hot byte-buffer growth paths invoke the intrinsics directly;
the shared module owns the less frequent layout and binary-format operations.

## Little-endian binary data

`luna.std.binary` is the only shared implementation of bounded little-endian
16-, 32- and 64-bit reads, buffer appends and in-place 32-/64-bit stores. The
LUNAOBJ1 codec, ELF reader/writer, assembler fixups and static linker use this
module instead of maintaining independent byte-shift loops. It operates on
explicit byte ranges and does not serialize Luna structure representations.

## UTF-8 text

`StdTextView` is borrowed and carries an explicit byte length. Empty text may
use a null address. Nonempty views are validated as Unicode scalar-value UTF-8:
overlong encodings, surrogate code points, out-of-range code points, isolated
continuations and truncated sequences are rejected.

The library provides bounded C-string discovery, validated byte slicing,
equality, owned copies, append and release. The maximum passed to
`std_text_from_c_string` includes the terminating NUL; failure to find a
terminator before that bound reports `name_too_long`. Slices must begin and end
on valid UTF-8 boundaries because the selected bytes are validated again.

Text equality is byte equality after validation. The minimum library does not
normalize Unicode, segment graphemes, change case or interpret locales.

## Paths

`StdPath` owns a UTF-8 byte buffer with exactly one terminal NUL. Its `length`
excludes that terminator. Empty paths, invalid UTF-8 and interior NUL bytes are
rejected. `std_path_as_c_string` exposes a borrowed pointer only while the path
owner remains live.

This is a safe boundary for the current Linux runtime calls, not a
platform-independent path model. It does not canonicalize separators, resolve
components or promise that the kernel accepts every valid UTF-8 path.

## I/O

`std_io_write_all` continues after short successful writes and retries only
`RuntimeError.interrupted`. A successful zero-byte write before completion is
reported as `io`, preventing an infinite loop. `would_block` remains visible
to the caller.

`std_io_read_to_end` reads in 4096-byte growth increments until EOF. On failure
its result owns the bytes read so far. `std_io_read_file` opens and closes the
file on every path; on read or close failure it releases partial storage and
returns an empty buffer. `std_io_write_file` gives a write error precedence
over a later close error. `std_io_write_executable_file` uses the runtime's
direct-system-call executable-file path with the same complete-write and
error-precedence rules; the self-hosted linker uses it for final ELF output.
`std_io_print` and `std_io_print_error` accept only validated text and borrow
the standard descriptors.

`std_io_read_to_end_limited` and `std_io_read_file_limited` add an explicit
maximum byte length without trusting the input file size. Reaching the limit
causes one bounded probe read: EOF at that point succeeds, while any excess
byte reports `out_of_memory`. A caller-supplied buffer with spare capacity
still cannot read past the declared maximum. The file helper releases partial
storage on an over-limit result just like any other read failure.

## Deliberate limits

This stage does not add formatting, generic containers, typed allocation,
filesystem traversal, buffered streams, asynchronous I/O, threads, locks,
environment access or optimization. Those facilities are not needed for the
Luna frontend or the upcoming type checker and typed IR builder. Adding them
now would enlarge the trusted bootstrap surface without improving
reproducibility.

## Correctness gates

The standard library is checked at these independent boundaries:

1. GoogleTest lowers the real interfaces and implementations through verified
   typed IR, assembly and object emission, checks type errors and enforces the
   no-raw-syscall source boundary.
2. Integration tests regenerate every `.lmi` and `.o` twice, compare them
   byte-for-byte with the sysroot, inspect symbols, require every module object
   at link time and execute file, allocation, buffer, UTF-8, path and I/O
   behavior natively or through QEMU, including exact-limit and one-byte-over
   reads with both empty and pre-reserved buffers.
3. A fixed-seed property test performs thousands of buffer growth and
   self-referential append operations and compares the result with an
   independent fixed-array model.
4. The frontend libFuzzer corpus contains a complete multi-unit
   standard-library-shaped module graph.

The resulting executable must have no dynamic interpreter, dynamic section or
unresolved symbol. Standard-library objects are rejected by the test if they
reference a raw `luna_linux_syscall` symbol.
