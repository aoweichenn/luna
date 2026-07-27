# Linux x86-64 system-call ABI

Luna's first self-hosting runtime boundary is a project-owned raw system-call
layer. It invokes the Linux x86-64 `syscall` instruction directly and has no
target libc, dynamic loader, `errno`, thread-local storage or host-assembler
dependency.

This layer is intentionally narrower than a runtime or standard library. It
only transports a system-call number, zero to six machine-word arguments and
the raw signed kernel result. The `luna.runtime` module now builds typed
process, file, virtual-memory and I/O services on this exact boundary;
allocation, buffering and higher-level policy remain separate.

## Luna contract

The source interface is `runtime/luna/linux/syscall.interface.luna`. The
explicit sysroot target generates target-specific metadata at:

```text
build/<preset>/sysroot/luna/linux/syscall.lmi
```

```sh
cmake --build --preset debug --target luna_sysroot
```

Applications import `luna.linux.syscall` and pass that metadata to `lunac`:

```luna
module example.write;

import luna.linux.syscall;

fn main() -> i32 {
    let text: *const u8 = "hello\n";
    let result: isize = luna_linux_syscall3(
        1,
        1,
        text as usize,
        6
    );
    return result == 6 ? 0 : 1;
}
```

```sh
build/debug/lunac --emit obj -o write.o \
  write.luna build/debug/sysroot/luna/linux/syscall.lmi
build/debug/lunalink -o write write.o
```

The seven declarations are:

```text
luna_linux_syscall0(number)
luna_linux_syscall1(number, argument0)
...
luna_linux_syscall6(number, argument0, ..., argument5)
```

Every number and argument has type `usize`; the result has type `isize`.
Pointers and signed values therefore cross this lowest boundary only through
an explicit `as usize` conversion. The interface does not guess the signature
of any particular kernel service. Semantic analysis reserves these seven ELF
names to this exact arity and type contract, even when a source file writes an
`extern fn` declaration directly instead of importing the sysroot metadata.

Linux reports failures as raw values from `-4095` through `-1`. The ABI layer
returns those values unchanged. It does not set a global error variable,
retry interrupted operations, translate errors, allocate memory or perform
cancellation. Future Luna-facing wrappers must define those policies
explicitly while retaining this raw result internally.

## Register mapping

The exported wrappers accept the ordinary x86-64 System V function ABI used by
Luna calls and move values into the Linux kernel ABI:

| Value | System V wrapper input | Linux `syscall` input |
| --- | --- | --- |
| number | `rdi` | `rax` |
| argument 0 | `rsi` | `rdi` |
| argument 1 | `rdx` | `rsi` |
| argument 2 | `rcx` | `rdx` |
| argument 3 | `r8` | `r10` |
| argument 4 | `r9` | `r8` |
| argument 5 | `[rsp + 8]` | `r9` |
| result | — | `rax` |

`syscall` destroys `rcx` and `r11`, both of which are caller-saved under
System V. The wrappers use no callee-saved registers, allocate no stack frame
and return the kernel's `rax` bits directly.

The sixth kernel argument is the seventh wrapper argument. System V places it
on the stack immediately after the return address, so `syscall6` loads
`[rsp + 8]` before entering the kernel. Unit and executable tests cover this
otherwise easy-to-miss ABI transition.

## Object ownership and linking

`src/runtime/x86_64/linux_syscall.c` renders the fixed wrapper set through
Luna's closed x86-64 assembler. It produces a deterministic, self-verified
ELF64 relocatable object without LLVM MC, GNU as or C code generation.

`lunalink` constructs and verifies this canonical object in memory for every
static link, then appends it after caller inputs. This makes the ABI available
without a host library and keeps the generated metadata free of machine-code
blobs. The bootstrap linker currently has no section garbage collection, so
all seven small wrappers remain in each executable; removal of unused
sections is an optimization and is deliberately deferred.

The symbols `luna_linux_syscall0` through `luna_linux_syscall6` are owned by
the project layer. A caller object attempting to define one causes the normal
duplicate-strong-symbol diagnostic. Undefined references resolve only to the
canonical object generated in the same linker process.

The public ABI-object verifier compares all bytes against a freshly generated
canonical object. Structural ELF verification remains a separate check. This
distinction matters: changing a valid instruction byte may preserve ELF
structure while violating the syscall register contract.

## Verification

The correctness gate includes:

- deterministic generation and exact-object verification;
- mutation tests that accept only the canonical byte sequence;
- duplicate-definition rejection for project-owned symbols;
- libFuzzer determinism over arbitrary ABI-object inputs;
- an executable test of all arities from zero through six;
- raw `-errno` preservation;
- direct `write`, `mmap` memory access and `munmap`;
- inspection that the result contains no dynamic interpreter or runtime.

The target executable remains entirely freestanding. Hosted C23 library use by
the `lunac` and `lunalink` development tools is unrelated to the generated
program's dependency graph.
