# Freestanding runtime

Luna's bootstrap runtime is implemented in Luna and depends only on the
project-owned x86-64 Linux system-call ABI. It does not link target libc, a
dynamic loader, a host assembler or generated C code.

The runtime is deliberately a mechanism layer. It provides the process, file
and virtual-memory operations required to build the minimum standard library
and self-hosting compiler. Allocation, buffering, UTF-8 text, path ownership
and complete-I/O retry policy are now implemented by the separate minimum
standard-library layer. Formatting and higher-level facilities remain
deferred.

## Sysroot artifacts

The public interface is `runtime/luna/runtime.interface.luna`; its
implementation is `runtime/luna/runtime.luna`. Build both compiled artifacts
with:

```sh
cmake --build --preset debug --target luna_sysroot
```

The target produces:

```text
build/debug/sysroot/luna/linux/syscall.lmi
build/debug/sysroot/luna/runtime.lmi
build/debug/sysroot/luna/runtime.o
```

An application consumes only `runtime.lmi` while compiling. The implementation
object privately depends on the raw system-call module:

```sh
build/debug/lunac --emit obj -o app.o \
  app.luna build/debug/sysroot/luna/runtime.lmi
build/debug/lunalink -o app \
  app.o build/debug/sysroot/luna/runtime.o
```

`lunalink` supplies and verifies the canonical raw system-call wrapper object.
The separately compiled runtime object has no `_start`; the executable root
continues to own the compiler-generated entry shim.

## Error contract

`RuntimeError` is a scoped `isize` enum. `RuntimeError.none` is zero. A Linux
result in `-4095` through `-1` becomes its positive error number, preserving
unknown kernel errors through an explicit `as isize` conversion without using
global `errno` or thread-local state. A result outside the raw ABI contract is
reported as `RuntimeError.io`.

Named members cover the errors needed by the bootstrap runtime, including
`not_found`, `interrupted`, `bad_descriptor`, `would_block`,
`out_of_memory`, `permission_denied`, `bad_address`, `invalid_argument`,
`no_space` and `broken_pipe`. Unnamed positive Linux error numbers remain
representable.

The runtime never retries `interrupted` or `would_block`. Retry, polling and
cancellation are caller policy. This makes every system call observable and
prevents a low-level operation from hiding unbounded work.

## Process operations

| Function | Contract |
| --- | --- |
| `runtime_process_id` | Returns a nonzero process ID or an error. |
| `runtime_process_exit` | Terminates the complete process with Linux `exit_group`; it does not return. |

The language does not yet have a `noreturn` function type, so
`runtime_process_exit` has a `void` source signature and an unreachable
fallback loop after the system call.

## File operations

`RuntimeFile` is a named descriptor type. A failed `RuntimeFileResult` contains
descriptor `-1`; the descriptor is usable only when its accompanying error is
`RuntimeError.none`.

| Function | Contract |
| --- | --- |
| `runtime_standard_input` | Returns borrowed descriptor 0. |
| `runtime_standard_output` | Returns borrowed descriptor 1. |
| `runtime_standard_error` | Returns borrowed descriptor 2. |
| `runtime_file_open_read` | Opens a NUL-terminated path read-only with `O_CLOEXEC`. |
| `runtime_file_create` | Opens a NUL-terminated path write-only with create, truncate and close-on-exec flags; requested mode is `0666` before the process umask. |
| `runtime_file_create_exclusive` | Creates a new write-only path with `O_EXCL` and `O_CLOEXEC`; it never truncates an existing path. |
| `runtime_file_create_executable` | Creates a truncated write-only file and enforces mode `0755` with direct `openat` and `fchmod` system calls. |
| `runtime_file_make_executable` | Applies mode `0755` to an open nonnegative descriptor with direct `fchmod`. |
| `runtime_file_read` | Performs one `read` and returns its byte count or error. |
| `runtime_file_write` | Performs one `write` and returns its byte count or error. |
| `runtime_file_close` | Performs one `close` for a nonnegative descriptor. |
| `runtime_path_replace` | Atomically replaces one path with another in the current directory context using `renameat`. |
| `runtime_path_remove` | Removes one path with `unlinkat`. |

Read and write success may be short. A zero-byte read is EOF only when the
caller requested a nonzero length. A zero-length operation returns immediate
success without dereferencing its buffer, after validating the descriptor.
A nonzero operation rejects a null buffer as `bad_address`.

Files returned by open/create transfer one close obligation to the caller.
Standard descriptors are borrowed and are not automatically closed. The
current language has no move or affine type system, so exactly-once close is a
documented resource invariant rather than a compiler-enforced property.

The minimum I/O layer builds `std_io_write_file_atomic` and
`std_io_write_executable_file_atomic` on these primitives. It writes an
exclusive same-directory temporary path, closes it, and only then replaces
the destination. Failed compilation, assembly, linking, writing or mode
changes therefore leave an existing output untouched and remove the
temporary file. The contract guarantees atomic visibility, not storage
durability across power loss; no implicit `fsync` policy is imposed.

## Virtual memory

`runtime_memory_map` creates private anonymous, readable and writable Linux
memory. A zero length is rejected. Success returns a non-null address and the
exact requested length; failure returns a null address and zero length.

Although address zero is normally excluded by Linux policy, it is a valid
kernel mapping result in principle. The runtime immediately unmaps such a
result and reports `io`, preserving the public non-null success invariant.

`runtime_memory_unmap` rejects a null address or zero length and otherwise
performs one `munmap`. A successful `RuntimeMemory` transfers one unmap
obligation to the caller. Copying the structure does not duplicate ownership;
using it after unmap or unmapping it twice violates the runtime contract.

This is virtual-memory plumbing, not a heap allocator. Alignment classes,
allocation metadata, growth strategies and object lifetimes belong to the
minimum standard library. Its current correctness-first allocator maps one
independent region per nonempty allocation.

See [the minimum standard-library contract](minimum-standard-library.md) for
the ownership, buffering, text, path and I/O policies built on these
operations.

## Correctness gates

The runtime is checked at four boundaries:

1. GoogleTest parses and lowers the real interface and implementation sources,
   verifies IR/object generation and rejects raw integers where a
   `RuntimeFile` is required.
2. The integration test rebuilds metadata and object files twice and requires
   byte-for-byte determinism against the sysroot artifacts.
3. Native or QEMU execution covers success and failure paths for process,
   file and virtual-memory operations, including EOF and direct process exit.
4. The frontend libFuzzer corpus carries a complete typed runtime module graph.

The integration linker gate also proves that omitting `runtime.o` is a hard
undefined-symbol error and that the final executable contains no interpreter,
dynamic section or unresolved symbol.
