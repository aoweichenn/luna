# Luna compiled module metadata

> **Historical m0 contract.** This file documents the C23 reconstruction
> seed's `.lmi` version 1 format on the archived `m0` branch. The current pure
> Luna compiler does not emit or consume `.lmi`; stage 1 and later compile
> dependency interfaces directly from `.lh` source. This format must not be
> treated as the current `lunac` CLI or as the design of a future binary module
> interface.

`.lmi` is Luna's target-specific compiled interface format. It is a
deterministic binary contract, not a serialization of parser objects, semantic
objects or host pointers. Version 1 uses little-endian integers regardless of
the compiler host.

## Header

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | magic bytes `LUNALMI\0` |
| 8 | 2 | format major version |
| 10 | 2 | format minor version |
| 12 | 4 | Luna language ABI version |
| 16 | 8 | payload size in bytes |
| 24 | 8 | FNV-1a 64-bit versioned interface fingerprint |

The current format version is `1.0` and the current language ABI version is
`1`. A different major version, newer minor version or different language ABI
is rejected. The payload size must account for every remaining file byte, and
the fingerprint must match before payload decoding begins.

The fingerprint hashes the format magic as a domain separator, the
little-endian language ABI version and the complete payload. Binding the
language ABI prevents objects from different calling-convention or layout
revisions from sharing link symbols even when their source-level interfaces
are textually identical.

The fingerprint detects accidental corruption and stale build combinations.
It is not a cryptographic signature and must not be treated as proof that an
untrusted file is authentic.

## Payload

Strings are a `u32` byte length followed by that many bytes, with no
terminating zero. Counts are `u32`. The payload is:

```text
target triple: string
module name: string
direct import count: u32
direct imports:
    module name: string
    dependency metadata content fingerprint: u64
type declaration count: u32
type declarations
function declaration count: u32
function declarations
```

Each imported fingerprint is the header content fingerprint of the exact
dependency `.lmi` used to build the importing module. Module-graph resolution
requires the supplied dependency metadata to match it.

Separate typed IR, x86-64 machine IR and assembly generation consumes the root
module's own `.lmi`. Every non-C module import and export carries that metadata
content fingerprint through both IR layers, and x86-64 symbol mangling encodes
it as 16 hexadecimal digits. Consequently, linking an object generated from
different root metadata leaves the expected symbol unresolved. This link-time
identity check complements the module graph's compile-time dependency check.

### Type references

Every type begins with a one-byte tag:

| Tag | Type | Following data |
| ---: | --- | --- |
| 1 | `void` | none |
| 2 | `bool` | none |
| 3–14 | `i8` through `f64` in language type order | none |
| 15 | named type | identifier string |
| 16 | pointer | `u8` read-only flag, pointee type |
| 17 | fixed array | `u64` positive element count, element type |

The scalar tag order is `i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `u16`,
`u32`, `u64`, `usize`, `f32`, `f64`.

### Type declarations

A declaration starts with a one-byte declaration tag, a one-byte exported
flag and its identifier string. Declaration tags are:

- `1`: structure;
- `2`: union;
- `3`: scoped enum.

A structure or union then stores a `u32` field count. Each field is an
identifier string followed by its type.

An enum stores its underlying type and a `u32` member count. Each member stores
its identifier and a one-byte explicit-initializer flag. An explicit value
stores a one-byte negative flag and a `u64` magnitude. Implicit values remain
implicit so the normal semantic checker verifies overflow and successor
rules.

### Function declarations

A function stores a one-byte flags field, its identifier, a `u32` parameter
count, every parameter name and type, and its return type. Bit 0 means
exported and bit 1 means external C linkage. All other bits are invalid.
Metadata functions are declarations and never contain bodies.

## Validation and limits

The decoder validates the complete envelope before constructing declarations.
It then rejects unknown tags, malformed flags, zero or oversized strings,
invalid identifiers and module names, zero array lengths, excess type nesting,
trailing bytes and allocation failures.

Current hard limits are:

- 16 MiB per metadata file;
- 1 MiB per encoded string;
- 1,048,576 records in any counted list;
- 64 nested pointer/array type levels.

Decoded declarations use arena-owned strings and nodes. Diagnostic spans point
to an empty synthetic source bearing the `.lmi` path, so malformed binary
bytes are never printed as source text.

## Determinism and compatibility

Declaration and import order are the validated interface source order.
Encoding the same interface, target and direct dependency fingerprints
produces byte-identical output. Command-line input order has no effect.

Any compiler change that alters the meaning, layout or calling convention of
an existing encoding must increment the language ABI version. An incompatible
binary layout change must increment the format major version. A backward
compatible extension may increment the format minor version only after older
decoders have a defined way to skip it.
